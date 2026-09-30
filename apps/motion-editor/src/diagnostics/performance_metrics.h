#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/system_monitor/performance_usage.h>
#include <map>
#include <mutex>
#include <optional>

namespace motion::diagnostics {

enum class PreviewTimingStage : std::size_t {
    Decode,
    TextShapeRasterization,
    Effects,
    Composition,
    FrameRender,
    RequestToViewerPaint,
    Count
};

enum class PreviewEffectKind : std::size_t {
    GaussianBlur,
    ColorAdjustment,
    Count
};

struct TimingSummary {
    std::uint64_t count = 0;
    std::uint64_t total_nanoseconds = 0;
    std::uint64_t maximum_nanoseconds = 0;
    std::uint64_t p95_nanoseconds = 0;
    std::uint64_t p99_nanoseconds = 0;
};

struct PreviewMetricsSnapshot {
    std::uint64_t requests = 0;
    std::uint64_t rendered_frames = 0;
    std::uint64_t coalesced_requests = 0;
    std::uint64_t stale_results = 0;
    std::array<TimingSummary,
               static_cast<std::size_t>(PreviewTimingStage::Count)> timings{};
    std::array<TimingSummary,
               static_cast<std::size_t>(PreviewEffectKind::Count)> effect_timings{};

    [[nodiscard]] bool hasActivity() const noexcept;
};

struct PreviewLogMetadata {
    int canvas_width = 0;
    int canvas_height = 0;
    std::int64_t frame_rate_numerator = 0;
    std::int64_t frame_rate_denominator = 0;
    std::size_t layer_count = 0;
    std::size_t effect_count = 0;
    std::size_t effect_worker_count = 0;
};

[[nodiscard]] creative_suite::diagnostics::Context makePreviewPerformanceContext(
    const PreviewMetricsSnapshot& metrics,
    const system_monitor::PerformanceSnapshot& resources,
    const PreviewLogMetadata& metadata);

// Motion Studio-specific interval collector. It accepts records from the
// preview worker and viewer thread and keeps only bounded per-interval samples.
class PerformanceMetrics final {
public:
    static PerformanceMetrics& instance() noexcept;

    void setEnabled(bool enabled) noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    void reset() noexcept;

    void recordRequest(std::uint64_t generation) noexcept;
    void recordCoalescedRequest(std::uint64_t generation) noexcept;
    void recordStaleResult(std::uint64_t generation) noexcept;
    void discardRequest(std::uint64_t generation) noexcept;
    void recordRenderedFrame() noexcept;
    void recordTiming(PreviewTimingStage stage,
                      std::uint64_t duration_nanoseconds) noexcept;
    void recordEffectTiming(PreviewEffectKind effect,
                            std::uint64_t duration_nanoseconds) noexcept;
    void recordViewerPaint(std::uint64_t generation) noexcept;

    [[nodiscard]] std::optional<PreviewMetricsSnapshot>
    takeSnapshotAndReset() noexcept;

private:
    struct TimingBucket {
        std::uint64_t count = 0;
        std::uint64_t total_nanoseconds = 0;
        std::uint64_t maximum_nanoseconds = 0;
        std::deque<std::uint64_t> percentile_samples;
    };

    static constexpr std::size_t maximum_percentile_samples_ = 8192;
    mutable std::mutex mutex_;
    bool enabled_ = false;
    std::uint64_t requests_ = 0;
    std::uint64_t rendered_frames_ = 0;
    std::uint64_t coalesced_requests_ = 0;
    std::uint64_t stale_results_ = 0;
    std::array<TimingBucket,
               static_cast<std::size_t>(PreviewTimingStage::Count)> timings_{};
    std::array<TimingBucket,
               static_cast<std::size_t>(PreviewEffectKind::Count)> effect_timings_{};
    std::map<std::uint64_t, std::chrono::steady_clock::time_point> request_started_;
};

[[nodiscard]] const char* previewTimingStageName(PreviewTimingStage stage) noexcept;
[[nodiscard]] const char* previewEffectKindName(PreviewEffectKind effect) noexcept;

} // namespace motion::diagnostics
