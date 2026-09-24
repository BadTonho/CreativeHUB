#pragma once

#include "editor_session.h"
#include "timeline/timeline_clip_edge_command.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace application {

enum class EditStatus { Applied, NoChange, Rejected, UndoUnavailable, RedoUnavailable };

enum class EditReason {
    None,
    InvalidTarget,
    InvalidPosition,
    Overlap,
    InvalidBoundary,
    InvalidRange,
    InvalidTimingMetadata,
    OfflineMedia,
    MediaNotFound,
    TransitionNotFound,
    InvalidName,
    InvalidValue,
    TrackNotEmpty,
};

struct TimelineEditResult {
    EditStatus status = EditStatus::Rejected;
    EditReason reason = EditReason::None;
    std::vector<timeline::TrackId> affected_track_ids;
    std::vector<timeline::ClipId> affected_clip_ids;
    EditorSelection selection;
    std::int64_t playhead_frame = 0;
    std::optional<std::int64_t> preserved_playhead_frame;
    bool invalidate_playback = false;

    [[nodiscard]] bool changed() const noexcept {
        return status == EditStatus::Applied;
    }
};

struct MoveClipCommand {
    timeline::ClipId clip_id = 0;
    timeline::TrackId target_track_id = 0;
    std::int64_t timeline_start_frame = 0;
};

struct ReorderClipCommand {
    timeline::ClipId clip_id = 0;
    std::size_t target_index = 0;
};

struct SplitClipCommand {
    timeline::ClipId clip_id = 0;
    std::int64_t local_frame = 0;
};

struct TrimClipEdgeCommand {
    timeline::ClipId clip_id = 0;
    timeline::ClipEdge edge = timeline::ClipEdge::Left;
    std::int64_t boundary_frame = 0;
    timeline::ClipEdgeEditMode mode = timeline::ClipEdgeEditMode::Rolling;
    std::int64_t timeline_playhead_frame = 0;
    std::int64_t playback_frame = 0;
};

struct TrimClipRangeCommand {
    timeline::ClipId clip_id = 0;
    std::int64_t source_start_frame = 0;
    std::int64_t duration_frames = 0;
};

struct DeleteClipCommand {
    timeline::ClipId clip_id = 0;
};

struct AddMediaClipCommand {
    std::filesystem::path source_path;
    timeline::TrackId track_id = 0;
    std::optional<std::int64_t> timeline_start_frame;
};

struct AddTextClipCommand {
    timeline::TrackId track_id = 0;
    std::int64_t timeline_start_frame = 0;
    std::int64_t duration_frames = 0;
    double frame_rate = 30.0;
};

struct AddTransitionCommand {
    timeline::TrackId track_id = 0;
    timeline::ClipId from_clip_id = 0;
    timeline::ClipId to_clip_id = 0;
    timeline::TransitionKind kind = timeline::TransitionKind::CrossDissolve;
    std::int64_t duration_frames = 15;
};

struct UpdateTransitionCommand {
    timeline::TrackId track_id = 0;
    timeline::ClipId from_clip_id = 0;
    timeline::ClipId to_clip_id = 0;
    timeline::TransitionKind kind = timeline::TransitionKind::CrossDissolve;
    std::int64_t duration_frames = 15;
};

struct RemoveTransitionCommand {
    timeline::TrackId track_id = 0;
    timeline::ClipId from_clip_id = 0;
    timeline::ClipId to_clip_id = 0;
};

struct AddTrackCommand { std::string name; };
struct RenameTrackCommand {
    timeline::TrackId track_id = 0;
    std::string name;
};
struct MoveTrackCommand {
    timeline::TrackId track_id = 0;
    std::size_t target_index = 0;
};
struct RemoveTrackCommand { timeline::TrackId track_id = 0; };
struct ClearTimelineCommand {};

struct SetClipAudioCommand {
    timeline::ClipId clip_id = 0;
    double gain = 1.0;
    bool muted = false;
};
struct SetTrackAudioCommand {
    timeline::TrackId track_id = 0;
    double gain = 1.0;
    bool muted = false;
};
struct SetClipTextCommand {
    timeline::ClipId clip_id = 0;
    timeline::TextStyle text;
};
struct SetTransformPropertyCommand {
    timeline::ClipId clip_id = 0;
    timeline::TransformProperty property = timeline::TransformProperty::PositionX;
    std::int64_t frame = 0;
    double value = 0.0;
};
struct ToggleTransformKeyframeCommand {
    timeline::ClipId clip_id = 0;
    timeline::TransformProperty property = timeline::TransformProperty::PositionX;
    std::int64_t frame = 0;
};

class TimelineCommandService final {
public:
    using EditBatchId = std::uint64_t;

    explicit TimelineCommandService(EditorSession& session) noexcept;

    [[nodiscard]] TimelineEditResult execute(const MoveClipCommand& command);
    [[nodiscard]] TimelineEditResult execute(const ReorderClipCommand& command);
    [[nodiscard]] TimelineEditResult execute(const SplitClipCommand& command);
    [[nodiscard]] TimelineEditResult execute(const TrimClipEdgeCommand& command);
    [[nodiscard]] TimelineEditResult execute(const TrimClipRangeCommand& command);
    [[nodiscard]] TimelineEditResult execute(const DeleteClipCommand& command);
    [[nodiscard]] TimelineEditResult execute(const AddMediaClipCommand& command);
    [[nodiscard]] TimelineEditResult execute(const AddTextClipCommand& command);
    [[nodiscard]] TimelineEditResult execute(const AddTransitionCommand& command);
    [[nodiscard]] TimelineEditResult execute(const UpdateTransitionCommand& command);
    [[nodiscard]] TimelineEditResult execute(const RemoveTransitionCommand& command);
    [[nodiscard]] TimelineEditResult execute(const AddTrackCommand& command);
    [[nodiscard]] TimelineEditResult execute(const RenameTrackCommand& command);
    [[nodiscard]] TimelineEditResult execute(const MoveTrackCommand& command);
    [[nodiscard]] TimelineEditResult execute(const RemoveTrackCommand& command);
    [[nodiscard]] TimelineEditResult execute(const ClearTimelineCommand& command);
    [[nodiscard]] TimelineEditResult execute(const SetClipAudioCommand& command);
    [[nodiscard]] TimelineEditResult execute(const SetTrackAudioCommand& command);
    [[nodiscard]] TimelineEditResult execute(const SetClipTextCommand& command);
    [[nodiscard]] TimelineEditResult execute(const SetTransformPropertyCommand& command);
    [[nodiscard]] TimelineEditResult execute(const ToggleTransformKeyframeCommand& command);

    [[nodiscard]] EditBatchId beginEditBatch();
    [[nodiscard]] TimelineEditResult finishEditBatch(EditBatchId batch_id);

    [[nodiscard]] TimelineEditResult undo();
    [[nodiscard]] TimelineEditResult redo();
    void recordLegacyEdit(timeline::EditState state);
    void clearHistory() noexcept;
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] std::size_t undoCount() const noexcept;

private:
    [[nodiscard]] TimelineEditResult result(
        EditStatus status,
        EditReason reason = EditReason::None) const;
    void recordSuccessfulEdit(timeline::EditState before);
    void selectClip(timeline::ClipLocation location);
    void selectClip(timeline::ClipId clip_id);
    void selectTransition(
        timeline::TrackId track_id,
        timeline::ClipId from_clip_id,
        timeline::ClipId to_clip_id);

    struct EditBatch {
        EditBatchId id = 0;
        timeline::EditState before;
    };

    EditorSession& session_;
    EditBatchId next_edit_batch_id_ = 1;
    std::optional<EditBatch> active_edit_batch_;
};

} // namespace application
