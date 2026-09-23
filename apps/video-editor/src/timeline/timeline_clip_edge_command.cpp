#include "timeline_clip_edge_command.h"

#include <algorithm>

namespace timeline {

ClipEdgeTrimOutcome applyClipEdgeTrim(
    TimelineModel& model,
    ClipLocation location,
    ClipEdge edge,
    std::int64_t boundary_frame,
    ClipEdgeEditMode mode,
    std::int64_t playhead_before,
    std::int64_t playback_frame_before) {
    if (location.track_index >= model.trackCount() ||
        location.clip_index >= model.clipCount(location.track_index)) {
        return {TrimClipResult::InvalidIndex, std::nullopt};
    }

    const auto clip_id = model.tracks()[location.track_index]
        .clips[location.clip_index].clip_id;
    const auto result = model.trimClipEdge(
        location.track_index, location.clip_index, edge, boundary_frame, mode);
    if (result != TrimClipResult::Trimmed) return {result, std::nullopt};

    const auto edited_location = model.locateClip(clip_id);
    if (!edited_location.has_value()) return {result, std::nullopt};

    const auto& edited_clip = model.tracks()[edited_location->track_index]
        .clips[edited_location->clip_index];
    const auto edited_end = edited_clip.timeline_start_frame +
        edited_clip.timeline_duration_frames;
    if (playhead_before >= edited_clip.timeline_start_frame &&
        playhead_before < edited_end) {
        return {result, ClipEdgeTrimSelection{
            *edited_location,
            playhead_before - edited_clip.timeline_start_frame,
            std::nullopt}};
    }

    return {result, ClipEdgeTrimSelection{
        *edited_location,
        std::clamp<std::int64_t>(
            playback_frame_before, 0, edited_clip.timeline_duration_frames - 1),
        playhead_before}};
}

} // namespace timeline
