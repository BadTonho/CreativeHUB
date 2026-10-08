#include "main_window/main_window.h"
#include "main_window_support.h"

#include "logging/logger.h"
#include "ui/preview/preview_widget.h"
#include "workspaces/render/ui/render_workspace.h"
#include "project/project_file.h"
#include "settings/user_preferences.h"
#include "timeline/timeline_clip_edge_command.h"
#include "timeline/timeline_track_header_overlay.h"
#include "timeline/timeline_zoom.h"
#include "timeline/timeline_widget.h"
#include "ui/media_browser/media_browser_list_widget.h"
#include "ui/system/system_memory_indicator.h"
#include "ui/timeline/timeline_end_buttons.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QIcon>
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
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QRunnable>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QStatusBar>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>


using namespace main_window_detail;

void MainWindow::applyMonitorVolumePercent(int percent) {
    const auto normalized = std::clamp(
        percent,
        settings::kMinimumMonitorVolumePercent,
        settings::kMaximumMonitorVolumePercent);
    settings::setMonitorVolumePercent(normalized);
    if (editUi().monitor_volume != nullptr &&
        editUi().monitor_volume->value() != normalized) {
        const QSignalBlocker blocker(editUi().monitor_volume);
        editUi().monitor_volume->setValue(normalized);
    }
    if (editUi().monitor_volume_indicator != nullptr) {
        editUi().monitor_volume_indicator->setText(
            QString::number(normalized) + "%");
    }
    if (playback_controller_ != nullptr) {
        playback_controller_->setMonitorVolume(static_cast<double>(normalized) / 100.0);
    }
}

bool MainWindow::hasSelectedMedia() const noexcept {
    return selectedMediaIndex().has_value();
}

std::optional<timeline::ClipLocation>
MainWindow::selectedTimelineClipLocation() const noexcept {
    if (!timeline_model_.hasClip()) return std::nullopt;
    if (active_timeline_clip_id_.has_value()) {
        if (const auto location = timeline_model_.locateClip(*active_timeline_clip_id_);
            location.has_value()) {
            const auto& clip = timeline_model_.tracks()[location->track_index]
                .clips[location->clip_index];
            if (clip.kind == timeline::ClipKind::Text) return location;
            const auto selected_index = selectedMediaIndex();
            if (selected_index.has_value() &&
                normalizedPath(clip.source_path) == normalizedPath(
                    media_items_[*selected_index].metadata.source_path)) {
                return location;
            }
        }
    }
    if (active_timeline_track_index_cache_.has_value() &&
        active_timeline_clip_index_cache_.has_value() &&
        *active_timeline_track_index_cache_ < timeline_model_.trackCount() &&
        *active_timeline_clip_index_cache_ < timeline_model_.clipCount(
            *active_timeline_track_index_cache_)) {
        const auto& active_clip = timeline_model_.tracks()
            [*active_timeline_track_index_cache_].clips[*active_timeline_clip_index_cache_];
        if (active_clip.kind == timeline::ClipKind::Text) {
            return timeline::ClipLocation{
                *active_timeline_track_index_cache_, *active_timeline_clip_index_cache_};
        }
        const auto selected_index = selectedMediaIndex();
        if (selected_index.has_value() &&
            active_clip.source_path == media_items_[*selected_index].metadata.source_path) {
            return timeline::ClipLocation{
                *active_timeline_track_index_cache_, *active_timeline_clip_index_cache_};
        }
    }
    const auto selected_index = selectedMediaIndex();
    if (!selected_index.has_value()) return std::nullopt;
    const auto& selected = media_items_[*selected_index];
    for (std::size_t track = 0; track < timeline_model_.trackCount(); ++track) {
        const auto& clips = timeline_model_.tracks()[track].clips;
        for (std::size_t index = 0; index < clips.size(); ++index) {
            if (timeline::isMediaClipKind(clips[index].kind) &&
                clips[index].source_path == selected.metadata.source_path) {
                return timeline::ClipLocation{track, index};
            }
        }
    }
    return std::nullopt;
}

void MainWindow::synchronizeActiveTimelineSelection() noexcept {
    if (active_timeline_clip_id_.has_value()) {
        const auto location = timeline_model_.locateClip(*active_timeline_clip_id_);
        if (!location.has_value()) {
            clearActiveTimelineSelection();
            return;
        }
        const auto& track = timeline_model_.tracks()[location->track_index];
        active_timeline_track_id_ = track.track_id;
        active_timeline_track_index_cache_ = location->track_index;
        active_timeline_clip_index_cache_ = location->clip_index;
        return;
    }
    if (active_timeline_track_id_.has_value()) {
        const auto track_index = timeline_model_.locateTrack(*active_timeline_track_id_);
        if (track_index.has_value()) {
            active_timeline_track_index_cache_ = *track_index;
            return;
        }
        active_timeline_track_id_.reset();
        active_timeline_track_index_cache_.reset();
    }
    if (active_timeline_track_index_cache_.has_value() &&
        *active_timeline_track_index_cache_ < timeline_model_.trackCount()) {
        const auto& track = timeline_model_.tracks()[*active_timeline_track_index_cache_];
        active_timeline_track_id_ = track.track_id;
        if (active_timeline_clip_index_cache_.has_value() &&
            *active_timeline_clip_index_cache_ < track.clips.size()) {
            active_timeline_clip_id_ = track.clips[*active_timeline_clip_index_cache_].clip_id;
        }
    } else if (!active_timeline_clip_index_cache_.has_value()) {
        active_timeline_track_index_cache_.reset();
    }
}

void MainWindow::setActiveTimelineSelection(timeline::ClipLocation location) noexcept {
    if (location.track_index >= timeline_model_.trackCount() ||
        location.clip_index >= timeline_model_.clipCount(location.track_index)) {
        clearActiveTimelineSelection();
        return;
    }
    const auto& track = timeline_model_.tracks()[location.track_index];
    active_timeline_track_id_ = track.track_id;
    active_timeline_clip_id_ = track.clips[location.clip_index].clip_id;
    active_timeline_track_index_cache_ = location.track_index;
    active_timeline_clip_index_cache_ = location.clip_index;
}

void MainWindow::clearActiveTimelineSelection() noexcept {
    active_timeline_track_id_.reset();
    active_timeline_clip_id_.reset();
    active_timeline_track_index_cache_.reset();
    active_timeline_clip_index_cache_.reset();
}

std::optional<std::size_t> MainWindow::selectedTimelineClipIndex() const noexcept {
    const auto location = selectedTimelineClipLocation();
    return location.has_value()
        ? std::optional<std::size_t>{location->clip_index}
        : std::nullopt;
}

bool MainWindow::selectedMediaMatchesTimeline() const noexcept {
    return selectedTimelineClipIndex().has_value();
}

bool MainWindow::canPreviewSelectedMedia() const noexcept {
    return hasSelectedMedia() &&
        !media_items_[*selectedMediaIndex()].offline &&
        (!timeline_model_.hasClip() || selectedMediaMatchesTimeline());
}

bool MainWindow::canPlaybackSelectedMedia() const noexcept {
    if (!timeline_model_.hasClip() ||
        !active_timeline_track_index_cache_.has_value() ||
        !active_timeline_clip_index_cache_.has_value() ||
        *active_timeline_track_index_cache_ >= timeline_model_.trackCount() ||
        *active_timeline_clip_index_cache_ >= timeline_model_.clipCount(
            *active_timeline_track_index_cache_)) {
        return false;
    }

    const auto& clip = timeline_model_.tracks()[*active_timeline_track_index_cache_]
        .clips[*active_timeline_clip_index_cache_];
    if (clip.kind == timeline::ClipKind::Text) {
        return playback_controller_ != nullptr && playback_controller_->available();
    }

    const auto media = std::find_if(
        media_items_.begin(),
        media_items_.end(),
        [&clip](const ImportedMedia& item) {
            return normalizedPath(item.metadata.source_path) ==
                normalizedPath(clip.source_path);
        });
    return media != media_items_.end() && !media->offline &&
        playback_controller_ != nullptr && playback_controller_->available();
}

std::int64_t MainWindow::timelinePlayheadFrame() const noexcept {
    if (preserved_timeline_playhead_frame_.has_value()) {
        return std::max<std::int64_t>(
            0, *preserved_timeline_playhead_frame_);
    }
    if (!active_timeline_track_index_cache_.has_value() ||
        !active_timeline_clip_index_cache_.has_value() ||
        *active_timeline_track_index_cache_ >= timeline_model_.trackCount() ||
        *active_timeline_clip_index_cache_ >= timeline_model_.clipCount(
            *active_timeline_track_index_cache_)) {
        return editUi().timeline != nullptr
            ? editUi().timeline->playheadFrame()
            : 0;
    }
    const auto& clip = timeline_model_.tracks()[*active_timeline_track_index_cache_]
        .clips[*active_timeline_clip_index_cache_];
    if (clip.timeline_duration_frames <= 0) return clip.timeline_start_frame;
    const auto local_frame = std::clamp<std::int64_t>(
        playback_frame_index_, 0, clip.timeline_duration_frames - 1);
    if (clip.timeline_start_frame >
        std::numeric_limits<std::int64_t>::max() - local_frame) {
        return clip.timeline_start_frame;
    }
    return clip.timeline_start_frame + local_frame;
}

void MainWindow::updateHistoryActions() {
    const bool can_undo = edit_workspace_ != nullptr &&
            edit_workspace_->controller() != nullptr
        ? edit_workspace_->controller()->canUndo()
        : timeline_command_service_.canUndo();
    const bool can_redo = edit_workspace_ != nullptr &&
            edit_workspace_->controller() != nullptr
        ? edit_workspace_->controller()->canRedo()
        : timeline_command_service_.canRedo();
    if (undo_action_ != nullptr) undo_action_->setEnabled(can_undo);
    if (redo_action_ != nullptr) redo_action_->setEnabled(can_redo);
}

void MainWindow::applyTimelineEditResult(
    const application::TimelineEditResult& result,
    bool stop_playback) {
    if (!result.changed()) return;
    active_timeline_track_id_ = result.selection.active_track_id;
    active_timeline_clip_id_ = result.selection.active_clip_id;
    active_transition_ = result.selection.active_transition;
    playback_frame_index_ = std::max<std::int64_t>(0, result.playhead_frame);
    preserved_timeline_playhead_frame_ = result.preserved_playhead_frame;
    synchronizeActiveTimelineSelection();
    if (result.selection.selected_source_path.has_value() && media_list_ != nullptr) {
        const auto selected_path = normalizedPath(*result.selection.selected_source_path);
        const auto selected_media = std::find_if(
            media_items_.begin(), media_items_.end(),
            [&selected_path](const ImportedMedia& item) {
                return normalizedPath(item.metadata.source_path) == selected_path;
            });
        if (selected_media != media_items_.end()) {
            const auto media_index = static_cast<qint64>(
                std::distance(media_items_.begin(), selected_media));
            const QSignalBlocker blocker(media_list_);
            for (int row = 0; row < media_list_->count(); ++row) {
                auto* item = media_list_->item(row);
                if (item != nullptr &&
                    item->data(media_browser_ui::kMediaIndexRole).toLongLong() == media_index) {
                    media_list_->setCurrentItem(item);
                    break;
                }
            }
        }
    }
    if (result.invalidate_playback) {
        if (playback_controller_ != nullptr) {
            playback_controller_->invalidate(stop_playback);
        }
        playback_is_playing_ = false;
    }
    if (result.project_settings_changed && render_workspace_ != nullptr) {
        render_workspace_->refreshProjectSettings();
    }
    updateHistoryActions();
    updateProjectDirtyState();
}

void MainWindow::updatePlaybackAudioParameters() {
    if (playback_controller_ == nullptr ||
        !active_timeline_track_index_cache_.has_value() ||
        !active_timeline_clip_index_cache_.has_value() ||
        *active_timeline_track_index_cache_ >= timeline_model_.trackCount() ||
        *active_timeline_clip_index_cache_ >= timeline_model_.clipCount(
            *active_timeline_track_index_cache_)) {
        return;
    }
    playback_controller_->setAudioParametersForActiveClip();
}

void MainWindow::updateTimelineState() {
    if (edit_workspace_ != nullptr && edit_workspace_->controller() != nullptr) {
        edit_workspace_->controller()->updateTimelineState();
        synchronizeActiveTimelineSelection();
        requestTimelineAudioWaveforms();
        return;
    }
    synchronizeActiveTimelineSelection();
    if (editUi().timeline != nullptr) {
        editUi().timeline->setFrameRate(timeline_model_.frameRate());
        editUi().timeline->setTracks(timeline_model_.tracks());
    }
    requestTimelineAudioWaveforms();
}

void MainWindow::requestTimelineAudioWaveforms() {
    auto* timeline_widget = editUi().timeline;
    if (timeline_widget == nullptr) return;

    timeline_widget->clearAudioWaveforms();
    std::unordered_set<std::filesystem::path> requested_sources;
    for (const auto& track : timeline_model_.tracks()) {
        if (track.kind != timeline::TrackKind::Audio) continue;
        for (const auto& clip : track.clips) {
            if (clip.kind != timeline::ClipKind::Audio) continue;
            const auto source_path = media::MediaLibrary::canonicalPath(clip.source_path);
            if (!requested_sources.insert(source_path).second) continue;

            const auto signature = media::audioWaveformSourceSignature(source_path);
            if (!signature.has_value()) {
                const auto pending = pending_audio_waveforms_.find(source_path);
                if (pending != pending_audio_waveforms_.end()) {
                    pending->second.cancelled->store(true, std::memory_order_relaxed);
                    pending_audio_waveforms_.erase(pending);
                }
                failed_audio_waveforms_.erase(source_path);
                continue;
            }

            if (auto cached = audio_waveform_cache_.find(source_path, *signature)) {
                failed_audio_waveforms_.erase(source_path);
                timeline_widget->setAudioWaveform(source_path, cached);
                continue;
            }

            const auto failed = failed_audio_waveforms_.find(source_path);
            if (failed != failed_audio_waveforms_.end()) {
                if (failed->second == *signature) continue;
                failed_audio_waveforms_.erase(failed);
            }

            const auto pending = pending_audio_waveforms_.find(source_path);
            if (pending != pending_audio_waveforms_.end()) {
                if (pending->second.signature == *signature &&
                    pending->second.project_generation == project_generation_) {
                    continue;
                }
                pending->second.cancelled->store(true, std::memory_order_relaxed);
                pending_audio_waveforms_.erase(pending);
            }

            const auto work_id = next_audio_waveform_work_id_++;
            const auto project_generation = project_generation_;
            auto cancelled = std::make_shared<std::atomic_bool>(false);
            pending_audio_waveforms_.emplace(source_path, PendingAudioWaveform{
                *signature, project_generation, work_id, cancelled});

            QPointer<MainWindow> guard(this);
            media_task_pool_.start(QRunnable::create(
                [guard, source_path, signature = *signature,
                 project_generation, work_id, cancelled]() mutable {
                    std::optional<media::AudioWaveform> waveform;
                    std::string failure;
                    try {
                        waveform = media::decodeAudioWaveform(
                            source_path,
                            [cancelled]() {
                                return cancelled->load(std::memory_order_relaxed);
                            });
                    } catch (const std::exception& error) {
                        failure = error.what();
                    } catch (...) {
                        failure = "Unknown failure while decoding the audio waveform.";
                    }
                    if (guard.isNull()) return;
                    QMetaObject::invokeMethod(
                        guard.data(),
                        [guard, source_path, signature, project_generation, work_id,
                         cancelled, waveform = std::move(waveform),
                         failure = std::move(failure)]() mutable {
                            if (guard.isNull()) return;
                            guard->finishTimelineAudioWaveform(
                                source_path, signature, project_generation, work_id,
                                cancelled->load(std::memory_order_relaxed)
                                    ? std::nullopt : std::move(waveform),
                                std::move(failure));
                        },
                        Qt::QueuedConnection);
                }));
        }
    }
}

void MainWindow::cancelTimelineAudioWaveforms() noexcept {
    for (auto& [source_path, pending] : pending_audio_waveforms_) {
        static_cast<void>(source_path);
        pending.cancelled->store(true, std::memory_order_relaxed);
    }
    pending_audio_waveforms_.clear();
}

void MainWindow::finishTimelineAudioWaveform(
    std::filesystem::path source_path,
    media::AudioWaveformSourceSignature signature,
    std::uint64_t project_generation,
    std::uint64_t work_id,
    std::optional<media::AudioWaveform> waveform,
    std::string failure) {
    const auto pending = pending_audio_waveforms_.find(source_path);
    if (pending == pending_audio_waveforms_.end() ||
        pending->second.work_id != work_id) {
        return;
    }
    pending_audio_waveforms_.erase(pending);
    if (project_generation != project_generation_) return;

    const auto still_in_timeline = std::any_of(
        timeline_model_.tracks().begin(), timeline_model_.tracks().end(),
        [&source_path](const timeline::TimelineTrack& track) {
            return track.kind == timeline::TrackKind::Audio &&
                std::any_of(track.clips.begin(), track.clips.end(),
                    [&source_path](const timeline::TimelineClip& clip) {
                        return clip.kind == timeline::ClipKind::Audio &&
                            clip.source_path == source_path;
                    });
        });
    if (!still_in_timeline) return;

    const auto current_signature = media::audioWaveformSourceSignature(source_path);
    if (!current_signature.has_value() || *current_signature != signature) return;
    if (!failure.empty()) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "audio",
            "timeline_waveform",
            failure,
            {{"error_code", "audio_waveform_decode_failed"},
             {"path", pathToUtf8(source_path)},
             {"project_generation", std::to_string(project_generation)},
             {"work_id", std::to_string(work_id)}});
        failed_audio_waveforms_[source_path] = signature;
        return;
    }
    if (!waveform.has_value() || waveform->peaks.empty()) {
        failed_audio_waveforms_[source_path] = signature;
        return;
    }

    std::shared_ptr<const media::AudioWaveform> cached;
    try {
        cached = std::make_shared<const media::AudioWaveform>(std::move(*waveform));
    } catch (const std::exception& error) {
        failed_audio_waveforms_[source_path] = signature;
        logging::Logger::instance().log(
            logging::Level::Warning,
            "audio",
            "timeline_waveform_cache",
            error.what(),
            {{"error_code", "audio_waveform_cache_allocation_failed"},
             {"path", pathToUtf8(source_path)},
             {"project_generation", std::to_string(project_generation)},
             {"work_id", std::to_string(work_id)}});
        return;
    } catch (...) {
        failed_audio_waveforms_[source_path] = signature;
        logging::Logger::instance().log(
            logging::Level::Warning,
            "audio",
            "timeline_waveform_cache",
            "Unknown failure while allocating waveform cache storage.",
            {{"error_code", "audio_waveform_cache_allocation_failed"},
             {"path", pathToUtf8(source_path)},
             {"project_generation", std::to_string(project_generation)},
             {"work_id", std::to_string(work_id)}});
        return;
    }
    const auto waveform_bytes = cached->memoryBytes();
    const bool exceeds_cache_limit =
        waveform_bytes > media::AudioWaveformCache::kDefaultByteLimit;
    if (!audio_waveform_cache_.insert(source_path, signature, cached)) {
        failed_audio_waveforms_[source_path] = signature;
        logging::Logger::instance().log(
            logging::Level::Warning,
            "audio",
            "timeline_waveform_cache",
            exceeds_cache_limit
                ? "The decoded waveform exceeded the bounded in-memory cache limit."
                : "The decoded waveform could not be retained in the bounded in-memory cache.",
            {{"error_code", exceeds_cache_limit
                    ? "audio_waveform_cache_limit"
                    : "audio_waveform_cache_insert_failed"},
             {"path", pathToUtf8(source_path)},
             {"project_generation", std::to_string(project_generation)},
             {"work_id", std::to_string(work_id)},
             {"waveform_bytes", std::to_string(waveform_bytes)},
             {"cache_limit_bytes",
              std::to_string(media::AudioWaveformCache::kDefaultByteLimit)}});
        return;
    }
    failed_audio_waveforms_.erase(source_path);
    if (editUi().timeline != nullptr) {
        editUi().timeline->setAudioWaveform(source_path, cached);
    }
}

void MainWindow::handleMediaDropAt(
    const QString& source_path,
    timeline::TrackId track_id,
    qint64 timeline_frame) {
    const auto source_text = source_path.toUtf8().toStdString();
    try {
        const auto path = normalizedPath(
            QFileInfo(source_path).filesystemFilePath());
        const auto media = std::find_if(
            media_items_.begin(),
            media_items_.end(),
            [&path](const ImportedMedia& item) {
                return normalizedPath(item.metadata.source_path) == path;
            });
        if (media == media_items_.end()) {
            statusBar()->showMessage(
                "Import this media before adding it to the timeline.");
            return;
        }
        if (media->offline) {
            statusBar()->showMessage(
                "Offline media cannot be added to the timeline.");
            return;
        }
        const auto result = edit_workspace_->controller()->addMediaClip(
            media->metadata.source_path, track_id, timeline_frame);
        if (result.changed()) {
            const auto media_index = static_cast<int>(
                std::distance(media_items_.begin(), media));
            updateMediaDetails(media_index);
        }
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "ui",
            "timeline_drop",
            error.what(),
            {{"path", source_text},
             {"track_id", std::to_string(track_id)},
             {"timeline_frame", std::to_string(timeline_frame)}});
        statusBar()->showMessage("Could not add dropped media to the timeline.");
        QMessageBox::warning(
            this,
            "Could not add media",
            "The dropped media could not be added to the timeline.");
    }
}

void MainWindow::handleMediaGroupDropAt(
    const QString& source_path,
    timeline::TrackKind track_kind,
    qint64 timeline_frame) {
    try {
        const auto path = normalizedPath(
            QFileInfo(source_path).filesystemFilePath());
        const auto media = std::find_if(
            media_items_.begin(), media_items_.end(),
            [&path](const ImportedMedia& item) {
                return normalizedPath(item.metadata.source_path) == path;
            });
        if (media == media_items_.end()) {
            statusBar()->showMessage(
                "Import this media before adding it to the timeline.");
            return;
        }
        if (media->offline) {
            statusBar()->showMessage(
                "Offline media cannot be added to the timeline.");
            return;
        }
        const auto result = edit_workspace_->controller()->addMediaClips(
            {media->metadata.source_path}, 0, timeline_frame, track_kind);
        if (result.changed()) {
            updateMediaDetails(static_cast<int>(
                std::distance(media_items_.begin(), media)));
        }
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error, "timeline", "add_media_to_empty_group",
            error.what(), {{"source_path", source_path.toStdString()},
                           {"timeline_frame", std::to_string(timeline_frame)}});
        statusBar()->showMessage(
            "The media could not be added to the Timeline.");
    }
}
