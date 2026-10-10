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
    if (delivery.texture_deliveries || delivery.rgba_deliveries ||
        delivery.texture_presented || delivery.rgba_presented ||
        delivery.presentation_recoveries || delivery.busy_drops || delivery.busy_retries) return true;
    if (requests != 0 || rendered_frames != 0 || coalesced_requests != 0 ||
        stale_results != 0 || timestamp_seek_attempts != 0 ||
        forward_decode_attempts != 0 || discarded_intermediate_frames != 0 ||
        gpu_composition_frames != 0 || gpu_composition_fallbacks != 0 ||
        gpu_composition_failures != 0 || gpu_color_adjustment_effects != 0 ||
        gpu_color_adjustment_fallbacks != 0 || gpu_color_adjustment_failures != 0 ||
        gpu_gaussian_blur_effects != 0 || gpu_gaussian_blur_fallbacks != 0 ||
        gpu_gaussian_blur_failures != 0) return true;
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
        timestamp_seek_attempts_ = timestamp_seek_successes_ = timestamp_seek_failures_ = 0;
        forward_decode_attempts_ = forward_decode_completions_ = 0;
        forward_decode_fallbacks_ = discarded_intermediate_frames_ = 0;
        gpu_composition_frames_ = gpu_composition_fallbacks_ = gpu_composition_failures_ = 0;
        gpu_composition_uploaded_bytes_ = gpu_composition_readback_bytes_ = 0;
        gpu_color_adjustment_effects_ = gpu_color_adjustment_fallbacks_ =
            gpu_color_adjustment_failures_ = 0;
        gpu_gaussian_blur_effects_ = gpu_gaussian_blur_fallbacks_ =
            gpu_gaussian_blur_failures_ = 0;
        delivery_ = {};
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
    delivery_ = {};
    requests_ = rendered_frames_ = coalesced_requests_ = stale_results_ = 0;
    timestamp_seek_attempts_ = timestamp_seek_successes_ = timestamp_seek_failures_ = 0;
    forward_decode_attempts_ = forward_decode_completions_ = 0;
    forward_decode_fallbacks_ = discarded_intermediate_frames_ = 0;
    gpu_composition_frames_ = gpu_composition_fallbacks_ = gpu_composition_failures_ = 0;
    gpu_composition_uploaded_bytes_ = gpu_composition_readback_bytes_ = 0;
    gpu_color_adjustment_effects_ = gpu_color_adjustment_fallbacks_ =
        gpu_color_adjustment_failures_ = 0;
    gpu_gaussian_blur_effects_ = gpu_gaussian_blur_fallbacks_ =
        gpu_gaussian_blur_failures_ = 0;
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

void PerformanceMetrics::recordTimestampSeek(
    bool succeeded,
    std::uint64_t duration_nanoseconds) noexcept
{
    std::lock_guard lock(mutex_);
    if (!enabled_) return;
    ++timestamp_seek_attempts_;
    if (succeeded) ++timestamp_seek_successes_;
    else ++timestamp_seek_failures_;
    recordTimingLocked(PreviewTimingStage::TimestampSeek, duration_nanoseconds);
}

void PerformanceMetrics::recordForwardDecode(
    bool completed,
    std::uint64_t duration_nanoseconds) noexcept
{
    std::lock_guard lock(mutex_);
    if (!enabled_) return;
    ++forward_decode_attempts_;
    if (completed) ++forward_decode_completions_;
    recordTimingLocked(PreviewTimingStage::ForwardDecode, duration_nanoseconds);
}

void PerformanceMetrics::recordForwardDecodeFallback() noexcept
{
    std::lock_guard lock(mutex_);
    if (enabled_) ++forward_decode_fallbacks_;
}

void PerformanceMetrics::recordDiscardedIntermediateFrame() noexcept
{
    std::lock_guard lock(mutex_);
    if (enabled_) ++discarded_intermediate_frames_;
}

void PerformanceMetrics::recordGpuComposition(
    bool completed,
    bool failed,
    std::uint64_t uploaded_bytes,
    std::uint64_t readback_bytes,
    std::uint64_t upload_nanoseconds,
    std::uint64_t draw_submission_nanoseconds,
    std::uint64_t readback_nanoseconds) noexcept
{
    std::lock_guard lock(mutex_);
    if (!enabled_) return;
    if (completed) ++gpu_composition_frames_;
    else ++gpu_composition_fallbacks_;
    if (failed) ++gpu_composition_failures_;
    gpu_composition_uploaded_bytes_ += uploaded_bytes;
    gpu_composition_readback_bytes_ += readback_bytes;
    recordTimingLocked(PreviewTimingStage::GpuCompositionUpload, upload_nanoseconds);
    recordTimingLocked(PreviewTimingStage::GpuCompositionDrawSubmission,
                       draw_submission_nanoseconds);
    recordTimingLocked(PreviewTimingStage::GpuCompositionReadback, readback_nanoseconds);
}

void PerformanceMetrics::recordGpuColorAdjustment(
    std::uint64_t applied_effects,
    std::uint64_t fallback_effects,
    bool failed,
    std::uint64_t submission_nanoseconds) noexcept
{
    std::lock_guard lock(mutex_);
    if (!enabled_) return;
    gpu_color_adjustment_effects_ += applied_effects;
    gpu_color_adjustment_fallbacks_ += fallback_effects;
    if (failed) ++gpu_color_adjustment_failures_;
    if (applied_effects != 0)
        recordTimingLocked(PreviewTimingStage::GpuColorAdjustment, submission_nanoseconds);
}

void PerformanceMetrics::recordGpuGaussianBlur(
    std::uint64_t applied_effects,
    std::uint64_t fallback_effects,
    bool failed,
    std::uint64_t submission_nanoseconds) noexcept
{
    std::lock_guard lock(mutex_);
    if (!enabled_) return;
    gpu_gaussian_blur_effects_ += applied_effects;
    gpu_gaussian_blur_fallbacks_ += fallback_effects;
    if (failed) ++gpu_gaussian_blur_failures_;
    if (applied_effects != 0)
        recordTimingLocked(PreviewTimingStage::GpuGaussianBlur, submission_nanoseconds);
}

void PerformanceMetrics::recordTiming(
    PreviewTimingStage stage,
    std::uint64_t duration_nanoseconds) noexcept
{
    const auto index = static_cast<std::size_t>(stage);
    if (index >= timings_.size()) return;
    std::lock_guard lock(mutex_);
    if (!enabled_) return;
    recordTimingLocked(stage, duration_nanoseconds);
}

void PerformanceMetrics::recordTimingLocked(
    PreviewTimingStage stage,
    std::uint64_t duration_nanoseconds) noexcept
{
    const auto index = static_cast<std::size_t>(stage);
    if (index >= timings_.size()) return;
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

void PerformanceMetrics::recordDelivery(bool texture) noexcept {
    std::lock_guard lock(mutex_);
    if (enabled_) ++(texture ? delivery_.texture_deliveries : delivery_.rgba_deliveries);
}
void PerformanceMetrics::recordPresentation(bool texture) noexcept {
    std::lock_guard lock(mutex_);
    if (enabled_) ++(texture ? delivery_.texture_presented : delivery_.rgba_presented);
}
void PerformanceMetrics::recordPresentationRecovery() noexcept {
    std::lock_guard lock(mutex_);
    if (enabled_) ++delivery_.presentation_recoveries;
}
void PerformanceMetrics::recordTexturePool(std::uint64_t bytes, unsigned occupancy,
                                         bool busy, bool playback) noexcept {
    std::lock_guard lock(mutex_);
    if (!enabled_) return;
    delivery_.pool_bytes = bytes;
    delivery_.pool_occupancy = occupancy;
    delivery_.pool_peak_bytes = std::max(delivery_.pool_peak_bytes, bytes);
    delivery_.pool_peak_occupancy = std::max(delivery_.pool_peak_occupancy, std::uint64_t(occupancy));
    if (busy) ++(playback ? delivery_.busy_drops : delivery_.busy_retries);
}

std::optional<PreviewMetricsSnapshot> PerformanceMetrics::takeSnapshotAndReset() noexcept
{
    std::lock_guard lock(mutex_);
    if (!enabled_) return std::nullopt;
    PreviewMetricsSnapshot result;
    result.delivery = delivery_;
    delivery_ = {};
    result.requests = std::exchange(requests_, 0);
    result.rendered_frames = std::exchange(rendered_frames_, 0);
    result.coalesced_requests = std::exchange(coalesced_requests_, 0);
    result.stale_results = std::exchange(stale_results_, 0);
    result.timestamp_seek_attempts = std::exchange(timestamp_seek_attempts_, 0);
    result.timestamp_seek_successes = std::exchange(timestamp_seek_successes_, 0);
    result.timestamp_seek_failures = std::exchange(timestamp_seek_failures_, 0);
    result.forward_decode_attempts = std::exchange(forward_decode_attempts_, 0);
    result.forward_decode_completions = std::exchange(forward_decode_completions_, 0);
    result.forward_decode_fallbacks = std::exchange(forward_decode_fallbacks_, 0);
    result.discarded_intermediate_frames =
        std::exchange(discarded_intermediate_frames_, 0);
    result.gpu_composition_frames = std::exchange(gpu_composition_frames_, 0);
    result.gpu_composition_fallbacks = std::exchange(gpu_composition_fallbacks_, 0);
    result.gpu_composition_failures = std::exchange(gpu_composition_failures_, 0);
    result.gpu_composition_uploaded_bytes =
        std::exchange(gpu_composition_uploaded_bytes_, 0);
    result.gpu_composition_readback_bytes =
        std::exchange(gpu_composition_readback_bytes_, 0);
    result.gpu_color_adjustment_effects =
        std::exchange(gpu_color_adjustment_effects_, 0);
    result.gpu_color_adjustment_fallbacks =
        std::exchange(gpu_color_adjustment_fallbacks_, 0);
    result.gpu_color_adjustment_failures =
        std::exchange(gpu_color_adjustment_failures_, 0);
    result.gpu_gaussian_blur_effects = std::exchange(gpu_gaussian_blur_effects_, 0);
    result.gpu_gaussian_blur_fallbacks = std::exchange(gpu_gaussian_blur_fallbacks_, 0);
    result.gpu_gaussian_blur_failures = std::exchange(gpu_gaussian_blur_failures_, 0);
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
    case PreviewTimingStage::TimestampSeek: return "timestamp_seek";
    case PreviewTimingStage::ForwardDecode: return "forward_decode";
    case PreviewTimingStage::TextShapeRasterization: return "text_shape_rasterization";
    case PreviewTimingStage::Effects: return "effects";
    case PreviewTimingStage::Composition: return "composition";
    case PreviewTimingStage::GpuCompositionUpload: return "gpu_composition_upload";
    case PreviewTimingStage::GpuCompositionDrawSubmission: return "gpu_composition_draw_submission";
    case PreviewTimingStage::GpuCompositionReadback: return "gpu_composition_readback";
    case PreviewTimingStage::GpuColorAdjustment: return "gpu_color_adjustment";
    case PreviewTimingStage::GpuGaussianBlur: return "gpu_gaussian_blur";
    case PreviewTimingStage::GpuProducerFenceSubmission: return "gpu_producer_fence_submission";
    case PreviewTimingStage::GpuViewerWaitSubmission: return "gpu_viewer_wait_submission";
    case PreviewTimingStage::GpuViewerDrawSubmission: return "gpu_viewer_draw_submission";
    case PreviewTimingStage::GpuViewerFenceSubmission: return "gpu_viewer_fence_submission";
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
        {"schema_version", "8"},
        {"interval_ms", "1000"},
        {"gpu_preview_texture_deliveries", std::to_string(metrics.delivery.texture_deliveries)},
        {"gpu_preview_rgba_deliveries", std::to_string(metrics.delivery.rgba_deliveries)},
        {"gpu_preview_texture_presented", std::to_string(metrics.delivery.texture_presented)},
        {"gpu_preview_rgba_presented", std::to_string(metrics.delivery.rgba_presented)},
        {"gpu_preview_presentation_recoveries", std::to_string(metrics.delivery.presentation_recoveries)},
        {"gpu_preview_pool_bytes", std::to_string(metrics.delivery.pool_bytes)},
        {"gpu_preview_pool_peak_bytes", std::to_string(metrics.delivery.pool_peak_bytes)},
        {"gpu_preview_pool_occupancy", std::to_string(metrics.delivery.pool_occupancy)},
        {"gpu_preview_pool_peak_occupancy", std::to_string(metrics.delivery.pool_peak_occupancy)},
        {"gpu_preview_busy_drops", std::to_string(metrics.delivery.busy_drops)},
        {"gpu_preview_busy_retries", std::to_string(metrics.delivery.busy_retries)},
        {"gpu_preview_texture_viewer_uploaded_bytes", "0"},
        {"process_cpu_percent", optionalNumber(resources.process_cpu_percent)},
        {"process_working_set_bytes", optionalNumber(resources.process_working_set_bytes)},
        {"process_private_usage_bytes", optionalNumber(resources.process_private_usage_bytes)},
        {"system_total_bytes", optionalNumber(resources.system_total_bytes)},
        {"system_available_bytes", optionalNumber(resources.system_available_bytes)},
        {"preview_requests", std::to_string(metrics.requests)},
        {"rendered_frames", std::to_string(metrics.rendered_frames)},
        {"coalesced_requests", std::to_string(metrics.coalesced_requests)},
        {"stale_results", std::to_string(metrics.stale_results)},
        {"timestamp_seek_attempts", std::to_string(metrics.timestamp_seek_attempts)},
        {"timestamp_seek_successes", std::to_string(metrics.timestamp_seek_successes)},
        {"timestamp_seek_failures", std::to_string(metrics.timestamp_seek_failures)},
        {"forward_decode_attempts", std::to_string(metrics.forward_decode_attempts)},
        {"forward_decode_completions", std::to_string(metrics.forward_decode_completions)},
        {"forward_decode_fallbacks", std::to_string(metrics.forward_decode_fallbacks)},
        {"discarded_intermediate_frames",
         std::to_string(metrics.discarded_intermediate_frames)},
        {"gpu_composition_frames", std::to_string(metrics.gpu_composition_frames)},
        {"gpu_composition_fallbacks", std::to_string(metrics.gpu_composition_fallbacks)},
        {"gpu_composition_failures", std::to_string(metrics.gpu_composition_failures)},
        {"gpu_composition_uploaded_bytes", std::to_string(metrics.gpu_composition_uploaded_bytes)},
        {"gpu_composition_readback_bytes", std::to_string(metrics.gpu_composition_readback_bytes)},
        {"gpu_color_adjustment_effects", std::to_string(metrics.gpu_color_adjustment_effects)},
        {"gpu_color_adjustment_fallbacks", std::to_string(metrics.gpu_color_adjustment_fallbacks)},
        {"gpu_color_adjustment_failures", std::to_string(metrics.gpu_color_adjustment_failures)},
        {"gpu_gaussian_blur_effects", std::to_string(metrics.gpu_gaussian_blur_effects)},
        {"gpu_gaussian_blur_fallbacks", std::to_string(metrics.gpu_gaussian_blur_fallbacks)},
        {"gpu_gaussian_blur_failures", std::to_string(metrics.gpu_gaussian_blur_failures)},
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
