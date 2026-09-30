#include "composition_frame_renderer.h"
#include "layer_content_renderer.h"
#include "layer_effect_processor.h"

#include <creative_suite/composition/frame_compositor.h>
#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/animation/animation.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace motion::ui {
namespace {

std::string pathForLog(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
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
    bool fail_on_media_error)
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
                auto session = video_sessions_.find(layer.source_path);
                if (session == video_sessions_.end()) {
                    auto opened = VideoPlaybackSession::open(layer.source_path);
                    session = video_sessions_.emplace(layer.source_path, std::move(opened)).first;
                }
                const auto decoded = session->second->decode_frame_at(
                    source_frame,
                    [&should_cancel] {
                        return should_cancel && should_cancel();
                    });
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
                auto processed = std::make_shared<creative_suite::media::RgbaFrame>(*frame);
                if (!applyLayerEffects(*processed, layer.effects, should_cancel)) {
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
