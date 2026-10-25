#pragma once

#include "../model/color_graph.h"
#include "media/video_frame.h"

#include <functional>
#include <optional>
#include <vector>

namespace color {

[[nodiscard]] std::optional<media::VideoFrame> evaluate(
    const Graph& graph, const media::VideoFrame& input,
    const std::function<bool()>& should_cancel = {});
[[nodiscard]] std::optional<media::VideoFrame> evaluateAtFrame(
    const Graph& graph, const media::VideoFrame& input, std::int64_t local_frame,
    const std::function<bool()>& should_cancel = {});

// Evaluates the upstream graph and returns the frame entering the selected
// Curves node, before that node's correction is applied.
[[nodiscard]] std::optional<media::VideoFrame> evaluateInput(
    const Graph& graph, NodeId curves_node, const media::VideoFrame& input,
    const std::function<bool()>& should_cancel = {});

// Evaluates the actual frame entering any correction node. Window geometry on
// upstream nodes is sampled at the supplied clip-local frame.
[[nodiscard]] std::optional<media::VideoFrame> evaluateNodeInputAtFrame(
    const Graph& graph, NodeId node_id, const media::VideoFrame& input,
    std::int64_t local_frame,
    const std::function<bool()>& should_cancel = {});

// Returns the enabled and disabled effect nodes on the selected Curves input
// path in processing order when that path can be represented as one stack.
[[nodiscard]] std::optional<std::vector<creative_suite::effects::EffectInstance>>
linearEffectStackBefore(const Graph& graph, NodeId curves_node);

}  // namespace color
