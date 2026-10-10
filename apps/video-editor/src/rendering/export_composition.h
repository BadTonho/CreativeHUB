#pragma once

#include "rendering/offline_export_renderer.h"
#include <creative_suite/media/native_video_frame.h>
#include <chrono>

namespace rendering::detail {
namespace composition = creative_suite::composition;

class ExportTimedScope {
public:
    explicit ExportTimedScope(std::uint64_t& total) : total_(total) {}
    ~ExportTimedScope() {
        total_ += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start_).count());
    }
private:
    std::uint64_t& total_;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

class ExportMetricsScope {
public:
    ExportMetricsScope(const RenderJob&, const OfflineExportOptions&);
    ~ExportMetricsScope();
    OfflineExportMetrics metrics;
private:
    const RenderJob& job_;
    const OfflineExportOptions& options_;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

class ExportComposition {
public:
    ExportComposition(const RenderJob&, const OfflineExportOptions&, OfflineExportMetrics&);
    composition::OpenGlFrameCompositor* graphBackend();
    bool nativeDelivery() const noexcept;
    void setCpuSourceRecovery(bool enabled) noexcept { cpu_source_recovery_ = enabled; }
    std::optional<creative_suite::media::RgbaFrame> compose(const std::vector<composition::CompositionLayer>&,
        std::int64_t output_frame, std::int64_t timeline_frame, const std::atomic_bool& canceled,
        creative_suite::media::NativeVideoFramePool* = nullptr,
        creative_suite::media::NativeVideoFramePtr* native_output = nullptr);
private:
    void fallback(const composition::OpenGlCompositionResult&, std::int64_t, std::int64_t, bool rgba_recovery = false);
    const RenderJob& job_;
    const OfflineExportOptions& options_;
    OfflineExportMetrics& metrics_;
    std::unique_ptr<ExportGpuCompositor> gpu_;
    std::unique_ptr<composition::OpenGlFrameCompositor> graph_gpu_;
    std::unique_ptr<composition::OpenGlFrameCompositor> native_gpu_;
    bool failed_ = false, warned_ = false;
    bool cpu_source_recovery_ = false;
};

// Internal control flow: the renderer must reevaluate original sources when
// GPU composition cannot consume a native frame or a completed Fusion lease.
struct CpuCompositionSourcesRequired {};

} // namespace rendering::detail
