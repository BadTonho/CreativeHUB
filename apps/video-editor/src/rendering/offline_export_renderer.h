#pragma once

#include "rendering/render_job.h"
#include <creative_suite/composition/opengl_frame_compositor.h>

#include <atomic>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <memory>

namespace rendering {

enum class ExportOutcome { Completed, Canceled, Failed };

// Per-job diagnostics; wall/submission times are not GPU execution queries.
struct OfflineExportMetrics {
    ExportOutcome outcome = ExportOutcome::Failed;
    bool gpu_requested = false;
    std::uint64_t cpu_frames = 0, gpu_frames = 0, encoded_frames = 0;
    std::uint64_t fallback_frames = 0, gpu_failures = 0;
    std::uint64_t preparation_nanoseconds = 0, composition_nanoseconds = 0;
    std::uint64_t cpu_composition_nanoseconds = 0, gpu_composition_nanoseconds = 0;
    std::uint64_t upload_nanoseconds = 0, draw_submission_nanoseconds = 0, readback_nanoseconds = 0;
    std::uint64_t uploaded_bytes = 0, readback_bytes = 0;
    std::uint64_t encoding_nanoseconds = 0, audio_nanoseconds = 0;
    std::uint64_t finalization_nanoseconds = 0, total_nanoseconds = 0;
    std::uint64_t peak_known_gpu_bytes = 0, peak_cpu_frame_bytes = 0, peak_prepared_source_bytes = 0;
};

// Small adapter seam for deterministic export fault tests. Production delegates
// to the shared OpenGL compositor, not a separate rendering engine.
class ExportGpuCompositor {
public:
    virtual ~ExportGpuCompositor() = default;
    virtual creative_suite::composition::OpenGlCompositionResult compose(int width, int height,
        const std::vector<creative_suite::composition::CompositionLayer>& layers,
        const creative_suite::composition::OpenGlFrameCompositor::CancellationPredicate& cancel,
        creative_suite::composition::OpenGlCompositionTimings* timings) = 0;
    virtual creative_suite::composition::OpenGlResourceUsage resourceUsage() const noexcept { return {}; }
};

struct OfflineExportOptions {
    // GUI-owned; retain through worker teardown. No surface means CPU fallback
    // when a GPU job is requested. No Qt GUI dependency for CPU-only callers.
    QOffscreenSurface* gpu_surface = nullptr;
    std::function<void(const std::string&, std::int64_t)> warning_callback;
    std::function<void(const OfflineExportMetrics&)> metrics_callback;
    std::function<std::unique_ptr<ExportGpuCompositor>(QOffscreenSurface*)> gpu_factory;
};

class ExportCanceled final : public std::runtime_error {
public:
    ExportCanceled() : std::runtime_error("Export canceled.") {}
};

class ExportError final : public std::runtime_error {
public:
    ExportError(std::string message, int error_code)
        : std::runtime_error(std::move(message)), error_code_(error_code) {}

    [[nodiscard]] int errorCode() const noexcept { return error_code_; }

private:
    int error_code_;
};

class OfflineExportRenderer final {
public:
    using ProgressCallback = std::function<void(int)>;

    // Renders every output frame synchronously. Call this from a worker thread.
    static void render(
        const RenderJob& job,
        const std::atomic_bool& cancel_requested,
        ProgressCallback report_progress = {},
        const OfflineExportOptions& options = {});
};

}  // namespace rendering
