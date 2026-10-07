#include "preview_renderer.h"
#include "composition_frame_renderer.h"

#include <creative_suite/diagnostics/logger.h>

#include <QMetaObject>

#include <utility>
#include <chrono>

namespace motion::ui {

PreviewRenderer::PreviewRenderer(
    QObject* result_receiver,
    ResultHandler result_handler,
    RenderFunction render_function,
    diagnostics::PerformanceMetrics* metrics,
    bool gpu_composition_enabled,
    QOffscreenSurface* gpu_surface)
    : result_receiver_(result_receiver)
    , result_handler_(std::move(result_handler))
    , render_function_(std::move(render_function))
    , frame_renderer_(std::make_unique<CompositionFrameRenderer>(
          true, gpu_composition_enabled, gpu_surface))
    , metrics_(metrics != nullptr ? metrics : &diagnostics::PerformanceMetrics::instance())
{
    setObjectName(QStringLiteral("motion-preview-renderer"));
    start();
}

PreviewRenderer::~PreviewRenderer()
{
    stopAndWait();
}

std::uint64_t PreviewRenderer::submit(PreviewRequest request, PreviewRequestMode mode)
{
    std::lock_guard lock(mutex_);
    if (stopping_) return generation_.load(std::memory_order_relaxed);
    if (mode == PreviewRequestMode::Interactive ||
        (in_flight_mode_.has_value() &&
         *in_flight_mode_ == PreviewRequestMode::Interactive)) {
        cancellation_generation_.fetch_add(1, std::memory_order_relaxed);
    }
    const auto current_generation = generation_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (pending_.has_value() && metrics_ != nullptr) {
        metrics_->recordCoalescedRequest(pending_->generation);
    }
    if (metrics_ != nullptr) metrics_->recordRequest(current_generation);
    pending_ = PendingRequest{
        current_generation,
        cancellation_generation_.load(std::memory_order_relaxed),
        mode,
        std::move(request)};
    wake_.notify_one();
    return current_generation;
}

void PreviewRenderer::resetSessions()
{
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    generation_.fetch_add(1, std::memory_order_relaxed);
    cancellation_generation_.fetch_add(1, std::memory_order_relaxed);
    if (pending_.has_value() && metrics_ != nullptr) {
        metrics_->recordStaleResult(pending_->generation);
    }
    pending_.reset();
    reset_sessions_pending_ = true;
    wake_.notify_one();
}

std::uint64_t PreviewRenderer::generation() const noexcept
{
    return generation_.load(std::memory_order_relaxed);
}

bool PreviewRenderer::canPresentResult(
    std::uint64_t request_generation,
    PreviewRequestMode mode,
    std::uint64_t cancellation_generation) const noexcept
{
    if (cancellation_generation !=
        cancellation_generation_.load(std::memory_order_relaxed)) {
        return false;
    }
    return mode == PreviewRequestMode::Playback || request_generation == generation();
}

void PreviewRenderer::stopAndWait()
{
    {
        std::lock_guard lock(mutex_);
        if (!stopping_) {
            stopping_ = true;
            generation_.fetch_add(1, std::memory_order_relaxed);
            cancellation_generation_.fetch_add(1, std::memory_order_relaxed);
            if (pending_.has_value() && metrics_ != nullptr) {
                metrics_->recordStaleResult(pending_->generation);
            }
            pending_.reset();
        }
    }
    wake_.notify_all();
    if (isRunning()) wait();
}

void PreviewRenderer::run()
{
    for (;;) {
        std::optional<PendingRequest> request;
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
            if (request.has_value()) {
                in_flight_mode_ = request->mode;
                in_flight_generation_ = request->generation;
            }
        }
        if (reset_sessions) {
            frame_renderer_->reset();
        }
        if (!request.has_value()) continue;

        creative_suite::media::RgbaFramePtr output;
        const auto render_started = std::chrono::steady_clock::now();
        try {
            if (render_function_) {
                const auto cancellation_generation = request->cancellation_generation;
                const auto is_cancelled = [this, cancellation_generation] {
                    return cancellation_generation !=
                        cancellation_generation_.load(std::memory_order_relaxed);
                };
                output = render_function_(request->request, is_cancelled);
            } else {
                output = render(
                    request->request,
                    request->cancellation_generation,
                    request->mode);
            }
        } catch (const std::exception& error) {
            creative_suite::diagnostics::Logger::instance().log(
                creative_suite::diagnostics::Level::Error,
                "motion_preview", "render_frame", error.what(),
                {{"frame", std::to_string(request->request.layers.empty()
                    ? 0 : request->request.layers.front().local_frame)}});
        } catch (...) {
            creative_suite::diagnostics::Logger::instance().log(
                creative_suite::diagnostics::Level::Error,
                "motion_preview", "render_frame", "Unknown preview rendering failure");
        }
        const auto render_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - render_started).count();
        if (metrics_ != nullptr) {
            if (render_elapsed >= 0) {
                metrics_->recordTiming(
                    diagnostics::PreviewTimingStage::FrameRender,
                    static_cast<std::uint64_t>(render_elapsed));
            }
            if (output != nullptr) metrics_->recordRenderedFrame();
        }
        {
            std::lock_guard lock(mutex_);
            if (in_flight_generation_ == request->generation) {
                in_flight_mode_.reset();
                in_flight_generation_ = 0;
            }
        }
        if (!canPresentResult(
                request->generation, request->mode, request->cancellation_generation) ||
            result_receiver_ == nullptr) {
            if (metrics_ != nullptr) metrics_->recordStaleResult(request->generation);
            continue;
        }
        const auto handler = result_handler_;
        const auto request_generation = request->generation;
        const auto request_mode = request->mode;
        const auto cancellation_generation = request->cancellation_generation;
        QMetaObject::invokeMethod(
            result_receiver_,
            [handler, request_generation, request_mode, cancellation_generation,
             output = std::move(output)]() mutable {
                if (handler) {
                    handler(request_generation, request_mode, cancellation_generation,
                            std::move(output));
                }
            },
            Qt::QueuedConnection);
    }
    frame_renderer_->shutdown();
}

creative_suite::media::RgbaFramePtr PreviewRenderer::render(
    const PreviewRequest& request,
    std::uint64_t cancellation_generation,
    PreviewRequestMode mode)
{
    if (!frame_renderer_) return {};
    return frame_renderer_->render(request, [this, cancellation_generation] {
        return cancellation_generation !=
            cancellation_generation_.load(std::memory_order_relaxed);
    }, false, mode);
}

} // namespace motion::ui
