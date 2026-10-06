#include "image_editor_performance_metrics.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace image_editor {
namespace {

std::uint64_t percentile(
    std::array<std::uint64_t, ImageEditorPerformanceMetrics::maximum_samples_per_stage> values,
    std::size_t count, double fraction) noexcept {
    if (count == 0) return 0;
    std::sort(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count));
    const auto rank = static_cast<std::size_t>(
        std::ceil(fraction * static_cast<double>(count)));
    return values[std::min(count - 1, rank == 0 ? 0 : rank - 1)];
}

double milliseconds(std::uint64_t nanoseconds) noexcept {
    return static_cast<double>(nanoseconds) / 1'000'000.0;
}

} // namespace

bool ImageEditorPerformanceSnapshot::hasActivity() const noexcept {
    return std::any_of(timings.begin(), timings.end(), [](const auto& timing) {
        return timing.count != 0;
    });
}

const ImageEditorTimingSummary& ImageEditorPerformanceSnapshot::timing(
    ImageEditorPerformanceStage stage) const noexcept {
    static const ImageEditorTimingSummary empty{};
    const auto index = static_cast<std::size_t>(stage);
    return index < timings.size() ? timings[index] : empty;
}

ImageEditorPerformanceMetrics& ImageEditorPerformanceMetrics::instance() noexcept {
    static ImageEditorPerformanceMetrics metrics;
    return metrics;
}

void ImageEditorPerformanceMetrics::setEnabled(bool enabled) noexcept {
    enabled_.store(enabled, std::memory_order_release);
}

bool ImageEditorPerformanceMetrics::enabled() const noexcept {
    return enabled_.load(std::memory_order_acquire);
}

void ImageEditorPerformanceMetrics::reset() noexcept {
    std::lock_guard lock(mutex_);
    timings_ = {};
}

void ImageEditorPerformanceMetrics::record(
    ImageEditorPerformanceStage stage,
    std::uint64_t duration_nanoseconds) noexcept {
    if (!enabled()) return;
    const auto index = static_cast<std::size_t>(stage);
    if (index >= timings_.size()) return;

    std::lock_guard lock(mutex_);
    if (!enabled()) return;
    auto& timing = timings_[index];
    ++timing.count;
    timing.total_nanoseconds += duration_nanoseconds;
    timing.maximum_nanoseconds = std::max(
        timing.maximum_nanoseconds, duration_nanoseconds);
    timing.samples[timing.next_sample] = duration_nanoseconds;
    timing.next_sample = (timing.next_sample + 1) % maximum_samples_per_stage;
    timing.sample_count = std::min(timing.sample_count + 1, maximum_samples_per_stage);
}

ImageEditorPerformanceSnapshot ImageEditorPerformanceMetrics::snapshot() const noexcept {
    ImageEditorPerformanceSnapshot result;
    std::lock_guard lock(mutex_);
    for (std::size_t index = 0; index < timings_.size(); ++index) {
        const auto& source = timings_[index];
        auto& target = result.timings[index];
        target.count = source.count;
        target.total_nanoseconds = source.total_nanoseconds;
        target.maximum_nanoseconds = source.maximum_nanoseconds;
        target.p50_nanoseconds = percentile(
            source.samples, source.sample_count, 0.50);
        target.p95_nanoseconds = percentile(
            source.samples, source.sample_count, 0.95);
    }
    return result;
}

ImageEditorPerformanceScope::ImageEditorPerformanceScope(
    ImageEditorPerformanceMetrics& metrics,
    ImageEditorPerformanceStage stage) noexcept
    : metrics_(metrics.enabled() ? &metrics : nullptr),
      stage_(stage),
      enabled_(metrics_ != nullptr) {
    if (enabled_) started_ = Clock::now();
}

ImageEditorPerformanceScope::~ImageEditorPerformanceScope() {
    if (metrics_ == nullptr || !enabled_) return;
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - started_).count();
    metrics_->record(stage_, elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0U);
}

const char* imageEditorPerformanceStageName(
    ImageEditorPerformanceStage stage) noexcept {
    switch (stage) {
    case ImageEditorPerformanceStage::OperationReplay: return "operation_replay";
    case ImageEditorPerformanceStage::PaintStroke: return "paint_stroke";
    case ImageEditorPerformanceStage::EraseStroke: return "erase_stroke";
    case ImageEditorPerformanceStage::LinearGradient: return "linear_gradient";
    case ImageEditorPerformanceStage::Shape: return "shape";
    case ImageEditorPerformanceStage::Text: return "text";
    case ImageEditorPerformanceStage::RasterImage: return "raster_image";
    case ImageEditorPerformanceStage::Transform: return "transform";
    case ImageEditorPerformanceStage::MaskApplication: return "mask_application";
    case ImageEditorPerformanceStage::LayerComposition: return "layer_composition";
    case ImageEditorPerformanceStage::GroupComposition: return "group_composition";
    case ImageEditorPerformanceStage::Composite: return "composite";
    case ImageEditorPerformanceStage::SelectedLayerRender: return "selected_layer_render";
    case ImageEditorPerformanceStage::SelectedGroupRender: return "selected_group_render";
    case ImageEditorPerformanceStage::LayerThumbnail: return "layer_thumbnail";
    case ImageEditorPerformanceStage::GroupThumbnail: return "group_thumbnail";
    case ImageEditorPerformanceStage::MaskThumbnail: return "mask_thumbnail";
    case ImageEditorPerformanceStage::ThumbnailCacheHit: return "thumbnail_cache_hit";
    case ImageEditorPerformanceStage::ThumbnailCacheMiss: return "thumbnail_cache_miss";
    case ImageEditorPerformanceStage::LayerRasterCacheHit: return "layer_raster_cache_hit";
    case ImageEditorPerformanceStage::LayerRasterCacheMiss: return "layer_raster_cache_miss";
    case ImageEditorPerformanceStage::LayerRasterCacheBypass: return "layer_raster_cache_bypass";
    case ImageEditorPerformanceStage::CanvasPaint: return "canvas_paint";
    case ImageEditorPerformanceStage::ExportRender: return "export_render";
    case ImageEditorPerformanceStage::JpegFlatten: return "jpeg_flatten";
    case ImageEditorPerformanceStage::ExportEncode: return "export_encode";
    case ImageEditorPerformanceStage::BenchmarkIteration: return "benchmark_iteration";
    case ImageEditorPerformanceStage::Count: break;
    }
    return "unknown";
}

QJsonObject imageEditorPerformanceSnapshotToJson(
    const ImageEditorPerformanceSnapshot& snapshot) {
    QJsonObject stages;
    for (std::size_t index = 0; index < snapshot.timings.size(); ++index) {
        const auto stage = static_cast<ImageEditorPerformanceStage>(index);
        const auto& timing = snapshot.timings[index];
        if (timing.count == 0) continue;
        stages.insert(QString::fromLatin1(imageEditorPerformanceStageName(stage)),
            QJsonObject{
                {QStringLiteral("count"), static_cast<qint64>(timing.count)},
                {QStringLiteral("average_ms"), milliseconds(timing.total_nanoseconds) /
                    static_cast<double>(timing.count)},
                {QStringLiteral("p50_ms"), milliseconds(timing.p50_nanoseconds)},
                {QStringLiteral("p95_ms"), milliseconds(timing.p95_nanoseconds)},
                {QStringLiteral("maximum_ms"), milliseconds(timing.maximum_nanoseconds)},
                {QStringLiteral("total_ms"), milliseconds(timing.total_nanoseconds)},
            });
    }
    return QJsonObject{{QStringLiteral("stages"), stages}};
}

} // namespace image_editor
