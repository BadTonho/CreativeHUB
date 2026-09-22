#include "preview_performance_metrics.h"

namespace rendering {
namespace {

using Nanoseconds = std::uint64_t;

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

} // namespace

double PreviewTimingSnapshot::averageMilliseconds() const noexcept {
    if (count == 0) return 0.0;
    return static_cast<double>(total_nanoseconds) /
        static_cast<double>(count) / 1'000'000.0;
}

double PreviewTimingSnapshot::maximumMilliseconds() const noexcept {
    return static_cast<double>(maximum_nanoseconds) / 1'000'000.0;
}

PreviewPerformanceMetrics& PreviewPerformanceMetrics::instance() noexcept {
    static PreviewPerformanceMetrics metrics;
    return metrics;
}

void PreviewPerformanceMetrics::setEnabled(bool enabled) noexcept {
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
    }

    const auto nanoseconds = toNanoseconds(elapsed);
    storage->count.fetch_add(1, std::memory_order_relaxed);
    storage->total_nanoseconds.fetch_add(nanoseconds, std::memory_order_relaxed);
    updateMaximum(storage->maximum_nanoseconds, nanoseconds);
}

void PreviewPerformanceMetrics::recordDecodedFrame() noexcept {
    if (isEnabled()) decoded_frames_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordDecodedCacheHits(std::uint64_t count) noexcept {
    if (isEnabled() && count > 0) {
        decoded_cache_hits_.fetch_add(count, std::memory_order_relaxed);
    }
}

void PreviewPerformanceMetrics::recordTextCacheHit() noexcept {
    if (isEnabled()) text_cache_hits_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordTextCompositionFastPathHit() noexcept {
    if (isEnabled()) {
        text_composition_fast_path_hits_.fetch_add(1, std::memory_order_relaxed);
    }
}

void PreviewPerformanceMetrics::recordSeekOperation() noexcept {
    if (isEnabled()) seek_operations_.fetch_add(1, std::memory_order_relaxed);
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

void PreviewPerformanceMetrics::recordGpuPresentedFrame() noexcept {
    if (isEnabled()) gpu_presented_frames_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPerformanceMetrics::recordOverwrittenFrame() noexcept {
    if (isEnabled()) overwritten_frames_.fetch_add(1, std::memory_order_relaxed);
}

PreviewTimingSnapshot PreviewPerformanceMetrics::takeTimingSnapshot(
    TimingStorage& storage) noexcept {
    return PreviewTimingSnapshot{
        storage.count.exchange(0, std::memory_order_relaxed),
        storage.total_nanoseconds.exchange(0, std::memory_order_relaxed),
        storage.maximum_nanoseconds.exchange(0, std::memory_order_relaxed)};
}

PreviewPerformanceSnapshot PreviewPerformanceMetrics::takeSnapshotAndReset() noexcept {
    return PreviewPerformanceSnapshot{
        decoded_frames_.exchange(0, std::memory_order_relaxed),
        decoded_cache_hits_.exchange(0, std::memory_order_relaxed),
        text_cache_hits_.exchange(0, std::memory_order_relaxed),
        text_composition_fast_path_hits_.exchange(0, std::memory_order_relaxed),
        seek_operations_.exchange(0, std::memory_order_relaxed),
        composed_frames_.exchange(0, std::memory_order_relaxed),
        composition_cache_hits_.exchange(0, std::memory_order_relaxed),
        emitted_frames_.exchange(0, std::memory_order_relaxed),
        received_frames_.exchange(0, std::memory_order_relaxed),
        submitted_frames_.exchange(0, std::memory_order_relaxed),
        gpu_presented_frames_.exchange(0, std::memory_order_relaxed),
        overwritten_frames_.exchange(0, std::memory_order_relaxed),
        last_frame_width_.exchange(0, std::memory_order_relaxed),
        last_frame_height_.exchange(0, std::memory_order_relaxed),
        takeTimingSnapshot(decode_),
        takeTimingSnapshot(decode_packet_),
        takeTimingSnapshot(decode_receive_),
        takeTimingSnapshot(pixel_conversion_),
        takeTimingSnapshot(frame_cache_copy_),
        takeTimingSnapshot(text_rasterization_),
        takeTimingSnapshot(seek_),
        takeTimingSnapshot(composition_),
        takeTimingSnapshot(payload_),
        takeTimingSnapshot(ui_callback_),
        takeTimingSnapshot(preview_submit_),
        takeTimingSnapshot(cpu_surface_),
        takeTimingSnapshot(gpu_upload_),
        takeTimingSnapshot(gpu_paint_)};
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
