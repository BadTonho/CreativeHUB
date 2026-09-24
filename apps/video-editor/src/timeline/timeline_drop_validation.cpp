#include "timeline_drop_validation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace timeline {

bool TimelineDropValidator::overlaps(
    const std::vector<TimelineTrack>& tracks,
    std::size_t track_index,
    std::int64_t start_frame,
    std::int64_t duration_frames,
    std::optional<ClipLocation> excluded) noexcept {
    if (track_index >= tracks.size() || start_frame < 0 || duration_frames <= 0) return true;
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto end_frame = start_frame > maximum - duration_frames
        ? maximum : start_frame + duration_frames;
    for (std::size_t clip_index = 0; clip_index < tracks[track_index].clips.size(); ++clip_index) {
        if (excluded == ClipLocation{track_index, clip_index}) continue;
        const auto& clip = tracks[track_index].clips[clip_index];
        const auto clip_end = clip.timeline_start_frame >
                maximum - clip.timeline_duration_frames
            ? maximum : clip.timeline_start_frame + clip.timeline_duration_frames;
        if (start_frame < clip_end && clip.timeline_start_frame < end_frame) return true;
    }
    return false;
}

SnapPlacement TimelineDropValidator::snap(
    const std::vector<TimelineTrack>& tracks,
    const TimelineGeometry& geometry,
    bool enabled,
    std::size_t track_index,
    std::int64_t raw_start_frame,
    std::int64_t duration_frames,
    std::optional<ClipLocation> excluded) noexcept {
    SnapPlacement result{std::max<std::int64_t>(0, raw_start_frame), std::nullopt};
    if (!enabled || track_index >= tracks.size() || duration_frames <= 0) return result;
    constexpr double tolerance_pixels = 8.0;
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto raw_end_frame = result.start_frame > maximum - duration_frames
        ? maximum : result.start_frame + duration_frames;
    struct Candidate {
        std::int64_t start_frame = 0;
        std::int64_t guide_frame = 0;
        double distance_pixels = 0.0;
    };
    std::optional<Candidate> best;
    const auto consider = [&](std::int64_t dragged_edge_frame,
                              std::int64_t candidate_start_frame,
                              std::int64_t guide_frame) {
        if (candidate_start_frame < 0 || guide_frame < 0) return;
        const auto distance = std::abs(
            geometry.contentXForFrame(dragged_edge_frame) -
            geometry.contentXForFrame(guide_frame));
        if (distance > tolerance_pixels) return;
        const Candidate candidate{candidate_start_frame, guide_frame, distance};
        if (!best.has_value() || candidate.distance_pixels < best->distance_pixels - 0.000001 ||
            (std::abs(candidate.distance_pixels - best->distance_pixels) <= 0.000001 &&
             (candidate.start_frame < best->start_frame ||
              (candidate.start_frame == best->start_frame &&
               candidate.guide_frame < best->guide_frame)))) {
            best = candidate;
        }
    };

    consider(result.start_frame, 0, 0);
    const auto timeline_end = geometry.displayDuration();
    if (timeline_end > 0 && duration_frames <= timeline_end) {
        consider(raw_end_frame, timeline_end - duration_frames, timeline_end);
    }
    const auto clip_end = [maximum](const TimelineClip& clip)
        -> std::optional<std::int64_t> {
        if (clip.timeline_duration_frames <= 0 || clip.timeline_start_frame < 0 ||
            clip.timeline_start_frame > maximum - clip.timeline_duration_frames) {
            return std::nullopt;
        }
        return clip.timeline_start_frame + clip.timeline_duration_frames;
    };
    for (std::size_t clip_index = 0; clip_index < tracks[track_index].clips.size(); ++clip_index) {
        if (excluded == ClipLocation{track_index, clip_index}) continue;
        const auto& clip = tracks[track_index].clips[clip_index];
        const auto end = clip_end(clip);
        if (!end.has_value()) continue;
        consider(result.start_frame, *end, *end);
        if (clip.timeline_start_frame >= duration_frames) {
            consider(raw_end_frame, clip.timeline_start_frame - duration_frames,
                     clip.timeline_start_frame);
        }
    }
    if (best.has_value()) {
        result.start_frame = best->start_frame;
        result.guide_frame = best->guide_frame;
    }
    return result;
}

} // namespace timeline
