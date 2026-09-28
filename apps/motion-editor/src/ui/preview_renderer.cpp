#include "preview_renderer.h"

#include <creative_suite/composition/frame_compositor.h>
#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/media/media_library.h>

#include <QMetaObject>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace motion::ui {
namespace {

std::string pathForLog(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

} // namespace

PreviewRenderer::PreviewRenderer(QObject* result_receiver, ResultHandler result_handler)
    : result_receiver_(result_receiver), result_handler_(std::move(result_handler))
{
    setObjectName(QStringLiteral("motion-preview-renderer"));
    start();
}

PreviewRenderer::~PreviewRenderer()
{
    stopAndWait();
}

std::uint64_t PreviewRenderer::submit(PreviewRequest request)
{
    std::lock_guard lock(mutex_);
    if (stopping_) return generation_.load(std::memory_order_relaxed);
    const auto current_generation = generation_.fetch_add(1, std::memory_order_relaxed) + 1;
    pending_.emplace(current_generation, std::move(request));
    wake_.notify_one();
    return current_generation;
}

void PreviewRenderer::resetSessions()
{
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    generation_.fetch_add(1, std::memory_order_relaxed);
    pending_.reset();
    reset_sessions_pending_ = true;
    wake_.notify_one();
}

std::uint64_t PreviewRenderer::generation() const noexcept
{
    return generation_.load(std::memory_order_relaxed);
}

void PreviewRenderer::stopAndWait()
{
    {
        std::lock_guard lock(mutex_);
        if (!stopping_) {
            stopping_ = true;
            generation_.fetch_add(1, std::memory_order_relaxed);
            pending_.reset();
        }
    }
    wake_.notify_all();
    if (isRunning()) wait();
}

void PreviewRenderer::run()
{
    for (;;) {
        std::optional<std::pair<std::uint64_t, PreviewRequest>> request;
        bool reset_sessions = false;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] {
                return stopping_ || pending_.has_value() || reset_sessions_pending_;
            });
            if (stopping_) break;
            reset_sessions = std::exchange(reset_sessions_pending_, false);
            request = std::move(pending_);
            pending_.reset();
        }
        if (reset_sessions) video_sessions_.clear();
        if (!request.has_value()) continue;

        creative_suite::media::RgbaFramePtr output;
        try {
            output = render(request->second, request->first);
        } catch (const std::exception& error) {
            creative_suite::diagnostics::Logger::instance().log(
                creative_suite::diagnostics::Level::Error,
                "motion_preview", "render_frame", error.what(),
                {{"frame", std::to_string(request->second.layers.empty()
                    ? 0 : request->second.layers.front().local_frame)}});
        } catch (...) {
            creative_suite::diagnostics::Logger::instance().log(
                creative_suite::diagnostics::Level::Error,
                "motion_preview", "render_frame", "Unknown preview rendering failure");
        }
        if (request->first != generation() || result_receiver_ == nullptr) continue;
        const auto handler = result_handler_;
        const auto request_generation = request->first;
        QMetaObject::invokeMethod(
            result_receiver_,
            [handler, request_generation, output = std::move(output)]() mutable {
                if (handler) handler(request_generation, std::move(output));
            },
            Qt::QueuedConnection);
    }
    video_sessions_.clear();
}

creative_suite::media::RgbaFramePtr PreviewRenderer::render(
    const PreviewRequest& request,
    std::uint64_t request_generation)
{
    using creative_suite::composition::CompositionLayer;
    using creative_suite::media::VideoPlaybackSession;

    if (request.layers.empty()) return {};
    if (request.frame_rate.numerator <= 0 || request.frame_rate.denominator <= 0) {
        return {};
    }

    std::vector<creative_suite::composition::CompositionLayer> composition_layers;
    std::vector<creative_suite::media::RgbaFramePtr> owned_frames;
    composition_layers.reserve(request.layers.size());
    owned_frames.reserve(request.layers.size());
    const long double timeline_rate =
        static_cast<long double>(request.frame_rate.numerator) /
        static_cast<long double>(request.frame_rate.denominator);

    for (const auto& layer : request.layers) {
        if (request_generation != generation()) return {};
        creative_suite::media::RgbaFramePtr frame;
        if (layer.kind == model::LayerKind::Image) {
            frame = layer.still_frame;
            if (frame == nullptr) {
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
                    [this, request_generation] {
                        return request_generation != generation();
                    });
                if (!decoded.has_value()) {
                    if (request_generation != generation()) return {};
                    reportDecodeError(layer.source_path, source_frame,
                                      "The video decoder returned no frame");
                    video_sessions_.erase(session);
                    continue;
                }
                frame = *decoded;
            } catch (const creative_suite::media::MediaError& error) {
                reportDecodeError(layer.source_path, source_frame, error.what(),
                                  error.error_code());
                video_sessions_.erase(layer.source_path);
                continue;
            } catch (const std::exception& error) {
                reportDecodeError(layer.source_path, source_frame, error.what());
                video_sessions_.erase(layer.source_path);
                continue;
            }
        } else {
            continue;
        }

        if (frame == nullptr) continue;
        owned_frames.push_back(frame);
        composition_layers.push_back(CompositionLayer{frame.get(), layer.transform});
    }

    if (request_generation != generation() || composition_layers.empty()) return {};
    auto composed = creative_suite::composition::FrameCompositor::compose(
        request.canvas_size.width,
        request.canvas_size.height,
        composition_layers);
    if (!composed.has_value()) {
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

void PreviewRenderer::reportDecodeError(
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
