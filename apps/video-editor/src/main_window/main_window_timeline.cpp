#include "main_window/main_window.h"
#include "main_window_support.h"

#include "logging/logger.h"
#include "ui/preview/preview_widget.h"
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
#include <QPushButton>
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
        return;
    }
    synchronizeActiveTimelineSelection();
    if (editUi().timeline != nullptr) {
        editUi().timeline->setTracks(timeline_model_.tracks());
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
