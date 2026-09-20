#include "main_window.h"
#include "main_window_support.h"

#include "logging/logger.h"
#include "preview_widget.h"
#include "project/project_file.h"
#include "timeline/timeline_widget.h"
#include "ui/media_browser_list_widget.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QAbstractItemView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSlider>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>


using namespace main_window_detail;

void MainWindow::initializePlayback() {
    qRegisterMetaType<playback::VideoFramePtr>();
    qRegisterMetaType<playback::CompositionLayerSpec>();
    qRegisterMetaType<QVector<playback::CompositionLayerSpec>>();

    playback_worker_ = new playback::PlaybackWorker;
    playback_worker_->moveToThread(&playback_thread_);

    connect(
        &playback_thread_,
        &QThread::finished,
        playback_worker_,
        &QObject::deleteLater);
    connect(
        playback_worker_,
        &playback::PlaybackWorker::frameReady,
        this,
        [this](playback::VideoFramePtr frame, qint64 frame_index, quint64 generation) {
            handlePlaybackFrame(std::move(frame), frame_index, generation);
        },
        Qt::QueuedConnection);
    connect(
        playback_worker_,
        &playback::PlaybackWorker::mediaReady,
        this,
        &MainWindow::handlePlaybackMediaReady,
        Qt::QueuedConnection);
    connect(
        playback_worker_,
        &playback::PlaybackWorker::playbackStateChanged,
        this,
        [this](bool playing, quint64 generation) {
            handlePlaybackStateChanged(playing, generation);
        },
        Qt::QueuedConnection);
    connect(
        playback_worker_,
        &playback::PlaybackWorker::playbackFinished,
        this,
        [this](quint64 generation, bool during_playback) {
            handlePlaybackFinished(generation, during_playback);
        },
        Qt::QueuedConnection);
    connect(
        playback_worker_,
        &playback::PlaybackWorker::playbackError,
        this,
        [this](const QString& message, qint64 error_code, quint64 generation) {
            handlePlaybackError(message, error_code, generation);
        },
        Qt::QueuedConnection);
    connect(
        playback_worker_,
        &playback::PlaybackWorker::audioWarning,
        this,
        [this](const QString&, qint64, quint64 generation) {
            if (generation != playback_generation_) return;
            statusBar()->showMessage(
                "Audio unavailable; continuing with video playback.");
            if (playback_status_label_ != nullptr) {
                playback_status_label_->setText(
                    "Audio unavailable; video fallback is active.");
            }
        },
        Qt::QueuedConnection);

    playback_thread_.start();
}
void MainWindow::shutdownPlayback() {
    if (playback_worker_ == nullptr) return;

    if (playback_thread_.isRunning()) {
        QMetaObject::invokeMethod(
            playback_worker_,
            "stop",
            Qt::BlockingQueuedConnection);
        playback_thread_.quit();
        playback_thread_.wait();
    }
    playback_worker_ = nullptr;
}

void MainWindow::sendCompositionToWorker() {
    if (playback_worker_ == nullptr) return;
    QVector<playback::CompositionLayerSpec> layers;
    for (std::size_t track_index = 0;
         track_index < timeline_model_.trackCount();
         ++track_index) {
        const auto& track = timeline_model_.tracks()[track_index];
        for (std::size_t clip_index = 0;
             clip_index < track.clips.size();
             ++clip_index) {
            const auto& clip = track.clips[clip_index];
            std::optional<ImportedMedia> imported;
            if (clip.kind == timeline::ClipKind::Video) {
                const auto found = std::find_if(
                    media_items_.begin(), media_items_.end(),
                    [&clip](const ImportedMedia& item) {
                        return normalizedPath(item.metadata.source_path) ==
                            normalizedPath(clip.source_path);
                    });
                if (found == media_items_.end() || found->offline) continue;
                imported = *found;
            }
            layers.push_back(playback::CompositionLayerSpec{
                clip.kind == timeline::ClipKind::Video
                    ? fromUtf8(pathToUtf8(clip.source_path))
                    : QString(),
                clip.frame_rate.value_or(
                    imported.has_value() && imported->metadata.frame_rate.has_value()
                        ? *imported->metadata.frame_rate
                        : 30.0),
                clip.timeline_start_frame,
                clip.source_start_frame,
                clip.timeline_duration_frames,
                static_cast<qint64>(track_index),
                static_cast<qint64>(clip_index),
                clip.transform,
                clip.keyframes,
                clip.kind,
                clip.text});
        }
    }
    QMetaObject::invokeMethod(
        playback_worker_,
        "setComposition",
        Qt::QueuedConnection,
        Q_ARG(QVector<playback::CompositionLayerSpec>, layers),
        Q_ARG(quint64, playback_generation_));
}

void MainWindow::activateTimelineClip(
    std::size_t clip_index,
    std::int64_t target_frame,
    bool resume_playback) {
    activateTimelineClipAt(0, clip_index, target_frame, resume_playback);
}

void MainWindow::activateTimelineClipAt(
    std::size_t track_index,
    std::size_t clip_index,
    std::int64_t target_frame,
    bool resume_playback) {
    if (playback_worker_ == nullptr ||
        track_index >= timeline_model_.trackCount() ||
        clip_index >= timeline_model_.clipCount(track_index)) {
        return;
    }

    const auto& clip = timeline_model_.tracks()[track_index].clips[clip_index];
    if (clip.kind == timeline::ClipKind::Text) {
        active_timeline_track_index_ = track_index;
        active_timeline_clip_index_ = clip_index;
        playback_frame_index_ = std::clamp<std::int64_t>(
            target_frame, 0, std::max<std::int64_t>(
                0, clip.timeline_duration_frames - 1));
        playback_is_playing_ = false;
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        sendCompositionToWorker();
        QMetaObject::invokeMethod(
            playback_worker_,
            "renderCompositionFrame",
            Qt::QueuedConnection,
            Q_ARG(qint64, static_cast<qint64>(timelinePlayheadFrame())),
            Q_ARG(qint64, static_cast<qint64>(playback_frame_index_)),
            Q_ARG(quint64, playback_generation_));
        return;
    }
    const auto media_item = std::find_if(
        media_items_.begin(),
        media_items_.end(),
        [&clip](const ImportedMedia& item) {
            return item.metadata.source_path == clip.source_path;
        });
    if (media_item == media_items_.end() || media_item->offline ||
        target_frame < 0 ||
        target_frame >= clip.timeline_duration_frames) {
        logging::Context context{
            {"track_index", std::to_string(track_index)},
            {"clip_index", std::to_string(clip_index)},
            {"path", pathToUtf8(clip.source_path)},
            {"requested_frame", std::to_string(target_frame)}};
        logging::Logger::instance().log(
            logging::Level::Error,
            "playback",
            "transition",
            media_item == media_items_.end()
                ? "The timeline clip has no matching imported media item."
                : media_item->offline
                ? "The timeline clip refers to offline media."
                : "The requested timeline transition frame is invalid.",
            context);
        statusBar()->showMessage("Could not activate the timeline clip.");
        QMessageBox::warning(
            this,
            "Playback transition error",
            "The next timeline clip could not be activated.");
        playback_is_playing_ = false;
        updatePlaybackControls();
        updatePlaybackStatus();
        return;
    }

    ++playback_generation_;
    pending_clip_activation_ = PendingClipActivation{
        clip_index,
        static_cast<std::size_t>(std::distance(media_items_.begin(), media_item)),
        target_frame,
        clip.source_start_frame,
        clip.timeline_duration_frames,
        resume_playback,
        playback_generation_,
        track_index};
    playback_is_playing_ = false;
    updatePlaybackControls();
    updatePlaybackStatus();
    statusBar()->showMessage("Loading timeline clip...");

    QMetaObject::invokeMethod(playback_worker_, "stop", Qt::QueuedConnection);
    const auto& item = *media_item;
    QMetaObject::invokeMethod(
        playback_worker_,
        "setMedia",
        Qt::QueuedConnection,
        Q_ARG(QString, fromUtf8(pathToUtf8(item.metadata.source_path))),
        Q_ARG(double, item.metadata.frame_rate.value_or(30.0)),
         Q_ARG(qint64, static_cast<qint64>(clip.source_start_frame)),
         Q_ARG(qint64, static_cast<qint64>(clip.timeline_duration_frames)),
         Q_ARG(double, timeline_model_.tracks()[track_index].audio_gain),
         Q_ARG(bool, timeline_model_.tracks()[track_index].audio_muted),
         Q_ARG(double, clip.audio_gain),
         Q_ARG(bool, clip.audio_muted),
         Q_ARG(qint64, static_cast<qint64>(track_index)),
         Q_ARG(qint64, static_cast<qint64>(clip_index)),
         Q_ARG(quint64, playback_generation_));
    sendCompositionToWorker();
}

void MainWindow::commitTimelineClipActivation(
    std::size_t track_index,
    std::size_t clip_index,
    std::size_t media_index,
    std::int64_t frame_index,
    bool show_cached_frame) {
    if (track_index >= timeline_model_.trackCount() ||
        clip_index >= timeline_model_.clipCount(track_index) ||
        media_index >= media_items_.size()) {
        return;
    }

    active_timeline_track_index_ = track_index;
    active_timeline_clip_index_ = clip_index;
    playback_frame_index_ = frame_index;
    {
        const QSignalBlocker blocker(media_list_);
        media_list_->setCurrentRow(static_cast<int>(media_index));
    }

    const auto& item = media_items_[media_index];
    media_details_->setText(mediaDetailsText(item.metadata));
    if (show_cached_frame) preview_widget_->setFrame(item.first_frame);
    updateTimelineState();
    updatePlaybackControls();
    updatePlaybackStatus();
}

void MainWindow::sendPlaybackCommand(const char* command) {
    if (playback_worker_ == nullptr || media_list_ == nullptr ||
        !canPlaybackSelectedMedia() || pending_clip_activation_.has_value()) {
        return;
    }

    const auto active_index = active_timeline_clip_index_;
    const auto active_track = active_timeline_track_index_.value_or(0);
    if (active_index.has_value() &&
        active_track < timeline_model_.trackCount() &&
        *active_index < timeline_model_.clipCount(active_track) &&
        std::string_view(command) == "stepForward") {
        const auto& clip = timeline_model_.tracks()[active_track].clips[*active_index];
        if (playback_frame_index_ >= clip.timeline_duration_frames - 1) {
            const auto global_frame = clip.timeline_start_frame +
                clip.timeline_duration_frames;
            if (const auto next = timeline_model_.topClipAt(global_frame);
                next.has_value()) {
                const auto& next_clip = timeline_model_.tracks()[next->track_index]
                    .clips[next->clip_index];
                activateTimelineClipAt(
                    next->track_index,
                    next->clip_index,
                    std::max<std::int64_t>(
                        0,
                        global_frame - next_clip.timeline_start_frame),
                    false);
            } else if (std::any_of(
                           timeline_model_.tracks().begin(),
                           timeline_model_.tracks().end(),
                           [global_frame](const timeline::TimelineTrack& track) {
                               return std::any_of(
                                   track.clips.begin(),
                                   track.clips.end(),
                                   [global_frame](const timeline::TimelineClip& candidate) {
                                       return candidate.timeline_start_frame > global_frame;
                                   });
                           })) {
                statusBar()->showMessage("Gap in timeline.");
            } else {
                statusBar()->showMessage("Already at the end of the timeline.");
            }
            return;
        }
    }

    if (active_index.has_value() &&
        active_track < timeline_model_.trackCount() &&
        *active_index < timeline_model_.clipCount(active_track) &&
        std::string_view(command) == "stepBackward" &&
        playback_frame_index_ <= 0) {
        const auto& clip = timeline_model_.tracks()[active_track].clips[*active_index];
        const auto global_frame = clip.timeline_start_frame - 1;
        if (global_frame >= 0) {
            if (const auto previous = timeline_model_.topClipAt(global_frame);
                previous.has_value()) {
                const auto& previous_clip = timeline_model_.tracks()[previous->track_index]
                    .clips[previous->clip_index];
            activateTimelineClipAt(
                previous->track_index,
                previous->clip_index,
                global_frame - previous_clip.timeline_start_frame,
                false);
            } else {
                statusBar()->showMessage("Gap in timeline.");
            }
        } else {
            statusBar()->showMessage("Already at the beginning of the timeline.");
        }
        return;
    }

    QMetaObject::invokeMethod(playback_worker_, command, Qt::QueuedConnection);
}

void MainWindow::updatePlaybackControls() {
    const bool has_media = canPlaybackSelectedMedia() &&
        !pending_clip_activation_.has_value();
    if (previous_frame_button_ != nullptr) previous_frame_button_->setEnabled(has_media);
    if (play_pause_button_ != nullptr) play_pause_button_->setEnabled(has_media);
    if (next_frame_button_ != nullptr) next_frame_button_->setEnabled(has_media);
    if (delete_clip_action_ != nullptr) {
        delete_clip_action_->setEnabled(
            canPlaybackSelectedMedia() && !pending_clip_activation_.has_value());
    }
    if (play_pause_button_ != nullptr) {
        play_pause_button_->setText(playback_is_playing_ ? "Pause" : "Play");
    }
}

void MainWindow::updatePlaybackStatus() {
    if (playback_status_label_ == nullptr) return;

    if (pending_clip_activation_.has_value()) {
        playback_status_label_->setText("Loading timeline clip...");
        return;
    }

    const auto selected_index = selectedMediaIndex();
    if (!selected_index.has_value()) {
        playback_status_label_->setText("No media selected.");
        return;
    }

    if (!canPreviewSelectedMedia()) {
        playback_status_label_->setText("Select the timeline media to play.");
        return;
    }

    const auto& metadata = media_items_[*selected_index].metadata;
    QString total = metadata.frame_count.has_value()
        ? QString::number(*metadata.frame_count)
        : "?";
    if (active_timeline_track_index_.has_value() &&
        active_timeline_clip_index_.has_value() &&
        *active_timeline_track_index_ < timeline_model_.trackCount() &&
        *active_timeline_clip_index_ < timeline_model_.clipCount(
            *active_timeline_track_index_)) {
        total = QString::number(
            timeline_model_.tracks()[*active_timeline_track_index_]
                .clips[*active_timeline_clip_index_]
                .timeline_duration_frames);
    }
    const QString state = playback_is_playing_ ? "Playing" : "Paused";
    playback_status_label_->setText(
        QString("%1 - Frame %2 / %3")
            .arg(state)
            .arg(playback_frame_index_ + 1)
            .arg(total));
}

void MainWindow::handlePlaybackFrame(
    playback::VideoFramePtr frame,
    qint64 frame_index,
    quint64 generation) {
    if (generation != playback_generation_ || frame == nullptr) return;

    if (pending_clip_activation_.has_value() &&
        pending_clip_activation_->generation == generation) {
        const auto pending = *pending_clip_activation_;
        pending_clip_activation_.reset();
        commitTimelineClipActivation(
            pending.track_index,
            pending.clip_index,
            pending.media_index,
            frame_index,
            false);
        preview_widget_->setFrame(*frame);
        if (timeline_widget_ != nullptr) {
            timeline_widget_->setPlayheadFrame(timelinePlayheadFrame());
        }
        updateInspector();
        updatePlaybackStatus();
        if (pending.resume_playback && playback_worker_ != nullptr) {
            QMetaObject::invokeMethod(playback_worker_, "play", Qt::QueuedConnection);
        }
        return;
    }

    playback_frame_index_ = frame_index;
    preview_widget_->setFrame(*frame);
    if (timeline_widget_ != nullptr && selectedMediaMatchesTimeline()) {
        timeline_widget_->setPlayheadFrame(timelinePlayheadFrame());
    }
    updateInspector();
    updatePlaybackStatus();
}

void MainWindow::handlePlaybackMediaReady(quint64 generation) {
    if (generation != playback_generation_ ||
        !pending_clip_activation_.has_value() ||
        pending_clip_activation_->generation != generation) {
        return;
    }

    const auto pending = *pending_clip_activation_;
    if (pending.target_frame > 0 || pending.source_start_frame > 0) {
        playback_worker_->requestSeek(
            static_cast<qint64>(pending.target_frame),
            generation);
        return;
    }

    pending_clip_activation_.reset();
    commitTimelineClipActivation(
        pending.track_index,
        pending.clip_index,
        pending.media_index,
        0,
        true);

    if (playback_worker_ != nullptr) {
        playback_worker_->requestSeek(0, generation);
    }

    if (pending.resume_playback && playback_worker_ != nullptr) {
        QMetaObject::invokeMethod(playback_worker_, "play", Qt::QueuedConnection);
    }
}

void MainWindow::handlePlaybackStateChanged(bool playing, quint64 generation) {
    if (generation != playback_generation_) return;

    playback_is_playing_ = playing;
    updatePlaybackControls();
    updatePlaybackStatus();
}

void MainWindow::handlePlaybackFinished(
    quint64 generation,
    bool during_playback) {
    if (generation != playback_generation_) return;

    playback_is_playing_ = false;
    if (during_playback && active_timeline_track_index_.has_value() &&
        active_timeline_clip_index_.has_value() &&
        *active_timeline_track_index_ < timeline_model_.trackCount() &&
        *active_timeline_clip_index_ < timeline_model_.clipCount(
            *active_timeline_track_index_)) {
        const auto& active_track = timeline_model_.tracks()
            [*active_timeline_track_index_];
        const auto& current_clip = active_track.clips
            [*active_timeline_clip_index_];
        const auto boundary = current_clip.timeline_start_frame +
            current_clip.timeline_duration_frames;
        if (const auto next = timeline_model_.topClipAt(boundary);
            next.has_value()) {
            const auto& next_clip = timeline_model_.tracks()[next->track_index]
                .clips[next->clip_index];
            activateTimelineClipAt(
                next->track_index,
                next->clip_index,
                std::max<std::int64_t>(
                    0,
                    boundary - next_clip.timeline_start_frame),
                true);
            return;
        }
        const bool has_future_clip = std::any_of(
            timeline_model_.tracks().begin(),
            timeline_model_.tracks().end(),
            [boundary](const timeline::TimelineTrack& track) {
                return std::any_of(
                    track.clips.begin(),
                    track.clips.end(),
                    [boundary](const timeline::TimelineClip& clip) {
                        return clip.timeline_start_frame > boundary;
                    });
            });
        if (has_future_clip) {
            updatePlaybackControls();
            if (playback_status_label_ != nullptr) {
                playback_status_label_->setText("Gap in timeline.");
            }
            preview_widget_->clearFrame("Gap in timeline.");
            statusBar()->showMessage("Gap in timeline.");
            return;
        }
    }

    updatePlaybackControls();
    if (playback_status_label_ != nullptr) {
        playback_status_label_->setText(
            during_playback ? "End of timeline." : "End of media.");
    }
}

void MainWindow::handlePlaybackError(
    const QString& message,
    qint64 error_code,
    quint64 generation) {
    if (generation != playback_generation_) return;

    if (pending_clip_activation_.has_value() &&
        pending_clip_activation_->generation == generation) {
        const auto pending = *pending_clip_activation_;
        if (pending.track_index >= timeline_model_.trackCount() ||
            pending.clip_index >= timeline_model_.clipCount(pending.track_index)) {
            pending_clip_activation_.reset();
            playback_is_playing_ = false;
            updatePlaybackControls();
            updatePlaybackStatus();
            QMessageBox::warning(this, "Playback error", message);
            return;
        }
        const auto& clip = timeline_model_.tracks()[pending.track_index]
            .clips[pending.clip_index];
        logging::Context context{
            {"path", pathToUtf8(clip.source_path)},
            {"track_index", std::to_string(pending.track_index)},
            {"clip_index", std::to_string(pending.clip_index)},
            {"media_index", std::to_string(pending.media_index)},
            {"requested_frame", std::to_string(pending.target_frame)},
            {"source_start_frame", std::to_string(pending.source_start_frame)},
            {"segment_frame_count", std::to_string(pending.segment_frame_count)},
            {"generation", std::to_string(generation)}};
        if (error_code >= 0) {
            context.emplace_back("error_code", std::to_string(error_code));
        }
        logging::Logger::instance().log(
            logging::Level::Error,
            "playback",
            "transition",
            message.toUtf8().toStdString(),
            context);
        pending_clip_activation_.reset();
        statusBar()->showMessage("Could not activate the next timeline clip.");
    }

    playback_is_playing_ = false;
    updatePlaybackControls();
    if (playback_status_label_ != nullptr) {
        playback_status_label_->setText("Playback error.");
    }
    if (timeline_widget_ != nullptr && timeline_model_.hasClip()) {
        timeline_widget_->setPlayheadFrame(timelinePlayheadFrame());
    }
    QMessageBox::warning(this, "Playback error", message);
}

void MainWindow::handleTimelineSeekStarted() {
    if (!canPlaybackSelectedMedia() || pending_clip_activation_.has_value()) return;

    playback_is_playing_ = false;
    updatePlaybackControls();
    updatePlaybackStatus();
    sendPlaybackCommand("pause");
}

void MainWindow::handleTimelineSeek(qint64 frame_index) {
    if (playback_worker_ == nullptr ||
        !canPlaybackSelectedMedia() ||
        pending_clip_activation_.has_value()) {
        return;
    }

    ++playback_generation_;
    playback_is_playing_ = false;
    if (timeline_widget_ != nullptr) {
        timeline_widget_->setPlayheadFrame(timelinePlayheadFrame());
    }
    updatePlaybackControls();
    updatePlaybackStatus();

    playback_worker_->requestSeek(frame_index, playback_generation_);
}
