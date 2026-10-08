#pragma once

#include "rendering/offline_export_renderer.h"
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
    std::optional<creative_suite::media::RgbaFrame> compose(const std::vector<composition::CompositionLayer>&,
        std::int64_t output_frame, std::int64_t timeline_frame, const std::atomic_bool& canceled);
private:
    void fallback(const composition::OpenGlCompositionResult&, std::int64_t, std::int64_t);
    const RenderJob& job_;
    const OfflineExportOptions& options_;
    OfflineExportMetrics& metrics_;
    std::unique_ptr<ExportGpuCompositor> gpu_;
    bool failed_ = false, warned_ = false;
};

} // namespace rendering::detail
