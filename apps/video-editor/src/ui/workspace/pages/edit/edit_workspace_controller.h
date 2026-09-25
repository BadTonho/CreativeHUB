#pragma once

#include "application/editor_session.h"
#include "application/timeline_command_service.h"
#include "playback/playback_controller.h"
#include "ui/workspace/pages/edit/edit_workspace_ui.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <filesystem>
#include <string>
#include <optional>

class QWidget;

namespace timeline {
class TimelineWidget;
}

namespace ui {

// Coordinates Edit commands against the session shared by the workspace
// pages. The command service remains the authority for edits and history.
class EditWorkspaceController final : public QObject {
    Q_OBJECT

public:
    EditWorkspaceController(
        application::EditorSession& session,
        application::TimelineCommandService& command_service,
        QObject* parent = nullptr) noexcept;

    template <typename Command>
    [[nodiscard]] application::TimelineEditResult execute(const Command& command) {
        const auto result = command_service_.execute(command);
        publishResult(result);
        return result;
    }

    [[nodiscard]] application::TimelineEditResult undo();
    [[nodiscard]] application::TimelineEditResult redo();
    [[nodiscard]] application::TimelineEditResult deleteSelectedClip();
    [[nodiscard]] application::TimelineEditResult addTrack(std::string name);
    [[nodiscard]] application::TimelineEditResult renameTrack(
        timeline::TrackId track_id,
        std::string name);
    [[nodiscard]] application::TimelineEditResult moveTrack(
        timeline::TrackId track_id,
        std::size_t target_index);
    [[nodiscard]] application::TimelineEditResult removeTrack(
        timeline::TrackId track_id);
    [[nodiscard]] application::TimelineEditResult addMediaClip(
        const std::filesystem::path& source_path,
        timeline::TrackId track_id,
        std::optional<std::int64_t> timeline_frame);
    void addTextClipAt(timeline::TrackId track_id, qint64 timeline_frame);
    void promptAddVideoTrack(QWidget* dialog_parent);
    void promptRenameActiveTrack(QWidget* dialog_parent);
    void moveActiveTrack(int direction);
    void removeActiveTrack();
    void setUi(EditWorkspaceUi ui);
    [[nodiscard]] const EditWorkspaceUi& ui() const noexcept;
    void setTimelineWidget(timeline::TimelineWidget* timeline_widget);
    void moveClip(
        timeline::ClipId clip_id,
        timeline::TrackId target_track_id,
        std::int64_t timeline_start_frame);
    [[nodiscard]] application::TimelineCommandService::EditBatchId beginEditBatch();
    [[nodiscard]] application::TimelineEditResult finishEditBatch(
        application::TimelineCommandService::EditBatchId batch_id);
    void recordLegacyEdit(timeline::EditState state);
    void clearHistory() noexcept;

    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] application::EditorSession& session() const noexcept;

    void setPlaybackPresentation(bool playing, bool loading, bool available);
    void requestPlaybackCommand(playback::PlaybackCommand command);
    void togglePlayback();
    void presentPlaybackFrame(qint64 clip_frame);
    void refreshTimelinePresentation();
    void refreshInspectorPresentation();
    void setTimelineReadOnly(bool read_only);
    void updateTimelineState();
    void applyTimelineZoom(double factor);
    void updateHistoryActions();
    void updateInspector();
    void beginAudioEdit();
    void finishAudioEdit();
    void applyClipAudioControls();
    void applyTrackAudioControls();
    void beginTransformEdit();
    void finishTransformEdit();
    void applyTextStyle();
    void chooseTextColor();
    void applyTransformProperty(int property_index, double value);
    void toggleTransformKeyframe(int property_index);
    void handleTimelineTransitionSelected(
        timeline::TrackId track_id,
        timeline::ClipId from_clip_id,
        timeline::ClipId to_clip_id);
    void handleTimelineTransitionSelectionCleared();
    void handleTimelineTransitionAdd(
        timeline::TrackId track_id,
        timeline::ClipId from_clip_id,
        timeline::ClipId to_clip_id,
        qint64 kind);
    void handleTimelineTransitionRemove(
        timeline::TrackId track_id,
        timeline::ClipId from_clip_id,
        timeline::ClipId to_clip_id);
    void applyTransitionSettings();
    void removeSelectedTransition();
    void handleTimelineClipSplit(timeline::ClipId clip_id, qint64 local_frame);
    void handleTimelineClipTrim(
        timeline::ClipId clip_id,
        qint64 edge,
        qint64 boundary_frame,
        qint64 mode);
    void handleTimelineTrimStarted();
    void handleTimelineClipSelectionChanged(
        timeline::TrackId track_id,
        timeline::ClipId clip_id);
    void handleTimelineClipSelectionCleared();
    void handleTimelineEffectDrop(
        const QString& effect_id,
        timeline::TrackId track_id,
        qint64 timeline_frame);
    void setMovePlayheadOnClipSelection(bool enabled) noexcept;
    void setRazorMode(bool enabled);
    void clearTimeline();
    void moveActiveTimelineClip(int direction);
    void deleteActiveTimelineClip();
    void splitActiveClipAtPlayhead();
    void handleTimelineSeekStarted();
    void handleTimelineSeek(qint64 global_frame);
    void handleTimelineSeekResult(
        qint64 requested_frame,
        playback::PlaybackCommandResult result);
    [[nodiscard]] std::optional<timeline::ClipLocation>
    selectedTimelineClipLocation() const noexcept;
    [[nodiscard]] bool canPlaybackSelectedMedia() const noexcept;
    [[nodiscard]] std::int64_t timelinePlayheadFrame() const noexcept;

signals:
    void commandResultProduced(const application::TimelineEditResult& result);
    void historyStateChanged(bool can_undo, bool can_redo);
    void statusMessageRequested(const QString& message);
    void warningMessageRequested(const QString& title, const QString& message);
    void timelineEditCommitted(
        const application::TimelineEditResult& result,
        bool stop_playback,
        bool refresh_composition,
        const QString& status_message);
    void timelineMediaDropRequested(
        const QString& source_path,
        timeline::TrackId track_id,
        qint64 timeline_frame);
    void timelineImageClipEditRequested(timeline::ClipId clip_id);
    void timelineSnapChanged(bool enabled);
    void projectDirtyStateUpdateRequested();
    void playbackInvalidateRequested(bool stop_playback);
    void refreshPlaybackCompositionRequested();
    void renderCompositionFrameRequested(qint64 timeline_frame, qint64 clip_frame);
    void seekActiveClipRequested(qint64 clip_frame);
    void playbackAudioParametersRequested();
    void playbackCommandRequested(playback::PlaybackCommand command);
    void seekTimelineRequested(qint64 global_frame);
    void clearPreviewRequested(const QString& message);
    void razorToolStateRequested(bool enabled);
    void razorToolStateChanged(bool enabled);
    void monitorVolumeChangedRequested(int percent);
    void selectMediaBrowserClipRequested(timeline::ClipId clip_id);
    void activateTimelineClipRequested(
        timeline::ClipId clip_id,
        qint64 clip_frame,
        bool resume_playback,
        bool preserve_timeline_playhead);
    void showTimelineClipPreviewRequested(timeline::ClipId clip_id);
    void clearMediaBrowserSelectionRequested();
    void refreshPlaybackUiRequested();
    void timelineSelectionPresentationChanged();

private:
    void synchronizeActiveTimelineSelection() noexcept;
    void publishResult(const application::TimelineEditResult& result);
    void publishHistoryState();
    void publishCommittedEdit(
        const application::TimelineEditResult& result,
        bool stop_playback,
        bool refresh_composition,
        const QString& status_message);

    application::EditorSession& session_;
    application::TimelineCommandService& command_service_;
    const timeline::TimelineModel& timeline_model_;
    std::optional<timeline::TrackId>& active_timeline_track_id_;
    std::optional<timeline::ClipId>& active_timeline_clip_id_;
    std::optional<std::int64_t>& preserved_timeline_playhead_frame_;
    std::optional<timeline::TransitionSelection>& active_transition_;
    std::int64_t& playback_frame_index_;
    EditWorkspaceUi ui_;
    timeline::TimelineWidget* timeline_widget_ = nullptr;
    bool playback_is_playing_ = false;
    bool playback_is_loading_ = false;
    bool playback_available_ = false;
    bool move_playhead_on_clip_selection_ = false;
    std::array<std::uint8_t, 4> text_color_{255, 255, 255, 255};
    std::optional<application::TimelineCommandService::EditBatchId>
        pending_audio_edit_batch_id_;
    std::optional<application::TimelineCommandService::EditBatchId>
        pending_transform_edit_batch_id_;
};

}  // namespace ui
