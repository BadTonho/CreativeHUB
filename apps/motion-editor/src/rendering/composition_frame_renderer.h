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
                             CompositionGpuMetrics* gpu_metrics = nullptr) noexcept
        : record_preview_metrics_(record_preview_metrics),
          gpu_composition_enabled_(gpu_composition_enabled),
          gpu_surface_(gpu_surface),
          gpu_metrics_(gpu_metrics) {}

    void reset();
    // Releases thread-affine OpenGL resources. Call from the renderer worker.
    void shutdown();
    [[nodiscard]] creative_suite::media::RgbaFramePtr render(
        const PreviewRequest& request,
        const CancellationPredicate& should_cancel = {},
        bool fail_on_media_error = false,
        PreviewRequestMode mode = PreviewRequestMode::Interactive);

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
};

} // namespace motion::ui
