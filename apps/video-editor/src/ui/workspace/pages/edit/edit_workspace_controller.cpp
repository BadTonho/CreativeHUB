#include "ui/workspace/pages/edit/edit_workspace_controller.h"

#include "logging/logger.h"
#include "timeline/timeline_clip_edge_command.h"
#include "timeline/timeline_zoom.h"
#include "timeline/timeline_widget.h"

#include <QApplication>
#include <QColorDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFontComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QInputDialog>
#include <QLineEdit>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

namespace ui {

namespace {

constexpr int transform_slider_resolution = 1000;
constexpr std::array<double, 5> transform_minimums{
    -10.0, -10.0, 0.01, -3600.0, 0.0};
constexpr std::array<double, 5> transform_maximums{
    10.0, 10.0, 20.0, 3600.0, 1.0};

int transformSliderValue(double value, double minimum, double maximum) {
    if (maximum <= minimum) return 0;
    const auto fraction = std::clamp(
        (value - minimum) / (maximum - minimum), 0.0, 1.0);
    return static_cast<int>(std::lround(fraction * transform_slider_resolution));
}

int timelineZoomLevelIndex(double factor) {
    const auto match = std::min_element(
        timeline::kTimelineZoomLevels.begin(), timeline::kTimelineZoomLevels.end(),
        [factor](double left, double right) {
            return std::abs(left - factor) < std::abs(right - factor);
        });
    return static_cast<int>(std::distance(
        timeline::kTimelineZoomLevels.begin(), match));
}

std::int64_t localFrameAtTimelinePlayhead(
    const timeline::TimelineClip& clip,
    std::int64_t playhead_frame) {
    if (playhead_frame < clip.timeline_start_frame) return 0;
    const auto local_frame = playhead_frame - clip.timeline_start_frame;
    return local_frame >= 0 && local_frame < clip.timeline_duration_frames
        ? local_frame
        : 0;
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return std::string(
        reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

}  // namespace

EditWorkspaceController::EditWorkspaceController(
    application::EditorSession& session,
    application::TimelineCommandService& command_service,
    QObject* parent) noexcept
    : QObject(parent),
      session_(session),
      command_service_(command_service),
      timeline_model_(session.timeline()),
      active_timeline_track_id_(session.selectionForUi().active_track_id),
      active_timeline_clip_id_(session.selectionForUi().active_clip_id),
      preserved_timeline_playhead_frame_(session.preservedPlayheadFrameForUi()),
      active_transition_(session.selectionForUi().active_transition),
      playback_frame_index_(session.playheadFrameForUi()) {}

application::TimelineEditResult EditWorkspaceController::undo() {
    try {
        const auto result = command_service_.undo();
        publishResult(result);
        if (result.changed()) {
            publishCommittedEdit(
                result, true, true, QStringLiteral("Timeline edit undone."));
        }
        return result;
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error, "timeline", "undo", error.what(), {});
        publishHistoryState();
        emit statusMessageRequested(
            QStringLiteral("Could not undo the timeline edit."));
        return {};
    }
}

application::TimelineEditResult EditWorkspaceController::redo() {
    try {
        const auto result = command_service_.redo();
        publishResult(result);
        if (result.changed()) {
            publishCommittedEdit(
                result, true, true, QStringLiteral("Timeline edit redone."));
        }
        return result;
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error, "timeline", "redo", error.what(), {});
        publishHistoryState();
        emit statusMessageRequested(
            QStringLiteral("Could not redo the timeline edit."));
        return {};
    }
}

application::TimelineEditResult EditWorkspaceController::deleteSelectedClip() {
    const auto clip_id = session_.selection().active_clip_id;
    if (!clip_id.has_value()) {
        application::TimelineEditResult result;
        result.status = application::EditStatus::NoChange;
        publishResult(result);
        return result;
    }
    return execute(application::DeleteClipCommand{*clip_id});
}

application::TimelineEditResult EditWorkspaceController::addTrack(std::string name) {
    auto result = execute(application::AddTrackCommand{std::move(name)});
    if (result.changed()) {
        publishCommittedEdit(result, true, false, QStringLiteral("Video track added."));
    }
    return result;
}

application::TimelineEditResult EditWorkspaceController::renameTrack(
    timeline::TrackId track_id,
    std::string name) {
    auto result = execute(application::RenameTrackCommand{track_id, std::move(name)});
    if (result.changed()) {
        publishCommittedEdit(result, true, false, QStringLiteral("Track renamed."));
    }
    return result;
}

application::TimelineEditResult EditWorkspaceController::moveTrack(
    timeline::TrackId track_id,
    std::size_t target_index) {
    auto result = execute(application::MoveTrackCommand{track_id, target_index});
    if (result.changed()) {
        publishCommittedEdit(result, true, true, QStringLiteral("Track order updated."));
    }
    return result;
}

application::TimelineEditResult EditWorkspaceController::removeTrack(
    timeline::TrackId track_id) {
    auto result = execute(application::RemoveTrackCommand{track_id});
    if (result.changed()) {
        publishCommittedEdit(result, true, true, QStringLiteral("Video track removed."));
    }
    return result;
}

application::TimelineEditResult EditWorkspaceController::addMediaClip(
    const std::filesystem::path& source_path,
    timeline::TrackId track_id,
    std::optional<std::int64_t> timeline_frame) {
    application::TimelineEditResult rejected;
    if (timeline_frame.has_value() && *timeline_frame < 0) {
        rejected.reason = application::EditReason::InvalidPosition;
        publishResult(rejected);
        emit statusMessageRequested(
            QStringLiteral("The media cannot be placed at that position."));
        return rejected;
    }

    const auto track_index = timeline_model_.locateTrack(track_id);
    if (!track_index.has_value()) {
        rejected.reason = application::EditReason::InvalidTarget;
        publishResult(rejected);
        emit statusMessageRequested(QStringLiteral("The selected track is unavailable."));
        return rejected;
    }

    const auto media_index = session_.mediaLibrary().indexForPath(source_path);
    if (media_index == session_.mediaItems().size()) {
        rejected.reason = application::EditReason::MediaNotFound;
        publishResult(rejected);
        emit statusMessageRequested(
            QStringLiteral("Import this media before adding it to the timeline."));
        return rejected;
    }
    const auto& media_item = session_.mediaItems()[media_index];
    if (media_item.offline) {
        rejected.reason = application::EditReason::OfflineMedia;
        publishResult(rejected);
        emit statusMessageRequested(
            QStringLiteral("Offline media cannot be added to the timeline."));
        return rejected;
    }

    try {
        const auto result = execute(application::AddMediaClipCommand{
            media_item.metadata.source_path,
            track_id,
            timeline_frame});
        if (!result.changed()) {
            if (result.reason == application::EditReason::Overlap ||
                result.reason == application::EditReason::InvalidPosition ||
                result.reason == application::EditReason::InvalidTarget) {
                emit statusMessageRequested(
                    QStringLiteral("The media cannot be placed at that position."));
            } else if (result.reason == application::EditReason::InvalidTimingMetadata) {
                logging::Logger::instance().log(
                    logging::Level::Error,
                    "timeline",
                    "add_clip",
                    "Media does not contain enough timing metadata for timeline placement.",
                    {{"path", pathToUtf8(media_item.metadata.source_path)},
                     {"track_id", std::to_string(track_id)},
                     {"timeline_frame", timeline_frame.has_value()
                            ? std::to_string(*timeline_frame)
                            : std::string("next_available")}});
                emit warningMessageRequested(
                    QStringLiteral("Could not add media"),
                    QStringLiteral("This media does not contain enough timing metadata for the timeline."));
                emit statusMessageRequested(
                    QStringLiteral("Could not add media to the timeline."));
            }
            return result;
        }

        publishCommittedEdit(
            result, true, true, QStringLiteral("Media added to the timeline."));
        if (result.selection.active_clip_id.has_value()) {
            if (*track_index == 0) {
                emit activateTimelineClipRequested(
                    *result.selection.active_clip_id, 0, false, false);
            } else {
                emit showTimelineClipPreviewRequested(
                    *result.selection.active_clip_id);
            }
        }
        return result;
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "add_clip",
            error.what(),
            {{"path", pathToUtf8(source_path)},
             {"track_id", std::to_string(track_id)},
             {"timeline_frame", timeline_frame.has_value()
                    ? std::to_string(*timeline_frame)
                    : std::string("next_available")}});
        emit statusMessageRequested(
            QStringLiteral("Could not add the media to the timeline."));
        emit warningMessageRequested(
            QStringLiteral("Timeline error"),
            QStringLiteral("The media could not be added to the timeline."));
        return rejected;
    }
}

void EditWorkspaceController::addTextClipAt(
    timeline::TrackId track_id,
    qint64 timeline_frame) {
    const auto track_index = timeline_model_.locateTrack(track_id);
    if (!track_index.has_value() || timeline_frame < 0) {
        emit statusMessageRequested(
            QStringLiteral("No video track is available for text."));
        return;
    }

    double frame_rate = 30.0;
    if (session_.selection().selected_source_path.has_value()) {
        const auto media_index = session_.mediaLibrary().indexForPath(
            *session_.selection().selected_source_path);
        if (media_index < session_.mediaItems().size()) {
            const auto& metadata = session_.mediaItems()[media_index].metadata;
            if (metadata.frame_rate.has_value() &&
                std::isfinite(*metadata.frame_rate) && *metadata.frame_rate > 0.0) {
                frame_rate = *metadata.frame_rate;
            }
        }
    }
    const auto duration_frames = std::max<std::int64_t>(
        1, static_cast<std::int64_t>(std::ceil(frame_rate * 5.0)));
    try {
        const auto result = execute(application::AddTextClipCommand{
            track_id, timeline_frame, duration_frames, frame_rate});
        if (!result.changed()) {
            if (result.reason == application::EditReason::Overlap) {
                emit statusMessageRequested(
                    QStringLiteral("A text clip already occupies that range."));
            } else {
                emit statusMessageRequested(
                    QStringLiteral("The text clip could not be added."));
            }
            return;
        }

        publishCommittedEdit(
            result, false, true, QStringLiteral("Text clip added."));
        if (playback_available_ && result.selection.active_clip_id.has_value()) {
            const auto location = timeline_model_.locateClip(
                *result.selection.active_clip_id);
            if (location.has_value() &&
                timeline_model_.tracks()[location->track_index]
                    .clips[location->clip_index].kind == timeline::ClipKind::Text) {
                emit renderCompositionFrameRequested(
                    timelinePlayheadFrame(), playback_frame_index_);
                return;
            }
        }
        if (canPlaybackSelectedMedia()) {
            emit seekActiveClipRequested(playback_frame_index_);
        }
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "add_text",
            error.what(),
            {{"track_id", std::to_string(track_id)},
             {"timeline_frame", std::to_string(timeline_frame)}});
        emit statusMessageRequested(
            QStringLiteral("Could not add the text clip."));
        emit warningMessageRequested(
            QStringLiteral("Timeline error"),
            QStringLiteral("The text clip could not be added."));
    }
}

void EditWorkspaceController::handleTimelineEffectDrop(
    const QString& effect_id,
    timeline::TrackId track_id,
    qint64 timeline_frame) {
    if (effect_id != QStringLiteral("text.text")) {
        emit statusMessageRequested(
            QStringLiteral("This effect cannot be added to the timeline."));
        return;
    }
    addTextClipAt(track_id, timeline_frame);
}

void EditWorkspaceController::promptAddVideoTrack(QWidget* dialog_parent) {
    bool accepted = false;
    const auto name = QInputDialog::getText(
        dialog_parent,
        QStringLiteral("Add Video Track"),
        QStringLiteral("Track name:"),
        QLineEdit::Normal,
        QStringLiteral("Video %1").arg(session_.timeline().trackCount() + 1),
        &accepted);
    if (!accepted) return;

    try {
        const auto result = addTrack(name.toUtf8().toStdString());
        if (!result.changed()) {
            emit statusMessageRequested(QStringLiteral("The track name is invalid."));
        }
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "add_track",
            error.what(),
            {});
        emit statusMessageRequested(QStringLiteral("Could not add the video track."));
    }
}

void EditWorkspaceController::promptRenameActiveTrack(QWidget* dialog_parent) {
    const auto track_id = session_.selection().active_track_id;
    if (!track_id.has_value()) return;
    const auto track_index = session_.timeline().locateTrack(*track_id);
    if (!track_index.has_value()) return;

    bool accepted = false;
    const auto current = QString::fromUtf8(
        session_.timeline().tracks()[*track_index].name.data(),
        static_cast<qsizetype>(session_.timeline().tracks()[*track_index].name.size()));
    const auto name = QInputDialog::getText(
        dialog_parent,
        QStringLiteral("Rename Track"),
        QStringLiteral("Track name:"),
        QLineEdit::Normal,
        current,
        &accepted);
    if (!accepted) return;

    try {
        const auto result = renameTrack(*track_id, name.toUtf8().toStdString());
        if (result.status == application::EditStatus::Rejected &&
            result.reason == application::EditReason::InvalidName) {
            emit statusMessageRequested(QStringLiteral("The track name is invalid."));
        }
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "rename_track",
            error.what(),
            {{"track_index", std::to_string(*track_index)}});
        emit statusMessageRequested(QStringLiteral("Could not rename the track."));
    }
}

void EditWorkspaceController::moveActiveTrack(int direction) {
    if (direction == 0 || session_.timeline().trackCount() < 2) return;
    const auto track_id = session_.selection().active_track_id;
    if (!track_id.has_value()) return;
    const auto source_index = session_.timeline().locateTrack(*track_id);
    if (!source_index.has_value()) return;
    if ((direction < 0 && *source_index == 0) ||
        (direction > 0 && *source_index + 1 >= session_.timeline().trackCount())) {
        return;
    }
    const auto target_index = direction < 0 ? *source_index - 1 : *source_index + 1;
    try {
        static_cast<void>(moveTrack(*track_id, target_index));
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "move_track",
            error.what(),
            {{"from_index", std::to_string(*source_index)},
             {"to_index", std::to_string(target_index)}});
        emit statusMessageRequested(QStringLiteral("Could not move the track."));
    }
}

void EditWorkspaceController::removeActiveTrack() {
    const auto track_id = session_.selection().active_track_id;
    if (!track_id.has_value()) return;
    const auto track_index = session_.timeline().locateTrack(*track_id);
    if (!track_index.has_value()) return;
    try {
        const auto result = removeTrack(*track_id);
        if (result.reason == application::EditReason::TrackNotEmpty) {
            emit statusMessageRequested(QStringLiteral("Only empty tracks can be removed."));
        }
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "remove_track",
            error.what(),
            {{"track_index", std::to_string(*track_index)}});
        emit statusMessageRequested(QStringLiteral("Could not remove the video track."));
    }
}

void EditWorkspaceController::setTimelineWidget(
    timeline::TimelineWidget* timeline_widget) {
    if (timeline_widget_ == timeline_widget) return;
    timeline_widget_ = timeline_widget;
    if (timeline_widget_ == nullptr) return;
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::clipMoveRequested,
        this,
        &EditWorkspaceController::moveClip);
    connect(timeline_widget_, &timeline::TimelineWidget::clipSelected,
            this, &EditWorkspaceController::handleTimelineClipSelectionChanged);
    connect(timeline_widget_, &timeline::TimelineWidget::clipSelectionCleared,
            this, &EditWorkspaceController::handleTimelineClipSelectionCleared);
    connect(timeline_widget_, &timeline::TimelineWidget::clipSplitRequested,
            this, &EditWorkspaceController::handleTimelineClipSplit);
    connect(timeline_widget_, &timeline::TimelineWidget::clipEdgeTrimRequested,
            this, &EditWorkspaceController::handleTimelineClipTrim);
    connect(timeline_widget_, &timeline::TimelineWidget::trimStarted,
            this, &EditWorkspaceController::handleTimelineTrimStarted);
    connect(timeline_widget_, &timeline::TimelineWidget::transitionSelected,
            this, &EditWorkspaceController::handleTimelineTransitionSelected);
    connect(timeline_widget_, &timeline::TimelineWidget::transitionSelectionCleared,
            this, &EditWorkspaceController::handleTimelineTransitionSelectionCleared);
    connect(timeline_widget_, &timeline::TimelineWidget::transitionAddRequested,
            this, &EditWorkspaceController::handleTimelineTransitionAdd);
    connect(timeline_widget_, &timeline::TimelineWidget::transitionRemoveRequested,
            this, &EditWorkspaceController::handleTimelineTransitionRemove);
    connect(timeline_widget_, &timeline::TimelineWidget::seekStarted,
            this, &EditWorkspaceController::handleTimelineSeekStarted);
    connect(timeline_widget_, &timeline::TimelineWidget::seekRequested,
            this, &EditWorkspaceController::handleTimelineSeek);
    connect(timeline_widget_, &timeline::TimelineWidget::mediaDropRequested,
            this, &EditWorkspaceController::timelineMediaDropRequested);
    connect(timeline_widget_, &timeline::TimelineWidget::effectDropRequested,
            this, &EditWorkspaceController::handleTimelineEffectDrop);
    connect(timeline_widget_, &timeline::TimelineWidget::editImageClipRequested,
            this, &EditWorkspaceController::timelineImageClipEditRequested);
    connect(timeline_widget_, &timeline::TimelineWidget::zoomRequested,
            this, &EditWorkspaceController::applyTimelineZoom);
    connect(timeline_widget_, &timeline::TimelineWidget::zoomChanged, this,
            [this](double factor) {
                if (ui_.zoom_slider != nullptr) {
                    const QSignalBlocker blocker(ui_.zoom_slider);
                    ui_.zoom_slider->setValue(timelineZoomLevelIndex(factor));
                }
                if (ui_.zoom_indicator != nullptr) {
                    ui_.zoom_indicator->setText(
                        QString::number(static_cast<int>(std::lround(factor * 100.0))) + "%");
                }
                if (ui_.zoom_out != nullptr) {
                    ui_.zoom_out->setEnabled(timeline_widget_->canZoomOut());
                }
                if (ui_.zoom_in != nullptr) {
                    ui_.zoom_in->setEnabled(timeline_widget_->canZoomIn());
                }
                emit projectDirtyStateUpdateRequested();
            });
    connect(timeline_widget_, &timeline::TimelineWidget::trackRowHeightChanged,
            this, [this](double) { emit projectDirtyStateUpdateRequested(); });
    connect(timeline_widget_, &timeline::TimelineWidget::snapEnabledChanged,
            this, &EditWorkspaceController::timelineSnapChanged);
}

void EditWorkspaceController::setUi(EditWorkspaceUi ui) {
    ui_ = ui;
    setTimelineWidget(ui_.timeline);
    if (ui_.zoom_slider != nullptr) {
        connect(ui_.zoom_slider, &QSlider::valueChanged, this, [this](int level) {
            if (timeline_widget_ == nullptr || level < 0 ||
                level >= static_cast<int>(timeline::kTimelineZoomLevels.size())) {
                return;
            }
            applyTimelineZoom(
                timeline::kTimelineZoomLevels[static_cast<std::size_t>(level)]);
        });
    }
    if (ui_.zoom_out != nullptr) {
        connect(ui_.zoom_out, &QPushButton::clicked, this, [this]() {
            if (timeline_widget_ != nullptr) {
                applyTimelineZoom(timeline_widget_->nextZoomFactor(-1));
            }
        });
    }
    if (ui_.zoom_in != nullptr) {
        connect(ui_.zoom_in, &QPushButton::clicked, this, [this]() {
            if (timeline_widget_ != nullptr) {
                applyTimelineZoom(timeline_widget_->nextZoomFactor(1));
            }
        });
    }
    if (ui_.apply_transition != nullptr) {
        connect(ui_.apply_transition, &QPushButton::clicked,
                this, &EditWorkspaceController::applyTransitionSettings);
    }
    if (ui_.remove_transition != nullptr) {
        connect(ui_.remove_transition, &QPushButton::clicked,
                this, &EditWorkspaceController::removeSelectedTransition);
    }
    if (ui_.previous_frame != nullptr) {
        connect(ui_.previous_frame, &QPushButton::clicked, this, [this]() {
            emit playbackCommandRequested(playback::PlaybackCommand::StepBackward);
        });
    }
    if (ui_.play_pause != nullptr) {
        connect(ui_.play_pause, &QPushButton::clicked, this, [this]() {
            togglePlayback();
        });
    }
    if (ui_.next_frame != nullptr) {
        connect(ui_.next_frame, &QPushButton::clicked, this, [this]() {
            emit playbackCommandRequested(playback::PlaybackCommand::StepForward);
        });
    }
    if (ui_.clear_timeline != nullptr) {
        connect(ui_.clear_timeline, &QPushButton::clicked,
                this, &EditWorkspaceController::clearTimeline);
    }
    if (ui_.selection_tool != nullptr) {
        connect(ui_.selection_tool, &QPushButton::clicked, this, [this]() {
            setRazorMode(false);
        });
    }
    if (ui_.razor_tool != nullptr) {
        connect(ui_.razor_tool, &QPushButton::toggled,
                this, &EditWorkspaceController::setRazorMode);
    }
    if (ui_.snap != nullptr) {
        connect(ui_.snap, &QPushButton::toggled, this, [this](bool enabled) {
            if (timeline_widget_ != nullptr &&
                timeline_widget_->snapEnabled() != enabled) {
                timeline_widget_->setSnapEnabled(enabled);
            }
        });
        connect(this, &EditWorkspaceController::timelineSnapChanged,
                ui_.snap, [button = ui_.snap](bool enabled) {
                    if (button->isChecked() == enabled) return;
                    const QSignalBlocker blocker(button);
                    button->setChecked(enabled);
                });
    }
    if (ui_.monitor_volume != nullptr) {
        connect(ui_.monitor_volume, &QSlider::valueChanged,
                this, &EditWorkspaceController::monitorVolumeChangedRequested);
    }
}

const EditWorkspaceUi& EditWorkspaceController::ui() const noexcept {
    return ui_;
}

void EditWorkspaceController::setPlaybackPresentation(
    bool playing,
    bool loading,
    bool available) {
    playback_is_playing_ = playing;
    playback_is_loading_ = loading;
    playback_available_ = available;
    bool has_available_timeline_clip = false;
    if (const auto location = timeline_model_.topClipAt(timelinePlayheadFrame());
        location.has_value()) {
        const auto& clip = timeline_model_.tracks()[location->track_index]
            .clips[location->clip_index];
        if (clip.kind == timeline::ClipKind::Text) {
            has_available_timeline_clip = true;
        } else {
            const auto media = std::find_if(
                session_.mediaItems().begin(), session_.mediaItems().end(),
                [&clip](const application::ImportedMedia& item) {
                    return item.metadata.source_path.lexically_normal() ==
                               clip.source_path.lexically_normal() &&
                           !item.offline;
                });
            has_available_timeline_clip = media != session_.mediaItems().end();
        }
    }
    const bool can_play = available && has_available_timeline_clip && !loading;
    if (ui_.previous_frame != nullptr) ui_.previous_frame->setEnabled(can_play);
    if (ui_.play_pause != nullptr) ui_.play_pause->setEnabled(can_play);
    if (ui_.next_frame != nullptr) ui_.next_frame->setEnabled(can_play);
    if (ui_.play_pause != nullptr) {
        ui_.play_pause->setIcon(QApplication::style()->standardIcon(
            playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    }
}

void EditWorkspaceController::requestPlaybackCommand(
    playback::PlaybackCommand command) {
    emit playbackCommandRequested(command);
}

void EditWorkspaceController::togglePlayback() {
    requestPlaybackCommand(playback_is_playing_
        ? playback::PlaybackCommand::Pause
        : playback::PlaybackCommand::Play);
}

void EditWorkspaceController::presentPlaybackFrame(qint64 clip_frame) {
    playback_frame_index_ = std::max<qint64>(0, clip_frame);
    if (timeline_widget_ != nullptr && canPlaybackSelectedMedia()) {
        timeline_widget_->setPlayheadFrame(timelinePlayheadFrame());
    }
    updateInspector();
}

void EditWorkspaceController::refreshTimelinePresentation() {
    updateTimelineState();
}

void EditWorkspaceController::refreshInspectorPresentation() {
    updateInspector();
}

void EditWorkspaceController::setTimelineReadOnly(bool read_only) {
    if (timeline_widget_ != nullptr) timeline_widget_->setReadOnly(read_only);
    if (ui_.timeline_controls != nullptr) {
        ui_.timeline_controls->setVisible(!read_only);
    }
    if (ui_.timeline_footer != nullptr) {
        ui_.timeline_footer->setVisible(!read_only);
    }
}

void EditWorkspaceController::applyTimelineZoom(double factor) {
    if (timeline_widget_ == nullptr || ui_.timeline_scroll == nullptr) return;
    auto* scroll_bar = ui_.timeline_scroll->horizontalScrollBar();
    const auto old_scroll = scroll_bar->value();
    const auto anchor_frame = timelinePlayheadFrame();
    const auto anchor_content_x = timeline_widget_->contentXForFrame(anchor_frame);
    const auto raw_anchor_viewport_x =
        anchor_content_x - static_cast<double>(old_scroll);
    const auto viewport_width =
        static_cast<double>(ui_.timeline_scroll->viewport()->width());
    const auto anchor_viewport_x = raw_anchor_viewport_x >= 0.0 &&
            raw_anchor_viewport_x <= viewport_width
        ? raw_anchor_viewport_x
        : viewport_width / 2.0;
    timeline_widget_->setZoomFactor(factor);
    QTimer::singleShot(0, ui_.timeline_scroll, [this, anchor_frame, anchor_viewport_x]() {
        if (timeline_widget_ == nullptr || ui_.timeline_scroll == nullptr) return;
        auto* bar = ui_.timeline_scroll->horizontalScrollBar();
        const auto new_anchor_content_x =
            timeline_widget_->contentXForFrame(anchor_frame);
        bar->setValue(static_cast<int>(std::llround(
            new_anchor_content_x - anchor_viewport_x)));
    });
}

void EditWorkspaceController::synchronizeActiveTimelineSelection() noexcept {
    if (active_timeline_clip_id_.has_value()) {
        const auto location = timeline_model_.locateClip(*active_timeline_clip_id_);
        if (!location.has_value()) {
            active_timeline_clip_id_.reset();
            active_timeline_track_id_.reset();
            return;
        }
        active_timeline_track_id_ =
            timeline_model_.tracks()[location->track_index].track_id;
        return;
    }
    if (active_timeline_track_id_.has_value() &&
        !timeline_model_.locateTrack(*active_timeline_track_id_).has_value()) {
        active_timeline_track_id_.reset();
    }
}

std::optional<timeline::ClipLocation>
EditWorkspaceController::selectedTimelineClipLocation() const noexcept {
    if (!timeline_model_.hasClip()) return std::nullopt;
    const auto selected_path = session_.selection().selected_source_path;
    if (active_timeline_clip_id_.has_value()) {
        if (const auto location = timeline_model_.locateClip(*active_timeline_clip_id_);
            location.has_value()) {
            const auto& clip = timeline_model_.tracks()[location->track_index]
                .clips[location->clip_index];
            if (clip.kind == timeline::ClipKind::Text) return location;
            if (selected_path.has_value() &&
                clip.source_path.lexically_normal() == selected_path->lexically_normal()) {
                return location;
            }
        }
    }
    if (!selected_path.has_value()) return std::nullopt;
    for (std::size_t track = 0; track < timeline_model_.trackCount(); ++track) {
        const auto& clips = timeline_model_.tracks()[track].clips;
        for (std::size_t index = 0; index < clips.size(); ++index) {
            if (timeline::isMediaClipKind(clips[index].kind) &&
                clips[index].source_path.lexically_normal() ==
                    selected_path->lexically_normal()) {
                return timeline::ClipLocation{track, index};
            }
        }
    }
    return std::nullopt;
}

bool EditWorkspaceController::canPlaybackSelectedMedia() const noexcept {
    if (!playback_available_) return false;
    const auto location = selectedTimelineClipLocation();
    if (!location.has_value()) return false;
    const auto& clip = timeline_model_.tracks()[location->track_index]
        .clips[location->clip_index];
    if (clip.kind == timeline::ClipKind::Text) return true;
    const auto found = std::find_if(
        session_.mediaItems().begin(), session_.mediaItems().end(),
        [&clip](const application::ImportedMedia& item) {
            return item.metadata.source_path.lexically_normal() ==
                       clip.source_path.lexically_normal() && !item.offline;
        });
    return found != session_.mediaItems().end();
}

std::int64_t EditWorkspaceController::timelinePlayheadFrame() const noexcept {
    if (preserved_timeline_playhead_frame_.has_value()) {
        return std::max<std::int64_t>(0, *preserved_timeline_playhead_frame_);
    }
    const auto location = selectedTimelineClipLocation();
    if (!location.has_value()) {
        return timeline_widget_ != nullptr
            ? timeline_widget_->playheadFrame()
            : std::max<std::int64_t>(0, session_.playheadFrame());
    }
    const auto& clip = timeline_model_.tracks()[location->track_index]
        .clips[location->clip_index];
    const auto local_frame = std::clamp<std::int64_t>(
        playback_frame_index_, 0,
        std::max<std::int64_t>(0, clip.timeline_duration_frames - 1));
    if (clip.timeline_start_frame >
        std::numeric_limits<std::int64_t>::max() - local_frame) {
        return clip.timeline_start_frame;
    }
    return clip.timeline_start_frame + local_frame;
}

void EditWorkspaceController::updateHistoryActions() {
    emit historyStateChanged(canUndo(), canRedo());
}

void EditWorkspaceController::updateTimelineState() {
    synchronizeActiveTimelineSelection();
    const bool occupied = timeline_model_.hasClip();
    if (ui_.clear_timeline != nullptr) {
        ui_.clear_timeline->setEnabled(occupied);
    }
    if (timeline_widget_ != nullptr) {
        timeline_widget_->setTracks(timeline_model_.tracks());
        timeline_widget_->setActiveClip(selectedTimelineClipLocation());
        timeline_widget_->setPlayheadFrame(timelinePlayheadFrame());
    }

    const auto location = selectedTimelineClipLocation();
    bool audio_enabled = false;
    if (location.has_value()) {
        const auto& clip = timeline_model_.tracks()[location->track_index]
            .clips[location->clip_index];
        audio_enabled = clip.kind == timeline::ClipKind::Video &&
            canPlaybackSelectedMedia();
    }
    if (ui_.clip_volume != nullptr && ui_.clip_mute != nullptr &&
        ui_.track_volume != nullptr && ui_.track_mute != nullptr) {
        ui_.clip_volume->setEnabled(audio_enabled);
        ui_.clip_mute->setEnabled(audio_enabled);
        ui_.track_volume->setEnabled(audio_enabled);
        ui_.track_mute->setEnabled(audio_enabled);
        if (audio_enabled) {
            const auto& track = timeline_model_.tracks()[location->track_index];
            const auto& clip = track.clips[location->clip_index];
            const QSignalBlocker clip_slider_blocker(ui_.clip_volume);
            const QSignalBlocker clip_mute_blocker(ui_.clip_mute);
            const QSignalBlocker track_slider_blocker(ui_.track_volume);
            const QSignalBlocker track_mute_blocker(ui_.track_mute);
            ui_.clip_volume->setValue(static_cast<int>(std::lround(
                std::clamp(clip.audio_gain, 0.0, 2.0) * 100.0)));
            ui_.clip_mute->setChecked(clip.audio_muted);
            ui_.track_volume->setValue(static_cast<int>(std::lround(
                std::clamp(track.audio_gain, 0.0, 2.0) * 100.0)));
            ui_.track_mute->setChecked(track.audio_muted);
        }
    }
    updateHistoryActions();
    updateInspector();
    emit projectDirtyStateUpdateRequested();
}

void EditWorkspaceController::moveClip(
    timeline::ClipId clip_id,
    timeline::TrackId target_track_id,
    std::int64_t timeline_start_frame) {
    if (timeline_start_frame < 0) return;
    try {
        const auto result = execute(application::MoveClipCommand{
            clip_id, target_track_id, timeline_start_frame});
        if (!result.changed()) {
            if (result.status != application::EditStatus::NoChange) {
                emit statusMessageRequested(
                    QStringLiteral("The clip cannot be moved to that position."));
            }
            return;
        }
        publishCommittedEdit(
            result, true, true, QStringLiteral("Timeline clip moved."));
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "move_clip",
            error.what(),
            {{"clip_id", std::to_string(clip_id)},
             {"target_track_id", std::to_string(target_track_id)},
             {"timeline_frame", std::to_string(timeline_start_frame)}});
        emit statusMessageRequested(
            QStringLiteral("Could not move the timeline clip."));
    }
}

application::TimelineCommandService::EditBatchId
EditWorkspaceController::beginEditBatch() {
    return command_service_.beginEditBatch();
}

application::TimelineEditResult EditWorkspaceController::finishEditBatch(
    application::TimelineCommandService::EditBatchId batch_id) {
    const auto result = command_service_.finishEditBatch(batch_id);
    publishResult(result);
    return result;
}

void EditWorkspaceController::recordLegacyEdit(timeline::EditState state) {
    command_service_.recordLegacyEdit(std::move(state));
    publishHistoryState();
}

void EditWorkspaceController::clearHistory() noexcept {
    command_service_.clearHistory();
    publishHistoryState();
}

bool EditWorkspaceController::canUndo() const noexcept {
    return command_service_.canUndo();
}

bool EditWorkspaceController::canRedo() const noexcept {
    return command_service_.canRedo();
}

application::EditorSession& EditWorkspaceController::session() const noexcept {
    return session_;
}

void EditWorkspaceController::publishResult(
    const application::TimelineEditResult& result) {
    emit commandResultProduced(result);
    publishHistoryState();
}

void EditWorkspaceController::publishHistoryState() {
    emit historyStateChanged(canUndo(), canRedo());
}

void EditWorkspaceController::publishCommittedEdit(
    const application::TimelineEditResult& result,
    bool stop_playback,
    bool refresh_composition,
    const QString& status_message) {
    emit timelineEditCommitted(
        result, stop_playback, refresh_composition, status_message);
}

void EditWorkspaceController::updateInspector() {
    bool transition_enabled = false;
    const timeline::TimelineTransition* selected_transition = nullptr;
    std::optional<std::size_t> transition_track_index;
    std::optional<std::size_t> transition_from_clip_index;
    std::optional<std::size_t> transition_to_clip_index;
    if (active_transition_.has_value()) {
        const auto& selection = *active_transition_;
        if (selection.track_id != 0) {
            const auto track_index = timeline_model_.locateTrack(selection.track_id);
            const auto from_location = timeline_model_.locateClip(selection.from_clip_id);
            const auto to_location = timeline_model_.locateClip(selection.to_clip_id);
            if (track_index.has_value() && from_location.has_value() &&
                to_location.has_value() &&
                from_location->track_index == *track_index &&
                to_location->track_index == *track_index) {
                transition_track_index = *track_index;
                transition_from_clip_index = from_location->clip_index;
                transition_to_clip_index = to_location->clip_index;
            }
        }
        if (transition_track_index.has_value() &&
            transition_from_clip_index.has_value() &&
            transition_to_clip_index.has_value()) {
            selected_transition = timeline_model_.transitionBetween(
                *transition_track_index,
                *transition_from_clip_index,
                *transition_to_clip_index);
            transition_enabled = selected_transition != nullptr;
        }
    }
    if (!transition_enabled) active_transition_.reset();

    const auto location = selectedTimelineClipLocation();
    const bool enabled = location.has_value() &&
        location->track_index < timeline_model_.trackCount() &&
        location->clip_index < timeline_model_.clipCount(location->track_index);
    const std::array<QDoubleSpinBox*, 5> spins = ui_.transform_spins;
    const std::array<QSlider*, 5> sliders = ui_.transform_sliders;
    for (auto* spin : spins) {
        if (spin != nullptr) spin->setEnabled(enabled && !transition_enabled);
    }
    for (auto* slider : sliders) {
        if (slider != nullptr) slider->setEnabled(enabled && !transition_enabled);
    }
    for (auto* button : ui_.transform_keys) {
        if (button != nullptr) button->setEnabled(enabled && !transition_enabled);
    }
    const bool text_enabled = enabled &&
        timeline_model_.tracks()[location->track_index]
            .clips[location->clip_index].kind == timeline::ClipKind::Text;
    if (ui_.text_controls != nullptr) {
        ui_.text_controls->setVisible(text_enabled && !transition_enabled);
    }
    if (ui_.text_content != nullptr) ui_.text_content->setEnabled(text_enabled);
    if (ui_.text_font != nullptr) ui_.text_font->setEnabled(text_enabled);
    if (ui_.text_font_size != nullptr) ui_.text_font_size->setEnabled(text_enabled);
    if (ui_.text_color != nullptr) ui_.text_color->setEnabled(text_enabled);
    if (ui_.text_alignment != nullptr) ui_.text_alignment->setEnabled(text_enabled);
    if (ui_.apply_text != nullptr) {
        ui_.apply_text->setEnabled(text_enabled && !transition_enabled);
    }
    if (ui_.transition_controls != nullptr) {
        ui_.transition_controls->setVisible(transition_enabled);
    }
    if (ui_.transition_type != nullptr) {
        ui_.transition_type->setEnabled(transition_enabled);
    }
    if (ui_.transition_duration != nullptr) {
        ui_.transition_duration->setEnabled(transition_enabled);
    }
    if (ui_.apply_transition != nullptr) {
        ui_.apply_transition->setEnabled(transition_enabled);
    }
    if (ui_.remove_transition != nullptr) {
        ui_.remove_transition->setEnabled(transition_enabled);
    }
    if (transition_enabled && selected_transition != nullptr) {
        for (auto* button : ui_.transform_keys) {
            if (button != nullptr) {
                const QSignalBlocker blocker(button);
                button->setChecked(false);
                button->setText(QStringLiteral("◇"));
                button->setToolTip(QStringLiteral("Select a clip to edit keyframes"));
            }
        }
        const auto& track = timeline_model_.tracks()[*transition_track_index];
        const auto& from = track.clips[*transition_from_clip_index];
        const auto& to = track.clips[*transition_to_clip_index];
        const auto maximum = std::min(
            from.timeline_duration_frames,
            to.timeline_duration_frames);
        if (ui_.transition_type != nullptr) {
            const QSignalBlocker blocker(ui_.transition_type);
            ui_.transition_type->setCurrentIndex(
                selected_transition->kind == timeline::TransitionKind::FadeToBlack
                    ? 1
                    : 0);
        }
        if (ui_.transition_duration != nullptr) {
            const QSignalBlocker blocker(ui_.transition_duration);
            ui_.transition_duration->setRange(
                1,
                static_cast<int>(std::min<std::int64_t>(
                    maximum, std::numeric_limits<int>::max())));
            ui_.transition_duration->setValue(
                static_cast<int>(selected_transition->duration_frames));
        }
        return;
    }
    if (location.has_value() && enabled) {
        const auto& clip = timeline_model_.tracks()[location->track_index]
            .clips[location->clip_index];
        if (clip.kind == timeline::ClipKind::Text) {
            if (ui_.text_content != nullptr) {
                const QSignalBlocker blocker(ui_.text_content);
                ui_.text_content->setPlainText(QString::fromUtf8(
                    clip.text.content.data(),
                    static_cast<qsizetype>(clip.text.content.size())));
            }
            if (ui_.text_font != nullptr) {
                const QSignalBlocker blocker(ui_.text_font);
                ui_.text_font->setCurrentFont(QFont(QString::fromUtf8(
                    clip.text.font_family.data(),
                    static_cast<qsizetype>(clip.text.font_family.size()))));
            }
            if (ui_.text_font_size != nullptr) {
                const QSignalBlocker blocker(ui_.text_font_size);
                ui_.text_font_size->setValue(
                    static_cast<int>(std::lround(clip.text.font_size_pixels)));
            }
            text_color_ = clip.text.color;
            if (ui_.text_color != nullptr) {
                ui_.text_color->setStyleSheet(
                    QString("background-color: rgba(%1, %2, %3, %4);")
                        .arg(text_color_[0]).arg(text_color_[1])
                        .arg(text_color_[2]).arg(text_color_[3]));
            }
            if (ui_.text_alignment != nullptr) {
                const QSignalBlocker blocker(ui_.text_alignment);
                ui_.text_alignment->setCurrentIndex(
                    clip.text.alignment == timeline::TextAlignment::Left
                        ? 0
                        : clip.text.alignment == timeline::TextAlignment::Right
                            ? 2
                            : 1);
            }
        }
        const auto evaluated = timeline::evaluateTransform(
            clip.transform,
            clip.keyframes,
            std::max<std::int64_t>(0, playback_frame_index_));
        const std::array<double, 5> values{
            evaluated.position_x,
            evaluated.position_y,
            evaluated.scale,
            evaluated.rotation_degrees,
            evaluated.opacity};
        for (std::size_t index = 0; index < spins.size(); ++index) {
            if (spins[index] != nullptr) {
                const QSignalBlocker blocker(spins[index]);
                spins[index]->setValue(values[index]);
            }
            if (sliders[index] != nullptr) {
                const QSignalBlocker blocker(sliders[index]);
                sliders[index]->setValue(transformSliderValue(
                    values[index],
                    transform_minimums[index],
                    transform_maximums[index]));
            }
            if (ui_.transform_keys[index] != nullptr) {
                const auto property = static_cast<timeline::TransformProperty>(index);
                const auto& keys = timeline::keyframesFor(clip.keyframes, property);
                const auto current_frame = std::max<std::int64_t>(0, playback_frame_index_);
                const bool has_key = std::any_of(keys.begin(), keys.end(),
                    [current_frame](const auto& key) {
                        return key.frame == current_frame;
                    });
                const QSignalBlocker blocker(ui_.transform_keys[index]);
                ui_.transform_keys[index]->setChecked(has_key);
                ui_.transform_keys[index]->setText(has_key ? QStringLiteral("◆") : QStringLiteral("◇"));
                ui_.transform_keys[index]->setToolTip(
                    has_key ? QStringLiteral("Remove keyframe at the current frame")
                            : QStringLiteral("Add keyframe at the current frame"));
            }
        }
    } else {
        for (auto* button : ui_.transform_keys) {
            if (button != nullptr) {
                const QSignalBlocker blocker(button);
                button->setChecked(false);
                button->setText(QStringLiteral("◇"));
                button->setToolTip(QStringLiteral("Add keyframe at the current frame"));
            }
        }
    }
}

void EditWorkspaceController::beginAudioEdit() {
    if (pending_audio_edit_batch_id_.has_value() ||
        !active_timeline_track_id_.has_value() ||
        !active_timeline_clip_id_.has_value()) {
        return;
    }
    pending_audio_edit_batch_id_ = beginEditBatch();
}

void EditWorkspaceController::finishAudioEdit() {
    if (!pending_audio_edit_batch_id_.has_value()) return;
    const auto batch_id = *pending_audio_edit_batch_id_;
    pending_audio_edit_batch_id_.reset();
    static_cast<void>(finishEditBatch(batch_id));
    updateHistoryActions();
    updateTimelineState();
    emit playbackAudioParametersRequested();
}

void EditWorkspaceController::applyClipAudioControls() {
    if (!active_timeline_clip_id_.has_value() || ui_.clip_volume == nullptr ||
        ui_.clip_mute == nullptr) {
        return;
    }
    beginAudioEdit();
    const auto result = execute(application::SetClipAudioCommand{
        *active_timeline_clip_id_,
        static_cast<double>(ui_.clip_volume->value()) / 100.0,
        ui_.clip_mute->isChecked()});
    if (!result.changed()) return;
    publishCommittedEdit(
        result, false, false, QStringLiteral("Clip audio settings changed."));
    emit playbackAudioParametersRequested();
}

void EditWorkspaceController::applyTrackAudioControls() {
    if (!active_timeline_track_id_.has_value() || ui_.track_volume == nullptr ||
        ui_.track_mute == nullptr) {
        return;
    }
    beginAudioEdit();
    const auto result = execute(application::SetTrackAudioCommand{
        *active_timeline_track_id_,
        static_cast<double>(ui_.track_volume->value()) / 100.0,
        ui_.track_mute->isChecked()});
    if (!result.changed()) return;
    publishCommittedEdit(
        result, false, false, QStringLiteral("Track audio settings changed."));
    emit playbackAudioParametersRequested();
}

void EditWorkspaceController::beginTransformEdit() {
    if (pending_transform_edit_batch_id_.has_value() ||
        !active_timeline_clip_id_.has_value()) {
        return;
    }
    pending_transform_edit_batch_id_ = beginEditBatch();
}

void EditWorkspaceController::finishTransformEdit() {
    if (!pending_transform_edit_batch_id_.has_value()) return;
    const auto batch_id = *pending_transform_edit_batch_id_;
    pending_transform_edit_batch_id_.reset();
    static_cast<void>(finishEditBatch(batch_id));
    updateHistoryActions();
    emit projectDirtyStateUpdateRequested();
}

void EditWorkspaceController::applyTextStyle() {
    const auto location = selectedTimelineClipLocation();
    if (!location.has_value() || ui_.text_content == nullptr ||
        ui_.text_font == nullptr || ui_.text_font_size == nullptr ||
        ui_.text_alignment == nullptr) {
        return;
    }
    const auto& clip = timeline_model_.tracks()[location->track_index]
        .clips[location->clip_index];
    if (clip.kind != timeline::ClipKind::Text) return;

    timeline::TextStyle text;
    text.content = ui_.text_content->toPlainText().toUtf8().toStdString();
    text.font_family = ui_.text_font->currentFont().family().toUtf8().toStdString();
    text.font_size_pixels = ui_.text_font_size->value();
    text.color = text_color_;
    text.alignment = ui_.text_alignment->currentIndex() == 0
        ? timeline::TextAlignment::Left
        : ui_.text_alignment->currentIndex() == 2
            ? timeline::TextAlignment::Right
            : timeline::TextAlignment::Center;

    emit playbackInvalidateRequested(false);
    playback_is_playing_ = false;
    const auto result = execute(application::SetClipTextCommand{clip.clip_id, text});
    if (!result.changed()) {
        updateInspector();
        return;
    }
    publishCommittedEdit(
        result, false, true, QStringLiteral("Text style updated."));
    emit renderCompositionFrameRequested(timelinePlayheadFrame(), playback_frame_index_);
}

void EditWorkspaceController::chooseTextColor() {
    if (ui_.text_color == nullptr) return;
    const QColor current(
        text_color_[0], text_color_[1], text_color_[2], text_color_[3]);
    const auto chosen = QColorDialog::getColor(
        current, ui_.text_color, QStringLiteral("Text color"),
        QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid()) return;
    text_color_ = {
        static_cast<std::uint8_t>(chosen.red()),
        static_cast<std::uint8_t>(chosen.green()),
        static_cast<std::uint8_t>(chosen.blue()),
        static_cast<std::uint8_t>(chosen.alpha())};
    ui_.text_color->setStyleSheet(
        QString("background-color: rgba(%1, %2, %3, %4);")
            .arg(chosen.red()).arg(chosen.green())
            .arg(chosen.blue()).arg(chosen.alpha()));
}

void EditWorkspaceController::applyTransformProperty(
    int property_index,
    double value) {
    const auto location = selectedTimelineClipLocation();
    if (property_index < 0 || property_index >= 5 || !location.has_value()) return;
    const auto& clip = timeline_model_.tracks()[location->track_index]
        .clips[location->clip_index];
    const auto property = static_cast<timeline::TransformProperty>(property_index);
    const auto local_frame = std::max<std::int64_t>(0, playback_frame_index_);
    emit playbackInvalidateRequested(false);
    playback_is_playing_ = false;
    const auto result = execute(application::SetTransformPropertyCommand{
        clip.clip_id, property, local_frame, value});
    if (!result.changed()) return;
    publishCommittedEdit(
        result, false, true, QStringLiteral("Transform updated."));
    emit seekActiveClipRequested(local_frame);
}

void EditWorkspaceController::toggleTransformKeyframe(int property_index) {
    const auto location = selectedTimelineClipLocation();
    if (property_index < 0 || property_index >= 5 || !location.has_value()) return;
    const auto& clip = timeline_model_.tracks()[location->track_index]
        .clips[location->clip_index];
    const auto frame = std::clamp<std::int64_t>(
        playback_frame_index_, 0,
        std::max<std::int64_t>(0, clip.timeline_duration_frames - 1));
    const auto property = static_cast<timeline::TransformProperty>(property_index);
    const auto& existing_keys = timeline::keyframesFor(clip.keyframes, property);
    const bool had_key = std::any_of(existing_keys.begin(), existing_keys.end(),
        [frame](const auto& key) { return key.frame == frame; });
    emit playbackInvalidateRequested(false);
    playback_is_playing_ = false;
    const auto result = execute(application::ToggleTransformKeyframeCommand{
        clip.clip_id, property, frame});
    if (!result.changed()) {
        updateInspector();
        return;
    }
    publishCommittedEdit(
        result, false, true,
        had_key ? QStringLiteral("Keyframe removed.")
                : QStringLiteral("Keyframe added."));
    emit seekActiveClipRequested(frame);
}

void EditWorkspaceController::handleTimelineTransitionSelected(
    timeline::TrackId track_id,
    timeline::ClipId from_clip_id,
    timeline::ClipId to_clip_id) {
    active_transition_.reset();
    const auto track = timeline_model_.locateTrack(track_id);
    const auto from = timeline_model_.locateClip(from_clip_id);
    const auto to = timeline_model_.locateClip(to_clip_id);
    if (!track.has_value() || !from.has_value() || !to.has_value() ||
        from->track_index != *track || to->track_index != *track ||
        timeline_model_.transitionBetween(
            *track, from->clip_index, to->clip_index) == nullptr) {
        updateInspector();
        return;
    }
    active_transition_ = timeline::TransitionSelection{
        track_id, from_clip_id, to_clip_id};
    updateInspector();
    emit statusMessageRequested(QStringLiteral("Timeline transition selected."));
}

void EditWorkspaceController::handleTimelineTransitionSelectionCleared() {
    active_transition_.reset();
    updateInspector();
}

void EditWorkspaceController::handleTimelineTransitionAdd(
    timeline::TrackId track_id,
    timeline::ClipId from_clip_id,
    timeline::ClipId to_clip_id,
    qint64 kind) {
    if (kind < 0 || kind > 1) return;
    try {
        const auto result = execute(application::AddTransitionCommand{
            track_id,
            from_clip_id,
            to_clip_id,
            kind == 0 ? timeline::TransitionKind::CrossDissolve
                      : timeline::TransitionKind::FadeToBlack,
            15});
        if (!result.changed()) {
            emit statusMessageRequested(
                result.status == application::EditStatus::NoChange
                    ? QStringLiteral("A transition is already present here.")
                    : QStringLiteral("The transition cannot be added here."));
            return;
        }
        publishCommittedEdit(
            result, false, true, QStringLiteral("Timeline transition added."));
        emit refreshPlaybackCompositionRequested();
        emit renderCompositionFrameRequested(
            timelinePlayheadFrame(), playback_frame_index_);
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error, "timeline", "add_transition", error.what(),
            {{"track_id", std::to_string(track_id)},
             {"from_clip_id", std::to_string(from_clip_id)},
             {"to_clip_id", std::to_string(to_clip_id)}});
        emit statusMessageRequested(
            QStringLiteral("Could not add the timeline transition."));
    }
}

void EditWorkspaceController::handleTimelineTransitionRemove(
    timeline::TrackId track_id,
    timeline::ClipId from_clip_id,
    timeline::ClipId to_clip_id) {
    try {
        const auto result = execute(application::RemoveTransitionCommand{
            track_id, from_clip_id, to_clip_id});
        if (!result.changed()) {
            emit statusMessageRequested(
                QStringLiteral("No transition is present at this junction."));
            return;
        }
        publishCommittedEdit(
            result, false, true, QStringLiteral("Timeline transition removed."));
        emit refreshPlaybackCompositionRequested();
        emit renderCompositionFrameRequested(
            timelinePlayheadFrame(), playback_frame_index_);
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error, "timeline", "remove_transition", error.what(),
            {{"track_id", std::to_string(track_id)},
             {"from_clip_id", std::to_string(from_clip_id)},
             {"to_clip_id", std::to_string(to_clip_id)}});
        emit statusMessageRequested(
            QStringLiteral("Could not remove the timeline transition."));
    }
}

void EditWorkspaceController::applyTransitionSettings() {
    if (!active_transition_.has_value() || ui_.transition_type == nullptr ||
        ui_.transition_duration == nullptr) {
        return;
    }
    const auto selection = *active_transition_;
    const auto track_index = timeline_model_.locateTrack(selection.track_id);
    const auto from = timeline_model_.locateClip(selection.from_clip_id);
    const auto to = timeline_model_.locateClip(selection.to_clip_id);
    if (!track_index.has_value() || !from.has_value() || !to.has_value() ||
        from->track_index != *track_index || to->track_index != *track_index ||
        timeline_model_.transitionBetween(
            *track_index, from->clip_index, to->clip_index) == nullptr) {
        active_transition_.reset();
        updateInspector();
        return;
    }
    try {
        const auto kind = ui_.transition_type->currentData().toInt() == 1
            ? timeline::TransitionKind::FadeToBlack
            : timeline::TransitionKind::CrossDissolve;
        const auto result = execute(application::UpdateTransitionCommand{
            selection.track_id,
            selection.from_clip_id,
            selection.to_clip_id,
            kind,
            ui_.transition_duration->value()});
        if (!result.changed()) {
            emit statusMessageRequested(
                QStringLiteral("The transition settings were not changed."));
            updateInspector();
            return;
        }
        publishCommittedEdit(
            result, false, true, QStringLiteral("Timeline transition updated."));
        emit refreshPlaybackCompositionRequested();
        emit renderCompositionFrameRequested(
            timelinePlayheadFrame(), playback_frame_index_);
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error, "timeline", "update_transition", error.what(),
            {{"track_id", std::to_string(selection.track_id)},
             {"from_clip_id", std::to_string(selection.from_clip_id)},
             {"to_clip_id", std::to_string(selection.to_clip_id)}});
        emit statusMessageRequested(
            QStringLiteral("Could not update the timeline transition."));
    }
}

void EditWorkspaceController::removeSelectedTransition() {
    if (!active_transition_.has_value()) return;
    const auto selection = *active_transition_;
    const auto track_index = timeline_model_.locateTrack(selection.track_id);
    const auto from = timeline_model_.locateClip(selection.from_clip_id);
    const auto to = timeline_model_.locateClip(selection.to_clip_id);
    if (!track_index.has_value() || !from.has_value() || !to.has_value() ||
        from->track_index != *track_index || to->track_index != *track_index) {
        active_transition_.reset();
        updateInspector();
        return;
    }
    handleTimelineTransitionRemove(
        selection.track_id, selection.from_clip_id, selection.to_clip_id);
}

void EditWorkspaceController::handleTimelineClipSplit(
    timeline::ClipId clip_id,
    qint64 local_frame) {
    if (local_frame < 0) return;
    const auto location = timeline_model_.locateClip(clip_id);
    if (!location.has_value()) return;
    const auto source_clip = timeline_model_.tracks()[location->track_index]
        .clips[location->clip_index];
    try {
        const auto result = execute(application::SplitClipCommand{
            clip_id, local_frame});
        if (!result.changed()) {
            emit statusMessageRequested(
                QStringLiteral("A clip cannot be split at its boundary."));
            return;
        }
        publishCommittedEdit(
            result, true, true, QStringLiteral("Clip split at the playhead."));
        if (!active_timeline_clip_id_.has_value()) return;
        const auto edited_location = timeline_model_.locateClip(
            *active_timeline_clip_id_);
        if (!edited_location.has_value()) return;
        const auto& edited = timeline_model_.tracks()[edited_location->track_index]
            .clips[edited_location->clip_index];
        emit selectMediaBrowserClipRequested(edited.clip_id);
        const auto media = std::find_if(
            session_.mediaItems().begin(), session_.mediaItems().end(),
            [&edited](const application::ImportedMedia& item) {
                return item.metadata.source_path.lexically_normal() ==
                       edited.source_path.lexically_normal();
            });
        if (media != session_.mediaItems().end() && !media->offline) {
            emit activateTimelineClipRequested(
                edited.clip_id, 0, false, false);
        } else if (source_clip.kind == timeline::ClipKind::Text) {
            emit renderCompositionFrameRequested(
                timelinePlayheadFrame(), playback_frame_index_);
        }
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error, "timeline", "split_clip", error.what(),
            {{"track_id", std::to_string(source_clip.track_id)},
             {"clip_id", std::to_string(clip_id)},
             {"local_frame", std::to_string(local_frame)}});
        emit statusMessageRequested(
            QStringLiteral("Could not split the timeline clip."));
    }
}

void EditWorkspaceController::handleTimelineTrimStarted() {
    if (!timeline_model_.hasClip()) return;
    emit playbackInvalidateRequested(true);
    playback_is_playing_ = false;
}

void EditWorkspaceController::handleTimelineClipTrim(
    timeline::ClipId clip_id,
    qint64 edge_value,
    qint64 boundary_frame,
    qint64 mode_value) {
    const auto location = timeline_model_.locateClip(clip_id);
    if (!location.has_value()) return;
    if (edge_value != static_cast<qint64>(timeline::ClipEdge::Left) &&
        edge_value != static_cast<qint64>(timeline::ClipEdge::Right)) {
        return;
    }
    if (mode_value != static_cast<qint64>(timeline::ClipEdgeEditMode::Rolling) &&
        mode_value != static_cast<qint64>(timeline::ClipEdgeEditMode::Individual)) {
        return;
    }
    const auto edge = static_cast<timeline::ClipEdge>(edge_value);
    const auto mode = static_cast<timeline::ClipEdgeEditMode>(mode_value);
    try {
        const auto result = execute(application::TrimClipEdgeCommand{
            clip_id, edge, boundary_frame, mode,
            timelinePlayheadFrame(), playback_frame_index_});
        if (result.status == application::EditStatus::NoChange) return;
        if (!result.changed()) {
            emit statusMessageRequested(
                QStringLiteral("The clip edge cannot move any farther."));
            return;
        }
        publishCommittedEdit(
            result, true, false, QStringLiteral("Timeline clip edge adjusted."));
        if (!active_timeline_clip_id_.has_value()) return;
        const auto edited_location = timeline_model_.locateClip(
            *active_timeline_clip_id_);
        if (!edited_location.has_value()) return;
        const auto& edited = timeline_model_.tracks()[edited_location->track_index]
            .clips[edited_location->clip_index];
        if (edited.kind == timeline::ClipKind::Text ||
            edited.kind == timeline::ClipKind::Image) {
            emit activateTimelineClipRequested(
                edited.clip_id, playback_frame_index_, false, true);
            return;
        }
        const auto media = std::find_if(
            session_.mediaItems().begin(), session_.mediaItems().end(),
            [&edited](const application::ImportedMedia& item) {
                return item.metadata.source_path.lexically_normal() ==
                           edited.source_path.lexically_normal() && !item.offline;
            });
        if (media != session_.mediaItems().end()) {
            emit activateTimelineClipRequested(
                edited.clip_id, playback_frame_index_, false, true);
        } else {
            emit refreshPlaybackCompositionRequested();
        }
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error, "timeline", "trim_clip_edge", error.what(),
            {{"track_id", std::to_string(timeline_model_.tracks()[location->track_index].track_id)},
             {"clip_id", std::to_string(clip_id)},
             {"edge", std::to_string(edge_value)},
             {"boundary_frame", std::to_string(boundary_frame)},
             {"mode", std::to_string(mode_value)}});
        emit statusMessageRequested(
            QStringLiteral("Could not adjust the timeline clip edge."));
    }
}

void EditWorkspaceController::setMovePlayheadOnClipSelection(bool enabled) noexcept {
    move_playhead_on_clip_selection_ = enabled;
}

void EditWorkspaceController::setRazorMode(bool enabled) {
    if (timeline_widget_ != nullptr && timeline_widget_->razorMode() != enabled) {
        timeline_widget_->setRazorMode(enabled);
    }
    if (ui_.razor_tool != nullptr && ui_.razor_tool->isChecked() != enabled) {
        const QSignalBlocker blocker(ui_.razor_tool);
        ui_.razor_tool->setChecked(enabled);
    }
    if (ui_.selection_tool != nullptr && ui_.selection_tool->isChecked() == enabled) {
        const QSignalBlocker blocker(ui_.selection_tool);
        ui_.selection_tool->setChecked(!enabled);
    }
    emit razorToolStateChanged(enabled);
}

void EditWorkspaceController::clearTimeline() {
    if (!timeline_model_.hasClip()) return;
    try {
        const auto result = execute(application::ClearTimelineCommand{});
        if (!result.changed()) return;
        publishCommittedEdit(
            result, true, true, QStringLiteral("Timeline cleared."));
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "clear_timeline",
            error.what(),
            {});
        emit statusMessageRequested(
            QStringLiteral("Could not clear the timeline."));
    }
}

void EditWorkspaceController::moveActiveTimelineClip(int direction) {
    if (direction == 0) return;
    const auto location = selectedTimelineClipLocation();
    if (!location.has_value() || playback_is_loading_) return;
    const auto& clip = timeline_model_.tracks()[location->track_index]
        .clips[location->clip_index];
    if (direction < 0 && clip.timeline_start_frame == 0) return;
    const auto new_start = clip.timeline_start_frame + direction;
    try {
        const auto result = execute(application::MoveClipCommand{
            clip.clip_id, clip.track_id, new_start});
        if (result.status == application::EditStatus::NoChange) return;
        if (!result.changed()) {
            emit statusMessageRequested(
                QStringLiteral("The clip cannot move to that frame."));
            return;
        }
        publishCommittedEdit(
            result, true, true, QStringLiteral("Timeline clip moved by one frame."));
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "move_clip",
            error.what(),
            {{"track_id", std::to_string(clip.track_id)},
             {"clip_id", std::to_string(clip.clip_id)},
             {"timeline_frame", std::to_string(new_start)}});
        emit statusMessageRequested(
            QStringLiteral("Could not move the timeline clip."));
    }
}

void EditWorkspaceController::deleteActiveTimelineClip() {
    const auto location = selectedTimelineClipLocation();
    if (!location.has_value() || playback_is_loading_) return;
    const auto clip = timeline_model_.tracks()[location->track_index]
        .clips[location->clip_index];
    if (clip.kind != timeline::ClipKind::Text) {
        const auto imported = std::find_if(
            session_.mediaItems().begin(), session_.mediaItems().end(),
            [&clip](const application::ImportedMedia& item) {
                return item.metadata.source_path.lexically_normal() ==
                           clip.source_path.lexically_normal() && !item.offline;
            });
        if (imported == session_.mediaItems().end()) return;
    }

    try {
        const auto result = execute(application::DeleteClipCommand{clip.clip_id});
        if (!result.changed()) return;
        publishCommittedEdit(
            result, true, true, QStringLiteral("Timeline clip deleted."));
        const auto next = selectedTimelineClipLocation();
        if (!next.has_value()) return;
        const auto& next_clip = timeline_model_.tracks()[next->track_index]
            .clips[next->clip_index];
        emit selectMediaBrowserClipRequested(next_clip.clip_id);
        if (next_clip.kind == timeline::ClipKind::Text) {
            emit activateTimelineClipRequested(next_clip.clip_id, 0, false, false);
        } else {
            const auto next_media = std::find_if(
                session_.mediaItems().begin(), session_.mediaItems().end(),
                [&next_clip](const application::ImportedMedia& item) {
                    return item.metadata.source_path.lexically_normal() ==
                               next_clip.source_path.lexically_normal() && !item.offline;
                });
            if (next_media != session_.mediaItems().end()) {
                emit activateTimelineClipRequested(
                    next_clip.clip_id, 0, false, false);
            }
        }
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "delete_clip",
            error.what(),
            {{"clip_id", std::to_string(clip.clip_id)}});
        emit statusMessageRequested(
            QStringLiteral("Could not delete the timeline clip."));
    }
}

void EditWorkspaceController::splitActiveClipAtPlayhead() {
    const auto location = selectedTimelineClipLocation();
    if (!location.has_value() || playback_is_loading_) return;
    const auto& clip = timeline_model_.tracks()[location->track_index]
        .clips[location->clip_index];
    if (clip.kind != timeline::ClipKind::Text && !canPlaybackSelectedMedia()) return;
    handleTimelineClipSplit(clip.clip_id, playback_frame_index_);
}

void EditWorkspaceController::handleTimelineSeekStarted() {
    if (!playback_available_ || playback_is_loading_) return;
    playback_is_playing_ = false;
    emit refreshPlaybackUiRequested();
    emit playbackCommandRequested(playback::PlaybackCommand::Pause);
}

void EditWorkspaceController::handleTimelineSeek(qint64 global_frame) {
    if (!playback_available_ || playback_is_loading_) return;
    emit seekTimelineRequested(global_frame);
}

void EditWorkspaceController::handleTimelineSeekResult(
    qint64 requested_frame,
    playback::PlaybackCommandResult result) {
    const auto total = timeline_model_.totalDurationFrames();
    if (result == playback::PlaybackCommandResult::Gap) {
        if (total > 0) {
            session_.setPlayheadFrame(std::clamp<std::int64_t>(
                requested_frame, 0, total - 1));
        }
        playback_is_playing_ = false;
        updateTimelineState();
        emit refreshPlaybackUiRequested();
        emit clearPreviewRequested(QStringLiteral("Gap in timeline."));
        emit statusMessageRequested(QStringLiteral("Gap in timeline."));
        return;
    }
    if (result == playback::PlaybackCommandResult::Rejected) {
        emit statusMessageRequested(
            QStringLiteral("The selected timeline media is unavailable."));
        return;
    }
    if (result == playback::PlaybackCommandResult::NoClip ||
        result == playback::PlaybackCommandResult::Unavailable) {
        return;
    }
    if (total > 0) {
        session_.setPlayheadFrame(std::clamp<std::int64_t>(
            requested_frame, 0, total - 1));
    }
    playback_is_playing_ = false;
    updateTimelineState();
    emit refreshPlaybackUiRequested();
}

void EditWorkspaceController::handleTimelineClipSelectionChanged(
    timeline::TrackId track_id,
    timeline::ClipId clip_id) {
    active_transition_.reset();
    const auto location = timeline_model_.locateClip(clip_id);
    if (!location.has_value() ||
        timeline_model_.tracks()[location->track_index].track_id != track_id) {
        return;
    }

    const auto& clip = timeline_model_.tracks()[location->track_index]
        .clips[location->clip_index];
    const auto previous_playhead = timelinePlayheadFrame();
    const bool move_playhead = move_playhead_on_clip_selection_;
    const auto selected_local_frame = move_playhead
        ? std::int64_t{0}
        : localFrameAtTimelinePlayhead(clip, previous_playhead);
    active_timeline_track_id_ = track_id;
    active_timeline_clip_id_ = clip_id;
    if (move_playhead) {
        preserved_timeline_playhead_frame_.reset();
    } else if (clip.timeline_start_frame + selected_local_frame != previous_playhead) {
        preserved_timeline_playhead_frame_ = previous_playhead;
    } else {
        preserved_timeline_playhead_frame_.reset();
    }

    const bool base_track_media = location->track_index == 0 &&
        timeline::isMediaClipKind(clip.kind);
    if (!base_track_media && clip.kind == timeline::ClipKind::Text) {
        playback_is_playing_ = false;
        playback_frame_index_ = selected_local_frame;
        updateTimelineState();
        emit timelineSelectionPresentationChanged();
        emit refreshPlaybackUiRequested();
        emit activateTimelineClipRequested(
            clip.clip_id, selected_local_frame, false, !move_playhead);
        emit statusMessageRequested(QStringLiteral("Text clip selected."));
        return;
    }

    const auto media = std::find_if(
        session_.mediaItems().begin(), session_.mediaItems().end(),
        [&clip](const application::ImportedMedia& item) {
            return item.metadata.source_path.lexically_normal() ==
                   clip.source_path.lexically_normal();
        });
    if (media == session_.mediaItems().end()) {
        logging::Logger::instance().log(
            logging::Level::Error, "timeline", "select_clip",
            "The selected timeline clip has no matching imported media item.",
            {{"track_id", std::to_string(track_id)},
             {"clip_id", std::to_string(clip_id)}});
        emit statusMessageRequested(
            QStringLiteral("Could not select the timeline clip."));
        return;
    }

    session_.selectionForUi().selected_source_path =
        media->metadata.source_path.lexically_normal();
    if (playback_is_loading_) emit playbackInvalidateRequested(true);
    playback_is_playing_ = false;
    playback_frame_index_ = selected_local_frame;
    emit showTimelineClipPreviewRequested(clip_id);
    updateTimelineState();
    emit timelineSelectionPresentationChanged();
    emit refreshPlaybackUiRequested();
    if (!media->offline) {
        emit activateTimelineClipRequested(
            clip_id, selected_local_frame, false, !move_playhead);
    }
    emit statusMessageRequested(QStringLiteral("Timeline clip selected."));
}

void EditWorkspaceController::handleTimelineClipSelectionCleared() {
    const auto previous_playhead = timelinePlayheadFrame();
    emit playbackInvalidateRequested(true);
    playback_is_playing_ = false;
    active_timeline_track_id_.reset();
    active_timeline_clip_id_.reset();
    active_transition_.reset();
    session_.selectionForUi().selected_source_path.reset();
    playback_frame_index_ = 0;
    if (move_playhead_on_clip_selection_ || previous_playhead == 0) {
        preserved_timeline_playhead_frame_.reset();
    } else {
        preserved_timeline_playhead_frame_ = previous_playhead;
    }
    emit clearMediaBrowserSelectionRequested();
    updateTimelineState();
    emit timelineSelectionPresentationChanged();
    emit refreshPlaybackUiRequested();
    emit statusMessageRequested(QStringLiteral("Gap in timeline."));
}

}  // namespace ui
