#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace rendering {

enum class PreviewTiming {
    Decode,
    TextRasterization,
    Seek,
    Composition,
    Payload,
    UiCallback,
    PreviewSubmit,
    CpuSurface,
    GpuUpload,
    GpuPaint,
};

struct PreviewTimingSnapshot {
    std::uint64_t count = 0;
    std::uint64_t total_nanoseconds = 0;
    std::uint64_t maximum_nanoseconds = 0;

    [[nodiscard]] double averageMilliseconds() const noexcept;
    [[nodiscard]] double maximumMilliseconds() const noexcept;
};

struct PreviewPerformanceSnapshot {
    std::uint64_t decoded_frames = 0;
    std::uint64_t decoded_cache_hits = 0;
    std::uint64_t text_cache_hits = 0;
    std::uint64_t seek_operations = 0;
    std::uint64_t composed_frames = 0;
    std::uint64_t composition_cache_hits = 0;
    std::uint64_t emitted_frames = 0;
    std::uint64_t received_frames = 0;
    std::uint64_t submitted_frames = 0;
    std::uint64_t gpu_presented_frames = 0;
    std::uint64_t overwritten_frames = 0;
    std::uint64_t last_frame_width = 0;
    std::uint64_t last_frame_height = 0;

    PreviewTimingSnapshot decode;
    PreviewTimingSnapshot text_rasterization;
    PreviewTimingSnapshot seek;
    PreviewTimingSnapshot composition;
    PreviewTimingSnapshot payload;
    PreviewTimingSnapshot ui_callback;
    PreviewTimingSnapshot preview_submit;
    PreviewTimingSnapshot cpu_surface;
    PreviewTimingSnapshot gpu_upload;
    PreviewTimingSnapshot gpu_paint;
};

class PreviewPerformanceMetrics final {
public:
    static PreviewPerformanceMetrics& instance() noexcept;

    void setEnabled(bool enabled) noexcept;
    [[nodiscard]] bool isEnabled() const noexcept;
    void reset() noexcept;

    void recordTiming(
        PreviewTiming timing,
        std::chrono::nanoseconds elapsed) noexcept;
    void recordDecodedFrame() noexcept;
    void recordDecodedCacheHits(std::uint64_t count) noexcept;
    void recordTextCacheHit() noexcept;
    void recordSeekOperation() noexcept;
    void recordComposedFrame() noexcept;
    void recordCompositionCacheHit() noexcept;
    void recordEmittedFrame() noexcept;
    void recordReceivedFrame() noexcept;
    void recordSubmittedFrame(int width, int height) noexcept;
    void recordGpuPresentedFrame() noexcept;
    void recordOverwrittenFrame() noexcept;

    [[nodiscard]] PreviewPerformanceSnapshot takeSnapshotAndReset() noexcept;

private:
    struct TimingStorage {
        std::atomic<std::uint64_t> count{0};
        std::atomic<std::uint64_t> total_nanoseconds{0};
        std::atomic<std::uint64_t> maximum_nanoseconds{0};
    };

    [[nodiscard]] static PreviewTimingSnapshot takeTimingSnapshot(
        TimingStorage& storage) noexcept;

    std::atomic_bool enabled_{false};
    std::atomic<std::uint64_t> decoded_frames_{0};
    std::atomic<std::uint64_t> decoded_cache_hits_{0};
    std::atomic<std::uint64_t> text_cache_hits_{0};
    std::atomic<std::uint64_t> seek_operations_{0};
    std::atomic<std::uint64_t> composed_frames_{0};
    std::atomic<std::uint64_t> composition_cache_hits_{0};
    std::atomic<std::uint64_t> emitted_frames_{0};
    std::atomic<std::uint64_t> received_frames_{0};
    std::atomic<std::uint64_t> submitted_frames_{0};
    std::atomic<std::uint64_t> gpu_presented_frames_{0};
    std::atomic<std::uint64_t> overwritten_frames_{0};
    std::atomic<std::uint64_t> last_frame_width_{0};
    std::atomic<std::uint64_t> last_frame_height_{0};
    TimingStorage decode_;
    TimingStorage text_rasterization_;
    TimingStorage seek_;
    TimingStorage composition_;
    TimingStorage payload_;
    TimingStorage ui_callback_;
    TimingStorage preview_submit_;
    TimingStorage cpu_surface_;
    TimingStorage gpu_upload_;
    TimingStorage gpu_paint_;
};

class PreviewPerformanceScope final {
public:
    PreviewPerformanceScope(
        PreviewPerformanceMetrics& metrics,
        PreviewTiming timing) noexcept;
    ~PreviewPerformanceScope();

    PreviewPerformanceScope(const PreviewPerformanceScope&) = delete;
    PreviewPerformanceScope& operator=(const PreviewPerformanceScope&) = delete;

private:
    using Clock = std::chrono::steady_clock;

    PreviewPerformanceMetrics* metrics_ = nullptr;
    PreviewTiming timing_ = PreviewTiming::Decode;
    Clock::time_point started_{};
    bool enabled_ = false;
};

} // namespace rendering
