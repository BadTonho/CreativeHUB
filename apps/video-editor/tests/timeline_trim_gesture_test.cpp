#include "timeline/timeline_trim_gesture.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

timeline::TimelineClip video(
    std::int64_t start,
    std::int64_t duration,
    std::int64_t source_start = 0) {
    timeline::TimelineClip clip;
    clip.timeline_start_frame = start;
    clip.timeline_duration_frames = duration;
    clip.source_start_frame = source_start;
    clip.frame_count = 300;
    clip.frame_rate = 30.0;
    return clip;
}

} // namespace

int main() {
    try {
        using timeline::ClipEdge;
        using timeline::ClipEdgeEditMode;
        using timeline::ClipLocation;
        using timeline::TimelineTrimGesture;
        using FinishKind = timeline::TrimGestureFinish::Kind;
        using Phase = TimelineTrimGesture::Phase;

        const std::vector<timeline::TimelineTrack> shared{
            {1, "Video 1", 1.0, false,
             {video(0, 100), video(100, 100, 100)}}};
        TimelineTrimGesture gesture;
        gesture.begin(shared, ClipLocation{0, 0}, ClipEdge::Right,
                      ClipEdgeEditMode::Rolling, 100, 1800, {1.0, 2.0},
                      std::pair<std::size_t, std::size_t>{0, 1});
        require(gesture.phase() == Phase::PendingTransition &&
                    gesture.scaleDuration() == 1800 &&
                    gesture.preview().has_value(),
                "A shared cut did not begin as a pending transition with a preview.");
        require(!gesture.move(shared, std::nullopt, {2.0, 2.0}).started &&
                    gesture.phase() == Phase::PendingTransition,
                "A missing boundary started a trim from a shared cut.");
        require(!gesture.move(shared, 100, {3.0, 2.0}).started &&
                    gesture.phase() == Phase::PendingTransition,
                "An unchanged boundary started a trim from a shared cut.");
        const auto click = gesture.finish(shared, 110, {4.0, 2.0});
        require(click.kind == FinishKind::SelectTransition &&
                    click.location == ClipLocation{0, 0} &&
                    click.transition_pair ==
                        std::pair<std::size_t, std::size_t>{0, 1} &&
                    gesture.phase() == Phase::Inactive &&
                    !gesture.preview().has_value(),
                "Releasing a shared cut without a valid move did not select its transition.");

        gesture.begin(shared, ClipLocation{0, 0}, ClipEdge::Right,
                      ClipEdgeEditMode::Rolling, 100, 1800, {1.0, 2.0},
                      std::pair<std::size_t, std::size_t>{0, 1});
        const auto start = gesture.move(shared, 110, {2.0, 2.0});
        require(start.started && start.repaint &&
                    gesture.phase() == Phase::Active &&
                    gesture.preview()->boundary_frame == 110 &&
                    gesture.preview()->neighbor_clip.has_value(),
                "A valid shared-boundary move did not start a rolling preview.");
        const auto invalid = gesture.move(shared, std::nullopt, {3.0, 2.0});
        require(!invalid.started && !invalid.repaint &&
                    gesture.preview()->boundary_frame == 110,
                "An invalid pointer boundary discarded the last valid preview.");
        const auto rolling = gesture.finish(shared, std::nullopt, {3.0, 2.0});
        require(rolling.kind == FinishKind::RequestTrim &&
                    rolling.location == ClipLocation{0, 0} &&
                    rolling.edge == ClipEdge::Right &&
                    rolling.mode == ClipEdgeEditMode::Rolling &&
                    rolling.boundary_frame == 110 &&
                    !rolling.legacy_range.has_value() &&
                    shared[0].clips[0].timeline_duration_frames == 100,
                "A rolling gesture did not request one edit while leaving the model untouched.");

        const std::vector<timeline::TimelineTrack> single{
            {1, "Video 1", 1.0, false, {video(50, 50, 50)}}};
        gesture.begin(single, ClipLocation{0, 0}, ClipEdge::Left,
                      ClipEdgeEditMode::Individual, 50, 1800, {1.0, 2.0});
        const auto no_change = gesture.finish(single, 50, {1.0, 2.0});
        require(no_change.kind == FinishKind::None &&
                    gesture.phase() == Phase::Inactive,
                "Clicking an outer edge requested an unchanged trim.");

        gesture.begin(single, ClipLocation{0, 0}, ClipEdge::Left,
                      ClipEdgeEditMode::Individual, 50, 1800, {1.0, 2.0});
        require(gesture.move(single, 60, {2.0, 2.0}).repaint &&
                    gesture.preview()->boundary_frame == 60,
                "An individual trim did not update its preview.");
        const auto final_position = gesture.finish(single, 70, {3.0, 2.0});
        require(final_position.kind == FinishKind::RequestTrim &&
                    final_position.boundary_frame == 70 &&
                    final_position.mode == ClipEdgeEditMode::Individual &&
                    final_position.legacy_range.has_value() &&
                    final_position.legacy_range->local_start_frame == 20 &&
                    final_position.legacy_range->local_end_frame == 50,
                "An individual trim did not use the release position or legacy range.");

        gesture.begin(single, ClipLocation{0, 0}, ClipEdge::Right,
                      ClipEdgeEditMode::Individual, 100, 1800, {1.0, 2.0});
        const auto shortened = gesture.finish(single, 90, {2.0, 2.0});
        require(shortened.kind == FinishKind::RequestTrim &&
                    shortened.legacy_range.has_value() &&
                    shortened.legacy_range->local_start_frame == 0 &&
                    shortened.legacy_range->local_end_frame == 40,
                "A right-edge shortening lost its legacy local range.");

        gesture.begin(single, ClipLocation{0, 0}, ClipEdge::Left,
                      ClipEdgeEditMode::Individual, 50, 1800, {1.0, 2.0});
        gesture.cancel();
        require(!gesture.active() && !gesture.preview().has_value() &&
                    gesture.scaleDuration() == 0 &&
                    gesture.finish(single, 70, {2.0, 2.0}).kind == FinishKind::None,
                "Cancelling a gesture left an edit or a frozen scale behind.");

        gesture.begin(single, ClipLocation{4, 0}, ClipEdge::Left,
                      ClipEdgeEditMode::Individual, 50, 1800, {1.0, 2.0});
        require(gesture.finish(single, 70, {2.0, 2.0}).kind == FinishKind::None,
                "An invalid clip location produced a trim request.");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
