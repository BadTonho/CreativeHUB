#include "composition_frame_renderer.h"
#include "layer_content_renderer.h"
#include "layer_effect_processor.h"
#include "../diagnostics/performance_metrics.h"

#include <creative_suite/composition/frame_compositor.h>
#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/animation/animation.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
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

void CompositionFrameRenderer::reset()
{
    video_sessions_.clear();
    content_frames_.clear();
}
creative_suite::media::RgbaFramePtr CompositionFrameRenderer::render(
    const PreviewRequest& request,
    const CancellationPredicate& should_cancel,
    bool fail_on_media_error,
    PreviewRequestMode mode)
{
    using creative_suite::composition::CompositionLayer;
    using creative_suite::media::VideoPlaybackSession;

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
    std::set<model::LayerId> active_content_ids;
    composition_layers.reserve(request.layers.size());
    owned_frames.reserve(request.layers.size());
    const long double timeline_rate =
        static_cast<long double>(request.frame_rate.numerator) /
        static_cast<long double>(request.frame_rate.denominator);

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
            const long double source_position =
                static_cast<long double>(layer.local_frame) *
                static_cast<long double>(layer.source_frame_rate) / timeline_rate;
            const auto exclusive_max = std::ldexp(1.0L, 63);
            const auto rounded_source_position = std::floor(source_position + 0.5L);
            if (!std::isfinite(source_position) || source_position < 0.0L ||
                source_position >= exclusive_max || rounded_source_position >= exclusive_max) {
                if (fail_on_media_error) {
                    throw std::runtime_error("Timeline position cannot be represented as a source frame: " +
                                             pathForLog(layer.source_path));
                }
                reportDecodeError(layer.source_path, -1,
                                  "Timeline position cannot be represented as a source frame");
                continue;
            }
            auto source_frame = static_cast<std::int64_t>(rounded_source_position);
            if (layer.source_frame_count > 0) {
                source_frame = std::min(source_frame, layer.source_frame_count - 1);
            }
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
        if (hasEnabledLayerEffects(layer.effects)) {
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
        composition_layers.push_back(CompositionLayer{frame.get(), transform});
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
