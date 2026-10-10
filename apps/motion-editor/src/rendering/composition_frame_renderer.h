#pragma once

#include "preview_renderer.h"

#include <creative_suite/media/video_playback.h>
#include <creative_suite/composition/opengl_frame_compositor.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <cstdint>

namespace motion::ui {

struct CompositionGpuMetrics {
    std::uint64_t composition_attempts = 0;
    std::uint64_t composition_frames = 0;
    std::uint64_t fallback_frames = 0;
    std::uint64_t failures = 0;
    std::uint64_t uploaded_bytes = 0;
    std::uint64_t readback_bytes = 0;
    std::uint64_t uploaded_layers = 0;
    std::uint64_t upload_nanoseconds = 0;
    std::uint64_t draw_submission_nanoseconds = 0;
    std::uint64_t readback_nanoseconds = 0;
    std::uint64_t readback_submit_nanoseconds = 0;
    std::uint64_t readback_wait_nanoseconds = 0;
    std::uint64_t readback_copy_nanoseconds = 0;
    std::uint64_t readback_frames_submitted = 0;
    std::uint64_t readback_frames_collected = 0;
    std::uint64_t readback_fence_waits = 0;
    std::uint64_t readback_peak_pending_frames = 0;
    std::uint64_t readback_peak_pending_bytes = 0;
    std::uint64_t readback_slots = 0;
    std::uint64_t color_adjustment_count = 0;
    std::uint64_t gaussian_blur_count = 0;

    void record(const creative_suite::composition::OpenGlCompositionTimings& timings) noexcept;
};

// Motion-owned frame evaluation shared by interactive preview and offline
// export. Each instance owns its decoder sessions and content cache and must
// be used from one worker thread at a time.
class CompositionFrameRenderer final {
public:
    using CancellationPredicate = std::function<bool()>;

    explicit CompositionFrameRenderer(bool record_preview_metrics = true) noexcept
        : record_preview_metrics_(record_preview_metrics) {}
    CompositionFrameRenderer(bool record_preview_metrics,
                             bool gpu_composition_enabled,
                             QOffscreenSurface* gpu_surface,
                             CompositionGpuMetrics* gpu_metrics = nullptr,
                             std::uint64_t async_readback_staging_budget_bytes =
                                 128ULL * 1024 * 1024) noexcept
        : record_preview_metrics_(record_preview_metrics),
          gpu_composition_enabled_(gpu_composition_enabled),
          gpu_surface_(gpu_surface),
          gpu_metrics_(gpu_metrics),
          async_readback_staging_budget_bytes_(async_readback_staging_budget_bytes) {}

    void reset();
    // Releases thread-affine OpenGL resources. Call from the renderer worker.
    void shutdown();
    [[nodiscard]] creative_suite::media::RgbaFramePtr render(
        const PreviewRequest& request,
        const CancellationPredicate& should_cancel = {},
        bool fail_on_media_error = false,
        PreviewRequestMode mode = PreviewRequestMode::Interactive,
        std::optional<creative_suite::composition::OpenGlReadbackTicket>* async_ticket = nullptr,
        bool* async_failure = nullptr,
        creative_suite::composition::OpenGlTextureFramePtr* texture = nullptr,
        bool* texture_busy = nullptr);
    // Worker-only. The share context is borrowed, never operated on this thread.
    void configureTextureDelivery(QOpenGLContext* share_context);
    void disableTextureDelivery(bool use_cpu);
    void collectTextureFrames();
    [[nodiscard]] std::uint64_t texturePoolBytes() const noexcept;
    [[nodiscard]] unsigned texturePoolOccupancy() const;
    // Drop outstanding PBO tickets and disable GPU composition after a failed
    // asynchronous export transfer. Call on the renderer worker thread.
    void recoverAsyncReadbackFailure();
    [[nodiscard]] creative_suite::composition::OpenGlReadbackResult collectAsyncReadback(
        creative_suite::composition::OpenGlReadbackTicket ticket,
        const CancellationPredicate& should_cancel = {});
    [[nodiscard]] unsigned asyncReadbackSlotCount() const noexcept
    {
        return async_readback_slots_;
    }

private:
    void reportDecodeError(
        const std::filesystem::path& path,
        std::int64_t source_frame,
        std::string cause,
        std::optional<int> error_code = std::nullopt);

    std::map<std::filesystem::path,
             std::unique_ptr<creative_suite::media::VideoPlaybackSession>> video_sessions_;
    struct CachedContentFrame {
        model::LayerContent content;
        creative_suite::media::RgbaFramePtr frame;
    };
    std::map<model::LayerId, CachedContentFrame> content_frames_;
    bool record_preview_metrics_ = true;
    bool gpu_composition_enabled_ = false;
    bool gpu_composition_disabled_after_failure_ = false;
    QOffscreenSurface* gpu_surface_ = nullptr; // borrowed from the GUI thread owner
    CompositionGpuMetrics* gpu_metrics_ = nullptr; // borrowed for one render job
    std::unique_ptr<creative_suite::composition::OpenGlFrameCompositor> gpu_compositor_;
    QOpenGLContext* texture_share_context_ = nullptr;
    bool texture_delivery_disabled_ = false;
    std::shared_ptr<creative_suite::composition::OpenGlTexturePoolBudget> texture_budget_;
    bool async_readback_prepared_ = false;
    unsigned async_readback_slots_ = 0;
    std::uint64_t async_readback_staging_budget_bytes_ = 128ULL * 1024 * 1024;
};

} // namespace motion::ui
