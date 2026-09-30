#include "performance_metrics.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace motion::diagnostics {
namespace {

std::uint64_t percentile(std::deque<std::uint64_t> values, double fraction)
{
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(
        std::ceil(fraction * static_cast<double>(values.size()))) - 1;
    return values[std::min(index, values.size() - 1)];
}

} // namespace

bool PreviewMetricsSnapshot::hasActivity() const noexcept
{
    if (requests != 0 || rendered_frames != 0 || coalesced_requests != 0 ||
        stale_results != 0) return true;
    const auto has_timing = [](const auto& values) {
        return std::any_of(values.begin(), values.end(), [](const TimingSummary& timing) {
            return timing.count != 0;
        });
    };
    return has_timing(timings) || has_timing(effect_timings);
}

PerformanceMetrics& PerformanceMetrics::instance() noexcept
{
    static PerformanceMetrics metrics;
    return metrics;
}

void PerformanceMetrics::setEnabled(bool enabled) noexcept
{
    std::lock_guard lock(mutex_);
    enabled_ = enabled;
    if (!enabled_) {
        requests_ = rendered_frames_ = coalesced_requests_ = stale_results_ = 0;
        timings_ = {};
        effect_timings_ = {};
        request_started_.clear();
    }
}

bool PerformanceMetrics::enabled() const noexcept
{
    std::lock_guard lock(mutex_);
    return enabled_;
}

void PerformanceMetrics::reset() noexcept
{
    std::lock_guard lock(mutex_);
    requests_ = rendered_frames_ = coalesced_requests_ = stale_results_ = 0;
    timings_ = {};
    effect_timings_ = {};
    request_started_.clear();
}

void PerformanceMetrics::recordRequest(std::uint64_t generation) noexcept
{
    std::lock_guard lock(mutex_);
    if (!enabled_) return;
    ++requests_;
    request_started_[generation] = std::chrono::steady_clock::now();
    while (request_started_.size() > maximum_percentile_samples_) {
        request_started_.erase(request_started_.begin());
    }
}

void PerformanceMetrics::discardRequest(std::uint64_t generation) noexcept
{
    std::lock_guard lock(mutex_);
    request_started_.erase(generation);
}

void PerformanceMetrics::recordCoalescedRequest(std::uint64_t generation) noexcept
{
    std::lock_guard lock(mutex_);
    if (!enabled_) return;
    ++coalesced_requests_;
    request_started_.erase(generation);
}

void PerformanceMetrics::recordStaleResult(std::uint64_t generation) noexcept
{
    std::lock_guard lock(mutex_);
    if (!enabled_) return;
    ++stale_results_;
    request_started_.erase(generation);
}

void PerformanceMetrics::recordRenderedFrame() noexcept
{
    std::lock_guard lock(mutex_);
    if (enabled_) ++rendered_frames_;
}

void PerformanceMetrics::recordTiming(
    PreviewTimingStage stage,
    std::uint64_t duration_nanoseconds) noexcept
{
    const auto index = static_cast<std::size_t>(stage);
    if (index >= timings_.size()) return;
    std::lock_guard lock(mutex_);
    if (!enabled_) return;
    auto& timing = timings_[index];
    ++timing.count;
    timing.total_nanoseconds += duration_nanoseconds;
    timing.maximum_nanoseconds = std::max(timing.maximum_nanoseconds, duration_nanoseconds);
    if (timing.percentile_samples.size() == maximum_percentile_samples_) {
        timing.percentile_samples.pop_front();
    }
    timing.percentile_samples.push_back(duration_nanoseconds);
}

void PerformanceMetrics::recordEffectTiming(
    PreviewEffectKind effect,
    std::uint64_t duration_nanoseconds) noexcept
{
    const auto index = static_cast<std::size_t>(effect);
    if (index >= effect_timings_.size()) return;
    std::lock_guard lock(mutex_);
    if (!enabled_) return;
    auto& timing = effect_timings_[index];
    ++timing.count;
    timing.total_nanoseconds += duration_nanoseconds;
    timing.maximum_nanoseconds = std::max(timing.maximum_nanoseconds, duration_nanoseconds);
    if (timing.percentile_samples.size() == maximum_percentile_samples_) {
        timing.percentile_samples.pop_front();
    }
    timing.percentile_samples.push_back(duration_nanoseconds);
}

void PerformanceMetrics::recordViewerPaint(std::uint64_t generation) noexcept
{
    std::chrono::steady_clock::time_point started;
    {
        std::lock_guard lock(mutex_);
        if (!enabled_) return;
        const auto found = request_started_.find(generation);
        if (found == request_started_.end()) return;
        started = found->second;
        request_started_.erase(found);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count();
    if (elapsed < 0) return;
    recordTiming(PreviewTimingStage::RequestToViewerPaint,
                 static_cast<std::uint64_t>(elapsed));
}

std::optional<PreviewMetricsSnapshot> PerformanceMetrics::takeSnapshotAndReset() noexcept
{
    std::lock_guard lock(mutex_);
    if (!enabled_) return std::nullopt;
    PreviewMetricsSnapshot result;
    result.requests = std::exchange(requests_, 0);
    result.rendered_frames = std::exchange(rendered_frames_, 0);
    result.coalesced_requests = std::exchange(coalesced_requests_, 0);
    result.stale_results = std::exchange(stale_results_, 0);
    for (std::size_t index = 0; index < timings_.size(); ++index) {
        auto& source = timings_[index];
        auto& destination = result.timings[index];
        destination.count = std::exchange(source.count, 0);
        destination.total_nanoseconds = std::exchange(source.total_nanoseconds, 0);
        destination.maximum_nanoseconds = std::exchange(source.maximum_nanoseconds, 0);
        destination.p95_nanoseconds = percentile(source.percentile_samples, 0.95);
        destination.p99_nanoseconds = percentile(source.percentile_samples, 0.99);
        source.percentile_samples.clear();
    }
    for (std::size_t index = 0; index < effect_timings_.size(); ++index) {
        auto& source = effect_timings_[index];
        auto& destination = result.effect_timings[index];
        destination.count = std::exchange(source.count, 0);
        destination.total_nanoseconds = std::exchange(source.total_nanoseconds, 0);
        destination.maximum_nanoseconds = std::exchange(source.maximum_nanoseconds, 0);
        destination.p95_nanoseconds = percentile(source.percentile_samples, 0.95);
        destination.p99_nanoseconds = percentile(source.percentile_samples, 0.99);
        source.percentile_samples.clear();
    }
    if (!result.hasActivity()) return std::nullopt;
    return result;
}

const char* previewTimingStageName(PreviewTimingStage stage) noexcept
{
    switch (stage) {
    case PreviewTimingStage::Decode: return "decode";
    case PreviewTimingStage::TextShapeRasterization: return "text_shape_rasterization";
    case PreviewTimingStage::Effects: return "effects";
    case PreviewTimingStage::Composition: return "composition";
    case PreviewTimingStage::FrameRender: return "frame_render";
    case PreviewTimingStage::RequestToViewerPaint: return "request_to_viewer_paint";
    case PreviewTimingStage::Count: break;
    }
    return "unknown";
}

const char* previewEffectKindName(PreviewEffectKind effect) noexcept
{
    switch (effect) {
    case PreviewEffectKind::GaussianBlur: return "gaussian_blur";
    case PreviewEffectKind::ColorAdjustment: return "color_adjustment";
    case PreviewEffectKind::Count: break;
    }
    return "unknown_effect";
}

creative_suite::diagnostics::Context makePreviewPerformanceContext(
    const PreviewMetricsSnapshot& metrics,
    const system_monitor::PerformanceSnapshot& resources,
    const PreviewLogMetadata& metadata)
{
    const auto optionalNumber = [](const auto& value) {
        return value.has_value() ? std::to_string(*value) : std::string("N/A");
    };
    creative_suite::diagnostics::Context context{
        {"schema_version", "3"},
        {"interval_ms", "1000"},
        {"process_cpu_percent", optionalNumber(resources.process_cpu_percent)},
        {"process_working_set_bytes", optionalNumber(resources.process_working_set_bytes)},
        {"process_private_usage_bytes", optionalNumber(resources.process_private_usage_bytes)},
        {"system_total_bytes", optionalNumber(resources.system_total_bytes)},
        {"system_available_bytes", optionalNumber(resources.system_available_bytes)},
        {"preview_requests", std::to_string(metrics.requests)},
        {"rendered_frames", std::to_string(metrics.rendered_frames)},
        {"coalesced_requests", std::to_string(metrics.coalesced_requests)},
        {"stale_results", std::to_string(metrics.stale_results)},
        {"canvas_width", std::to_string(metadata.canvas_width)},
        {"canvas_height", std::to_string(metadata.canvas_height)},
        {"frame_rate_numerator", std::to_string(metadata.frame_rate_numerator)},
        {"frame_rate_denominator", std::to_string(metadata.frame_rate_denominator)},
        {"layer_count", std::to_string(metadata.layer_count)},
        {"effect_count", std::to_string(metadata.effect_count)},
        {"effect_worker_count", std::to_string(metadata.effect_worker_count)}};

    for (std::size_t index = 0; index < metrics.timings.size(); ++index) {
        const auto& timing = metrics.timings[index];
        const auto prefix = std::string(previewTimingStageName(
            static_cast<PreviewTimingStage>(index)));
        const auto average = timing.count == 0 ? 0.0 :
            static_cast<double>(timing.total_nanoseconds) /
                static_cast<double>(timing.count) / 1'000'000.0;
        const auto milliseconds = [](std::uint64_t ns) {
            return std::to_string(static_cast<double>(ns) / 1'000'000.0);
        };
        context.emplace_back(prefix + "_count", std::to_string(timing.count));
        context.emplace_back(prefix + "_average_ms", std::to_string(average));
        context.emplace_back(prefix + "_maximum_ms", milliseconds(timing.maximum_nanoseconds));
        context.emplace_back(prefix + "_p95_ms", milliseconds(timing.p95_nanoseconds));
        context.emplace_back(prefix + "_p99_ms", milliseconds(timing.p99_nanoseconds));
    }
    for (std::size_t index = 0; index < metrics.effect_timings.size(); ++index) {
        const auto& timing = metrics.effect_timings[index];
        const auto prefix = std::string(previewEffectKindName(
            static_cast<PreviewEffectKind>(index)));
        const auto average = timing.count == 0 ? 0.0 :
            static_cast<double>(timing.total_nanoseconds) /
                static_cast<double>(timing.count) / 1'000'000.0;
        const auto milliseconds = [](std::uint64_t ns) {
            return std::to_string(static_cast<double>(ns) / 1'000'000.0);
        };
        context.emplace_back(prefix + "_apply_count", std::to_string(timing.count));
        context.emplace_back(prefix + "_average_ms", std::to_string(average));
        context.emplace_back(prefix + "_maximum_ms", milliseconds(timing.maximum_nanoseconds));
        context.emplace_back(prefix + "_p95_ms", milliseconds(timing.p95_nanoseconds));
        context.emplace_back(prefix + "_p99_ms", milliseconds(timing.p99_nanoseconds));
    }
    return context;
}

} // namespace motion::diagnostics
