#include "adjustment_composition.h"
#include "workspaces/color/evaluation/color_graph_evaluator.h"

#include <algorithm>
#include <cmath>

namespace rendering {

bool adjustmentRequiresProcessing(
    const creative_suite::animation::Transform2D& transform,
    std::span<const creative_suite::effects::EffectInstance> effects,
    const color::Graph* color_graph) noexcept {
    if (transform.opacity == 0.0) return false;
    return transform.position_x != 0.5 || transform.position_y != 0.5 ||
        transform.scale != 1.0 || transform.rotation_degrees != 0.0 ||
        std::any_of(effects.begin(), effects.end(), [](const auto& effect) { return effect.enabled; }) ||
        (color_graph && std::any_of(color_graph->nodes.begin(), color_graph->nodes.end(),
            [](const color::Node& node) {
                return node.type == color::NodeType::Effect && node.effect && node.effect->enabled;
            }));
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
        if (pending.empty() || !adjustmentRequiresProcessing(
                step.layer.transform, step.layer.effect_stack,
                step.color_graph ? &*step.color_graph : nullptr))
            continue;
        auto original = FrameCompositor::compose(width, height, pending);
        if (!original || should_cancel()) return {};
        pending.clear();
        accumulated.reset();
        auto adjustment = step.layer;
        adjustment.frame = &*original;
        std::optional<creative_suite::media::RgbaFrame> color_adjusted;
        if (step.color_graph) {
            color_adjusted = color::evaluate(*step.color_graph, *original, canceled);
            if (!color_adjusted || should_cancel()) return {};
            adjustment.frame = &*color_adjusted;
            adjustment.effect_stack = {};
        }
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

creative_suite::composition::GpuTextureCompositionResult composeAdjustmentLayersGpu(
    int width, int height, const std::vector<TimelineCompositionStep>& steps,
    creative_suite::composition::GpuFrameCompositor& backend,
    const creative_suite::composition::GpuFrameCompositor::CancellationPredicate& canceled,
    creative_suite::composition::OpenGlCompositionTimings* timings) {
    using namespace creative_suite::composition;
    const auto was_canceled = [&] { return canceled && canceled(); };
    const auto fail = [](OpenGlCompositionStatus status, std::string operation,
                         std::string cause = {}, std::int64_t code = 0) {
        return GpuTextureCompositionResult{status, {}, std::move(operation), std::move(cause), code};
    };
    if (width <= 0 || height <= 0)
        return fail(OpenGlCompositionStatus::Unsupported, "adjustment-canvas", "Invalid adjustment canvas dimensions.");

    std::vector<CompositionLayer> pending;
    pending.reserve(steps.size() + 1);
    OpenGlTextureFramePtr accumulated;
    creative_suite::media::RgbaFrame accumulated_geometry{width, height, width * 4, {}};
    for (const auto& step : steps) {
        if (was_canceled()) return fail(OpenGlCompositionStatus::Cancelled, "adjustment-cancelled");
        if (!step.adjustment) {
            pending.push_back(step.layer);
            continue;
        }
        if (pending.empty() || !adjustmentRequiresProcessing(
                step.layer.transform, step.layer.effect_stack,
                step.color_graph ? &*step.color_graph : nullptr))
            continue;

        if (step.color_graph && !color::linearEffectStack(*step.color_graph))
            return fail(OpenGlCompositionStatus::Unsupported, "adjustment-color-graph",
                "A branched Color graph uses the CPU adjustment path.");

        auto original = backend.composeTexture(width, height, pending, canceled, timings);
        if (original.status != OpenGlCompositionStatus::Complete || !original.frame)
            return fail(original.status == OpenGlCompositionStatus::Complete ? OpenGlCompositionStatus::Failed : original.status,
                original.operation.empty() ? "adjustment-lower-composition" : original.operation,
                original.cause.empty() ? "The lower-track GPU composition returned no texture." : original.cause,
                original.error_code);
        const auto passes = creative_suite::effects::colorAdjustmentPasses(step.layer.effect_stack);
        if (!passes)
            return fail(OpenGlCompositionStatus::Unsupported, "adjustment-effects",
                "The GPU adjustment operation does not support one of the effect-stack entries.");

        OpenGlImageOperation operation;
        operation.kind = OpenGlImageOperationKind::Adjustment;
        operation.background.texture = original.frame;
        operation.foreground.texture = original.frame;
        operation.transform = step.layer.transform;
        operation.colors = *passes;
        auto adjusted = backend.processImage(operation, canceled, timings);
        if (adjusted.status != OpenGlCompositionStatus::Complete || !adjusted.frame)
            return fail(adjusted.status == OpenGlCompositionStatus::Complete ? OpenGlCompositionStatus::Failed : adjusted.status,
                adjusted.operation.empty() ? "adjustment-image-operation" : adjusted.operation,
                adjusted.cause.empty() ? "The GPU adjustment operation returned no texture." : adjusted.cause,
                adjusted.error_code);
        if (was_canceled()) return fail(OpenGlCompositionStatus::Cancelled, "adjustment-cancelled");

        accumulated = std::move(adjusted.frame);
        pending.clear();
        CompositionLayer accumulated_layer;
        accumulated_layer.frame = &accumulated_geometry;
        accumulated_layer.texture_frame = accumulated;
        pending.push_back(std::move(accumulated_layer));
    }
    if (was_canceled()) return fail(OpenGlCompositionStatus::Cancelled, "adjustment-cancelled");
    if (accumulated && pending.size() == 1 && pending.front().texture_frame == accumulated)
        return {OpenGlCompositionStatus::Complete, std::move(accumulated)};
    auto result = backend.composeTexture(width, height, pending, canceled, timings);
    if (result.status == OpenGlCompositionStatus::Complete && !result.frame)
        return fail(OpenGlCompositionStatus::Failed, "adjustment-upper-composition",
            "The upper-track GPU composition returned no texture.");
    return result;
}

} // namespace rendering
