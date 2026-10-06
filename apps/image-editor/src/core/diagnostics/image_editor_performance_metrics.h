#pragma once

#include <QJsonObject>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace image_editor {

enum class ImageEditorPerformanceStage : std::size_t {
    OperationReplay,
    PaintStroke,
    EraseStroke,
    Shape,
    Text,
    RasterImage,
    Transform,
    MaskApplication,
    LayerComposition,
    GroupComposition,
    Composite,
    SelectedLayerRender,
    SelectedGroupRender,
    LayerThumbnail,
    GroupThumbnail,
    MaskThumbnail,
    ThumbnailCacheHit,
    ThumbnailCacheMiss,
    CanvasPaint,
    ExportRender,
    JpegFlatten,
    ExportEncode,
    BenchmarkIteration,
    Count,
};

struct ImageEditorTimingSummary {
    std::uint64_t count = 0;
    std::uint64_t total_nanoseconds = 0;
    std::uint64_t maximum_nanoseconds = 0;
    std::uint64_t p50_nanoseconds = 0;
    std::uint64_t p95_nanoseconds = 0;
};

struct ImageEditorPerformanceSnapshot {
    std::array<ImageEditorTimingSummary,
        static_cast<std::size_t>(ImageEditorPerformanceStage::Count)> timings{};

    [[nodiscard]] bool hasActivity() const noexcept;
    [[nodiscard]] const ImageEditorTimingSummary& timing(
        ImageEditorPerformanceStage stage) const noexcept;
};

class ImageEditorPerformanceMetrics final {
public:
    static ImageEditorPerformanceMetrics& instance() noexcept;

    void setEnabled(bool enabled) noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    void reset() noexcept;
    void record(ImageEditorPerformanceStage stage,
                std::uint64_t duration_nanoseconds = 0) noexcept;
    [[nodiscard]] ImageEditorPerformanceSnapshot snapshot() const noexcept;

    static constexpr std::size_t maximum_samples_per_stage = 2048;

private:
    struct TimingBucket {
        std::uint64_t count = 0;
        std::uint64_t total_nanoseconds = 0;
        std::uint64_t maximum_nanoseconds = 0;
        std::array<std::uint64_t, maximum_samples_per_stage> samples{};
        std::size_t sample_count = 0;
        std::size_t next_sample = 0;
    };

    std::atomic_bool enabled_{false};
    mutable std::mutex mutex_;
    std::array<TimingBucket,
        static_cast<std::size_t>(ImageEditorPerformanceStage::Count)> timings_{};
};

class ImageEditorPerformanceScope final {
public:
    ImageEditorPerformanceScope(ImageEditorPerformanceMetrics& metrics,
                                ImageEditorPerformanceStage stage) noexcept;
    ~ImageEditorPerformanceScope();

    ImageEditorPerformanceScope(const ImageEditorPerformanceScope&) = delete;
    ImageEditorPerformanceScope& operator=(const ImageEditorPerformanceScope&) = delete;

    void setStage(ImageEditorPerformanceStage stage) noexcept {
        if (enabled_) stage_ = stage;
    }

private:
    using Clock = std::chrono::steady_clock;
    ImageEditorPerformanceMetrics* metrics_ = nullptr;
    ImageEditorPerformanceStage stage_ = ImageEditorPerformanceStage::Composite;
    Clock::time_point started_{};
    bool enabled_ = false;
};

[[nodiscard]] const char* imageEditorPerformanceStageName(
    ImageEditorPerformanceStage stage) noexcept;
[[nodiscard]] QJsonObject imageEditorPerformanceSnapshotToJson(
    const ImageEditorPerformanceSnapshot& snapshot);

} // namespace image_editor
