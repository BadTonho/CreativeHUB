#include "frame_step_navigation.h"

#include <algorithm>

namespace playback::detail {

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
        const auto boundary = clip.timeline_start_frame + clip.timeline_duration_frames;
        if (const auto next = model.topClipAt(boundary); next.has_value()) {
            const auto& next_clip = model.tracks()[next->track_index]
                .clips[next->clip_index];
            return {
                FrameStepAction::ActivateClip,
                next,
                std::max<std::int64_t>(0, boundary - next_clip.timeline_start_frame)};
        }
        const bool future = std::any_of(
            model.tracks().begin(), model.tracks().end(),
            [boundary](const timeline::TimelineTrack& track) {
                return std::any_of(
                    track.clips.begin(), track.clips.end(),
                    [boundary](const timeline::TimelineClip& candidate) {
                        return candidate.timeline_start_frame > boundary;
                    });
            });
        return {future ? FrameStepAction::Gap : FrameStepAction::End, std::nullopt, 0};
    }

    if (local_frame > 0) return {};
    const auto previous_frame = clip.timeline_start_frame - 1;
    if (previous_frame < 0) {
        return {FrameStepAction::Beginning, std::nullopt, 0};
    }
    if (const auto previous = model.topClipAt(previous_frame); previous.has_value()) {
        const auto& previous_clip = model.tracks()[previous->track_index]
            .clips[previous->clip_index];
        return {
            FrameStepAction::ActivateClip,
            previous,
            previous_frame - previous_clip.timeline_start_frame};
    }
    return {FrameStepAction::Gap, std::nullopt, 0};
}

} // namespace playback::detail
