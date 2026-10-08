#pragma once

#include "../model/node_graph.h"
#include "media/video_frame.h"

#include <optional>
#include <unordered_map>

namespace fusion::nodes {

// Input frames are supplied by the caller so Preview and offline Render share
// exactly the same graph operations while owning their decoder lifetimes.
using InputFrames = std::unordered_map<NodeId, media::VideoFramePtr>;

struct EvaluationContext {
    std::int64_t local_frame = 0;
};

[[nodiscard]] std::optional<media::VideoFrame> evaluate(
    const NodeGraph& graph, const InputFrames& inputs);
[[nodiscard]] std::optional<media::VideoFrame> evaluate(
    const NodeGraph& graph, const InputFrames& inputs, NodeId target_node);
[[nodiscard]] std::optional<media::VideoFrame> evaluate(
    const NodeGraph& graph, const InputFrames& inputs,
    EvaluationContext context);
[[nodiscard]] std::optional<media::VideoFrame> evaluate(
    const NodeGraph& graph, const InputFrames& inputs, NodeId target_node,
    EvaluationContext context);

} // namespace fusion::nodes
