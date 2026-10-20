#include "masked_layer_renderer.h"
#include "layer_effect_processor.h"
#include "layer_effect_worker_pool.h"
#include "model/layer_transform_hierarchy.h"
#include "diagnostics/performance_metrics.h"
#include <creative_suite/composition/frame_compositor.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace motion::ui {
namespace c = creative_suite::composition;
namespace m = creative_suite::media;
namespace model = motion::model;
bool hasActiveDropShadow(const PreviewLayerSnapshot &layer) {
    return std::any_of(layer.effects.begin(), layer.effects.end(), [&](const auto &effect) {
        const auto *e = std::get_if<model::DropShadowEffect>(&effect);
        return e && e->enabled &&
               model::evaluateEffectParameter(effect, model::EffectParameterProperty::ShadowOpacityPercent,
                                              layer.local_frame) > 0;
    });
}
MaskedLayerFrame renderExpandedLayer(const PreviewLayerSnapshot &layer, m::RgbaFramePtr source,
                                     const PreviewRequest &request, MotionMaskRasterCache &cache,
                                     c::OpenGlFrameCompositor *gpu, const std::function<bool()> &cancel,
                                     const c::AffineTransform2D *resolved_world, const PathRaster *raster,
                                     std::uint64_t preparation_budget_bytes, bool record_metrics) {
    using Clock = std::chrono::steady_clock;
    const auto cancelled = [&] { return cancel && cancel(); };
    const auto source_bytes = std::uint64_t(source->stride) * source->height;
    const auto canvas_bytes = std::uint64_t(request.canvas_size.width) * request.canvas_size.height * 4;
    if (std::max(source_bytes * 3, source_bytes * 2 + canvas_bytes * (layer.transform_mask ? 3 : 1)) >
        preparation_budget_bytes)
        throw std::runtime_error("Expanded layer preparation exceeds the remaining frame budget");
    auto rgba = source;
    c::OpenGlImageInput current{source.get()};
    c::OpenGlCompositionTimings timings;
    const bool intrinsic = layer.kind == model::LayerKind::Shape || layer.kind == model::LayerKind::Text;
    const bool fusion = layer.presentation && layer.presentation->profile ==
                                                  creative_suite::composition_exchange::Profile::FusionCanvas;
    const double fit = intrinsic || fusion ? 1.
                                           : std::min(double(request.canvas_size.width) / source->width,
                                                      double(request.canvas_size.height) / source->height);
    double ox = raster ? raster->origin_x : -source->width * .5,
           oy = raster ? raster->origin_y : -source->height * .5;
    c::MaskRasterSpace space =
        raster ? raster->space : c::MaskRasterSpace{source->width, source->height, 0, 0};
    auto world = resolved_world ? *resolved_world : c::AffineTransform2D{};
    if (!resolved_world) {
        model::CompositionLayer local{layer.id, model::LayerKind::Null, ""};
        local.transform = layer.transform;
        local.keyframes = layer.keyframes;
        local.anchor_point = layer.anchor_point;
        local.anchor_keyframes = layer.anchor_keyframes;
        local.scale_y = layer.scale_y;
        world = model::localLayerGeometry(local, request.canvas_size, layer.local_frame);
    }
    const auto evaluated = model::evaluateLayerEffects(layer.effects, layer.local_frame);
    const auto coverage = [&](model::MaskId id, int width, int height, bool local = true) {
        if (!request.sceneMasks())
            throw std::runtime_error("Missing expanded-layer mask definitions");
        const auto &masks = *request.sceneMasks();
        const auto it =
            std::find_if(masks.begin(), masks.end(), [&](const auto &mask) { return mask.id == id; });
        if (it == masks.end())
            throw std::runtime_error("Invalid expanded-layer mask reference");
        const auto path =
            request.path_override && request.path_override->target == model::PathTarget{id, true}
                ? request.path_override->geometry.get()
                : nullptr;
        auto result = cache.coverage(*it, request.composition_frame, width, height, cancel,
                                     local ? space : c::MaskRasterSpace{}, path);
        if (!result && !cancelled())
            throw std::runtime_error("Expanded mask coverage exceeds its bounded budget");
        return result;
    };
    const auto process = [&](c::OpenGlImageOperation operation) {
        c::OpenGlCompositionTimings measured;
        auto result = gpu->processImage(operation, cancel, &measured);
        addMaskTimings(timings, measured);
        if (result.status == c::OpenGlCompositionStatus::Cancelled)
            return c::OpenGlTextureFramePtr{};
        if (result.status != c::OpenGlCompositionStatus::Complete || !result.frame)
            throw std::runtime_error(
                result.operation + ": " +
                (result.cause.empty() ? "Expanded effect texture budget is occupied" : result.cause));
        return result.frame;
    };
    const auto bounded = [](int width, int height) {
        if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
            std::uint64_t(width) * height * 4 > 64ULL * 1024 * 1024)
            throw std::runtime_error("Expanded effect image exceeds its 64 MiB / 16384 pixel budget");
    };
    const auto padCpu = [&](const m::RgbaFrame &input, int width, int height, int left, int top) {
        bounded(width, height);
        auto out = std::make_shared<m::RgbaFrame>(m::RgbaFrame{
            width, height, width * 4, std::vector<std::uint8_t>(std::size_t(width) * height * 4)});
        for (int y = 0; y < input.height; ++y) {
            if (cancelled())
                return std::shared_ptr<m::RgbaFrame>{};
            std::copy_n(input.rgba_pixels.data() + std::size_t(y) * input.stride, input.width * 4,
                        out->rgba_pixels.data() + std::size_t(y + top) * out->stride + left * 4);
        }
        return out;
    };
    const auto padGpu = [&](c::OpenGlImageInput input, int width, int height, int left, int top) {
        bounded(width, height);
        c::OpenGlImageOperation op;
        op.kind = c::OpenGlImageOperationKind::Transform;
        op.background = input;
        op.output_width = width;
        op.output_height = height;
        op.source_to_canvas = c::AffineTransform2D{1, 0, 0, 1, double(left), double(top)};
        return process(op);
    };
    if (layer.source_mask) {
        auto mask = coverage(layer.source_mask, current.width(), current.height());
        if (!mask)
            return {};
        if (gpu) {
            c::OpenGlImageOperation op;
            op.kind = c::OpenGlImageOperationKind::ApplyMask;
            op.background = current;
            op.mask = {mask.get()};
            current = {nullptr, process(op)};
        } else {
            auto next = std::make_shared<m::RgbaFrame>(*rgba);
            if (!c::applyMask(*next, *mask, cancel))
                return {};
            rgba = next;
            current = {rgba.get()};
        }
    }
    bool expanded = false;
    for (std::size_t index = 0; index < evaluated.size(); ++index) {
        if (cancelled())
            return {};
        const auto &effect = evaluated[index];
        if (!std::visit([](const auto &e) { return e.enabled; }, effect))
            continue;
        const auto id = std::visit([](const auto &e) { return e.mask; }, effect);
        auto original = current;
        auto original_rgba = rgba;
        int left = 0, top = 0, width = current.width(), height = current.height();
        const auto *shadow = std::get_if<model::DropShadowEffect>(&effect);
        if (shadow && shadow->parameters.opacity_percent == 0)
            continue;
        try {
            if (shadow) {
                const auto b = creative_suite::effects::dropShadowBounds(width, height, shadow->parameters);
                left = b.left;
                top = b.top;
                width = b.width;
                height = b.height;
            } else if (expanded)
                if (const auto *blur = std::get_if<model::GaussianBlurEffect>(&effect);
                    blur && std::lround(blur->radius_pixels) > 0) {
                    left = top = 3 * int(std::lround(blur->radius_pixels)) + 1;
                    width += 2 * left;
                    height += 2 * top;
                }
            bounded(width, height);
            const auto image_bytes = std::uint64_t(width) * height * 4;
            const auto input_bytes = std::uint64_t(current.width()) * current.height() * 4;
            const auto stage_peak = image_bytes * 4 + input_bytes + source_bytes;
            const auto transform_peak =
                image_bytes * 2 + source_bytes + canvas_bytes * (layer.transform_mask ? 3 : 1);
            if (std::max(stage_peak, transform_peak) > preparation_budget_bytes)
                throw std::runtime_error(
                    "Expanded effect working set exceeds the remaining frame preparation budget");
            const auto started = Clock::now();
            if (gpu) {
                if (!shadow && (left || top))
                    current = {nullptr, padGpu(current, width, height, left, top)};
                c::OpenGlImageOperation op;
                op.background = current;
                if (shadow) {
                    op.kind = c::OpenGlImageOperationKind::DropShadow;
                    op.drop_shadow = shadow->parameters;
                } else if (const auto *blur = std::get_if<model::GaussianBlurEffect>(&effect)) {
                    op.kind = c::OpenGlImageOperationKind::GaussianBlur;
                    op.blur_radius_pixels = blur->radius_pixels;
                } else {
                    const auto &color = std::get<model::ColorAdjustmentEffect>(effect);
                    op.kind = c::OpenGlImageOperationKind::Color;
                    op.colors = {{color.brightness, color.contrast_percent, color.saturation_percent}};
                }
                current = {nullptr, process(op)};
            } else {
                if (shadow) {
                    auto result = creative_suite::effects::applyDropShadow(
                        *rgba, shadow->parameters, cancel,
                        [](auto count, const auto &cancelled, const auto &operation) {
                            return detail::sharedLayerEffectWorkerPool().parallelFor(count, cancelled,
                                                                                     operation);
                        });
                    if (!result)
                        return {};
                    rgba = std::make_shared<const m::RgbaFrame>(std::move(*result));
                } else {
                    auto next = left || top ? padCpu(*rgba, width, height, left, top)
                                            : std::make_shared<m::RgbaFrame>(*rgba);
                    if (!next || !applyLayerEffects(*next, {effect}, cancel))
                        return {};
                    rgba = next;
                }
                current = {rgba.get()};
            }
            if (cancelled())
                return {};
            if (shadow) {
                if (record_metrics) {
                    const auto duration =
                        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count();
                    diagnostics::PerformanceMetrics::instance().recordEffectTiming(
                        diagnostics::PreviewEffectKind::DropShadow, duration);
                    if (gpu)
                        diagnostics::PerformanceMetrics::instance().recordGpuDropShadow(1, 0, false,
                                                                                        duration);
                }
                expanded = true;
            }
            ox -= left;
            oy -= top;
            space.origin_x -= left;
            space.origin_y -= top;
            if (id) {
                auto mask = coverage(id, width, height);
                if (!mask)
                    return {};
                if (gpu) {
                    if (left || top || width != original.width() || height != original.height())
                        original = {nullptr, padGpu(original, width, height, left, top)};
                    c::OpenGlImageOperation op;
                    op.kind = c::OpenGlImageOperationKind::MaskedBlend;
                    op.background = original;
                    op.foreground = current;
                    op.mask = {mask.get()};
                    current = {nullptr, process(op)};
                } else {
                    auto base =
                        left || top || width != original_rgba->width || height != original_rgba->height
                            ? padCpu(*original_rgba, width, height, left, top)
                            : std::make_shared<m::RgbaFrame>(*original_rgba);
                    if (!base)
                        return {};
                    auto mixed = std::make_shared<m::RgbaFrame>(*rgba);
                    if (!c::maskedBlend(*base, *mixed, *mask, cancel))
                        return {};
                    rgba = mixed;
                    current = {rgba.get()};
                }
            }
        } catch (const std::exception &error) {
            throw std::runtime_error("Drop Shadow preparation layer " + std::to_string(layer.id) +
                                     ", effect " + std::to_string(index) + ", frame " +
                                     std::to_string(request.composition_frame) + ": " + error.what());
        }
    }
    const auto opacity =
        creative_suite::animation::evaluateTransform(layer.transform, layer.keyframes, layer.local_frame)
            .opacity;
    const auto source_matrix =
        c::composeAffine(c::composeAffine(world, {fit, 0, 0, fit, 0, 0}), {1, 0, 0, 1, ox, oy});
    const auto identity_matrix =
        c::composeAffine({fit, 0, 0, fit, request.canvas_size.width * .5, request.canvas_size.height * .5},
                         {1, 0, 0, 1, ox, oy});
    if (gpu) {
        const auto transform = [&](c::AffineTransform2D matrix, double alpha) {
            c::OpenGlImageOperation op;
            op.kind = c::OpenGlImageOperationKind::Transform;
            op.background = current;
            op.output_width = request.canvas_size.width;
            op.output_height = request.canvas_size.height;
            op.source_to_canvas = matrix;
            op.transform.opacity = alpha;
            return process(op);
        };
        c::OpenGlTextureFramePtr original;
        if (layer.transform_mask)
            original = transform(identity_matrix, 1);
        auto output = transform(source_matrix, opacity);
        current = {}; // Release the local image before the final masked canvas blend.
        if (cancelled())
            return {};
        if (layer.transform_mask) {
            auto mask =
                coverage(layer.transform_mask, request.canvas_size.width, request.canvas_size.height, false);
            if (!mask)
                return {};
            c::OpenGlImageOperation op;
            op.kind = c::OpenGlImageOperationKind::MaskedBlend;
            op.background = {nullptr, original};
            op.foreground = {nullptr, output};
            op.mask = {mask.get()};
            output = process(op);
        }
        return {{}, output, timings};
    }
    auto output = c::transformAffineFrame(*rgba, request.canvas_size.width, request.canvas_size.height,
                                          source_matrix, opacity, cancel);
    if (!output) {
        if (cancelled())
            return {};
        throw std::runtime_error("The expanded layer affine transform could not be resolved");
    }
    if (layer.transform_mask) {
        auto original = c::transformAffineFrame(*rgba, request.canvas_size.width, request.canvas_size.height,
                                                identity_matrix, 1, cancel);
        auto mask =
            coverage(layer.transform_mask, request.canvas_size.width, request.canvas_size.height, false);
        if (!original || !mask || !c::maskedBlend(*original, *output, *mask, cancel))
            return {};
    }
    return {std::make_shared<const m::RgbaFrame>(std::move(*output)), {}, timings};
}
} // namespace motion::ui
