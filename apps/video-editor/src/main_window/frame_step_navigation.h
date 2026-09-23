#pragma once

#include "timeline/timeline_model.h"

#include <cstdint>
#include <optional>

namespace main_window_detail {

enum class FrameStepDirection { Forward, Backward };

enum class FrameStepAction {
    StepWorker,
    ActivateClip,
    Gap,
    Beginning,
    End,
};

struct FrameStepDecision {
    FrameStepAction action = FrameStepAction::StepWorker;
    std::optional<timeline::ClipLocation> destination;
    std::int64_t local_frame = 0;
};

// Decide only the boundary action. MainWindow owns media activation, worker
// commands, eligibility checks, and user-facing messages.
[[nodiscard]] FrameStepDecision decideFrameStep(
    const timeline::TimelineModel& model,
    std::optional<timeline::ClipLocation> active_clip,
    std::int64_t local_frame,
    FrameStepDirection direction);

} // namespace main_window_detail
