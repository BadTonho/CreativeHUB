#pragma once

#include "model/composition_document.h"
#include "rendering/composition_frame_renderer.h"
#include "rendering/preview_renderer.h"

#include <creative_suite/media/video_encoder.h>

#include <QPointer>
#include <QThread>

#include <atomic>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace motion::ui {

struct MotionExportSettings {
    std::filesystem::path output_path;
    std::string container_name;
    std::string video_encoder_name;
    int width = 0;
    int height = 0;
    model::FrameRate frame_rate;
    double video_bitrate_mbps = 10.0;
};

struct MotionExportSnapshot {
    model::CanvasSize canvas_size{};
    model::FrameRate frame_rate{};
    std::vector<model::CompositionLayer> layers;
    std::map<std::filesystem::path, creative_suite::media::RgbaFramePtr> still_frames;
};

struct MotionExportPerformanceSummary {
    bool gpu_composition_requested = false;
    bool gpu_surface_available = false;
    std::uint64_t elapsed_nanoseconds = 0;
    std::uint64_t frames_rendered = 0;
    std::uint64_t render_count = 0;
    std::uint64_t render_total_nanoseconds = 0;
    std::uint64_t render_maximum_nanoseconds = 0;
    std::uint64_t write_count = 0;
    std::uint64_t write_total_nanoseconds = 0;
    std::uint64_t write_maximum_nanoseconds = 0;
    CompositionGpuMetrics gpu;
};

struct MotionExportRenderOptions {
    bool gpu_composition_enabled = false;
    // Created/destroyed by the GUI thread owner and kept alive until the export
    // worker has released its context and compositor.
    QOffscreenSurface* gpu_surface = nullptr;
};

class MotionExportCancelled final : public std::exception {
public:
    [[nodiscard]] const char* what() const noexcept override
    {
        return "Motion Studio video export was canceled.";
    }
};

class MotionVideoExporter final {
public:
    using ProgressCallback = std::function<void(int)>;

    // Writes the full layer extent from frame zero. The snapshot and its media
    // references must remain valid for the duration of this synchronous call.
    // When GPU composition is enabled, call on a worker thread and keep the
    // GUI-created offscreen surface alive until this call returns.
    static void exportVideo(
        const MotionExportSnapshot& snapshot,
        const MotionExportSettings& settings,
        const std::atomic_bool& cancel_requested,
        ProgressCallback report_progress = {},
        MotionExportPerformanceSummary* performance_summary = nullptr,
        MotionExportRenderOptions render_options = {});
};

struct MotionExportResult {
    bool succeeded = false;
    bool cancelled = false;
    std::string error_message;
    int error_code = -1;
    std::filesystem::path output_path;
};

class MotionVideoExportWorker final : public QThread {
public:
    using ProgressHandler = std::function<void(int)>;
    using FinishedHandler = std::function<void(MotionExportResult)>;

    MotionVideoExportWorker(
        QObject* receiver,
        MotionExportSnapshot snapshot,
        MotionExportSettings settings,
        ProgressHandler progress_handler,
        FinishedHandler finished_handler,
        MotionExportRenderOptions render_options = {});
    ~MotionVideoExportWorker() override;

    void cancel() noexcept;
    void cancelAndWait();

protected:
    void run() override;

private:
    QPointer<QObject> receiver_;
    MotionExportSnapshot snapshot_;
    MotionExportSettings settings_;
    MotionExportRenderOptions render_options_;
    std::shared_ptr<std::atomic_bool> cancel_requested_;
    ProgressHandler progress_handler_;
    FinishedHandler finished_handler_;
};

} // namespace motion::ui
