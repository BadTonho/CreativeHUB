#include "adjustment_composition.h"

#include <algorithm>
#include <cmath>

namespace rendering {

bool adjustmentRequiresProcessing(
    const creative_suite::animation::Transform2D& transform,
    std::span<const creative_suite::effects::EffectInstance> effects) noexcept {
    if (transform.opacity == 0.0) return false;
    return transform.position_x != 0.5 || transform.position_y != 0.5 ||
        transform.scale != 1.0 || transform.rotation_degrees != 0.0 ||
        std::any_of(effects.begin(), effects.end(), [](const auto& effect) { return effect.enabled; });
}

std::optional<creative_suite::media::RgbaFrame> composeAdjustmentLayers(
    int width, int height, const std::vector<TimelineCompositionStep>& steps,
    const std::function<bool()>& canceled) {
    const auto should_cancel = [&] { return canceled && canceled(); };
    std::optional<creative_suite::media::RgbaFrame> accumulated;
    std::vector<CompositionLayer> pending;
    pending.reserve(steps.size() + 1);
    for (const auto& step : steps) {
        if (should_cancel()) return {};
        if (!step.adjustment) {
            pending.push_back(step.layer);
            continue;
        }
        if (pending.empty() || !adjustmentRequiresProcessing(step.layer.transform, step.layer.effect_stack))
            continue;
        auto original = FrameCompositor::compose(width, height, pending);
        if (!original || should_cancel()) return {};
        pending.clear();
        accumulated.reset();
        auto adjustment = step.layer;
        adjustment.frame = &*original;
        const auto strength = adjustment.transform.opacity;
        adjustment.transform.opacity = 1.0;
        auto adjusted = FrameCompositor::compose(width, height, {adjustment});
        if (!adjusted || should_cancel()) return {};
        if (strength < 1.0) {
            for (int y = 0; y < height; ++y) {
                if (should_cancel()) return {};
                const auto offset = static_cast<std::size_t>(y) * adjusted->stride;
                const auto original_offset = static_cast<std::size_t>(y) * original->stride;
                for (int x = 0; x < width * 4; ++x) {
                    auto& value = adjusted->rgba_pixels[offset + x];
                    value = static_cast<std::uint8_t>(std::lround(
                        original->rgba_pixels[original_offset + x] * (1.0 - strength) + value * strength));
                }
            }
        }
        accumulated = std::move(adjusted);
        pending.push_back({&*accumulated, {}, {}});
    }
    if (should_cancel()) return {};
    if (accumulated && pending.size() == 1 && pending.front().frame == &*accumulated)
        return accumulated;
    auto result = FrameCompositor::compose(width, height, pending);
    return should_cancel() ? std::nullopt : std::move(result);
}

} // namespace rendering
