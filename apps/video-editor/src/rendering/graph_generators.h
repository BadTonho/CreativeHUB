#pragma once
#include "workspaces/fusion/nodes/evaluation/node_graph_evaluator.h"
namespace rendering {
// Saturating addition for generated-clip source clocks at timeline limits.
[[nodiscard]] inline std::int64_t graphFrameClock(std::int64_t frame, std::int64_t offset) noexcept {
    constexpr auto maximum = INT64_MAX, minimum = INT64_MIN;
    if (offset > 0 && frame > maximum-offset) return maximum;
    if (offset < 0 && frame < minimum-offset) return minimum;
    return frame+offset;
}
// Borrowed input owners survive evaluation. Fit uses a transparent canvas;
// generators are rasterized on the calling playback/export worker.
void prepareGraphFrames(const fusion::nodes::NodeGraph&, fusion::nodes::InputFrames&,
    int width, int height, bool independent, const std::function<bool()>& canceled = {}, std::int64_t frame = 0);
} // namespace rendering
