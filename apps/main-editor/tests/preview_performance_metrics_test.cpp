#include "rendering/preview_performance_metrics.h"

#include <chrono>
#include <cstdio>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
    try {
        rendering::PreviewPerformanceMetrics metrics;

        metrics.setEnabled(false);
        metrics.recordDecodedFrame();
        metrics.recordTiming(
            rendering::PreviewTiming::Decode,
            std::chrono::milliseconds(5));
        metrics.recordTiming(
            rendering::PreviewTiming::DecodePacket,
            std::chrono::milliseconds(5));
        metrics.recordTiming(
            rendering::PreviewTiming::DecodeReceive,
            std::chrono::milliseconds(5));
        metrics.recordTiming(
            rendering::PreviewTiming::PixelConversion,
            std::chrono::milliseconds(5));
        metrics.recordTiming(
            rendering::PreviewTiming::FrameCacheCopy,
            std::chrono::milliseconds(5));
        metrics.recordTiming(
            rendering::PreviewTiming::TextRasterization,
            std::chrono::milliseconds(6));
        const auto disabled = metrics.takeSnapshotAndReset();
        require(disabled.decoded_frames == 0,
                "Disabled metrics recorded a frame.");
        require(disabled.decode.count == 0,
                "Disabled metrics recorded timing data.");
        require(disabled.decode_packet.count == 0 &&
                    disabled.decode_receive.count == 0 &&
                    disabled.pixel_conversion.count == 0 &&
                    disabled.frame_cache_copy.count == 0,
                "Disabled metrics recorded decode substage timing data.");
        require(disabled.text_rasterization.count == 0,
                "Disabled metrics recorded text rasterization timing data.");
        require(disabled.playback_ticks == 0 &&
                    disabled.pacing_skipped_frames == 0 &&
                    disabled.pacing_coalesced_frames == 0 &&
                    disabled.pacing_lag.count == 0,
                "Disabled metrics recorded playback pacing data.");
        metrics.recordTextCompositionFastPathHit();
        metrics.recordPlaybackTick();
        metrics.recordPacingSkippedFrames(3);
        metrics.recordPacingCoalescedFrame();
        metrics.recordTiming(
            rendering::PreviewTiming::PacingLag,
            std::chrono::milliseconds(5));
        require(metrics.takeSnapshotAndReset().text_composition_fast_path_hits == 0,
                "Disabled metrics recorded text composition fast-path data.");

        metrics.setEnabled(true);
        metrics.recordDecodedFrame();
        metrics.recordDecodedCacheHits(2);
        metrics.recordTextCacheHit();
        metrics.recordTextCompositionFastPathHit();
        metrics.recordSeekOperation();
        metrics.recordComposedFrame();
        metrics.recordCompositionCacheHit();
        metrics.recordEmittedFrame();
        metrics.recordReceivedFrame();
        metrics.recordSubmittedFrame(1920, 1080);
        metrics.recordGpuPresentedFrame();
        metrics.recordOverwrittenFrame();
        metrics.recordPlaybackTick();
        metrics.recordPacingSkippedFrames(2);
        metrics.recordPacingCoalescedFrame();
        metrics.recordTiming(
            rendering::PreviewTiming::Decode,
            std::chrono::milliseconds(2));
        metrics.recordTiming(
            rendering::PreviewTiming::Decode,
            std::chrono::milliseconds(4));
        metrics.recordTiming(
            rendering::PreviewTiming::DecodePacket,
            std::chrono::milliseconds(1));
        metrics.recordTiming(
            rendering::PreviewTiming::DecodePacket,
            std::chrono::milliseconds(3));
        metrics.recordTiming(
            rendering::PreviewTiming::DecodeReceive,
            std::chrono::milliseconds(4));
        metrics.recordTiming(
            rendering::PreviewTiming::PixelConversion,
            std::chrono::milliseconds(5));
        metrics.recordTiming(
            rendering::PreviewTiming::PixelConversion,
            std::chrono::milliseconds(7));
        metrics.recordTiming(
            rendering::PreviewTiming::FrameCacheCopy,
            std::chrono::milliseconds(2));
        metrics.recordTiming(
            rendering::PreviewTiming::TextRasterization,
            std::chrono::milliseconds(3));
        metrics.recordTiming(
            rendering::PreviewTiming::TextRasterization,
            std::chrono::milliseconds(7));
        metrics.recordTiming(
            rendering::PreviewTiming::PacingLag,
            std::chrono::milliseconds(2));
        metrics.recordTiming(
            rendering::PreviewTiming::PacingLag,
            std::chrono::milliseconds(6));

        const auto snapshot = metrics.takeSnapshotAndReset();
        require(snapshot.decoded_frames == 1,
                "Decoded frame count is incorrect.");
        require(snapshot.decoded_cache_hits == 2,
                "Decoded cache hit count is incorrect.");
        require(snapshot.text_cache_hits == 1,
                "Text cache hit count is incorrect.");
        require(snapshot.text_composition_fast_path_hits == 1,
                "Text composition fast-path hit count is incorrect.");
        require(snapshot.seek_operations == 1,
                "Seek operation count is incorrect.");
        require(snapshot.composed_frames == 1,
                "Composed frame count is incorrect.");
        require(snapshot.composition_cache_hits == 1,
                "Composition cache hit count is incorrect.");
        require(snapshot.emitted_frames == 1,
                "Emitted frame count is incorrect.");
        require(snapshot.received_frames == 1,
                "Received frame count is incorrect.");
        require(snapshot.submitted_frames == 1,
                "Submitted frame count is incorrect.");
        require(snapshot.gpu_presented_frames == 1,
                "GPU presented frame count is incorrect.");
        require(snapshot.overwritten_frames == 1,
                "Overwritten frame count is incorrect.");
        require(snapshot.playback_ticks == 1,
                "Playback tick count is incorrect.");
        require(snapshot.pacing_skipped_frames == 2,
                "Skipped pacing frame count is incorrect.");
        require(snapshot.pacing_coalesced_frames == 1,
                "Coalesced pacing frame count is incorrect.");
        require(snapshot.last_frame_width == 1920 &&
                    snapshot.last_frame_height == 1080,
                "Last frame dimensions are incorrect.");
        require(snapshot.decode.count == 2,
                "Decode timing count is incorrect.");
        require(snapshot.decode.total_nanoseconds == 6'000'000,
                "Decode timing total is incorrect.");
        require(snapshot.decode.maximum_nanoseconds == 4'000'000,
                "Decode timing maximum is incorrect.");
        require(snapshot.decode.averageMilliseconds() == 3.0,
                "Decode timing average is incorrect.");
        require(snapshot.decode.maximumMilliseconds() == 4.0,
                "Decode timing maximum conversion is incorrect.");
        require(snapshot.decode_packet.count == 2 &&
                    snapshot.decode_packet.total_nanoseconds == 4'000'000 &&
                    snapshot.decode_packet.maximum_nanoseconds == 3'000'000 &&
                    snapshot.decode_packet.averageMilliseconds() == 2.0 &&
                    snapshot.decode_packet.maximumMilliseconds() == 3.0,
                "Decode packet timing aggregation is incorrect.");
        require(snapshot.decode_receive.count == 1 &&
                    snapshot.decode_receive.total_nanoseconds == 4'000'000 &&
                    snapshot.decode_receive.maximum_nanoseconds == 4'000'000 &&
                    snapshot.decode_receive.averageMilliseconds() == 4.0,
                "Decode receive timing aggregation is incorrect.");
        require(snapshot.pixel_conversion.count == 2 &&
                    snapshot.pixel_conversion.total_nanoseconds == 12'000'000 &&
                    snapshot.pixel_conversion.maximum_nanoseconds == 7'000'000 &&
                    snapshot.pixel_conversion.averageMilliseconds() == 6.0 &&
                    snapshot.pixel_conversion.maximumMilliseconds() == 7.0,
                "Pixel conversion timing aggregation is incorrect.");
        require(snapshot.frame_cache_copy.count == 1 &&
                    snapshot.frame_cache_copy.total_nanoseconds == 2'000'000 &&
                    snapshot.frame_cache_copy.maximum_nanoseconds == 2'000'000 &&
                    snapshot.frame_cache_copy.averageMilliseconds() == 2.0,
                "Frame cache copy timing aggregation is incorrect.");
        require(snapshot.text_rasterization.count == 2,
                "Text rasterization timing count is incorrect.");
        require(snapshot.text_rasterization.total_nanoseconds == 10'000'000,
                "Text rasterization timing total is incorrect.");
        require(snapshot.text_rasterization.maximum_nanoseconds == 7'000'000,
                "Text rasterization timing maximum is incorrect.");
        require(snapshot.text_rasterization.averageMilliseconds() == 5.0,
                "Text rasterization timing average is incorrect.");
        require(snapshot.text_rasterization.maximumMilliseconds() == 7.0,
                "Text rasterization timing maximum conversion is incorrect.");
        require(snapshot.pacing_lag.count == 2 &&
                    snapshot.pacing_lag.total_nanoseconds == 8'000'000 &&
                    snapshot.pacing_lag.maximum_nanoseconds == 6'000'000 &&
                    snapshot.pacing_lag.averageMilliseconds() == 4.0 &&
                    snapshot.pacing_lag.maximumMilliseconds() == 6.0,
                "Pacing lag timing aggregation is incorrect.");

        const auto reset = metrics.takeSnapshotAndReset();
        require(reset.decoded_frames == 0 && reset.decode.count == 0 &&
                    reset.decode_packet.count == 0 &&
                    reset.decode_receive.count == 0 &&
                    reset.pixel_conversion.count == 0 &&
                    reset.frame_cache_copy.count == 0 &&
                    reset.text_rasterization.count == 0 &&
                    reset.playback_ticks == 0 &&
                    reset.pacing_skipped_frames == 0 &&
                    reset.pacing_coalesced_frames == 0 &&
                    reset.pacing_lag.count == 0 &&
                    reset.text_composition_fast_path_hits == 0,
                "Taking a snapshot did not reset the metrics.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
