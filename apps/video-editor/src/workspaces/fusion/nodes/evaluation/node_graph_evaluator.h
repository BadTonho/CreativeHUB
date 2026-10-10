#pragma once

#include "../model/node_graph.h"
#include "media/video_frame.h"

#include <optional>
#include <unordered_map>
#include <creative_suite/composition/opengl_frame_compositor.h>
#include <creative_suite/media/native_video_frame.h>

namespace fusion::nodes {

// Input frames are supplied by the caller so Preview and offline Render share
// exactly the same graph operations while owning their decoder lifetimes.
using InputFrames = std::unordered_map<NodeId, media::VideoFramePtr>;
using NativeInputFrames = std::unordered_map<NodeId, creative_suite::media::NativeVideoFramePtr>;

struct EvaluationContext {
    std::int64_t local_frame = 0;
    creative_suite::composition::OpenGlFrameCompositor* gpu = nullptr;
    bool* gpu_used = nullptr;
    std::function<bool()> should_cancel;
    const NativeInputFrames* native_inputs = nullptr;
    creative_suite::composition::OpenGlCompositionTimings* gpu_timings = nullptr;
};

struct EvaluatedGraphFrame {
    std::optional<media::VideoFrame> rgba;
    creative_suite::composition::OpenGlTextureFramePtr texture;
};
// Keeps a completed GPU graph on its device. CPU fallback is an owned RGBA
// result evaluated from original inputs, including explicit native downloads.
[[nodiscard]] std::optional<EvaluatedGraphFrame> evaluateFrame(
    const NodeGraph&, const InputFrames&, EvaluationContext);
[[nodiscard]] std::optional<EvaluatedGraphFrame> evaluateFrame(
    const NodeGraph&, const InputFrames&, NodeId target_node, EvaluationContext);

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
