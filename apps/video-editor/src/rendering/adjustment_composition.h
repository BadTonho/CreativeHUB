#pragma once

#include "rendering/frame_compositor.h"
#include "workspaces/color/model/color_graph.h"
#include <creative_suite/composition/gpu_frame_compositor.h>
#include <functional>
#include <span>

namespace rendering {

// Application-owned ordered timeline operations. Frames and effect stacks are
// borrowed until composeAdjustmentLayers returns; adjustments have no frame.
struct TimelineCompositionStep {
    CompositionLayer layer;
    bool adjustment = false;
    std::optional<color::Graph> color_graph;
};

[[nodiscard]] bool adjustmentRequiresProcessing(
    const creative_suite::animation::Transform2D& transform,
    std::span<const creative_suite::effects::EffectInstance> effects,
    const color::Graph* color_graph = nullptr) noexcept;

// CPU orchestration reuses the shared effects/compositor. Opacity mixes the
// original lower-track canvas with the fully transformed adjusted canvas.
// Cancellation discards incomplete output; allocation exceptions propagate.
[[nodiscard]] std::optional<creative_suite::media::RgbaFrame> composeAdjustmentLayers(
    int width, int height, const std::vector<TimelineCompositionStep>& steps,
    const std::function<bool()>& canceled = {});

// GPU orchestration composes lower tracks to an immutable texture at each
// adjustment barrier, applies the shared image operation, then continues with
// upper tracks. The backend owns all intermediate textures; no pixel readback
// occurs between barriers. Unsupported or failed operations return their
// status so the caller can reconstruct the same frame through the CPU path.
[[nodiscard]] creative_suite::composition::GpuTextureCompositionResult
composeAdjustmentLayersGpu(int width, int height,
    const std::vector<TimelineCompositionStep>& steps,
    creative_suite::composition::GpuFrameCompositor& backend,
    const creative_suite::composition::GpuFrameCompositor::CancellationPredicate& canceled = {},
    creative_suite::composition::OpenGlCompositionTimings* timings = nullptr);

} // namespace rendering
