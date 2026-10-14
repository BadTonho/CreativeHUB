#pragma once
#include "workspaces/fusion/nodes/evaluation/node_graph_evaluator.h"
namespace rendering {
// Borrowed input owners survive evaluation. Fit uses a transparent canvas;
// generators are rasterized on the calling playback/export worker.
void prepareGraphFrames(const fusion::nodes::NodeGraph&, fusion::nodes::InputFrames&,
    int width, int height, bool independent, const std::function<bool()>& canceled = {});
} // namespace rendering
