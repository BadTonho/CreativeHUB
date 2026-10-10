#include "composition_frame_renderer.h"
#include "layer_content_renderer.h"
#include "layer_effect_processor.h"
#include "../diagnostics/performance_metrics.h"

#include <creative_suite/composition/frame_compositor.h>
#include <creative_suite/composition/opengl_frame_compositor.h>
#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/animation/animation.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace motion::ui {
namespace {

class StageTimer final {
public:
    StageTimer(bool enabled, diagnostics::PreviewTimingStage stage) noexcept
        : enabled_(enabled), stage_(stage), started_(std::chrono::steady_clock::now()) {}
    ~StageTimer()
    {
        if (!enabled_) return;
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started_).count();
        if (elapsed >= 0) {
            diagnostics::PerformanceMetrics::instance().recordTiming(
                stage_, static_cast<std::uint64_t>(elapsed));
        }
    }

private:
    bool enabled_;
    diagnostics::PreviewTimingStage stage_;
    std::chrono::steady_clock::time_point started_;
};

std::string pathForLog(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

class MotionDecodeObserver final : public creative_suite::media::DecodeObserver {
public:
    [[nodiscard]] bool is_enabled() const noexcept override
    {
        return diagnostics::PerformanceMetrics::instance().enabled();
    }

    void record_timing(
        creative_suite::media::DecodeTimingStage,
        std::uint64_t) noexcept override
    {
        // Motion records aggregate decode, timestamp-seek, and forward-decode
        // timings at the operation boundary rather than duplicating FFmpeg
        // substages in its one-second preview samples.
    }

    void record_discarded_frame() noexcept override
    {
        diagnostics::PerformanceMetrics::instance().recordDiscardedIntermediateFrame();
    }

    void record_timestamp_seek(
        creative_suite::media::DecodeSeekResult result,
        std::uint64_t nanoseconds) noexcept override
    {
        diagnostics::PerformanceMetrics::instance().recordTimestampSeek(
            result == creative_suite::media::DecodeSeekResult::Succeeded,
            nanoseconds);
    }
};

MotionDecodeObserver& motionDecodeObserver() noexcept
{
    static MotionDecodeObserver observer;
    return observer;
}

} // namespace

void CompositionGpuMetrics::record(
    const creative_suite::composition::OpenGlCompositionTimings& timings) noexcept
{
    uploaded_bytes += timings.uploaded_bytes;
    readback_bytes += timings.readback_bytes;
    uploaded_layers += timings.uploaded_layers;
    upload_nanoseconds += timings.upload_nanoseconds;
    draw_submission_nanoseconds += timings.draw_submission_nanoseconds;
    readback_nanoseconds += timings.readback_nanoseconds;
    color_adjustment_count += timings.color_adjustment_count;
    gaussian_blur_count += timings.gaussian_blur_count;
}

void CompositionFrameRenderer::configureTextureDelivery(QOpenGLContext* share_context) {
    texture_share_context_ = share_context;
    texture_budget_ = std::make_shared<creative_suite::composition::OpenGlTexturePoolBudget>();
}
void CompositionFrameRenderer::disableTextureDelivery(bool use_cpu) {
    texture_delivery_disabled_ = true;
    if (use_cpu) gpu_composition_disabled_after_failure_ = true;
}
std::uint64_t CompositionFrameRenderer::texturePoolBytes() const noexcept {
    return texture_budget_ ? texture_budget_->bytes() : 0;
}
unsigned CompositionFrameRenderer::texturePoolOccupancy() const {
    return gpu_compositor_ ? gpu_compositor_->texturePoolOccupancy() : 0;
}
void CompositionFrameRenderer::collectTextureFrames() {
    if (!gpu_compositor_ || !texture_budget_) return;
    const auto result = gpu_compositor_->collectReleasedTextureFrames();
    if (result.status == creative_suite::composition::OpenGlCompositionStatus::Failed &&
        !texture_delivery_disabled_) {
        disableTextureDelivery(true);
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_preview", "collect_texture_frames", result.cause,
            {{"operation", result.operation}, {"error_code", std::to_string(result.error_code)}});
    }
}

void CompositionFrameRenderer::reset()
{
    video_sessions_.clear();
    content_frames_.clear();
}

void CompositionFrameRenderer::shutdown()
{
    reset();
    // OpenGlFrameCompositor must be destroyed on the same worker that created it.
    gpu_compositor_.reset();
}
void CompositionFrameRenderer::recoverAsyncReadbackFailure()
{
    if (gpu_compositor_) gpu_compositor_->discardAsyncReadbacks();
    gpu_composition_disabled_after_failure_ = true;
}
creative_suite::composition::OpenGlReadbackResult
CompositionFrameRenderer::collectAsyncReadback(
    creative_suite::composition::OpenGlReadbackTicket ticket,
    const CancellationPredicate& should_cancel)
{
    if (!gpu_compositor_) return {
        creative_suite::composition::OpenGlCompositionStatus::Failed, {}, {},
        "collect-pbo-ticket", "The export OpenGL compositor is unavailable."};
    auto collected = gpu_compositor_->collectAsyncReadback(ticket, should_cancel);
    if (gpu_metrics_ != nullptr &&
        collected.status == creative_suite::composition::OpenGlCompositionStatus::Complete) {
        ++gpu_metrics_->readback_frames_collected;
        gpu_metrics_->readback_nanoseconds += collected.fence_wait_nanoseconds +
            collected.copy_nanoseconds;
        gpu_metrics_->readback_wait_nanoseconds += collected.fence_wait_nanoseconds;
        gpu_metrics_->readback_copy_nanoseconds += collected.copy_nanoseconds;
        if (collected.fence_wait_nanoseconds > 0) ++gpu_metrics_->readback_fence_waits;
    }
    return collected;
}
creative_suite::media::RgbaFramePtr CompositionFrameRenderer::render(
    const PreviewRequest& request,
    const CancellationPredicate& should_cancel,
    bool fail_on_media_error,
    PreviewRequestMode mode,
    std::optional<creative_suite::composition::OpenGlReadbackTicket>* async_ticket,
    bool* async_failure,
    creative_suite::composition::OpenGlTextureFramePtr* texture,
    bool* texture_busy)
{
    using creative_suite::composition::CompositionLayer;
    using creative_suite::media::VideoPlaybackSession;
    if (texture) texture->reset();
    if (texture_busy) *texture_busy = false;
    if (async_ticket) async_ticket->reset();
    if (async_failure) *async_failure = false;

    if (request.layers.empty()) {
        content_frames_.clear();
        if (!request.black_canvas_when_empty) return {};
    }
    if (request.frame_rate.numerator <= 0 || request.frame_rate.denominator <= 0) {
        if (fail_on_media_error) {
            throw std::runtime_error("The Motion Studio composition frame rate is invalid.");
        }
        return {};
    }

    std::vector<creative_suite::composition::CompositionLayer> composition_layers;
    std::vector<creative_suite::media::RgbaFramePtr> owned_frames;
    struct PendingGpuEffects {
        std::size_t composition_index = 0;
        model::LayerId layer_id = 0;
        creative_suite::media::RgbaFramePtr source;
        std::vector<model::LayerEffect> effects;
    };
    std::vector<PendingGpuEffects> pending_gpu_effects;
    std::vector<model::LayerId> composition_layer_ids;
    std::uint64_t cpu_color_adjustment_fallbacks = 0;
    std::uint64_t gpu_color_adjustment_count = 0;
    std::uint64_t cpu_gaussian_blur_fallbacks = 0;
    std::uint64_t gpu_gaussian_blur_count = 0;
    bool gpu_color_adjustment_failed = false;
    std::string gpu_failure_operation;
    std::int64_t gpu_failure_code = 0;
    const bool try_gpu_effects = gpu_composition_enabled_ &&
        !gpu_composition_disabled_after_failure_;
    std::set<model::LayerId> active_content_ids;
    composition_layers.reserve(request.layers.size());
    owned_frames.reserve(request.layers.size());
    composition_layer_ids.reserve(request.layers.size());
    for (const auto& layer : request.layers) {
        if ((should_cancel && should_cancel())) return {};
        creative_suite::media::RgbaFramePtr frame;
        if (layer.kind == model::LayerKind::Image) {
            frame = layer.still_frame;
            if (frame == nullptr) {
                if (fail_on_media_error) {
                    throw std::runtime_error("The still-image layer has no cached decoded frame: " +
                                             pathForLog(layer.source_path));
                }
                creative_suite::diagnostics::Logger::instance().log(
                    creative_suite::diagnostics::Level::Error,
                    "motion_preview", "load_still_frame",
                    "The still-image layer has no cached decoded frame",
                    {{"path", pathForLog(layer.source_path)}});
                continue;
            }
        } else if (layer.kind == model::LayerKind::Video) {
            if (layer.source_path.empty() || !std::isfinite(layer.source_frame_rate) ||
                layer.source_frame_rate <= 0.0 || layer.local_frame < 0) {
                if (fail_on_media_error) {
                    throw std::runtime_error("Video layer has invalid source timing metadata: " +
                                             pathForLog(layer.source_path));
                }
                reportDecodeError(layer.source_path, -1,
                                  "Video layer has invalid source timing metadata");
                continue;
            }
            const auto mapped_source_frame = model::sourceFrameForTimelineFrame(
                layer.local_frame, layer.source_start_frame, layer.source_frame_rate,
                request.frame_rate, layer.source_frame_count);
            if (!mapped_source_frame.has_value()) {
                if (fail_on_media_error) {
                    throw std::runtime_error("Timeline position cannot be represented as a source frame: " +
                                             pathForLog(layer.source_path));
                }
                reportDecodeError(layer.source_path, -1,
                                  "Timeline position cannot be represented as a source frame");
                continue;
            }
            const auto source_frame = *mapped_source_frame;
            try {
                StageTimer decode_timer(
                    record_preview_metrics_, diagnostics::PreviewTimingStage::Decode);
                auto session = video_sessions_.find(layer.source_path);
                if (session == video_sessions_.end()) {
                    auto opened = VideoPlaybackSession::open(
                        layer.source_path,
                        record_preview_metrics_ ? &motionDecodeObserver() : nullptr);
                    session = video_sessions_.emplace(layer.source_path, std::move(opened)).first;
                }
                const auto cancelled = [&should_cancel] {
                    return should_cancel && should_cancel();
                };
                std::optional<creative_suite::media::VideoFramePtr> decoded;
                const bool collect_forward_metrics =
                    record_preview_metrics_ &&
                    diagnostics::PerformanceMetrics::instance().enabled();
                if (shouldUseSequentialPlaybackDecode(
                        mode,
                        session->second->current_frame_index(),
                        source_frame)) {
                    creative_suite::media::ForwardDecodeDiagnostics forward_diagnostics;
                    try {
                        decoded = session->second->decode_forward_to(
                            source_frame,
                            cancelled,
                            collect_forward_metrics ? &forward_diagnostics : nullptr);
                    } catch (const std::exception&) {
                        // The shared decoder logs operational exceptions. If
                        // they were not caused by cancellation, try the normal
                        // timestamp-seek path below to recover this preview.
                        if (cancelled()) return {};
                    }
                    if (collect_forward_metrics && forward_diagnostics.attempted) {
                        diagnostics::PerformanceMetrics::instance().recordForwardDecode(
                            forward_diagnostics.completed,
                            forward_diagnostics.elapsed_nanoseconds);
                    }
                    if (!decoded.has_value()) {
                        if (!shouldFallbackToTimestampSeek(cancelled())) return {};
                        if (collect_forward_metrics) {
                            diagnostics::PerformanceMetrics::instance()
                                .recordForwardDecodeFallback();
                        }
                        decoded = session->second->decode_frame_at(source_frame, cancelled);
                    }
                } else {
                    decoded = session->second->decode_frame_at(source_frame, cancelled);
                }
                if (!decoded.has_value()) {
                    if ((should_cancel && should_cancel())) return {};
                    if (fail_on_media_error) {
                        throw std::runtime_error("The video decoder returned no frame: " +
                                                 pathForLog(layer.source_path));
                    }
                    reportDecodeError(layer.source_path, source_frame,
                                      "The video decoder returned no frame");
                    video_sessions_.erase(session);
                    continue;
                }
                frame = *decoded;
            } catch (const creative_suite::media::MediaError& error) {
                if (should_cancel && should_cancel()) return {};
                if (fail_on_media_error) {
                    throw creative_suite::media::MediaError(
                        "Video decode failed for " + pathForLog(layer.source_path) +
                            ": " + error.what(),
                        error.error_code());
                }
                reportDecodeError(layer.source_path, source_frame, error.what(),
                                  error.error_code());
                video_sessions_.erase(layer.source_path);
                continue;
            } catch (const std::exception& error) {
                if (should_cancel && should_cancel()) return {};
                if (fail_on_media_error) {
                    throw std::runtime_error("Video decode failed for " +
                        pathForLog(layer.source_path) + ": " + error.what());
                }
                reportDecodeError(layer.source_path, source_frame, error.what());
                video_sessions_.erase(layer.source_path);
                continue;
            }
        } else if (layer.kind == model::LayerKind::Text ||
                   layer.kind == model::LayerKind::Shape) {
            active_content_ids.insert(layer.id);
            auto cached = content_frames_.find(layer.id);
            if (cached == content_frames_.end() || cached->second.content != layer.content) {
                StageTimer raster_timer(
                    record_preview_metrics_,
                    diagnostics::PreviewTimingStage::TextShapeRasterization);
                const auto rasterized = rasterizeLayerContent(layer.content);
                if (!rasterized.has_value()) {
                    if (fail_on_media_error) {
                        throw std::runtime_error("Text or shape content could not be rasterized for layer " +
                                                 std::to_string(layer.id));
                    }
                    creative_suite::diagnostics::Logger::instance().log(
                        creative_suite::diagnostics::Level::Warning,
                        "motion_preview", "rasterize_layer_content",
                        "Text or shape content could not be rasterized within the supported limits",
                        {{"layer_id", std::to_string(layer.id)}});
                    content_frames_.erase(layer.id);
                    continue;
                }
                cached = content_frames_.insert_or_assign(
                    layer.id,
                    CachedContentFrame{
                        layer.content,
                        std::make_shared<const creative_suite::media::RgbaFrame>(
                            *rasterized)}).first;
            }
            frame = cached->second.frame;
        } else {
            continue;
        }

        if (frame == nullptr) continue;
        std::vector<creative_suite::effects::ColorAdjustmentParameters> gpu_adjustments;
        std::vector<creative_suite::composition::GpuCompositionEffect> gpu_effects;
        const bool has_enabled_effects = hasEnabledLayerEffects(layer.effects);
        const bool gpu_color_only_stack = try_gpu_effects && has_enabled_effects &&
            model::validLayerEffects(layer.effects) &&
            std::all_of(layer.effects.begin(), layer.effects.end(), [](const auto& effect) {
                return std::visit([](const auto& value) {
                    using Effect = std::decay_t<decltype(value)>;
                    return !value.enabled ||
                        std::is_same_v<Effect, model::ColorAdjustmentEffect>;
                }, effect);
            });
        const bool gpu_ordered_stack = try_gpu_effects && has_enabled_effects &&
            model::validLayerEffects(layer.effects) &&
            std::any_of(layer.effects.begin(), layer.effects.end(), [](const auto& effect) {
                const auto* blur = std::get_if<model::GaussianBlurEffect>(&effect);
                return blur && blur->enabled;
            });
        if (gpu_color_only_stack) {
            gpu_adjustments.reserve(layer.effects.size());
            for (const auto& effect : layer.effects) {
                if (const auto* color = std::get_if<model::ColorAdjustmentEffect>(&effect);
                    color && color->enabled) {
                    gpu_adjustments.push_back({color->brightness,
                        color->contrast_percent, color->saturation_percent});
                }
            }
        } else if (gpu_ordered_stack) {
            gpu_effects.reserve(layer.effects.size());
            for (const auto& effect : layer.effects) {
                std::visit([&](const auto& value) {
                    using Effect = std::decay_t<decltype(value)>;
                    if (!value.enabled) return;
                    if constexpr (std::is_same_v<Effect, model::GaussianBlurEffect>) {
                        gpu_effects.emplace_back(
                            creative_suite::composition::GpuGaussianBlurParameters{
                                value.radius_pixels});
                    } else {
                        gpu_effects.emplace_back(
                            creative_suite::effects::ColorAdjustmentParameters{
                                value.brightness, value.contrast_percent,
                                value.saturation_percent});
                    }
                }, effect);
            }
        } else if (gpu_composition_enabled_ && has_enabled_effects) {
            cpu_color_adjustment_fallbacks += static_cast<std::uint64_t>(
                std::count_if(layer.effects.begin(), layer.effects.end(), [](const auto& effect) {
                    return std::holds_alternative<model::ColorAdjustmentEffect>(effect) &&
                        std::get<model::ColorAdjustmentEffect>(effect).enabled;
                }));
            cpu_gaussian_blur_fallbacks += static_cast<std::uint64_t>(
                std::count_if(layer.effects.begin(), layer.effects.end(), [](const auto& effect) {
                    return std::holds_alternative<model::GaussianBlurEffect>(effect) &&
                        std::get<model::GaussianBlurEffect>(effect).enabled;
                }));
        }
        const bool gpu_stack = gpu_color_only_stack || gpu_ordered_stack;
        if (has_enabled_effects && !gpu_stack) {
            try {
                StageTimer effects_timer(
                    record_preview_metrics_, diagnostics::PreviewTimingStage::Effects);
                auto processed = std::make_shared<creative_suite::media::RgbaFrame>(*frame);
                const EffectTimingRecorder effect_timing_recorder =
                    [record = record_preview_metrics_](
                        LayerEffectKind effect, std::uint64_t duration_nanoseconds) {
                        if (!record) return;
                        const auto kind = effect == LayerEffectKind::GaussianBlur
                            ? diagnostics::PreviewEffectKind::GaussianBlur
                            : diagnostics::PreviewEffectKind::ColorAdjustment;
                        diagnostics::PerformanceMetrics::instance().recordEffectTiming(
                            kind, duration_nanoseconds);
                    };
                if (!applyLayerEffects(*processed, layer.effects, should_cancel,
                                       effect_timing_recorder)) {
                    return {};
                }
                frame = std::move(processed);
            } catch (const std::exception& error) {
                if (should_cancel && should_cancel()) return {};
                if (fail_on_media_error) {
                    throw std::runtime_error("Effect processing failed for layer " +
                        std::to_string(layer.id) + ": " + error.what());
                }
                creative_suite::diagnostics::Logger::instance().log(
                    creative_suite::diagnostics::Level::Error,
                    "motion_preview", "apply_layer_effects", error.what(),
                    {{"layer_id", std::to_string(layer.id)}});
                continue;
            }
        }
        owned_frames.push_back(frame);
        auto transform = creative_suite::animation::evaluateTransform(
            layer.transform, layer.keyframes, layer.local_frame);
        if (layer.kind == model::LayerKind::Text ||
            layer.kind == model::LayerKind::Shape) {
            const double fit_scale = std::min(
                static_cast<double>(request.canvas_size.width) / frame->width,
                static_cast<double>(request.canvas_size.height) / frame->height);
            if (!std::isfinite(fit_scale) || fit_scale <= 0.0) continue;
            // The shared compositor aspect-fits every source frame to the
            // canvas. Compensate here so intrinsic pixel dimensions on native
            // text/shape layers remain pixel-sized at transform scale 1.
            transform.scale /= fit_scale;
        }
        const auto composition_index = composition_layers.size();
        composition_layers.push_back(CompositionLayer{frame.get(), transform, {}, {},
            std::move(gpu_adjustments), std::move(gpu_effects)});
        composition_layer_ids.push_back(layer.id);
        if (!composition_layers.back().gpu_color_adjustments.empty() ||
            !composition_layers.back().gpu_effects.empty()) {
            gpu_color_adjustment_count +=
                composition_layers.back().gpu_color_adjustments.size();
            for (const auto& effect : composition_layers.back().gpu_effects) {
                if (std::holds_alternative<creative_suite::effects::ColorAdjustmentParameters>(effect))
                    ++gpu_color_adjustment_count;
                else if (std::lround(std::get<
                             creative_suite::composition::GpuGaussianBlurParameters>(effect)
                                 .radius_pixels) > 0)
                    ++gpu_gaussian_blur_count;
            }
            pending_gpu_effects.push_back({composition_index, layer.id, frame,
                layer.effects});
        }
    }

    for (auto cached = content_frames_.begin(); cached != content_frames_.end();) {
        if (!active_content_ids.contains(cached->first)) {
            cached = content_frames_.erase(cached);
        } else {
            ++cached;
        }
    }

    if ((should_cancel && should_cancel()) ||
        (composition_layers.empty() && !request.black_canvas_when_empty)) {
        return {};
    }
    StageTimer composition_timer(
        record_preview_metrics_, diagnostics::PreviewTimingStage::Composition);
    if (gpu_composition_enabled_ && !gpu_composition_disabled_after_failure_) {
        using creative_suite::composition::OpenGlCompositionStatus;
        creative_suite::composition::OpenGlCompositionTimings gpu_timings;
        creative_suite::composition::OpenGlCompositionResult gpu_result;
        creative_suite::composition::OpenGlTextureFramePtr gpu_texture;
        if (gpu_metrics_ != nullptr) ++gpu_metrics_->composition_attempts;
        if (gpu_surface_ == nullptr) {
            gpu_result.status = OpenGlCompositionStatus::Failed;
            gpu_result.operation = "create_surface";
            gpu_result.cause = "The OpenGL offscreen surface is unavailable.";
        } else {
            try {
                if (!gpu_compositor_) {
                    gpu_compositor_ = std::make_unique<
                        creative_suite::composition::OpenGlFrameCompositor>(
                            gpu_surface_, creative_suite::composition::OpenGlPrecisionPolicy::Automatic,
                            texture_share_context_, texture_budget_);
                }
                bool use_synchronous_gpu = true;
                if (texture && texture_share_context_ && !texture_delivery_disabled_) {
                    const auto direct = gpu_compositor_->composeTexture(
                        request.canvas_size.width, request.canvas_size.height,
                        composition_layers, should_cancel, &gpu_timings);
                    if (record_preview_metrics_) {
                        diagnostics::PerformanceMetrics::instance().recordTexturePool(
                            texturePoolBytes(), texturePoolOccupancy());
                    }
                    if (direct.status == OpenGlCompositionStatus::Busy) {
                        if (texture_busy) *texture_busy = true;
                        return {};
                    }
                    if (direct.status == OpenGlCompositionStatus::Complete && direct.frame) {
                        gpu_texture = direct.frame;
                        gpu_result.status = direct.status;
                        use_synchronous_gpu = false;
                    } else if (direct.status == OpenGlCompositionStatus::Unsupported) {
                        // A target beyond the bounded pool uses the legacy RGBA route.
                        texture_delivery_disabled_ = true;
                        creative_suite::diagnostics::Logger::instance().log(
                            creative_suite::diagnostics::Level::Warning,
                            "motion_preview", "texture_delivery_unavailable", direct.cause,
                            {{"operation", direct.operation}, {"error_code", std::to_string(direct.error_code)}});
                    } else {
                        gpu_result.status = direct.status;
                        gpu_result.operation = direct.operation;
                        gpu_result.cause = direct.cause;
                        gpu_result.error_code = direct.error_code;
                        use_synchronous_gpu = false;
                    }
                }
                if (async_ticket != nullptr) {
                    if (!async_readback_prepared_) {
                        async_readback_prepared_ = true;
                        const auto frame_bytes = static_cast<std::uint64_t>(
                            request.canvas_size.width) * request.canvas_size.height * 4;
                        const auto pbo_budget =
                            frame_bytes < async_readback_staging_budget_bytes_
                                ? async_readback_staging_budget_bytes_ - frame_bytes : 0;
                        const auto prepared = gpu_compositor_->prepareAsyncReadback(
                            request.canvas_size.width, request.canvas_size.height,
                            pbo_budget, 2, &async_readback_slots_);
                        if (prepared.status == OpenGlCompositionStatus::Complete &&
                            async_readback_slots_ > 0) {
                            use_synchronous_gpu = false;
                            if (gpu_metrics_ != nullptr)
                                gpu_metrics_->readback_slots = async_readback_slots_;
                        } else {
                            async_readback_slots_ = 0;
                            creative_suite::diagnostics::Logger::instance().log(
                                creative_suite::diagnostics::Level::Warning,
                                "motion_export", "async_readback_unavailable",
                                prepared.cause.empty()
                                    ? "Asynchronous GPU readback is unavailable; using synchronous GPU readback."
                                    : prepared.cause,
                                {{"gpu_operation", prepared.operation},
                                 {"error_code", std::to_string(prepared.error_code)},
                                 {"canvas_width", std::to_string(request.canvas_size.width)},
                                 {"canvas_height", std::to_string(request.canvas_size.height)}});
                        }
                    } else {
                        use_synchronous_gpu = async_readback_slots_ == 0;
                    }
                    if (!use_synchronous_gpu) {
                        const auto submitted = gpu_compositor_->submitAsyncReadback(
                            request.canvas_size.width, request.canvas_size.height,
                            composition_layers, should_cancel);
                        gpu_timings = submitted.composition_timings;
                        if (submitted.status == OpenGlCompositionStatus::Complete) {
                            if (gpu_metrics_ != nullptr) {
                                gpu_metrics_->record(submitted.composition_timings);
                                gpu_metrics_->readback_bytes += submitted.bytes;
                                gpu_metrics_->readback_submit_nanoseconds +=
                                    submitted.submission_nanoseconds;
                                ++gpu_metrics_->readback_frames_submitted;
                                ++gpu_metrics_->composition_frames;
                            }
                            if (async_ticket) *async_ticket = submitted.ticket;
                            return {};
                        }
                        if (submitted.status == OpenGlCompositionStatus::Cancelled ||
                            (should_cancel && should_cancel())) return {};
                        if (async_failure) *async_failure = true;
                        gpu_result.status = submitted.status;
                        gpu_result.operation = submitted.operation;
                        gpu_result.cause = submitted.cause;
                        gpu_result.error_code = submitted.error_code;
                        use_synchronous_gpu = false;
                    }
                }
                if (use_synchronous_gpu) {
                    gpu_result = gpu_compositor_->compose(
                        request.canvas_size.width,
                        request.canvas_size.height,
                        composition_layers,
                        should_cancel,
                        &gpu_timings);
                }
            } catch (const std::exception& error) {
                gpu_result.status = OpenGlCompositionStatus::Failed;
                gpu_result.operation = "compose";
                gpu_result.cause = error.what();
            } catch (...) {
                gpu_result.status = OpenGlCompositionStatus::Failed;
                gpu_result.operation = "compose";
                gpu_result.cause = "Unknown OpenGL composition failure";
            }
        }
        if (gpu_metrics_ != nullptr) gpu_metrics_->record(gpu_timings);
        if (gpu_result.status == OpenGlCompositionStatus::Cancelled ||
            (should_cancel && should_cancel())) {
            return {};
        }
        if (gpu_result.status == OpenGlCompositionStatus::Complete &&
            (gpu_result.frame.has_value() || gpu_texture)) {
            if (gpu_metrics_ != nullptr) ++gpu_metrics_->composition_frames;
            if (record_preview_metrics_ &&
                (gpu_timings.color_adjustment_count != 0 ||
                 cpu_color_adjustment_fallbacks != 0)) {
                diagnostics::PerformanceMetrics::instance().recordGpuColorAdjustment(
                    gpu_timings.color_adjustment_count, cpu_color_adjustment_fallbacks, false,
                    gpu_timings.color_adjustment_submission_nanoseconds);
            }
            if (record_preview_metrics_ &&
                (gpu_timings.gaussian_blur_count != 0 || cpu_gaussian_blur_fallbacks != 0)) {
                diagnostics::PerformanceMetrics::instance().recordGpuGaussianBlur(
                    gpu_timings.gaussian_blur_count, cpu_gaussian_blur_fallbacks, false,
                    gpu_timings.gaussian_blur_submission_nanoseconds);
                if (gpu_timings.gaussian_blur_count != 0)
                    diagnostics::PerformanceMetrics::instance().recordEffectTiming(
                        diagnostics::PreviewEffectKind::GaussianBlur,
                        gpu_timings.gaussian_blur_submission_nanoseconds);
            }
            if (record_preview_metrics_) {
                diagnostics::PerformanceMetrics::instance().recordGpuComposition(
                    true, false, gpu_timings.uploaded_bytes, gpu_timings.readback_bytes,
                    gpu_timings.upload_nanoseconds, gpu_timings.draw_submission_nanoseconds,
                    gpu_timings.readback_nanoseconds);
            }
            if (gpu_texture && texture) {
                if (record_preview_metrics_) {
                    diagnostics::PerformanceMetrics::instance().recordTiming(
                        diagnostics::PreviewTimingStage::GpuProducerFenceSubmission,
                        gpu_timings.producer_fence_submission_nanoseconds);
                }
                *texture = std::move(gpu_texture);
                return {};
            }
            return std::make_shared<const creative_suite::media::RgbaFrame>(
                std::move(*gpu_result.frame));
        }

        const bool gpu_failed = gpu_result.status == OpenGlCompositionStatus::Failed ||
            gpu_result.status == OpenGlCompositionStatus::Complete;
        if (gpu_metrics_ != nullptr && gpu_failed) ++gpu_metrics_->failures;
        gpu_color_adjustment_failed = gpu_failed;
        gpu_failure_operation = gpu_result.operation;
        gpu_failure_code = gpu_result.error_code;
        const std::string failing_layer_id = gpu_result.layer_index >= 0 &&
            static_cast<std::size_t>(gpu_result.layer_index) < composition_layer_ids.size()
            ? std::to_string(composition_layer_ids[static_cast<std::size_t>(gpu_result.layer_index)])
            : std::string("unknown");
        gpu_composition_disabled_after_failure_ = true;
        const char* const gpu_subsystem = record_preview_metrics_
            ? "motion_preview" : "motion_export";
        creative_suite::diagnostics::Logger::instance().log(
            gpu_failed ? creative_suite::diagnostics::Level::Error
                       : creative_suite::diagnostics::Level::Warning,
            gpu_subsystem, "gpu_composition_fallback",
            gpu_result.cause.empty() ? "OpenGL composition is unavailable; using CPU composition"
                                     : gpu_result.cause,
            {{"gpu_operation", gpu_result.operation},
             {"error_code", std::to_string(gpu_result.error_code)},
             {"canvas_width", std::to_string(request.canvas_size.width)},
             {"canvas_height", std::to_string(request.canvas_size.height)},
             {"layer_id", failing_layer_id},
             {"layer_index", std::to_string(gpu_result.layer_index)},
             {"status", gpu_result.status == OpenGlCompositionStatus::Unsupported
                    ? "unsupported" : "failed"}});
        if (record_preview_metrics_) {
            diagnostics::PerformanceMetrics::instance().recordGpuComposition(
                false, gpu_failed, gpu_timings.uploaded_bytes, gpu_timings.readback_bytes,
                gpu_timings.upload_nanoseconds, gpu_timings.draw_submission_nanoseconds,
                gpu_timings.readback_nanoseconds);
            diagnostics::PerformanceMetrics::instance().recordGpuColorAdjustment(
                0, cpu_color_adjustment_fallbacks + gpu_color_adjustment_count,
                gpu_color_adjustment_failed && gpu_color_adjustment_count != 0,
                gpu_timings.color_adjustment_submission_nanoseconds);
            diagnostics::PerformanceMetrics::instance().recordGpuGaussianBlur(
                0, cpu_gaussian_blur_fallbacks + gpu_gaussian_blur_count,
                gpu_failed && gpu_gaussian_blur_count != 0,
                gpu_timings.gaussian_blur_submission_nanoseconds);
        }
    }
    if (gpu_composition_enabled_ && !try_gpu_effects &&
        cpu_color_adjustment_fallbacks != 0 && record_preview_metrics_) {
        diagnostics::PerformanceMetrics::instance().recordGpuColorAdjustment(
            0, cpu_color_adjustment_fallbacks, false, 0);
    }
    if (gpu_composition_enabled_ && !try_gpu_effects &&
        cpu_gaussian_blur_fallbacks != 0 && record_preview_metrics_) {
        diagnostics::PerformanceMetrics::instance().recordGpuGaussianBlur(
            0, cpu_gaussian_blur_fallbacks, false, 0);
    }
    for (const auto& pending : pending_gpu_effects) {
        if (should_cancel && should_cancel()) return {};
        try {
            auto processed = std::make_shared<creative_suite::media::RgbaFrame>(
                *pending.source);
            const EffectTimingRecorder effect_timing_recorder =
                [record = record_preview_metrics_](
                    LayerEffectKind effect, std::uint64_t duration_nanoseconds) {
                    if (!record) return;
                    const auto kind = effect == LayerEffectKind::GaussianBlur
                        ? diagnostics::PreviewEffectKind::GaussianBlur
                        : diagnostics::PreviewEffectKind::ColorAdjustment;
                    diagnostics::PerformanceMetrics::instance().recordEffectTiming(
                        kind, duration_nanoseconds);
                };
            if (!applyLayerEffects(*processed, pending.effects, should_cancel,
                                   effect_timing_recorder)) return {};
            owned_frames.push_back(processed);
            composition_layers[pending.composition_index].frame = processed.get();
            composition_layers[pending.composition_index].gpu_color_adjustments.clear();
            composition_layers[pending.composition_index].gpu_effects.clear();
        } catch (const std::exception& error) {
            if (should_cancel && should_cancel()) return {};
            if (fail_on_media_error) {
                throw std::runtime_error("GPU effect fallback failed for layer " +
                    std::to_string(pending.layer_id) + ": " + error.what());
            }
            creative_suite::diagnostics::Logger::instance().log(
                creative_suite::diagnostics::Level::Error,
                record_preview_metrics_ ? "motion_preview" : "motion_export",
                "gpu_effect_cpu_fallback", error.what(),
                {{"layer_id", std::to_string(pending.layer_id)},
                 {"gpu_operation", gpu_failure_operation},
                 {"error_code", std::to_string(gpu_failure_code)}});
            return {};
        }
    }
    auto composed = creative_suite::composition::FrameCompositor::compose(
        request.canvas_size.width,
        request.canvas_size.height,
        composition_layers);
    if (!composed.has_value()) {
        if (fail_on_media_error) {
            throw std::runtime_error("The shared compositor rejected the Motion Studio canvas.");
        }
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_preview", "compose_frame",
            "The shared compositor rejected the Motion Studio canvas",
            {{"width", std::to_string(request.canvas_size.width)},
             {"height", std::to_string(request.canvas_size.height)}});
        return {};
    }
    if (gpu_composition_enabled_ && gpu_metrics_ != nullptr)
        ++gpu_metrics_->fallback_frames;
    return std::make_shared<const creative_suite::media::RgbaFrame>(std::move(*composed));
}

void CompositionFrameRenderer::reportDecodeError(
    const std::filesystem::path& path,
    std::int64_t source_frame,
    std::string cause,
    std::optional<int> error_code)
{
    creative_suite::diagnostics::Context context{
        {"path", pathForLog(path)},
        {"source_frame", source_frame >= 0 ? std::to_string(source_frame) : std::string{}}};
    if (error_code.has_value()) context.emplace_back("error_code", std::to_string(*error_code));
    creative_suite::diagnostics::Logger::instance().log(
        creative_suite::diagnostics::Level::Error,
        "motion_preview", "decode_video_frame", cause, context);
}
} // namespace motion::ui
