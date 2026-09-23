#include "main_window/frame_step_navigation.h"

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using main_window_detail::FrameStepAction;
using main_window_detail::FrameStepDirection;

struct ClipRange {
    std::int64_t start;
    std::int64_t duration;
};

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

timeline::TimelineModel makeModel(
    const std::vector<std::vector<ClipRange>>& track_ranges) {
    timeline::TimelineModel model;
    auto snapshot = model.snapshot();
    snapshot.tracks.clear();
    timeline::TrackId next_track_id = 1;
    timeline::ClipId next_clip_id = 1;
    for (const auto& ranges : track_ranges) {
        timeline::TimelineTrack track;
        track.track_id = next_track_id++;
        track.name = "Video " + std::to_string(track.track_id);
        for (const auto& range : ranges) {
            timeline::TimelineClip clip;
            clip.timeline_start_frame = range.start;
            clip.timeline_duration_frames = range.duration;
            clip.clip_id = next_clip_id++;
            clip.track_id = track.track_id;
            track.clips.push_back(clip);
        }
        snapshot.tracks.push_back(std::move(track));
    }
    model.restore(std::move(snapshot));
    return model;
}

void expectDecision(
    const timeline::TimelineModel& model,
    std::optional<timeline::ClipLocation> active,
    std::int64_t local_frame,
    FrameStepDirection direction,
    FrameStepAction action,
    std::optional<timeline::ClipLocation> destination = std::nullopt,
    std::int64_t destination_local_frame = 0) {
    const auto decision = main_window_detail::decideFrameStep(
        model, active, local_frame, direction);
    require(decision.action == action &&
                decision.destination == destination &&
                decision.local_frame == destination_local_frame,
            "Frame-step boundary decision did not match the current behavior.");
}

void validateContiguousAndLimits() {
    const auto model = makeModel({{{0, 5}, {5, 4}}});
    const timeline::ClipLocation first{0, 0};
    const timeline::ClipLocation second{0, 1};

    expectDecision(model, first, 2, FrameStepDirection::Forward,
                   FrameStepAction::StepWorker);
    expectDecision(model, second, 2, FrameStepDirection::Backward,
                   FrameStepAction::StepWorker);
    expectDecision(model, first, 4, FrameStepDirection::Forward,
                   FrameStepAction::ActivateClip, second, 0);
    expectDecision(model, second, 0, FrameStepDirection::Backward,
                   FrameStepAction::ActivateClip, first, 4);
    expectDecision(model, first, 0, FrameStepDirection::Backward,
                   FrameStepAction::Beginning);
    expectDecision(model, second, 3, FrameStepDirection::Forward,
                   FrameStepAction::End);
    expectDecision(model, first, 99, FrameStepDirection::Forward,
                   FrameStepAction::ActivateClip, second, 0);
    expectDecision(model, second, -1, FrameStepDirection::Backward,
                   FrameStepAction::ActivateClip, first, 4);
}

void validateOneFrameClipsAndGaps() {
    const auto one_frame = makeModel({{{0, 1}, {1, 1}}});
    expectDecision(one_frame, timeline::ClipLocation{0, 0}, 0,
                   FrameStepDirection::Forward, FrameStepAction::ActivateClip,
                   timeline::ClipLocation{0, 1}, 0);
    expectDecision(one_frame, timeline::ClipLocation{0, 1}, 0,
                   FrameStepDirection::Backward, FrameStepAction::ActivateClip,
                   timeline::ClipLocation{0, 0}, 0);

    const auto gap = makeModel({{{0, 3}, {6, 2}}});
    expectDecision(gap, timeline::ClipLocation{0, 0}, 2,
                   FrameStepDirection::Forward, FrameStepAction::Gap);
    expectDecision(gap, timeline::ClipLocation{0, 1}, 0,
                   FrameStepDirection::Backward, FrameStepAction::Gap);
}

void validateOverlapsAndTracks() {
    const auto overlap = makeModel({{{0, 6}, {4, 4}}});
    expectDecision(overlap, timeline::ClipLocation{0, 0}, 5,
                   FrameStepDirection::Forward, FrameStepAction::ActivateClip,
                   timeline::ClipLocation{0, 1}, 2);
    expectDecision(overlap, timeline::ClipLocation{0, 1}, 0,
                   FrameStepDirection::Backward, FrameStepAction::ActivateClip,
                   timeline::ClipLocation{0, 0}, 3);

    const auto tracks = makeModel({{{3, 4}}, {{0, 3}}});
    expectDecision(tracks, timeline::ClipLocation{1, 0}, 2,
                   FrameStepDirection::Forward, FrameStepAction::ActivateClip,
                   timeline::ClipLocation{0, 0}, 0);
    expectDecision(tracks, timeline::ClipLocation{0, 0}, 0,
                   FrameStepDirection::Backward, FrameStepAction::ActivateClip,
                   timeline::ClipLocation{1, 0}, 2);

    const auto layered = makeModel({{{3, 4}}, {{3, 4}}, {{0, 3}}});
    expectDecision(layered, timeline::ClipLocation{2, 0}, 2,
                   FrameStepDirection::Forward, FrameStepAction::ActivateClip,
                   timeline::ClipLocation{0, 0}, 0);
    expectDecision(layered, timeline::ClipLocation{0, 0}, 0,
                   FrameStepDirection::Backward, FrameStepAction::ActivateClip,
                   timeline::ClipLocation{2, 0}, 2);

    const auto entering_upper_layer = makeModel({{{3, 4}}, {{0, 6}}});
    expectDecision(entering_upper_layer, timeline::ClipLocation{1, 0}, 2,
                   FrameStepDirection::Forward, FrameStepAction::StepWorker);
}

void validateTransitionsAndInvalidSelection() {
    auto model = makeModel({{{0, 5}, {5, 5}}});
    require(model.addTransition(0, 0, 1,
                timeline::TransitionKind::CrossDissolve, 2) ==
                timeline::TransitionMutationResult::Added,
            "The frame-step transition fixture could not be created.");
    expectDecision(model, timeline::ClipLocation{0, 0}, 4,
                   FrameStepDirection::Forward, FrameStepAction::ActivateClip,
                   timeline::ClipLocation{0, 1}, 0);
    expectDecision(model, timeline::ClipLocation{0, 1}, 0,
                   FrameStepDirection::Backward, FrameStepAction::ActivateClip,
                   timeline::ClipLocation{0, 0}, 4);

    require(model.updateTransition(0, 0, 1,
                timeline::TransitionKind::FadeToBlack, 2) ==
                timeline::TransitionMutationResult::Updated,
            "The frame-step fade fixture could not be created.");
    expectDecision(model, timeline::ClipLocation{0, 0}, 4,
                   FrameStepDirection::Forward, FrameStepAction::ActivateClip,
                   timeline::ClipLocation{0, 1}, 0);

    expectDecision(model, std::nullopt, 4, FrameStepDirection::Forward,
                   FrameStepAction::StepWorker);
    expectDecision(model, timeline::ClipLocation{0, 9}, 4,
                   FrameStepDirection::Forward, FrameStepAction::StepWorker);
    expectDecision(model, timeline::ClipLocation{9, 0}, 0,
                   FrameStepDirection::Backward, FrameStepAction::StepWorker);
}

} // namespace

int main() {
    try {
        validateContiguousAndLimits();
        validateOneFrameClipsAndGaps();
        validateOverlapsAndTracks();
        validateTransitionsAndInvalidSelection();
        std::cout << "Frame-step navigation tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
