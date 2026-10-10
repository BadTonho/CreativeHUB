#pragma once

#include "model/composition_document.h"
#include "preview_frame.h"
#include "../diagnostics/performance_metrics.h"

#include <creative_suite/media/video_frame.h>

#include <QThread>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

class QOffscreenSurface;
class QOpenGLContext;

namespace motion::ui {

struct PreviewLayerSnapshot {
    model::LayerId id = 0;
    model::LayerKind kind = model::LayerKind::Image;
    std::filesystem::path source_path;
    std::int64_t local_frame = 0;
    std::int64_t source_frame_count = 0;
    std::int64_t source_start_frame = 0;
    double source_frame_rate = 0.0;
    creative_suite::animation::Transform2D transform;
    creative_suite::animation::TransformKeyframes keyframes;
    model::LayerContent content;
    std::vector<model::LayerEffect> effects;
    creative_suite::media::RgbaFramePtr still_frame;
};

struct PreviewRequest {
    model::CanvasSize canvas_size{};
    model::FrameRate frame_rate{};
    std::vector<PreviewLayerSnapshot> layers;
    bool black_canvas_when_empty = false;
};

class CompositionFrameRenderer;

enum class PreviewRequestMode : std::uint8_t {
    Interactive,
    Playback,
};

inline constexpr std::int64_t maximum_sequential_playback_decode_gap_frames = 8;

[[nodiscard]] inline bool shouldUseSequentialPlaybackDecode(
    PreviewRequestMode mode,
    std::int64_t current_frame,
    std::int64_t requested_frame) noexcept
{
    if (mode != PreviewRequestMode::Playback || current_frame < 0 ||
        requested_frame <= current_frame) {
        return false;
    }
    const auto gap = requested_frame - current_frame;
    return gap >= 2 && gap <= maximum_sequential_playback_decode_gap_frames;
}

[[nodiscard]] inline bool shouldFallbackToTimestampSeek(
    bool forward_decode_was_cancelled) noexcept
{
    return !forward_decode_was_cancelled;
}

// Serializes decoding on a worker thread and keeps decoder sessions local to
// that thread. Interactive requests cancel stale decodes; playback requests
// replace pending work without interrupting the in-flight playback decode.
class PreviewRenderer final : public QThread {
public:
    using ResultHandler = std::function<void(
        std::uint64_t,
        PreviewRequestMode,
        std::uint64_t,
        creative_suite::media::RgbaFramePtr)>;
    using FrameResultHandler = std::function<void(PreviewFrame)>;
    using CancellationPredicate = std::function<bool()>;
    using RenderFunction = std::function<creative_suite::media::RgbaFramePtr(
        const PreviewRequest&,
        const CancellationPredicate&)>;

    PreviewRenderer(QObject* result_receiver,
                    ResultHandler result_handler,
                    RenderFunction render_function = {},
                    diagnostics::PerformanceMetrics* metrics = nullptr,
                    bool gpu_composition_enabled = false,
                    QOffscreenSurface* gpu_surface = nullptr,
                    FrameResultHandler frame_handler = {},
                    QOpenGLContext* texture_share_context = nullptr);
    ~PreviewRenderer() override;

    [[nodiscard]] std::uint64_t submit(
        PreviewRequest request,
        PreviewRequestMode mode = PreviewRequestMode::Interactive);
    void resetSessions();
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] bool canPresentResult(
        std::uint64_t request_generation,
        PreviewRequestMode mode,
        std::uint64_t cancellation_generation) const noexcept;
    void stopAndWait();
    // GUI thread: invalidate outstanding deliveries; the next request is
    // recomposed on the worker through RGBA (or CPU after context failure).
    void recoverPresentation(bool use_cpu);
    [[nodiscard]] unsigned pendingDeliveryCount() const;

protected:
    void run() override;

private:
    [[nodiscard]] PreviewFrame render(
        const PreviewRequest& request,
        std::uint64_t cancellation_generation,
        PreviewRequestMode mode, bool& texture_busy);
    QObject* result_receiver_ = nullptr;
    struct Mailbox {
        std::mutex mutex;
        std::optional<PreviewFrame> frame;
        bool drain_queued = false;
    };
    std::shared_ptr<Mailbox> mailbox_ = std::make_shared<Mailbox>();
    void publish(PreviewFrame frame);
    void clearDelivery();
    FrameResultHandler frame_handler_;
    RenderFunction render_function_;
    struct PendingRequest {
        std::uint64_t generation = 0;
        std::uint64_t cancellation_generation = 0;
        PreviewRequestMode mode = PreviewRequestMode::Interactive;
        PreviewRequest request;
    };
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<PendingRequest> pending_;
    std::optional<PreviewRequestMode> in_flight_mode_;
    std::uint64_t in_flight_generation_ = 0;
    bool reset_sessions_pending_ = false;
    bool stopping_ = false;
    bool disable_texture_pending_ = false;
    bool disable_gpu_pending_ = false;
    QOpenGLContext* texture_share_context_ = nullptr;
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<std::uint64_t> cancellation_generation_{0};
    std::unique_ptr<CompositionFrameRenderer> frame_renderer_;
    diagnostics::PerformanceMetrics* metrics_ = nullptr;
};

} // namespace motion::ui
