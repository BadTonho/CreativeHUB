#pragma once

#include "rendering/frame_compositor.h"
#include <functional>
#include <span>

namespace rendering {

// Application-owned ordered timeline operations. Frames and effect stacks are
// borrowed until composeAdjustmentLayers returns; adjustments have no frame.
struct TimelineCompositionStep {
    CompositionLayer layer;
    bool adjustment = false;
};

[[nodiscard]] bool adjustmentRequiresProcessing(
    const creative_suite::animation::Transform2D& transform,
    std::span<const creative_suite::effects::EffectInstance> effects) noexcept;

// CPU orchestration reuses the shared effects/compositor. Opacity mixes the
// original lower-track canvas with the fully transformed adjusted canvas.
// Cancellation discards incomplete output; allocation exceptions propagate.
[[nodiscard]] std::optional<creative_suite::media::RgbaFrame> composeAdjustmentLayers(
    int width, int height, const std::vector<TimelineCompositionStep>& steps,
    const std::function<bool()>& canceled = {});

} // namespace rendering
