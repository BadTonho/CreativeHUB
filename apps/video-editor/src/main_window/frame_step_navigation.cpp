#include "frame_step_navigation.h"

#include <algorithm>

namespace main_window_detail {

FrameStepDecision decideFrameStep(
    const timeline::TimelineModel& model,
    std::optional<timeline::ClipLocation> active_clip,
    std::int64_t local_frame,
    FrameStepDirection direction) {
    if (!active_clip.has_value() ||
        active_clip->track_index >= model.trackCount() ||
        active_clip->clip_index >= model.clipCount(active_clip->track_index)) {
        return {};
    }

    const auto& clip = model.tracks()[active_clip->track_index]
        .clips[active_clip->clip_index];
    if (direction == FrameStepDirection::Forward) {
        if (local_frame < clip.timeline_duration_frames - 1) return {};

        const auto global_frame = clip.timeline_start_frame +
            clip.timeline_duration_frames;
        if (const auto next = model.topClipAt(global_frame); next.has_value()) {
            const auto& next_clip = model.tracks()[next->track_index]
                .clips[next->clip_index];
            return {
                FrameStepAction::ActivateClip,
                next,
                std::max<std::int64_t>(
                    0, global_frame - next_clip.timeline_start_frame)};
        }
        const bool has_future_clip = std::any_of(
            model.tracks().begin(),
            model.tracks().end(),
            [global_frame](const timeline::TimelineTrack& track) {
                return std::any_of(
                    track.clips.begin(),
                    track.clips.end(),
                    [global_frame](const timeline::TimelineClip& candidate) {
                        return candidate.timeline_start_frame > global_frame;
                    });
            });
        return {
            has_future_clip ? FrameStepAction::Gap : FrameStepAction::End,
            std::nullopt,
            0};
    }

    if (local_frame > 0) return {};
    const auto global_frame = clip.timeline_start_frame - 1;
    if (global_frame < 0) {
        return {FrameStepAction::Beginning, std::nullopt, 0};
    }
    if (const auto previous = model.topClipAt(global_frame);
        previous.has_value()) {
        const auto& previous_clip = model.tracks()[previous->track_index]
            .clips[previous->clip_index];
        return {
            FrameStepAction::ActivateClip,
            previous,
            global_frame - previous_clip.timeline_start_frame};
    }
    return {FrameStepAction::Gap, std::nullopt, 0};
}

} // namespace main_window_detail
