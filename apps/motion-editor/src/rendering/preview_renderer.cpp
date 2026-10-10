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
    QOffscreenSurface* gpu_surface,
    FrameResultHandler frame_handler,
    QOpenGLContext* texture_share_context)
    : result_receiver_(result_receiver)
    , frame_handler_(frame_handler ? std::move(frame_handler) :
          FrameResultHandler([handler = std::move(result_handler)](PreviewFrame frame) {
              if (handler) handler(frame.generation,
                  frame.playback ? PreviewRequestMode::Playback : PreviewRequestMode::Interactive,
                  frame.cancellation_generation, std::move(frame.rgba));
          }))
    , render_function_(std::move(render_function))
    , frame_renderer_(std::make_unique<CompositionFrameRenderer>(
          true, gpu_composition_enabled, gpu_surface))
    , metrics_(metrics != nullptr ? metrics : &diagnostics::PerformanceMetrics::instance())
{
    texture_share_context_ = texture_share_context;
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
    clearDelivery();
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
    clearDelivery();
    if (isRunning()) wait();
}

void PreviewRenderer::run()
{
    if (texture_share_context_) {
        try { frame_renderer_->configureTextureDelivery(texture_share_context_); }
        catch (const std::exception& error) {
            frame_renderer_->disableTextureDelivery(true);
            texture_share_context_ = nullptr;
            creative_suite::diagnostics::Logger::instance().log(
                creative_suite::diagnostics::Level::Error, "motion_preview", "configure_texture_delivery",
                error.what(), {{"error_code", "0"}});
        }
    }
    for (;;) {
        std::optional<PendingRequest> request;
        bool reset_sessions = false;
        {
            std::unique_lock lock(mutex_);
            const auto ready = [this] {
                return stopping_ || pending_.has_value() || reset_sessions_pending_ ||
                       disable_texture_pending_;
            };
            if (texture_share_context_ && frame_renderer_->texturePoolOccupancy())
                wake_.wait_for(lock, std::chrono::milliseconds(5), ready);
            else wake_.wait(lock, ready);
            if (disable_texture_pending_) {
                frame_renderer_->disableTextureDelivery(disable_gpu_pending_);
                disable_texture_pending_ = disable_gpu_pending_ = false;
            }
            if (stopping_) break;
            reset_sessions = std::exchange(reset_sessions_pending_, false);
            request = std::move(pending_);
            pending_.reset();
            if (request.has_value()) {
                in_flight_mode_ = request->mode;
                in_flight_generation_ = request->generation;
            }
        }
        try { frame_renderer_->collectTextureFrames(); }
        catch (const std::exception& error) {
            frame_renderer_->disableTextureDelivery(true);
            texture_share_context_ = nullptr;
            creative_suite::diagnostics::Logger::instance().log(
                creative_suite::diagnostics::Level::Error, "motion_preview", "collect_texture_frames",
                error.what(), {{"error_code", "0"}});
        }
        if (reset_sessions) {
            frame_renderer_->reset();
        }
        if (!request.has_value()) continue;

        PreviewFrame output;
        output.generation = request->generation;
        output.cancellation_generation = request->cancellation_generation;
        output.playback = request->mode == PreviewRequestMode::Playback;
        bool texture_busy = false;
        const auto render_started = std::chrono::steady_clock::now();
        try {
            if (render_function_) {
                const auto cancellation_generation = request->cancellation_generation;
                const auto is_cancelled = [this, cancellation_generation] {
                    return cancellation_generation !=
                        cancellation_generation_.load(std::memory_order_relaxed);
                };
                output.rgba = render_function_(request->request, is_cancelled);
            } else {
                auto rendered = render(request->request, request->cancellation_generation,
                                       request->mode, texture_busy);
                output.rgba = std::move(rendered.rgba);
                output.texture = std::move(rendered.texture);
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
            if (output.valid()) metrics_->recordRenderedFrame();
        }
        {
            std::lock_guard lock(mutex_);
            if (in_flight_generation_ == request->generation) {
                in_flight_mode_.reset();
                in_flight_generation_ = 0;
            }
        }
        if (texture_busy) {
            if (metrics_) metrics_->recordTexturePool(frame_renderer_->texturePoolBytes(),
                frame_renderer_->texturePoolOccupancy(), true,
                request->mode == PreviewRequestMode::Playback);
            std::unique_lock lock(mutex_);
            if (request->mode == PreviewRequestMode::Interactive && !stopping_ &&
                !pending_ && canPresentResult(request->generation, request->mode,
                                             request->cancellation_generation)) {
                // Wait before retrying and let new seeks supersede this request.
                wake_.wait_for(lock, std::chrono::milliseconds(5), [this] {
                    return stopping_ || pending_.has_value() || reset_sessions_pending_ ||
                           disable_texture_pending_;
                });
                if (!stopping_ && !pending_ && canPresentResult(request->generation,
                    request->mode, request->cancellation_generation)) pending_ = std::move(request);
            } else if (metrics_) metrics_->discardRequest(request->generation);
            continue;
        }
        if (!canPresentResult(request->generation, request->mode,
                              request->cancellation_generation) || !result_receiver_) {
            if (metrics_) metrics_->recordStaleResult(request->generation);
            continue;
        }
        publish(std::move(output));
    }
    frame_renderer_->shutdown();
}

void PreviewRenderer::clearDelivery() {
    std::lock_guard lock(mailbox_->mutex);
    if (mailbox_->frame && metrics_) metrics_->recordStaleResult(mailbox_->frame->generation);
    mailbox_->frame.reset();
}
unsigned PreviewRenderer::pendingDeliveryCount() const {
    std::lock_guard lock(mailbox_->mutex);
    return mailbox_->frame.has_value() ? 1U : 0U;
}
void PreviewRenderer::recoverPresentation(bool use_cpu) {
    clearDelivery();
    std::lock_guard lock(mutex_);
    cancellation_generation_.fetch_add(1, std::memory_order_relaxed);
    disable_texture_pending_ = true;
    disable_gpu_pending_ = disable_gpu_pending_ || use_cpu;
    if (metrics_) metrics_->recordPresentationRecovery();
    wake_.notify_one();
}
void PreviewRenderer::publish(PreviewFrame frame) {
    std::lock_guard publication_lock(mutex_);
    if (stopping_ || !canPresentResult(frame.generation,
        frame.playback ? PreviewRequestMode::Playback : PreviewRequestMode::Interactive,
        frame.cancellation_generation)) {
        if (metrics_) metrics_->recordStaleResult(frame.generation);
        return;
    }
    auto mailbox = mailbox_;
    {
        std::lock_guard lock(mailbox->mutex);
        if (mailbox->frame && metrics_) metrics_->recordStaleResult(mailbox->frame->generation);
        if (frame.valid() && metrics_) metrics_->recordDelivery(bool(frame.texture));
        mailbox->frame = std::move(frame);
        if (mailbox->drain_queued) return;
        mailbox->drain_queued = true;
    }
    const auto handler = frame_handler_;
    QMetaObject::invokeMethod(result_receiver_, [mailbox, handler] {
        std::optional<PreviewFrame> frame;
        {
            std::lock_guard lock(mailbox->mutex);
            frame = std::move(mailbox->frame);
            mailbox->frame.reset();
            mailbox->drain_queued = false;
        }
        if (frame && handler) handler(std::move(*frame));
    }, Qt::QueuedConnection);
}
PreviewFrame PreviewRenderer::render(const PreviewRequest& request,
    std::uint64_t cancellation_generation, PreviewRequestMode mode, bool& texture_busy)
{
    PreviewFrame output;
    if (!frame_renderer_) return output;
    output.rgba = frame_renderer_->render(request, [this, cancellation_generation] {
        return cancellation_generation != cancellation_generation_.load(std::memory_order_relaxed);
    }, false, mode, nullptr, nullptr,
       texture_share_context_ ? &output.texture : nullptr, &texture_busy);
    return output;
}

} // namespace motion::ui
