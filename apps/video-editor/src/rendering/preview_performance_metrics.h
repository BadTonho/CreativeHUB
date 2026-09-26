#pragma once

#include "frame_compositor.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>

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
    MediaOpen,
    AudioSetup,
    CompositionSetup,
    ActivationToPresentation,
    PlaybackStartToPresentation,
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

enum class SlowFrameLayerKind : std::uint8_t {
    Video,
    Image,
    Text,
};

enum class SlowFrameDecodePath : std::uint8_t {
    None,
    Forward,
    FrameAt,
    ForwardFallbackFrameAt,
    StaticFrame,
    TextCache,
    TextRasterization,
};

struct SlowFrameLayerSample {
    std::uint64_t track_id = 0;
    std::uint64_t clip_id = 0;
    std::int64_t track_index = -1;
    std::int64_t clip_index = -1;
    std::int64_t source_frame = -1;
    std::int64_t source_width = 0;
    std::int64_t source_height = 0;
    std::int64_t source_stride = 0;
    double transform_position_x = 0.0;
    double transform_position_y = 0.0;
    double transform_scale = 1.0;
    double transform_rotation_degrees = 0.0;
    double transform_opacity = 1.0;
    SlowFrameLayerKind kind = SlowFrameLayerKind::Video;
    SlowFrameDecodePath decode_path = SlowFrameDecodePath::None;
    CompositionRasterPath composition_path = CompositionRasterPath::Unprocessed;
    FullFrameCopyEligibility full_frame_copy_eligibility;
    std::uint64_t decode_nanoseconds = 0;
    std::uint64_t composition_nanoseconds = 0;
    std::uint64_t composition_setup_nanoseconds = 0;
    std::uint64_t raster_blend_nanoseconds = 0;
    std::uint64_t fast_path_copy_nanoseconds = 0;
    bool blend_lookup_built = false;
    std::uint64_t blend_lookup_build_nanoseconds = 0;
    std::uint64_t blend_lookup_active_block_nanoseconds = 0;
    std::uint64_t blend_lookup_pixel_count = 0;
    std::uint64_t blend_lookup_active_block_count = 0;
    bool forward_decode_collected = false;
    bool forward_decode_completed = false;
    bool forward_decode_cancelled = false;
    std::int64_t forward_decode_start_frame = -1;
    std::int64_t forward_decode_requested_frame = -1;
    std::uint64_t forward_decode_discarded_frames = 0;
    std::uint64_t forward_decode_elapsed_nanoseconds = 0;
    std::uint64_t forward_decode_packet_io_nanoseconds = 0;
    std::uint64_t forward_decode_receive_nanoseconds = 0;
    std::uint64_t forward_decode_pixel_conversion_nanoseconds = 0;

    [[nodiscard]] std::uint64_t totalNanoseconds() const noexcept {
        return decode_nanoseconds + composition_nanoseconds;
    }
};

struct SlowFrameSample {
    std::uint64_t playback_generation = 0;
    std::int64_t timeline_frame = -1;
    std::uint64_t frame_rate_milli = 0;
    std::uint64_t timeline_frame_rate_numerator = 0;
    std::uint64_t timeline_frame_rate_denominator = 0;
    std::uint64_t frame_budget_nanoseconds = 0;
    std::uint64_t processing_nanoseconds = 0;
    std::uint64_t decode_nanoseconds = 0;
    std::uint64_t composition_nanoseconds = 0;
    std::uint64_t payload_nanoseconds = 0;
    std::uint64_t composition_adapter_nanoseconds = 0;
    std::uint64_t output_buffer_create_nanoseconds = 0;
    std::uint64_t output_background_fill_nanoseconds = 0;
    std::uint64_t composition_layer_setup_nanoseconds = 0;
    std::uint64_t composition_raster_blend_nanoseconds = 0;
    std::uint64_t composition_fast_path_copy_nanoseconds = 0;
    std::int64_t composition_canvas_width = 0;
    std::int64_t composition_canvas_height = 0;
    std::uint64_t active_layer_count = 0;
    std::uint8_t slow_layer_count = 0;
    std::array<SlowFrameLayerSample, 4> slow_layers{};
};

enum class PreviewFrameDeliveryStage : std::uint8_t {
    WorkerEmitted,
    MailboxPublished,
    ControllerDelivered,
    WindowReceived,
    PreviewSubmitted,
    GpuUploaded,
    GpuDrawn,
    QtFrameSwapped,
    CpuPainted,
    Count,
};

enum class PreviewFrameDeliveryDropReason : std::uint8_t {
    None,
    MailboxCoalesced,
    StaleGeneration,
    TimelineBehind,
    PreviewOverwritten,
    InvalidFrame,
    NoActiveClip,
    GpuFailure,
    Shutdown,
    Count,
};

enum class PreviewFrameDeliveryTiming : std::uint8_t {
    WorkerToMailbox,
    MailboxWait,
    ControllerToWindow,
    WindowToPreview,
    PreviewToGpuUpload,
    GpuUploadToDraw,
    DrawToQtSwap,
    PreviewToCpuPaint,
    WorkerToQtSwap,
    WorkerToCpuPaint,
    Count,
};

struct PreviewFrameDeliverySample {
    std::uint64_t trace_id = 0;
    std::uint64_t playback_generation = 0;
    std::int64_t timeline_frame = -1;
    PreviewFrameDeliveryStage last_stage = PreviewFrameDeliveryStage::WorkerEmitted;
    PreviewFrameDeliveryDropReason drop_reason = PreviewFrameDeliveryDropReason::None;
    bool complete = false;
    std::uint64_t age_nanoseconds = 0;
    std::uint64_t end_to_end_nanoseconds = 0;
};

struct PreviewFrameDeliverySnapshot {
    static constexpr std::size_t kSampleLimit = 4;
    std::array<std::uint64_t,
        static_cast<std::size_t>(PreviewFrameDeliveryStage::Count)> stage_counts{};
    std::array<std::uint64_t,
        static_cast<std::size_t>(PreviewFrameDeliveryDropReason::Count)> drop_counts{};
    std::array<PreviewTimingSnapshot,
        static_cast<std::size_t>(PreviewFrameDeliveryTiming::Count)> timings{};
    std::array<PreviewFrameDeliverySample, kSampleLimit> samples{};
    std::uint64_t sample_count = 0;
    std::uint64_t incomplete_trace_count = 0;
    std::uint64_t trace_evictions = 0;
    std::uint64_t unknown_trace_updates = 0;
};

void addSlowFrameLayer(
    SlowFrameSample& sample,
    const SlowFrameLayerSample& layer) noexcept;

struct PreviewPerformanceSnapshot {
    std::uint64_t decoded_frames = 0;
    std::uint64_t decode_discarded_frames = 0;
    std::uint64_t stale_frames_discarded = 0;
    std::uint64_t decoded_cache_hits = 0;
    std::uint64_t decoded_cache_entries = 0;
    std::uint64_t decoded_cache_bytes = 0;
    std::uint64_t text_cache_hits = 0;
    std::uint64_t text_composition_fast_path_hits = 0;
    std::uint64_t activation_events = 0;
    std::uint64_t playback_start_events = 0;
    std::uint64_t seek_requests = 0;
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
    std::uint64_t pacing_audio_catchup_frames = 0;
    std::uint64_t pacing_deadline_catchup_frames = 0;
    std::uint64_t pacing_coalesced_frames = 0;
    std::uint64_t audio_clock_drift_samples = 0;
    std::int64_t audio_clock_drift_total_nanoseconds = 0;
    std::uint64_t audio_clock_drift_max_abs_nanoseconds = 0;
    std::optional<std::uint64_t> audio_buffered_usecs;
    std::uint64_t playback_worker_thread_id = 0;
    std::uint64_t last_frame_width = 0;
    std::uint64_t last_frame_height = 0;
    std::uint64_t composition_layer_count = 0;
    std::uint64_t composition_text_layer_count = 0;
    std::uint64_t composition_transition_count = 0;
    std::uint64_t blend_lookup_composition_frames = 0;
    std::uint64_t blend_lookup_layer_observations = 0;
    std::uint64_t blend_lookup_active_layer_observations = 0;
    std::uint64_t blend_lookup_table_builds = 0;
    std::uint64_t blend_lookup_build_nanoseconds = 0;
    std::uint64_t blend_lookup_pixel_count = 0;
    std::uint64_t blend_lookup_active_block_count = 0;
    std::uint64_t blend_lookup_active_block_nanoseconds = 0;
    std::uint64_t preview_window_elapsed_nanoseconds = 0;
    std::uint64_t playback_active_nanoseconds = 0;
    std::uint64_t first_frame_nanoseconds = 0;
    std::uint64_t target_frame_rate_milli = 0;
    std::uint64_t timeline_frame_rate_numerator = 0;
    std::uint64_t timeline_frame_rate_denominator = 0;
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
    PreviewTimingSnapshot media_open;
    PreviewTimingSnapshot audio_setup;
    PreviewTimingSnapshot composition_setup;
    PreviewTimingSnapshot activation_to_presentation;
    PreviewTimingSnapshot playback_start_to_presentation;
    PreviewTimingSnapshot seek_to_presentation;
    std::uint64_t slow_frame_count = 0;
    std::optional<SlowFrameSample> worst_slow_frame;
    PreviewFrameDeliverySnapshot frame_delivery;
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
    void recordSlowFrame(const SlowFrameSample& sample) noexcept;
    void recordBlendLookupComposition(
        const FrameCompositionTimings& timings) noexcept;
    [[nodiscard]] std::uint64_t createFrameDeliveryTrace(
        std::uint64_t playback_generation,
        std::int64_t timeline_frame) noexcept;
    void recordFrameDeliveryStage(
        std::uint64_t trace_id,
        PreviewFrameDeliveryStage stage) noexcept;
    void recordFrameDeliveryDrop(
        std::uint64_t trace_id,
        PreviewFrameDeliveryDropReason reason) noexcept;
    void recordDecodedFrame() noexcept;
    void recordDecodeDiscardedFrame() noexcept;
    void recordStaleFrameDiscarded() noexcept;
    void recordDecodedCacheHits(std::uint64_t count) noexcept;
    void recordDecodedCacheState(
        std::uint64_t entries,
        std::uint64_t bytes) noexcept;
    void recordTextCacheHit() noexcept;
    void recordTextCompositionFastPathHit() noexcept;
    void recordActivationStarted() noexcept;
    void recordSeekRequest() noexcept;
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
    void recordPacingAudioCatchupFrames(std::uint64_t count) noexcept;
    void recordPacingDeadlineCatchupFrames(std::uint64_t count) noexcept;
    void recordPacingCoalescedFrame() noexcept;
    void recordAudioClockDrift(std::chrono::nanoseconds drift) noexcept;
    void setAudioBufferedUsecs(
        std::optional<std::uint64_t> buffered_usecs) noexcept;
    void setPlaybackWorkerThreadId(std::uint64_t thread_id) noexcept;
    void setTargetFrameRate(double frame_rate) noexcept;
    void setTimelineFrameRate(
        std::uint64_t numerator,
        std::uint64_t denominator) noexcept;
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
    static constexpr std::size_t kFrameDeliveryTraceCapacity = 512;
    static constexpr std::uint64_t kTimelineRateDenominatorBits = 20;
    static constexpr std::uint64_t kTimelineRateDenominatorMask =
        (std::uint64_t{1} << kTimelineRateDenominatorBits) - 1U;

    struct FrameDeliveryTraceRecord {
        std::uint64_t trace_id = 0;
        std::uint64_t playback_generation = 0;
        std::int64_t timeline_frame = -1;
        std::array<std::uint64_t,
            static_cast<std::size_t>(PreviewFrameDeliveryStage::Count)> stage_nanoseconds{};
        PreviewFrameDeliveryStage last_stage =
            PreviewFrameDeliveryStage::WorkerEmitted;
        PreviewFrameDeliveryDropReason drop_reason =
            PreviewFrameDeliveryDropReason::None;
        bool active = false;
        bool interval_dirty = true;
    };

    struct TimingStorage {
        std::atomic<std::uint64_t> count{0};
        std::atomic<std::uint64_t> total_nanoseconds{0};
        std::atomic<std::uint64_t> maximum_nanoseconds{0};
        std::array<std::atomic<std::uint64_t>, kTimingHistogramBucketCount>
            histogram{};
    };

    [[nodiscard]] static PreviewTimingSnapshot takeTimingSnapshot(
        TimingStorage& storage) noexcept;
    [[nodiscard]] PreviewFrameDeliverySnapshot takeFrameDeliverySnapshotAndReset() noexcept;

    std::atomic_bool enabled_{false};
    std::atomic<std::uint64_t> enabled_started_nanoseconds_{0};
    std::atomic<std::uint64_t> active_started_nanoseconds_{0};
    std::atomic<std::uint64_t> playback_active_nanoseconds_{0};
    std::atomic<std::uint64_t> first_frame_nanoseconds_{0};
    std::atomic<std::uint64_t> activation_started_nanoseconds_{0};
    std::atomic<std::uint64_t> playback_started_nanoseconds_{0};
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
    std::atomic<std::uint64_t> activation_events_{0};
    std::atomic<std::uint64_t> playback_start_events_{0};
    std::atomic<std::uint64_t> seek_requests_{0};
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
    std::atomic<std::uint64_t> pacing_audio_catchup_frames_{0};
    std::atomic<std::uint64_t> pacing_deadline_catchup_frames_{0};
    std::atomic<std::uint64_t> pacing_coalesced_frames_{0};
    std::atomic<std::uint64_t> audio_clock_drift_samples_{0};
    std::atomic<std::int64_t> audio_clock_drift_total_nanoseconds_{0};
    std::atomic<std::uint64_t> audio_clock_drift_max_abs_nanoseconds_{0};
    static constexpr std::uint64_t kUnavailableAudioBufferUsecs =
        std::numeric_limits<std::uint64_t>::max();
    std::atomic<std::uint64_t> audio_buffered_usecs_{
        kUnavailableAudioBufferUsecs};
    std::atomic<std::uint64_t> playback_worker_thread_id_{0};
    std::atomic<std::uint64_t> last_frame_width_{0};
    std::atomic<std::uint64_t> last_frame_height_{0};
    std::atomic<std::uint64_t> composition_layer_count_{0};
    std::atomic<std::uint64_t> composition_text_layer_count_{0};
    std::atomic<std::uint64_t> composition_transition_count_{0};
    std::atomic<std::uint64_t> blend_lookup_composition_frames_{0};
    std::atomic<std::uint64_t> blend_lookup_layer_observations_{0};
    std::atomic<std::uint64_t> blend_lookup_active_layer_observations_{0};
    std::atomic<std::uint64_t> blend_lookup_table_builds_{0};
    std::atomic<std::uint64_t> blend_lookup_build_nanoseconds_{0};
    std::atomic<std::uint64_t> blend_lookup_pixel_count_{0};
    std::atomic<std::uint64_t> blend_lookup_active_block_count_{0};
    std::atomic<std::uint64_t> blend_lookup_active_block_nanoseconds_{0};
    std::atomic<std::uint64_t> target_frame_rate_milli_{0};
    std::atomic<std::uint64_t> timeline_frame_rate_packed_{0};
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
    TimingStorage media_open_;
    TimingStorage audio_setup_;
    TimingStorage composition_setup_;
    TimingStorage activation_to_presentation_;
    TimingStorage playback_start_to_presentation_;
    TimingStorage seek_to_presentation_;
    std::mutex slow_frames_mutex_;
    std::uint64_t slow_frame_count_ = 0;
    std::optional<SlowFrameSample> worst_slow_frame_;
    std::mutex frame_delivery_mutex_;
    std::uint64_t next_frame_delivery_trace_id_ = 0;
    std::array<FrameDeliveryTraceRecord, kFrameDeliveryTraceCapacity>
        frame_delivery_traces_{};
    std::array<std::uint64_t,
        static_cast<std::size_t>(PreviewFrameDeliveryStage::Count)> frame_delivery_stage_counts_{};
    std::array<std::uint64_t,
        static_cast<std::size_t>(PreviewFrameDeliveryDropReason::Count)> frame_delivery_drop_counts_{};
    std::uint64_t frame_delivery_trace_evictions_ = 0;
    std::uint64_t frame_delivery_unknown_updates_ = 0;
    std::array<TimingStorage,
        static_cast<std::size_t>(PreviewFrameDeliveryTiming::Count)> frame_delivery_timings_{};
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
