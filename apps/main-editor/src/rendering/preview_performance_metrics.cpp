#include "preview_performance_metrics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace rendering {
namespace {

using Nanoseconds = std::uint64_t;

using Clock = std::chrono::steady_clock;

Nanoseconds nowNanoseconds() noexcept {
    const auto count = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now().time_since_epoch()).count();
    return count <= 0 ? 0U : static_cast<Nanoseconds>(count);
}

Nanoseconds toNanoseconds(std::chrono::nanoseconds elapsed) noexcept {
    const auto count = elapsed.count();
    return count <= 0 ? 0U : static_cast<Nanoseconds>(count);
}

void updateMaximum(
    std::atomic<Nanoseconds>& target,
    Nanoseconds value) noexcept {
    auto current = target.load(std::memory_order_relaxed);
    while (current < value &&
           !target.compare_exchange_weak(
               current,
               value,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

std::size_t histogramBucket(
    Nanoseconds value,
    std::size_t bucket_count) noexcept {
    if (value == 0 || bucket_count == 0) return 0;

    std::size_t bucket = 0;
    while (value > 1 && bucket + 1 < bucket_count) {
        value >>= 1U;
        ++bucket;
    }
    return bucket;
}

Nanoseconds histogramBucketUpperBound(std::size_t bucket) noexcept {
    if (bucket >= 63) return std::numeric_limits<Nanoseconds>::max();
    return (Nanoseconds{1} << (bucket + 1U)) - 1U;
}

Nanoseconds histogramPercentile(
    const std::array<Nanoseconds, 64>& histogram,
    Nanoseconds count,
    Nanoseconds percentile) noexcept {
    if (count == 0) return 0;

    const auto rank = std::max<Nanoseconds>(
        1,
        (count * percentile + 99U) / 100U);
    Nanoseconds cumulative = 0;
    for (std::size_t bucket = 0; bucket < histogram.size(); ++bucket) {
        cumulative += histogram[bucket];
        if (cumulative >= rank) {
            return histogramBucketUpperBound(bucket);
        }
    }
    return std::numeric_limits<Nanoseconds>::max();
}

void recordPresentation(
    PreviewPerformanceMetrics& metrics,
    std::atomic<std::uint64_t>& first_frame,
    std::atomic<std::uint64_t>& activation_started,
    std::atomic<std::uint64_t>& playback_started,
    std::atomic<std::uint64_t>& seek_started,
    std::atomic<std::uint64_t>& enabled_started) noexcept {
    const auto now = nowNanoseconds();
    const auto enabled_at = enabled_started.load(std::memory_order_relaxed);
    if (enabled_at != 0) {
        auto expected = Nanoseconds{0};
        first_frame.compare_exchange_strong(
            expected,
            now >= enabled_at ? now - enabled_at : 0U,
            std::memory_order_relaxed,
            std::memory_order_relaxed);
    }

    const auto activation_at = activation_started.exchange(
        0,
        std::memory_order_relaxed);
    if (activation_at != 0 && now >= activation_at) {
        metrics.recordTiming(
            PreviewTiming::ActivationToPresentation,
            std::chrono::nanoseconds(now - activation_at));
    }

    const auto playback_at = playback_started.exchange(
        0,
        std::memory_order_relaxed);
    if (playback_at != 0 && now >= playback_at) {
        metrics.recordTiming(
            PreviewTiming::PlaybackStartToPresentation,
            std::chrono::nanoseconds(now - playback_at));
    }

    const auto seek_at = seek_started.exchange(0, std::memory_order_relaxed);
    if (seek_at != 0 && now >= seek_at) {
        metrics.recordTiming(
            PreviewTiming::SeekToPresentation,
            std::chrono::nanoseconds(now - seek_at));
    }
}

} // namespace

double PreviewTimingSnapshot::averageMilliseconds() const noexcept {
    if (count == 0) return 0.0;
    return static_cast<double>(total_nanoseconds) /
        static_cast<double>(count) / 1'000'000.0;
}

double PreviewTimingSnapshot::maximumMilliseconds() const noexcept {
    return static_cast<double>(maximum_nanoseconds) / 1'000'000.0;
}

double PreviewTimingSnapshot::percentile95Milliseconds() const noexcept {
    return static_cast<double>(percentile95_nanoseconds) / 1'000'000.0;
}

double PreviewTimingSnapshot::percentile99Milliseconds() const noexcept {
    return static_cast<double>(percentile99_nanoseconds) / 1'000'000.0;
}

PreviewPerformanceMetrics& PreviewPerformanceMetrics::instance() noexcept {
    static PreviewPerformanceMetrics metrics;
    return metrics;
}

void PreviewPerformanceMetrics::setEnabled(bool enabled) noexcept {
    if (enabled) {
        enabled_started_nanoseconds_.store(
            nowNanoseconds(),
            std::memory_order_relaxed);
    } else {
        enabled_started_nanoseconds_.store(0, std::memory_order_relaxed);
        activation_started_nanoseconds_.store(0, std::memory_order_relaxed);
        playback_started_nanoseconds_.store(0, std::memory_order_relaxed);
        seek_started_nanoseconds_.store(0, std::memory_order_relaxed);
    }
    enabled_.store(enabled, std::memory_order_release);
}

bool PreviewPerformanceMetrics::isEnabled() const noexcept {
    return enabled_.load(std::memory_order_acquire);
}

void PreviewPerformanceMetrics::reset() noexcept {
    static_cast<void>(takeSnapshotAndReset());
}

void PreviewPerformanceMetrics::recordTiming(
    PreviewTiming timing,
    std::chrono::nanoseconds elapsed) noexcept {
    if (!isEnabled()) return;

    auto* storage = &decode_;
    switch (timing) {
    case PreviewTiming::Decode: storage = &decode_; break;
    case PreviewTiming::DecodePacket: storage = &decode_packet_; break;
    case PreviewTiming::DecodeReceive: storage = &decode_receive_; break;
    case PreviewTiming::PixelConversion: storage = &pixel_conversion_; break;
    case PreviewTiming::FrameCacheCopy: storage = &frame_cache_copy_; break;
    case PreviewTiming::TextRasterization: storage = &text_rasterization_; break;
    case PreviewTiming::Seek: storage = &seek_; break;
    case PreviewTiming::Composition: storage = &composition_; break;
    case PreviewTiming::Payload: storage = &payload_; break;
    case PreviewTiming::UiCallback: storage = &ui_callback_; break;
    case PreviewTiming::PreviewSubmit: storage = &preview_submit_; break;
    case PreviewTiming::CpuSurface: storage = &cpu_surface_; break;
    case PreviewTiming::GpuUpload: storage = &gpu_upload_; break;
    case PreviewTiming::GpuPaint: storage = &gpu_paint_; break;
    case PreviewTiming::PacingLag: storage = &pacing_lag_; break;
    case PreviewTiming::MediaOpen: storage = &media_open_; break;
    case PreviewTiming::AudioSetup: storage = &audio_setup_; break;
    case PreviewTiming::CompositionSetup: storage = &composition_setup_; break;
    case PreviewTiming::ActivationToPresentation:
        storage = &activation_to_presentation_;
        break;
    case PreviewTiming::PlaybackStartToPresentation:
        storage = &playback_start_to_presentation_;
        break;
    case PreviewTiming::SeekToPresentation:
        storage = &seek_to_presentation_;
        break;
    }

    const auto nanoseconds = toNanoseconds(elapsed);
    storage->count.fetch_add(1, std::memory_order_relaxed);
    storage->total_nanoseconds.fetch_add(nanoseconds, std::memory_order_relaxed);
    updateMaximum(storage->maximum_nanoseconds, nanoseconds);
    storage->histogram[histogramBucket(
        nanoseconds,
        storage->histogram.size())].fetch_add(1, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordDecodedFrame() noexcept {
    if (isEnabled()) decoded_frames_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordDecodeDiscardedFrame() noexcept {
    if (isEnabled()) {
        decode_discarded_frames_.fetch_add(1, std::memory_order_relaxed);
    }
}

void PreviewPerformanceMetrics::recordStaleFrameDiscarded() noexcept {
    if (isEnabled()) stale_frames_discarded_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordDecodedCacheHits(std::uint64_t count) noexcept {
    if (isEnabled() && count > 0) {
        decoded_cache_hits_.fetch_add(count, std::memory_order_relaxed);
    }
}

void PreviewPerformanceMetrics::recordDecodedCacheState(
    std::uint64_t entries,
    std::uint64_t bytes) noexcept {
    if (!isEnabled()) return;
    decoded_cache_entries_.store(entries, std::memory_order_relaxed);
    decoded_cache_bytes_.store(bytes, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordTextCacheHit() noexcept {
    if (isEnabled()) text_cache_hits_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordTextCompositionFastPathHit() noexcept {
    if (isEnabled()) {
        text_composition_fast_path_hits_.fetch_add(1, std::memory_order_relaxed);
    }
}

void PreviewPerformanceMetrics::recordActivationStarted() noexcept {
    if (!isEnabled()) return;
    activation_events_.fetch_add(1, std::memory_order_relaxed);
    activation_started_nanoseconds_.store(nowNanoseconds(), std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordSeekRequest() noexcept {
    if (isEnabled()) seek_requests_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordSeekOperation() noexcept {
    if (!isEnabled()) return;
    seek_operations_.fetch_add(1, std::memory_order_relaxed);
    seek_started_nanoseconds_.store(nowNanoseconds(), std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordComposedFrame() noexcept {
    if (isEnabled()) composed_frames_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordCompositionCacheHit() noexcept {
    if (isEnabled()) composition_cache_hits_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordEmittedFrame() noexcept {
    if (isEnabled()) emitted_frames_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordReceivedFrame() noexcept {
    if (isEnabled()) received_frames_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordSubmittedFrame(int width, int height) noexcept {
    if (!isEnabled()) return;
    submitted_frames_.fetch_add(1, std::memory_order_relaxed);
    if (width > 0) last_frame_width_.store(
        static_cast<std::uint64_t>(width), std::memory_order_relaxed);
    if (height > 0) last_frame_height_.store(
        static_cast<std::uint64_t>(height), std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordCpuPresentedFrame() noexcept {
    if (!isEnabled()) return;
    cpu_presented_frames_.fetch_add(1, std::memory_order_relaxed);
    recordPresentation(
        *this,
        first_frame_nanoseconds_,
        activation_started_nanoseconds_,
        playback_started_nanoseconds_,
        seek_started_nanoseconds_,
        enabled_started_nanoseconds_);
}

void PreviewPerformanceMetrics::recordGpuPresentedFrame() noexcept {
    if (!isEnabled()) return;
    gpu_presented_frames_.fetch_add(1, std::memory_order_relaxed);
    recordPresentation(
        *this,
        first_frame_nanoseconds_,
        activation_started_nanoseconds_,
        playback_started_nanoseconds_,
        seek_started_nanoseconds_,
        enabled_started_nanoseconds_);
}

void PreviewPerformanceMetrics::recordOverwrittenFrame() noexcept {
    if (isEnabled()) overwritten_frames_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordDecodeFailure() noexcept {
    if (isEnabled()) decode_failures_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordSeekFailure() noexcept {
    if (isEnabled()) seek_failures_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordCompositionFailure() noexcept {
    if (isEnabled()) composition_failures_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordGpuFailure() noexcept {
    if (isEnabled()) gpu_failures_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordPlaybackTick() noexcept {
    if (isEnabled()) playback_ticks_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordPacingAudioCatchupFrames(
    std::uint64_t count) noexcept {
    if (isEnabled() && count > 0) {
        pacing_audio_catchup_frames_.fetch_add(count, std::memory_order_relaxed);
    }
}

void PreviewPerformanceMetrics::recordPacingDeadlineCatchupFrames(
    std::uint64_t count) noexcept {
    if (isEnabled() && count > 0) {
        pacing_deadline_catchup_frames_.fetch_add(count, std::memory_order_relaxed);
    }
}

void PreviewPerformanceMetrics::recordPacingSkippedFrames(
    std::uint64_t count) noexcept {
    if (isEnabled() && count > 0) {
        pacing_skipped_frames_.fetch_add(count, std::memory_order_relaxed);
    }
}

void PreviewPerformanceMetrics::recordPacingCoalescedFrame() noexcept {
    if (isEnabled()) {
        pacing_coalesced_frames_.fetch_add(1, std::memory_order_relaxed);
    }
}

void PreviewPerformanceMetrics::setPlaybackWorkerThreadId(
    std::uint64_t thread_id) noexcept {
    if (thread_id != 0) {
        playback_worker_thread_id_.store(thread_id, std::memory_order_relaxed);
    }
}

void PreviewPerformanceMetrics::setTargetFrameRate(double frame_rate) noexcept {
    if (!std::isfinite(frame_rate) || frame_rate <= 0.0) {
        target_frame_rate_milli_.store(0, std::memory_order_relaxed);
        return;
    }
    const auto scaled = static_cast<std::uint64_t>(
        std::llround(std::min(frame_rate, 1000.0) * 1000.0));
    target_frame_rate_milli_.store(scaled, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::setCompositionWorkload(
    std::uint64_t layer_count,
    std::uint64_t text_layer_count,
    std::uint64_t transition_count,
    bool enabled) noexcept {
    composition_layer_count_.store(layer_count, std::memory_order_relaxed);
    composition_text_layer_count_.store(text_layer_count, std::memory_order_relaxed);
    composition_transition_count_.store(transition_count, std::memory_order_relaxed);
    composition_enabled_.store(enabled, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::setAudioEnabled(bool enabled) noexcept {
    audio_enabled_.store(enabled, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::setPlaybackActive(bool active) noexcept {
    const auto now = nowNanoseconds();
    if (active) {
        auto expected = Nanoseconds{0};
        if (active_started_nanoseconds_.compare_exchange_strong(
                expected,
                now,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            playback_start_events_.fetch_add(1, std::memory_order_relaxed);
            playback_started_nanoseconds_.store(now, std::memory_order_relaxed);
        }
        return;
    }

    const auto started = active_started_nanoseconds_.exchange(
        0,
        std::memory_order_relaxed);
    if (started != 0 && now >= started) {
        playback_active_nanoseconds_.fetch_add(
            now - started,
            std::memory_order_relaxed);
    }
    playback_started_nanoseconds_.store(0, std::memory_order_relaxed);
}

PreviewTimingSnapshot PreviewPerformanceMetrics::takeTimingSnapshot(
    TimingStorage& storage) noexcept {
    const auto count = storage.count.exchange(0, std::memory_order_relaxed);
    const auto total = storage.total_nanoseconds.exchange(0, std::memory_order_relaxed);
    const auto maximum = storage.maximum_nanoseconds.exchange(0, std::memory_order_relaxed);
    std::array<Nanoseconds, kTimingHistogramBucketCount> histogram{};
    for (std::size_t index = 0; index < histogram.size(); ++index) {
        histogram[index] = storage.histogram[index].exchange(
            0,
            std::memory_order_relaxed);
    }
    return PreviewTimingSnapshot{
        count,
        total,
        maximum,
        histogramPercentile(histogram, count, 95),
        histogramPercentile(histogram, count, 99)};
}

PreviewPerformanceSnapshot PreviewPerformanceMetrics::takeSnapshotAndReset() noexcept {
    PreviewPerformanceSnapshot snapshot;
    const auto now = nowNanoseconds();
    const auto enabled = isEnabled();
    const auto enabled_at = enabled_started_nanoseconds_.load(std::memory_order_relaxed);
    if (enabled && enabled_at != 0 && now >= enabled_at) {
        snapshot.preview_window_elapsed_nanoseconds = now - enabled_at;
        enabled_started_nanoseconds_.store(now, std::memory_order_relaxed);
    }
    if (!enabled) {
        seek_started_nanoseconds_.store(0, std::memory_order_relaxed);
        active_started_nanoseconds_.store(0, std::memory_order_relaxed);
        playback_active_nanoseconds_.store(0, std::memory_order_relaxed);
    }

    const auto active_started = active_started_nanoseconds_.load(std::memory_order_relaxed);
    auto active_elapsed = playback_active_nanoseconds_.exchange(
        0,
        std::memory_order_relaxed);
    if (active_started != 0 && now >= active_started) {
        active_elapsed += now - active_started;
        active_started_nanoseconds_.store(now, std::memory_order_relaxed);
    }
    snapshot.playback_active_nanoseconds = active_elapsed;
    snapshot.first_frame_nanoseconds = first_frame_nanoseconds_.exchange(
        0,
        std::memory_order_relaxed);

    snapshot.decoded_frames = decoded_frames_.exchange(0, std::memory_order_relaxed);
    snapshot.decode_discarded_frames = decode_discarded_frames_.exchange(0, std::memory_order_relaxed);
    snapshot.stale_frames_discarded = stale_frames_discarded_.exchange(0, std::memory_order_relaxed);
    snapshot.decoded_cache_hits = decoded_cache_hits_.exchange(0, std::memory_order_relaxed);
    snapshot.decoded_cache_entries = decoded_cache_entries_.load(std::memory_order_relaxed);
    snapshot.decoded_cache_bytes = decoded_cache_bytes_.load(std::memory_order_relaxed);
    snapshot.text_cache_hits = text_cache_hits_.exchange(0, std::memory_order_relaxed);
    snapshot.text_composition_fast_path_hits = text_composition_fast_path_hits_.exchange(0, std::memory_order_relaxed);
    snapshot.activation_events = activation_events_.exchange(0, std::memory_order_relaxed);
    snapshot.playback_start_events = playback_start_events_.exchange(0, std::memory_order_relaxed);
    snapshot.seek_requests = seek_requests_.exchange(0, std::memory_order_relaxed);
    snapshot.seek_operations = seek_operations_.exchange(0, std::memory_order_relaxed);
    snapshot.composed_frames = composed_frames_.exchange(0, std::memory_order_relaxed);
    snapshot.composition_cache_hits = composition_cache_hits_.exchange(0, std::memory_order_relaxed);
    snapshot.emitted_frames = emitted_frames_.exchange(0, std::memory_order_relaxed);
    snapshot.received_frames = received_frames_.exchange(0, std::memory_order_relaxed);
    snapshot.submitted_frames = submitted_frames_.exchange(0, std::memory_order_relaxed);
    snapshot.cpu_presented_frames = cpu_presented_frames_.exchange(0, std::memory_order_relaxed);
    snapshot.gpu_presented_frames = gpu_presented_frames_.exchange(0, std::memory_order_relaxed);
    snapshot.overwritten_frames = overwritten_frames_.exchange(0, std::memory_order_relaxed);
    snapshot.decode_failures = decode_failures_.exchange(0, std::memory_order_relaxed);
    snapshot.seek_failures = seek_failures_.exchange(0, std::memory_order_relaxed);
    snapshot.composition_failures = composition_failures_.exchange(0, std::memory_order_relaxed);
    snapshot.gpu_failures = gpu_failures_.exchange(0, std::memory_order_relaxed);
    snapshot.playback_ticks = playback_ticks_.exchange(0, std::memory_order_relaxed);
    snapshot.pacing_skipped_frames = pacing_skipped_frames_.exchange(0, std::memory_order_relaxed);
    snapshot.pacing_audio_catchup_frames = pacing_audio_catchup_frames_.exchange(0, std::memory_order_relaxed);
    snapshot.pacing_deadline_catchup_frames = pacing_deadline_catchup_frames_.exchange(0, std::memory_order_relaxed);
    snapshot.pacing_coalesced_frames = pacing_coalesced_frames_.exchange(0, std::memory_order_relaxed);
    snapshot.playback_worker_thread_id = playback_worker_thread_id_.load(std::memory_order_relaxed);
    snapshot.last_frame_width = last_frame_width_.exchange(0, std::memory_order_relaxed);
    snapshot.last_frame_height = last_frame_height_.exchange(0, std::memory_order_relaxed);
    snapshot.composition_layer_count = composition_layer_count_.load(std::memory_order_relaxed);
    snapshot.composition_text_layer_count = composition_text_layer_count_.load(std::memory_order_relaxed);
    snapshot.composition_transition_count = composition_transition_count_.load(std::memory_order_relaxed);
    snapshot.target_frame_rate_milli = target_frame_rate_milli_.load(std::memory_order_relaxed);
    snapshot.composition_enabled = composition_enabled_.load(std::memory_order_relaxed);
    snapshot.audio_enabled = audio_enabled_.load(std::memory_order_relaxed);
    snapshot.decode = takeTimingSnapshot(decode_);
    snapshot.decode_packet = takeTimingSnapshot(decode_packet_);
    snapshot.decode_receive = takeTimingSnapshot(decode_receive_);
    snapshot.pixel_conversion = takeTimingSnapshot(pixel_conversion_);
    snapshot.frame_cache_copy = takeTimingSnapshot(frame_cache_copy_);
    snapshot.text_rasterization = takeTimingSnapshot(text_rasterization_);
    snapshot.seek = takeTimingSnapshot(seek_);
    snapshot.composition = takeTimingSnapshot(composition_);
    snapshot.payload = takeTimingSnapshot(payload_);
    snapshot.ui_callback = takeTimingSnapshot(ui_callback_);
    snapshot.preview_submit = takeTimingSnapshot(preview_submit_);
    snapshot.cpu_surface = takeTimingSnapshot(cpu_surface_);
    snapshot.gpu_upload = takeTimingSnapshot(gpu_upload_);
    snapshot.gpu_paint = takeTimingSnapshot(gpu_paint_);
    snapshot.pacing_lag = takeTimingSnapshot(pacing_lag_);
    snapshot.media_open = takeTimingSnapshot(media_open_);
    snapshot.audio_setup = takeTimingSnapshot(audio_setup_);
    snapshot.composition_setup = takeTimingSnapshot(composition_setup_);
    snapshot.activation_to_presentation = takeTimingSnapshot(activation_to_presentation_);
    snapshot.playback_start_to_presentation = takeTimingSnapshot(playback_start_to_presentation_);
    snapshot.seek_to_presentation = takeTimingSnapshot(seek_to_presentation_);
    return snapshot;
}

PreviewPerformanceScope::PreviewPerformanceScope(
    PreviewPerformanceMetrics& metrics,
    PreviewTiming timing) noexcept
    : metrics_(&metrics), timing_(timing), enabled_(metrics.isEnabled()) {
    if (enabled_) started_ = Clock::now();
}

PreviewPerformanceScope::~PreviewPerformanceScope() {
    if (enabled_ && metrics_ != nullptr) {
        metrics_->recordTiming(timing_, Clock::now() - started_);
    }
}

} // namespace rendering
