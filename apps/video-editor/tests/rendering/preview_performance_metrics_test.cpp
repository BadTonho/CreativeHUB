#include "rendering/preview_performance_metrics.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

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
        metrics.recordDecodeDiscardedFrame();
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
        const auto disabled_trace = metrics.createFrameDeliveryTrace(1, 1);
        metrics.recordFrameDeliveryStage(
            disabled_trace,
            rendering::PreviewFrameDeliveryStage::MailboxPublished);
        rendering::SlowFrameSample disabled_slow_frame;
        disabled_slow_frame.processing_nanoseconds = 40'000'000;
        disabled_slow_frame.frame_budget_nanoseconds = 33'000'000;
        metrics.recordSlowFrame(disabled_slow_frame);
        metrics.setTimelineFrameRate(30, 1'000'001);
        const auto disabled = metrics.takeSnapshotAndReset();
        require(disabled.decoded_frames == 0 &&
                    disabled.decode_discarded_frames == 0 &&
                    disabled.stale_frames_discarded == 0 &&
                    disabled.cpu_presented_frames == 0 &&
                    disabled.decode_failures == 0 &&
                    disabled.seek_failures == 0 &&
                    disabled.composition_failures == 0 &&
                    disabled.gpu_failures == 0 &&
                    disabled.activation_events == 0 &&
                    disabled.playback_start_events == 0 &&
                    disabled.seek_requests == 0 &&
                    disabled.audio_clock_drift_samples == 0 &&
                    disabled.audio_clock_drift_total_nanoseconds == 0 &&
                    disabled.audio_clock_drift_max_abs_nanoseconds == 0 &&
                    !disabled.audio_buffered_usecs.has_value() &&
                    disabled.pacing_audio_catchup_frames == 0 &&
                    disabled.pacing_deadline_catchup_frames == 0 &&
                    disabled.slow_frame_count == 0 &&
                    !disabled.worst_slow_frame.has_value() &&
                    disabled.timeline_frame_rate_numerator == 0 &&
                    disabled.timeline_frame_rate_denominator == 0 &&
                    disabled.frame_delivery.sample_count == 0 &&
                    disabled.frame_delivery.incomplete_trace_count == 0,
                "Disabled metrics recorded decoded-frame data.");
        require(disabled.decode.count == 0,
                "Disabled metrics recorded timing data.");
        require(disabled.decode.percentile95_nanoseconds == 0 &&
                    disabled.decode.percentile99_nanoseconds == 0,
                "Disabled metrics recorded percentile data.");
        metrics.setTimelineFrameRate(1'001, 1);
        const auto invalid_timeline_rate = metrics.takeSnapshotAndReset();
        require(invalid_timeline_rate.timeline_frame_rate_numerator == 0 &&
                    invalid_timeline_rate.timeline_frame_rate_denominator == 0,
                "Metrics accepted a Timeline rate outside the project rate bounds.");
        require(disabled.decode_packet.count == 0 &&
                    disabled.decode_receive.count == 0 &&
                    disabled.pixel_conversion.count == 0 &&
                    disabled.frame_cache_copy.count == 0,
                "Disabled metrics recorded decode substage timing data.");
        require(disabled.text_rasterization.count == 0,
                "Disabled metrics recorded text rasterization timing data.");
        require(disabled.media_open.count == 0 &&
                    disabled.audio_setup.count == 0 &&
                    disabled.composition_setup.count == 0 &&
                    disabled.activation_to_presentation.count == 0 &&
                    disabled.playback_start_to_presentation.count == 0 &&
                    disabled.seek_to_presentation.count == 0,
                "Disabled metrics recorded lifecycle timing data.");
        require(disabled.playback_ticks == 0 &&
                    disabled.pacing_skipped_frames == 0 &&
                    disabled.pacing_audio_catchup_frames == 0 &&
                    disabled.pacing_deadline_catchup_frames == 0 &&
                    disabled.pacing_coalesced_frames == 0 &&
                    disabled.pacing_lag.count == 0,
                "Disabled metrics recorded playback pacing data.");
        metrics.recordTextCompositionFastPathHit();
        metrics.recordPlaybackTick();
        metrics.recordPacingSkippedFrames(3);
        metrics.recordPacingAudioCatchupFrames(2);
        metrics.recordPacingDeadlineCatchupFrames(1);
        metrics.recordPacingCoalescedFrame();
        metrics.recordTiming(
            rendering::PreviewTiming::PacingLag,
            std::chrono::milliseconds(5));
        require(metrics.takeSnapshotAndReset().text_composition_fast_path_hits == 0,
                "Disabled metrics recorded text composition fast-path data.");

        metrics.setEnabled(true);
        metrics.recordActivationStarted();
        metrics.recordSeekRequest();
        metrics.recordSeekOperation();
        metrics.setPlaybackActive(true);
        metrics.recordDecodedFrame();
        metrics.recordDecodeDiscardedFrame();
        metrics.recordDecodeDiscardedFrame();
        metrics.recordStaleFrameDiscarded();
        metrics.recordDecodedCacheHits(2);
        metrics.recordDecodedCacheState(3, 4096);
        metrics.recordTextCacheHit();
        metrics.recordTextCompositionFastPathHit();
        metrics.recordComposedFrame();
        metrics.recordCompositionCacheHit();
        metrics.recordEmittedFrame();
        metrics.recordReceivedFrame();
        metrics.recordSubmittedFrame(1920, 1080);
        metrics.recordCpuPresentedFrame();
        metrics.recordGpuPresentedFrame();
        metrics.recordOverwrittenFrame();
        metrics.recordDecodeFailure();
        metrics.recordSeekFailure();
        metrics.recordCompositionFailure();
        metrics.recordGpuFailure();
        metrics.recordPlaybackTick();
        metrics.recordPacingSkippedFrames(2);
        metrics.recordPacingAudioCatchupFrames(1);
        metrics.recordPacingDeadlineCatchupFrames(1);
        metrics.recordAudioClockDrift(std::chrono::milliseconds(-20));
        metrics.recordAudioClockDrift(std::chrono::milliseconds(40));
        metrics.setAudioBufferedUsecs(125'000);
        metrics.recordPacingCoalescedFrame();
        metrics.setPlaybackWorkerThreadId(12345);
        metrics.setTargetFrameRate(23.976);
        metrics.setTimelineFrameRate(30'000, 1'001);
        metrics.setCompositionWorkload(4, 1, 2, true);
        metrics.setAudioEnabled(true);
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
        metrics.recordTiming(
            rendering::PreviewTiming::MediaOpen,
            std::chrono::milliseconds(2));
        metrics.recordTiming(
            rendering::PreviewTiming::MediaOpen,
            std::chrono::milliseconds(4));
        metrics.recordTiming(
            rendering::PreviewTiming::AudioSetup,
            std::chrono::milliseconds(3));
        metrics.recordTiming(
            rendering::PreviewTiming::CompositionSetup,
            std::chrono::milliseconds(5));

        rendering::SlowFrameSample on_budget_slow_frame;
        on_budget_slow_frame.processing_nanoseconds = 33'000'000;
        on_budget_slow_frame.frame_budget_nanoseconds = 33'000'000;
        metrics.recordSlowFrame(on_budget_slow_frame);
        rendering::SlowFrameSample first_slow_frame;
        first_slow_frame.playback_generation = 7;
        first_slow_frame.timeline_frame = 120;
        first_slow_frame.frame_rate_milli = 30'000;
        first_slow_frame.timeline_frame_rate_numerator = 30'000;
        first_slow_frame.timeline_frame_rate_denominator = 1'001;
        first_slow_frame.frame_budget_nanoseconds = 33'000'000;
        first_slow_frame.processing_nanoseconds = 34'000'000;
        first_slow_frame.decode_nanoseconds = 20'000'000;
        first_slow_frame.composition_nanoseconds = 12'000'000;
        first_slow_frame.payload_nanoseconds = 2'000'000;
        first_slow_frame.active_layer_count = 5;
        first_slow_frame.composition_canvas_width = 1920;
        first_slow_frame.composition_canvas_height = 1080;
        for (std::uint64_t layer_index = 0; layer_index < 6; ++layer_index) {
            rendering::SlowFrameLayerSample layer;
            layer.track_id = 11;
            layer.clip_id = 21 + layer_index;
            layer.track_index = 0;
            layer.clip_index = static_cast<std::int64_t>(layer_index);
            layer.source_frame = 120;
            layer.source_width = 1920;
            layer.source_height = 1080;
            layer.source_stride = 7680;
            layer.transform_position_x = 0.5;
            layer.transform_position_y = 0.5;
            layer.transform_scale = 1.0;
            layer.transform_rotation_degrees = 0.0;
            layer.transform_opacity = 1.0;
            layer.kind = rendering::SlowFrameLayerKind::Video;
            layer.decode_path = rendering::SlowFrameDecodePath::Forward;
            layer.composition_path = rendering::CompositionRasterPath::FullFrameCopy;
            layer.full_frame_copy_eligibility = rendering::FullFrameCopyEligibility{
                true, true, true, true, true, true, true, true, true};
            layer.decode_nanoseconds = (layer_index + 1) * 1'000'000;
            if (layer_index == 5) {
                layer.forward_decode_collected = true;
                layer.forward_decode_completed = true;
                layer.forward_decode_start_frame = 90;
                layer.forward_decode_requested_frame = 120;
                layer.forward_decode_discarded_frames = 29;
                layer.forward_decode_elapsed_nanoseconds = 15'000'000;
                layer.forward_decode_packet_io_nanoseconds = 4'000'000;
                layer.forward_decode_receive_nanoseconds = 6'000'000;
                layer.forward_decode_pixel_conversion_nanoseconds = 3'000'000;
            }
            rendering::addSlowFrameLayer(first_slow_frame, layer);
        }
        metrics.recordSlowFrame(first_slow_frame);
        auto slower_frame = first_slow_frame;
        slower_frame.timeline_frame = 121;
        slower_frame.processing_nanoseconds = 41'000'000;
        slower_frame.slow_layers[0].clip_id = 22;
        metrics.recordSlowFrame(slower_frame);

        const auto snapshot = metrics.takeSnapshotAndReset();
        metrics.setPlaybackActive(false);
        require(snapshot.decoded_frames == 1,
                "Decoded frame count is incorrect.");
        require(snapshot.decode_discarded_frames == 2,
                "Discarded decode frame count is incorrect.");
        require(snapshot.stale_frames_discarded == 1,
                "Stale frame count is incorrect.");
        require(snapshot.decoded_cache_hits == 2,
                "Decoded cache hit count is incorrect.");
        require(snapshot.decoded_cache_entries == 3 &&
                    snapshot.decoded_cache_bytes == 4096,
                "Decoded cache state is incorrect.");
        require(snapshot.text_cache_hits == 1,
                "Text cache hit count is incorrect.");
        require(snapshot.text_composition_fast_path_hits == 1,
                "Text composition fast-path hit count is incorrect.");
        require(snapshot.activation_events == 1 &&
                    snapshot.playback_start_events == 1 &&
                    snapshot.seek_requests == 1,
                "Lifecycle event counts are incorrect.");
        require(snapshot.seek_operations == 1,
                "Seek operation count is incorrect.");
        require(snapshot.slow_frame_count == 2 &&
                    snapshot.worst_slow_frame.has_value() &&
                    snapshot.worst_slow_frame->timeline_frame == 121 &&
                    snapshot.worst_slow_frame->timeline_frame_rate_numerator == 30'000 &&
                    snapshot.worst_slow_frame->timeline_frame_rate_denominator == 1'001 &&
                    snapshot.worst_slow_frame->processing_nanoseconds ==
                        41'000'000 &&
                    snapshot.worst_slow_frame->composition_canvas_width == 1920 &&
                    snapshot.worst_slow_frame->composition_canvas_height == 1080 &&
                    snapshot.worst_slow_frame->slow_layer_count == 4 &&
                    snapshot.worst_slow_frame->slow_layers[0].clip_id == 22 &&
                    snapshot.worst_slow_frame->slow_layers[0].source_width == 1920 &&
                    snapshot.worst_slow_frame->slow_layers[0].source_height == 1080 &&
                    snapshot.worst_slow_frame->slow_layers[0].source_stride == 7680 &&
                    snapshot.worst_slow_frame->slow_layers[0].transform_position_x == 0.5 &&
                    snapshot.worst_slow_frame->slow_layers[0].composition_path ==
                        rendering::CompositionRasterPath::FullFrameCopy &&
                    snapshot.worst_slow_frame->slow_layers[0]
                        .full_frame_copy_eligibility.alpha_check_performed &&
                    snapshot.worst_slow_frame->slow_layers[0]
                        .full_frame_copy_eligibility.source_pixels_opaque &&
                    snapshot.worst_slow_frame->slow_layers[0].forward_decode_collected &&
                    snapshot.worst_slow_frame->slow_layers[0].forward_decode_completed &&
                    snapshot.worst_slow_frame->slow_layers[0].forward_decode_start_frame == 90 &&
                    snapshot.worst_slow_frame->slow_layers[0].forward_decode_requested_frame == 120 &&
                    snapshot.worst_slow_frame->slow_layers[0].forward_decode_discarded_frames == 29 &&
                    snapshot.worst_slow_frame->slow_layers[0].forward_decode_elapsed_nanoseconds == 15'000'000 &&
                    snapshot.worst_slow_frame->slow_layers[0].forward_decode_packet_io_nanoseconds == 4'000'000 &&
                    snapshot.worst_slow_frame->slow_layers[0].forward_decode_receive_nanoseconds == 6'000'000 &&
                    snapshot.worst_slow_frame->slow_layers[0].forward_decode_pixel_conversion_nanoseconds == 3'000'000 &&
                    snapshot.worst_slow_frame->slow_layers[1].clip_id == 25 &&
                    snapshot.worst_slow_frame->slow_layers[3].clip_id == 23,
                "Slow-frame threshold, count, worst sample, or layer bound is incorrect.");
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
        require(snapshot.cpu_presented_frames == 1,
                "CPU presented frame count is incorrect.");
        require(snapshot.gpu_presented_frames == 1,
                "GPU presented frame count is incorrect.");
        require(snapshot.overwritten_frames == 1,
                "Overwritten frame count is incorrect.");
        require(snapshot.decode_failures == 1 &&
                    snapshot.seek_failures == 1 &&
                    snapshot.composition_failures == 1 &&
                    snapshot.gpu_failures == 1,
                "Failure counters are incorrect.");
        require(snapshot.playback_ticks == 1,
                "Playback tick count is incorrect.");
        require(snapshot.pacing_skipped_frames == 2,
                "Skipped pacing frame count is incorrect.");
        require(snapshot.pacing_audio_catchup_frames == 1 &&
                    snapshot.pacing_deadline_catchup_frames == 1,
                "Pacing catch-up cause counts are incorrect.");
        require(snapshot.audio_clock_drift_samples == 2 &&
                    snapshot.audio_clock_drift_total_nanoseconds == 20'000'000 &&
                    snapshot.audio_clock_drift_max_abs_nanoseconds == 40'000'000 &&
                    snapshot.audio_buffered_usecs.has_value() &&
                    *snapshot.audio_buffered_usecs == 125'000,
                "Audio clock diagnostics are incorrect.");
        require(snapshot.pacing_coalesced_frames == 1,
                "Coalesced pacing frame count is incorrect.");
        require(snapshot.playback_worker_thread_id == 12345,
                "Playback worker thread identifier is incorrect.");
        require(snapshot.last_frame_width == 1920 &&
                    snapshot.last_frame_height == 1080,
                "Last frame dimensions are incorrect.");
        require(snapshot.target_frame_rate_milli == 23976 &&
                    snapshot.timeline_frame_rate_numerator == 30'000 &&
                    snapshot.timeline_frame_rate_denominator == 1'001 &&
                    snapshot.composition_layer_count == 4 &&
                    snapshot.composition_text_layer_count == 1 &&
                    snapshot.composition_transition_count == 2 &&
                    snapshot.composition_enabled && snapshot.audio_enabled,
                "Workload context is incorrect.");
        require(snapshot.first_frame_nanoseconds <=
                    snapshot.preview_window_elapsed_nanoseconds,
                "First-frame latency is outside the metrics window.");
        require(snapshot.seek_to_presentation.count == 1,
                "Seek-to-presentation latency was not recorded.");
        require(snapshot.activation_to_presentation.count == 1 &&
                    snapshot.playback_start_to_presentation.count == 1,
                "Lifecycle-to-presentation latency was not recorded.");
        require(snapshot.media_open.count == 2 &&
                    snapshot.audio_setup.count == 1 &&
                    snapshot.composition_setup.count == 1,
                "Playback setup timing was not recorded.");
        require(snapshot.media_open.percentile95_nanoseconds == 4'194'303 &&
                    snapshot.media_open.percentile99_nanoseconds == 4'194'303,
                "Media-open percentiles are incorrect.");
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
        require(snapshot.decode.percentile95_nanoseconds == 4'194'303 &&
                    snapshot.decode.percentile99_nanoseconds == 4'194'303,
                "Decode timing percentiles are incorrect.");
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

        metrics.recordTiming(
            rendering::PreviewTiming::MediaOpen,
            std::chrono::nanoseconds::max());
        const auto overflow = metrics.takeSnapshotAndReset();
        require(overflow.media_open.count == 1 &&
                    overflow.media_open.maximum_nanoseconds ==
                        static_cast<std::uint64_t>(
                            std::chrono::nanoseconds::max().count()) &&
                    overflow.media_open.percentile95_nanoseconds ==
                        static_cast<std::uint64_t>(
                            std::chrono::nanoseconds::max().count()) &&
                    overflow.media_open.percentile99_nanoseconds ==
                        static_cast<std::uint64_t>(
                            std::chrono::nanoseconds::max().count()),
                "Timing histogram overflow handling is incorrect.");

        const auto reset = metrics.takeSnapshotAndReset();
        require(reset.decoded_frames == 0 &&
                    reset.decode_discarded_frames == 0 &&
                    reset.activation_events == 0 &&
                    reset.playback_start_events == 0 &&
                    reset.seek_requests == 0 &&
                    reset.seek_operations == 0 &&
                    reset.decode.count == 0 &&
                    reset.decode_packet.count == 0 &&
                    reset.decode_receive.count == 0 &&
                    reset.pixel_conversion.count == 0 &&
                    reset.frame_cache_copy.count == 0 &&
                    reset.text_rasterization.count == 0 &&
                    reset.playback_ticks == 0 &&
                    reset.pacing_skipped_frames == 0 &&
                    reset.pacing_audio_catchup_frames == 0 &&
                    reset.pacing_deadline_catchup_frames == 0 &&
                    reset.pacing_coalesced_frames == 0 &&
                    reset.audio_clock_drift_samples == 0 &&
                    reset.audio_clock_drift_total_nanoseconds == 0 &&
                    reset.audio_clock_drift_max_abs_nanoseconds == 0 &&
                    reset.audio_buffered_usecs.has_value() &&
                    *reset.audio_buffered_usecs == 125'000 &&
                    reset.playback_worker_thread_id == 12345 &&
                    reset.pacing_lag.count == 0 &&
                    reset.text_composition_fast_path_hits == 0 &&
                    reset.stale_frames_discarded == 0 &&
                    reset.cpu_presented_frames == 0 &&
                    reset.decode_failures == 0 &&
                    reset.media_open.count == 0 &&
                    reset.audio_setup.count == 0 &&
                    reset.composition_setup.count == 0 &&
                    reset.activation_to_presentation.count == 0 &&
                    reset.playback_start_to_presentation.count == 0 &&
                    reset.seek_to_presentation.count == 0 &&
                    reset.slow_frame_count == 0 &&
                    !reset.worst_slow_frame.has_value() &&
                    reset.frame_delivery.sample_count == 0 &&
                    reset.frame_delivery.incomplete_trace_count == 0,
                "Taking a snapshot did not reset the metrics.");

        constexpr std::size_t slow_samples_per_thread = 250;
        constexpr std::size_t slow_producer_count = 4;
        std::vector<std::thread> producers;
        std::atomic_size_t completed_producers{0};
        std::uint64_t concurrently_flushed_slow_samples = 0;
        bool saw_concurrent_worst_sample = false;
        for (std::size_t thread_index = 0;
             thread_index < slow_producer_count;
             ++thread_index) {
            producers.emplace_back([&metrics, &completed_producers, thread_index]() {
                rendering::SlowFrameSample sample;
                sample.frame_budget_nanoseconds = 1;
                sample.processing_nanoseconds = 2;
                sample.timeline_frame = static_cast<std::int64_t>(thread_index);
                for (std::size_t index = 0;
                     index < slow_samples_per_thread;
                     ++index) {
                    metrics.recordSlowFrame(sample);
                }
                completed_producers.fetch_add(1, std::memory_order_release);
            });
        }
        while (completed_producers.load(std::memory_order_acquire) <
               slow_producer_count) {
            const auto partial = metrics.takeSnapshotAndReset();
            concurrently_flushed_slow_samples += partial.slow_frame_count;
            saw_concurrent_worst_sample = saw_concurrent_worst_sample ||
                partial.worst_slow_frame.has_value();
            std::this_thread::yield();
        }
        for (auto& producer : producers) producer.join();
        const auto concurrent_final = metrics.takeSnapshotAndReset();
        concurrently_flushed_slow_samples += concurrent_final.slow_frame_count;
        saw_concurrent_worst_sample = saw_concurrent_worst_sample ||
            concurrent_final.worst_slow_frame.has_value();
        require(concurrently_flushed_slow_samples ==
                    slow_producer_count * slow_samples_per_thread &&
                    saw_concurrent_worst_sample,
                "Concurrent slow-frame aggregation lost samples.");

        metrics.reset();
        const auto complete_trace = metrics.createFrameDeliveryTrace(12, 240);
        metrics.recordFrameDeliveryStage(
            complete_trace,
            rendering::PreviewFrameDeliveryStage::MailboxPublished);
        metrics.recordFrameDeliveryStage(
            complete_trace,
            rendering::PreviewFrameDeliveryStage::ControllerDelivered);
        metrics.recordFrameDeliveryStage(
            complete_trace,
            rendering::PreviewFrameDeliveryStage::WindowReceived);
        metrics.recordFrameDeliveryStage(
            complete_trace,
            rendering::PreviewFrameDeliveryStage::PreviewSubmitted);
        metrics.recordFrameDeliveryStage(
            complete_trace,
            rendering::PreviewFrameDeliveryStage::GpuUploaded);
        metrics.recordFrameDeliveryStage(
            complete_trace,
            rendering::PreviewFrameDeliveryStage::GpuDrawn);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        metrics.recordFrameDeliveryStage(
            complete_trace,
            rendering::PreviewFrameDeliveryStage::QtFrameSwapped);

        const auto coalesced_trace = metrics.createFrameDeliveryTrace(12, 241);
        metrics.recordFrameDeliveryStage(
            coalesced_trace,
            rendering::PreviewFrameDeliveryStage::MailboxPublished);
        metrics.recordFrameDeliveryDrop(
            coalesced_trace,
            rendering::PreviewFrameDeliveryDropReason::MailboxCoalesced);
        const auto incomplete_trace = metrics.createFrameDeliveryTrace(12, 242);
        metrics.recordFrameDeliveryStage(
            incomplete_trace,
            rendering::PreviewFrameDeliveryStage::MailboxPublished);
        const auto stale_trace = metrics.createFrameDeliveryTrace(12, 243);
        metrics.recordFrameDeliveryDrop(
            stale_trace,
            rendering::PreviewFrameDeliveryDropReason::StaleGeneration);
        const auto delivery = metrics.takeSnapshotAndReset().frame_delivery;
        const auto find_delivery_sample = [&](std::uint64_t trace_id) {
            for (std::size_t index = 0; index < delivery.sample_count; ++index) {
                if (delivery.samples[index].trace_id == trace_id) {
                    return &delivery.samples[index];
                }
            }
            return static_cast<const rendering::PreviewFrameDeliverySample*>(nullptr);
        };
        const auto* complete_sample = find_delivery_sample(complete_trace);
        const auto* coalesced_sample = find_delivery_sample(coalesced_trace);
        const auto* incomplete_sample = find_delivery_sample(incomplete_trace);
        const auto* stale_sample = find_delivery_sample(stale_trace);
        require(complete_trace != 0 && complete_sample != nullptr &&
                    complete_sample->last_stage ==
                        rendering::PreviewFrameDeliveryStage::QtFrameSwapped &&
                    complete_sample->drop_reason ==
                        rendering::PreviewFrameDeliveryDropReason::None &&
                    complete_sample->complete &&
                    complete_sample->end_to_end_nanoseconds > 0 &&
                    coalesced_sample != nullptr &&
                    coalesced_sample->drop_reason ==
                        rendering::PreviewFrameDeliveryDropReason::MailboxCoalesced &&
                    !coalesced_sample->complete &&
                    incomplete_sample != nullptr &&
                    !incomplete_sample->complete &&
                    incomplete_sample->last_stage ==
                        rendering::PreviewFrameDeliveryStage::MailboxPublished &&
                    stale_sample != nullptr &&
                    stale_sample->drop_reason ==
                        rendering::PreviewFrameDeliveryDropReason::StaleGeneration &&
                    delivery.incomplete_trace_count == 1 &&
                    delivery.drop_counts[static_cast<std::size_t>(
                        rendering::PreviewFrameDeliveryDropReason::MailboxCoalesced)] == 1 &&
                    delivery.drop_counts[static_cast<std::size_t>(
                        rendering::PreviewFrameDeliveryDropReason::StaleGeneration)] == 1 &&
                    delivery.stage_counts[static_cast<std::size_t>(
                        rendering::PreviewFrameDeliveryStage::QtFrameSwapped)] == 1 &&
                    delivery.timings[static_cast<std::size_t>(
                        rendering::PreviewFrameDeliveryTiming::WorkerToQtSwap)].count == 1,
                "Frame delivery did not correlate terminal, dropped, and incomplete stages.");

        metrics.reset();
        std::uint64_t oldest_trace = 0;
        for (std::size_t index = 0;
             index < rendering::PreviewFrameDeliverySnapshot::kSampleLimit + 509;
             ++index) {
            const auto trace_id = metrics.createFrameDeliveryTrace(15, index);
            if (index == 0) oldest_trace = trace_id;
        }
        metrics.recordFrameDeliveryStage(
            oldest_trace,
            rendering::PreviewFrameDeliveryStage::MailboxPublished);
        const auto bounded = metrics.takeSnapshotAndReset().frame_delivery;
        require(bounded.trace_evictions == 1 &&
                    bounded.unknown_trace_updates == 1 &&
                    bounded.incomplete_trace_count == 512 &&
                    bounded.sample_count == rendering::PreviewFrameDeliverySnapshot::kSampleLimit,
                "Frame delivery traces exceeded their bounded registry or sample limit.");

        metrics.reset();
        constexpr std::size_t delivery_traces_per_thread = 50;
        constexpr std::size_t delivery_producer_count = 4;
        constexpr std::size_t expected_delivery_traces =
            delivery_traces_per_thread * delivery_producer_count;
        std::vector<std::thread> delivery_producers;
        std::atomic_size_t completed_delivery_producers{0};
        std::uint64_t observed_worker_stages = 0;
        std::uint64_t observed_cpu_paint_stages = 0;
        std::uint64_t observed_unknown_updates = 0;
        const auto collect_delivery_counts = [&](
            const rendering::PreviewFrameDeliverySnapshot& partial) {
            observed_worker_stages += partial.stage_counts[static_cast<std::size_t>(
                rendering::PreviewFrameDeliveryStage::WorkerEmitted)];
            observed_cpu_paint_stages += partial.stage_counts[static_cast<std::size_t>(
                rendering::PreviewFrameDeliveryStage::CpuPainted)];
            observed_unknown_updates += partial.unknown_trace_updates;
        };
        for (std::size_t producer = 0;
             producer < delivery_producer_count;
             ++producer) {
            delivery_producers.emplace_back([&metrics, &completed_delivery_producers, producer]() {
                for (std::size_t index = 0;
                     index < delivery_traces_per_thread;
                     ++index) {
                    const auto trace_id = metrics.createFrameDeliveryTrace(
                        producer + 1,
                        static_cast<std::int64_t>(index));
                    metrics.recordFrameDeliveryStage(
                        trace_id,
                        rendering::PreviewFrameDeliveryStage::MailboxPublished);
                    metrics.recordFrameDeliveryStage(
                        trace_id,
                        rendering::PreviewFrameDeliveryStage::ControllerDelivered);
                    metrics.recordFrameDeliveryStage(
                        trace_id,
                        rendering::PreviewFrameDeliveryStage::WindowReceived);
                    metrics.recordFrameDeliveryStage(
                        trace_id,
                        rendering::PreviewFrameDeliveryStage::PreviewSubmitted);
                    metrics.recordFrameDeliveryStage(
                        trace_id,
                        rendering::PreviewFrameDeliveryStage::CpuPainted);
                }
                completed_delivery_producers.fetch_add(1, std::memory_order_release);
            });
        }
        while (completed_delivery_producers.load(std::memory_order_acquire) <
               delivery_producer_count) {
            collect_delivery_counts(metrics.takeSnapshotAndReset().frame_delivery);
            std::this_thread::yield();
        }
        for (auto& producer : delivery_producers) producer.join();
        collect_delivery_counts(metrics.takeSnapshotAndReset().frame_delivery);
        require(observed_worker_stages == expected_delivery_traces &&
                    observed_cpu_paint_stages == expected_delivery_traces &&
                    observed_unknown_updates == 0,
                "Concurrent frame delivery tracing lost correlated stages.");
        metrics.setEnabled(false);
        const auto disabled_after_audio = metrics.takeSnapshotAndReset();
        require(
            !disabled_after_audio.audio_buffered_usecs.has_value(),
            "Disabled metrics retained an audio buffer value.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
