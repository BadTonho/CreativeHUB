#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace rendering {

enum class PreviewTiming {
    Decode,
    DecodePacket,
    DecodeReceive,
    PixelConversion,
    FrameCacheCopy,
    TextRasterization,
    Seek,
    Composition,
    Payload,
    UiCallback,
    PreviewSubmit,
    CpuSurface,
    GpuUpload,
    GpuPaint,
    PacingLag,
    SeekToPresentation,
};

struct PreviewTimingSnapshot {
    std::uint64_t count = 0;
    std::uint64_t total_nanoseconds = 0;
    std::uint64_t maximum_nanoseconds = 0;
    std::uint64_t percentile95_nanoseconds = 0;
    std::uint64_t percentile99_nanoseconds = 0;

    [[nodiscard]] double averageMilliseconds() const noexcept;
    [[nodiscard]] double maximumMilliseconds() const noexcept;
    [[nodiscard]] double percentile95Milliseconds() const noexcept;
    [[nodiscard]] double percentile99Milliseconds() const noexcept;
};

struct PreviewPerformanceSnapshot {
    std::uint64_t decoded_frames = 0;
    std::uint64_t decode_discarded_frames = 0;
    std::uint64_t stale_frames_discarded = 0;
    std::uint64_t decoded_cache_hits = 0;
    std::uint64_t decoded_cache_entries = 0;
    std::uint64_t decoded_cache_bytes = 0;
    std::uint64_t text_cache_hits = 0;
    std::uint64_t text_composition_fast_path_hits = 0;
    std::uint64_t seek_operations = 0;
    std::uint64_t composed_frames = 0;
    std::uint64_t composition_cache_hits = 0;
    std::uint64_t emitted_frames = 0;
    std::uint64_t received_frames = 0;
    std::uint64_t submitted_frames = 0;
    std::uint64_t cpu_presented_frames = 0;
    std::uint64_t gpu_presented_frames = 0;
    std::uint64_t overwritten_frames = 0;
    std::uint64_t decode_failures = 0;
    std::uint64_t seek_failures = 0;
    std::uint64_t composition_failures = 0;
    std::uint64_t gpu_failures = 0;
    std::uint64_t playback_ticks = 0;
    std::uint64_t pacing_skipped_frames = 0;
    std::uint64_t pacing_coalesced_frames = 0;
    std::uint64_t playback_worker_thread_id = 0;
    std::uint64_t last_frame_width = 0;
    std::uint64_t last_frame_height = 0;
    std::uint64_t composition_layer_count = 0;
    std::uint64_t composition_text_layer_count = 0;
    std::uint64_t composition_transition_count = 0;
    std::uint64_t preview_window_elapsed_nanoseconds = 0;
    std::uint64_t playback_active_nanoseconds = 0;
    std::uint64_t first_frame_nanoseconds = 0;
    std::uint64_t target_frame_rate_milli = 0;
    bool composition_enabled = false;
    bool audio_enabled = false;

    PreviewTimingSnapshot decode;
    PreviewTimingSnapshot decode_packet;
    PreviewTimingSnapshot decode_receive;
    PreviewTimingSnapshot pixel_conversion;
    PreviewTimingSnapshot frame_cache_copy;
    PreviewTimingSnapshot text_rasterization;
    PreviewTimingSnapshot seek;
    PreviewTimingSnapshot composition;
    PreviewTimingSnapshot payload;
    PreviewTimingSnapshot ui_callback;
    PreviewTimingSnapshot preview_submit;
    PreviewTimingSnapshot cpu_surface;
    PreviewTimingSnapshot gpu_upload;
    PreviewTimingSnapshot gpu_paint;
    PreviewTimingSnapshot pacing_lag;
    PreviewTimingSnapshot seek_to_presentation;
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
    void recordDecodeDiscardedFrame() noexcept;
    void recordStaleFrameDiscarded() noexcept;
    void recordDecodedCacheHits(std::uint64_t count) noexcept;
    void recordDecodedCacheState(
        std::uint64_t entries,
        std::uint64_t bytes) noexcept;
    void recordTextCacheHit() noexcept;
    void recordTextCompositionFastPathHit() noexcept;
    void recordSeekOperation() noexcept;
    void recordComposedFrame() noexcept;
    void recordCompositionCacheHit() noexcept;
    void recordEmittedFrame() noexcept;
    void recordReceivedFrame() noexcept;
    void recordSubmittedFrame(int width, int height) noexcept;
    void recordCpuPresentedFrame() noexcept;
    void recordGpuPresentedFrame() noexcept;
    void recordOverwrittenFrame() noexcept;
    void recordDecodeFailure() noexcept;
    void recordSeekFailure() noexcept;
    void recordCompositionFailure() noexcept;
    void recordGpuFailure() noexcept;
    void recordPlaybackTick() noexcept;
    void recordPacingSkippedFrames(std::uint64_t count) noexcept;
    void recordPacingCoalescedFrame() noexcept;
    void setPlaybackWorkerThreadId(std::uint64_t thread_id) noexcept;
    void setTargetFrameRate(double frame_rate) noexcept;
    void setCompositionWorkload(
        std::uint64_t layer_count,
        std::uint64_t text_layer_count,
        std::uint64_t transition_count,
        bool enabled) noexcept;
    void setAudioEnabled(bool enabled) noexcept;
    void setPlaybackActive(bool active) noexcept;

    [[nodiscard]] PreviewPerformanceSnapshot takeSnapshotAndReset() noexcept;

private:
    static constexpr std::size_t kTimingHistogramBucketCount = 64;

    struct TimingStorage {
        std::atomic<std::uint64_t> count{0};
        std::atomic<std::uint64_t> total_nanoseconds{0};
        std::atomic<std::uint64_t> maximum_nanoseconds{0};
        std::array<std::atomic<std::uint64_t>, kTimingHistogramBucketCount>
            histogram{};
    };

    [[nodiscard]] static PreviewTimingSnapshot takeTimingSnapshot(
        TimingStorage& storage) noexcept;

    std::atomic_bool enabled_{false};
    std::atomic<std::uint64_t> enabled_started_nanoseconds_{0};
    std::atomic<std::uint64_t> active_started_nanoseconds_{0};
    std::atomic<std::uint64_t> playback_active_nanoseconds_{0};
    std::atomic<std::uint64_t> first_frame_nanoseconds_{0};
    std::atomic<std::uint64_t> seek_started_nanoseconds_{0};
    std::atomic<std::uint64_t> seek_to_presentation_nanoseconds_{0};
    std::atomic<std::uint64_t> decoded_frames_{0};
    std::atomic<std::uint64_t> decode_discarded_frames_{0};
    std::atomic<std::uint64_t> stale_frames_discarded_{0};
    std::atomic<std::uint64_t> decoded_cache_hits_{0};
    std::atomic<std::uint64_t> decoded_cache_entries_{0};
    std::atomic<std::uint64_t> decoded_cache_bytes_{0};
    std::atomic<std::uint64_t> text_cache_hits_{0};
    std::atomic<std::uint64_t> text_composition_fast_path_hits_{0};
    std::atomic<std::uint64_t> seek_operations_{0};
    std::atomic<std::uint64_t> composed_frames_{0};
    std::atomic<std::uint64_t> composition_cache_hits_{0};
    std::atomic<std::uint64_t> emitted_frames_{0};
    std::atomic<std::uint64_t> received_frames_{0};
    std::atomic<std::uint64_t> submitted_frames_{0};
    std::atomic<std::uint64_t> cpu_presented_frames_{0};
    std::atomic<std::uint64_t> gpu_presented_frames_{0};
    std::atomic<std::uint64_t> overwritten_frames_{0};
    std::atomic<std::uint64_t> decode_failures_{0};
    std::atomic<std::uint64_t> seek_failures_{0};
    std::atomic<std::uint64_t> composition_failures_{0};
    std::atomic<std::uint64_t> gpu_failures_{0};
    std::atomic<std::uint64_t> playback_ticks_{0};
    std::atomic<std::uint64_t> pacing_skipped_frames_{0};
    std::atomic<std::uint64_t> pacing_coalesced_frames_{0};
    std::atomic<std::uint64_t> playback_worker_thread_id_{0};
    std::atomic<std::uint64_t> last_frame_width_{0};
    std::atomic<std::uint64_t> last_frame_height_{0};
    std::atomic<std::uint64_t> composition_layer_count_{0};
    std::atomic<std::uint64_t> composition_text_layer_count_{0};
    std::atomic<std::uint64_t> composition_transition_count_{0};
    std::atomic<std::uint64_t> target_frame_rate_milli_{0};
    std::atomic_bool composition_enabled_{false};
    std::atomic_bool audio_enabled_{false};
    TimingStorage decode_;
    TimingStorage decode_packet_;
    TimingStorage decode_receive_;
    TimingStorage pixel_conversion_;
    TimingStorage frame_cache_copy_;
    TimingStorage text_rasterization_;
    TimingStorage seek_;
    TimingStorage composition_;
    TimingStorage payload_;
    TimingStorage ui_callback_;
    TimingStorage preview_submit_;
    TimingStorage cpu_surface_;
    TimingStorage gpu_upload_;
    TimingStorage gpu_paint_;
    TimingStorage pacing_lag_;
    TimingStorage seek_to_presentation_;
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
