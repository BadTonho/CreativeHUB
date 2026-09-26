#include "media/video_metadata.h"
#include "media/video_playback.h"
#include "rendering/preview_performance_metrics.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path uniqueTestDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("creative-suite-video-playback-test-" + std::to_string(stamp));
}

void expectMediaError(const std::filesystem::path& path, const std::string& expected_text) {
    try {
        static_cast<void>(media::VideoPlaybackSession::open(path));
    } catch (const media::MediaError& error) {
        require(std::string(error.what()).find(expected_text) != std::string::npos,
                "Unexpected media error: " + std::string(error.what()));
        return;
    }

    throw std::runtime_error("Expected VideoPlaybackSession to reject the input.");
}

struct TestVideoWriter final {
    AVFormatContext* format = nullptr;
    AVCodecContext* encoder = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;

    ~TestVideoWriter() {
        if (packet != nullptr) av_packet_free(&packet);
        if (frame != nullptr) av_frame_free(&frame);
        if (encoder != nullptr) avcodec_free_context(&encoder);
        if (format != nullptr) {
            if (format->pb != nullptr &&
                (format->oformat->flags & AVFMT_NOFILE) == 0) {
                avio_closep(&format->pb);
            }
            avformat_free_context(format);
        }
    }
};

void requireFfmpeg(int result, const char* operation) {
    if (result >= 0) return;
    char message[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(result, message, sizeof(message));
    throw std::runtime_error(std::string(operation) + ": " + message);
}

std::filesystem::path createInterframeTestVideo(
    const std::filesystem::path& output_path) {
    constexpr int width = 64;
    constexpr int height = 64;
    constexpr int frame_count = 120;
    const auto output_utf8 = output_path.u8string();
    const std::string output_name(
        reinterpret_cast<const char*>(output_utf8.data()), output_utf8.size());

    TestVideoWriter writer;
    requireFfmpeg(
        avformat_alloc_output_context2(
            &writer.format, nullptr, "matroska", output_name.c_str()),
        "Creating the interframe test container");
    require(writer.format != nullptr, "Creating the interframe test container failed.");
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    require(codec != nullptr, "The FFmpeg MPEG-4 encoder is unavailable for test setup.");
    auto* stream = avformat_new_stream(writer.format, nullptr);
    require(stream != nullptr, "Creating the interframe test stream failed.");

    writer.encoder = avcodec_alloc_context3(codec);
    require(writer.encoder != nullptr, "Allocating the interframe test encoder failed.");
    writer.encoder->codec_type = AVMEDIA_TYPE_VIDEO;
    writer.encoder->codec_id = AV_CODEC_ID_MPEG4;
    writer.encoder->width = width;
    writer.encoder->height = height;
    writer.encoder->pix_fmt = AV_PIX_FMT_YUV420P;
    writer.encoder->time_base = AVRational{1, 30};
    writer.encoder->framerate = AVRational{30, 1};
    writer.encoder->bit_rate = 500000;
    writer.encoder->gop_size = 12;
    writer.encoder->max_b_frames = 0;
    if ((writer.format->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
        writer.encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    requireFfmpeg(avcodec_open2(writer.encoder, codec, nullptr),
                   "Opening the interframe test encoder");
    requireFfmpeg(
        avcodec_parameters_from_context(stream->codecpar, writer.encoder),
        "Writing the interframe test stream parameters");
    stream->time_base = writer.encoder->time_base;
    if ((writer.format->oformat->flags & AVFMT_NOFILE) == 0) {
        requireFfmpeg(avio_open(&writer.format->pb, output_name.c_str(), AVIO_FLAG_WRITE),
                      "Opening the interframe test output");
    }
    requireFfmpeg(avformat_write_header(writer.format, nullptr),
                   "Writing the interframe test header");

    writer.frame = av_frame_alloc();
    writer.packet = av_packet_alloc();
    require(writer.frame != nullptr && writer.packet != nullptr,
            "Allocating interframe test buffers failed.");
    writer.frame->format = writer.encoder->pix_fmt;
    writer.frame->width = width;
    writer.frame->height = height;
    requireFfmpeg(av_frame_get_buffer(writer.frame, 32),
                   "Allocating interframe test pixels");

    const auto write_packets = [&]() {
        while (true) {
            const int receive_result =
                avcodec_receive_packet(writer.encoder, writer.packet);
            if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF) return;
            requireFfmpeg(receive_result, "Encoding an interframe test packet");
            av_packet_rescale_ts(
                writer.packet, writer.encoder->time_base, stream->time_base);
            writer.packet->stream_index = stream->index;
            const int write_result =
                av_interleaved_write_frame(writer.format, writer.packet);
            av_packet_unref(writer.packet);
            requireFfmpeg(write_result, "Writing an interframe test packet");
        }
    };

    for (int index = 0; index < frame_count; ++index) {
        requireFfmpeg(av_frame_make_writable(writer.frame),
                       "Preparing interframe test pixels");
        writer.frame->pts = index;
        for (int y = 0; y < height; ++y) {
            auto* row = writer.frame->data[0] + y * writer.frame->linesize[0];
            for (int x = 0; x < width; ++x) {
                row[x] = static_cast<std::uint8_t>(
                    16 + ((index * 13 + x * 3 + y * 7) % 220));
            }
        }
        for (int y = 0; y < height / 2; ++y) {
            auto* u_row = writer.frame->data[1] + y * writer.frame->linesize[1];
            auto* v_row = writer.frame->data[2] + y * writer.frame->linesize[2];
            for (int x = 0; x < width / 2; ++x) {
                u_row[x] = static_cast<std::uint8_t>(80 + (index % 80));
                v_row[x] = static_cast<std::uint8_t>(160 - (index % 80));
            }
        }
        requireFfmpeg(avcodec_send_frame(writer.encoder, writer.frame),
                       "Submitting an interframe test frame");
        write_packets();
    }
    requireFfmpeg(avcodec_send_frame(writer.encoder, nullptr),
                   "Flushing the interframe test encoder");
    write_packets();
    requireFfmpeg(av_write_trailer(writer.format),
                   "Finishing the interframe test container");
    return output_path;
}

void validateForwardDecode(
    const std::filesystem::path& path,
    rendering::PreviewPerformanceMetrics& metrics) {
    constexpr std::int64_t target_frame = 8;

    auto reference_session = media::VideoPlaybackSession::open(path);
    media::VideoFramePtr reference;
    for (std::int64_t index = 0; index <= target_frame; ++index) {
        const auto frame = reference_session->decode_next_frame();
        require(frame.has_value() && *frame != nullptr,
                "The reference frame for forward decode was not available.");
        reference = *frame;
    }

    auto session = media::VideoPlaybackSession::open(path);
    const auto first = session->decode_next_frame();
    require(first.has_value() && *first != nullptr,
            "Forward decode could not initialize its decoder position.");

    metrics.reset();
    media::ForwardDecodeDiagnostics forward_diagnostics;
    const auto advanced = session->decode_forward_to(
        target_frame,
        {},
        &forward_diagnostics);
    require(advanced.has_value() && *advanced != nullptr,
            "Forward decode did not return its target frame.");
    require(session->current_frame_index() == target_frame,
            "Forward decode finished at the wrong frame.");
    require((*advanced)->width == reference->width &&
                (*advanced)->height == reference->height &&
                (*advanced)->stride == reference->stride &&
                (*advanced)->rgba_pixels == reference->rgba_pixels,
            "Forward decode changed the target RGBA frame.");

    const auto forward_snapshot = metrics.takeSnapshotAndReset();
    require(forward_snapshot.decode_discarded_frames == target_frame - 1,
            "Forward decode did not discard the expected intermediate frames.");
    require(forward_snapshot.pixel_conversion.count == 1,
            "Forward decode converted an intermediate frame to RGBA.");
    const auto forward_substage_nanoseconds =
        forward_diagnostics.packet_io_nanoseconds +
        forward_diagnostics.decoder_receive_nanoseconds +
        forward_diagnostics.target_pixel_conversion_nanoseconds;
    require(forward_diagnostics.collected && forward_diagnostics.attempted &&
                forward_diagnostics.completed && !forward_diagnostics.cancelled &&
                forward_diagnostics.starting_frame == 0 &&
                forward_diagnostics.requested_frame == target_frame &&
                forward_diagnostics.discarded_intermediate_frames == target_frame - 1 &&
                forward_diagnostics.elapsed_nanoseconds > 0 &&
                forward_diagnostics.target_pixel_conversion_nanoseconds > 0 &&
                forward_substage_nanoseconds <=
                    forward_diagnostics.elapsed_nanoseconds,
            "Forward decode diagnostics did not describe the measured operation.");

    const auto cached_target = session->decode_frame_at(target_frame);
    require(cached_target.has_value() && *cached_target == *advanced,
            "Forward decode did not cache its final shared frame.");
    require(session->take_cache_hit_count() >= 1,
            "Forward decode did not reuse its final cached frame.");

    const auto uncached_intermediate = session->decode_frame_at(target_frame - 1);
    require(uncached_intermediate.has_value() && *uncached_intermediate != nullptr,
            "The frame before a forward target could not be decoded.");
    require(session->take_cache_hit_count() == 0,
            "Forward decode cached a discarded intermediate frame.");

    auto cancelled_session = media::VideoPlaybackSession::open(path);
    require(cancelled_session->decode_next_frame().has_value(),
            "The cancellation test could not initialize its decoder position.");
    int cancellation_checks = 0;
    media::ForwardDecodeDiagnostics cancelled_diagnostics;
    const auto cancelled = cancelled_session->decode_forward_to(
        target_frame,
        [&cancellation_checks]() {
            return ++cancellation_checks >= 3;
        },
        &cancelled_diagnostics);
    const auto cancelled_substage_nanoseconds =
        cancelled_diagnostics.packet_io_nanoseconds +
        cancelled_diagnostics.decoder_receive_nanoseconds +
        cancelled_diagnostics.target_pixel_conversion_nanoseconds;
    require(!cancelled.has_value(),
            "Forward decode ignored its cancellation predicate.");
    require(cancelled_session->current_frame_index() < target_frame,
            "Cancelled forward decode reached its target frame.");
    require(cancelled_diagnostics.collected &&
                cancelled_diagnostics.attempted &&
                !cancelled_diagnostics.completed &&
                cancelled_diagnostics.cancelled &&
                cancelled_diagnostics.starting_frame == 0 &&
                cancelled_diagnostics.requested_frame == target_frame &&
                cancelled_diagnostics.discarded_intermediate_frames == 2 &&
                cancelled_diagnostics.elapsed_nanoseconds > 0 &&
                cancelled_diagnostics.target_pixel_conversion_nanoseconds == 0 &&
                cancelled_substage_nanoseconds <=
                    cancelled_diagnostics.elapsed_nanoseconds,
            "Cancelled forward decode did not preserve its partial diagnostics.");
    const auto recovered = cancelled_session->decode_frame_at(target_frame);
    require(recovered.has_value() && *recovered != nullptr,
            "A random seek did not recover after forward decode cancellation.");

    metrics.setEnabled(false);
    auto unmeasured_session = media::VideoPlaybackSession::open(path);
    require(unmeasured_session->decode_next_frame().has_value(),
            "The disabled-metrics test could not initialize its decoder.");
    media::ForwardDecodeDiagnostics disabled_diagnostics;
    const auto unmeasured = unmeasured_session->decode_forward_to(
        target_frame,
        {},
        &disabled_diagnostics);
    require(unmeasured.has_value() && !disabled_diagnostics.collected &&
                !disabled_diagnostics.attempted &&
                disabled_diagnostics.elapsed_nanoseconds == 0 &&
                disabled_diagnostics.packet_io_nanoseconds == 0 &&
                disabled_diagnostics.decoder_receive_nanoseconds == 0 &&
                disabled_diagnostics.target_pixel_conversion_nanoseconds == 0,
            "Forward decode collected detailed timings while metrics were disabled.");
    metrics.setEnabled(true);
}

void validateLongSeekDecode(
    const std::filesystem::path& path,
    rendering::PreviewPerformanceMetrics& metrics) {
    constexpr std::int64_t target_frame = 31;
    constexpr std::int64_t next_frame = target_frame + 1;

    auto reference_session = media::VideoPlaybackSession::open(path);
    media::VideoFramePtr reference_target;
    media::VideoFramePtr reference_next;
    for (std::int64_t index = 0; index <= next_frame; ++index) {
        const auto frame = reference_session->decode_next_frame();
        require(frame.has_value() && *frame != nullptr,
                "The reference frames for long seek decode were not available.");
        if (index == target_frame) reference_target = *frame;
        if (index == next_frame) reference_next = *frame;
    }

    auto session = media::VideoPlaybackSession::open(path);
    const auto first = session->decode_next_frame();
    require(first.has_value() && *first != nullptr,
            "Long seek decode could not initialize its decoder position.");

    metrics.reset();
    const auto sought = session->decode_frame_at(target_frame);
    const auto seek_snapshot = metrics.takeSnapshotAndReset();
    require(sought.has_value() && *sought != nullptr &&
                (*sought)->rgba_pixels == reference_target->rgba_pixels,
            "Long seek decode changed the target RGBA frame.");
    require(session->current_frame_index() == target_frame,
            "Long seek decode finished at the wrong source frame.");
    require(seek_snapshot.decode_discarded_frames > 0 &&
                seek_snapshot.pixel_conversion.count == 1,
            "Long seek decode did not discard raw intermediates as expected (discarded=" +
                std::to_string(seek_snapshot.decode_discarded_frames) +
                ", conversions=" +
                std::to_string(seek_snapshot.pixel_conversion.count) + ").");

    const auto continued = session->decode_next_frame();
    require(continued.has_value() && *continued != nullptr &&
                (*continued)->rgba_pixels == reference_next->rgba_pixels &&
                session->current_frame_index() == next_frame,
            "Sequential decoding did not continue correctly after a long seek.");
    static_cast<void>(session->take_cache_hit_count());
    const auto cached_target = session->decode_frame_at(target_frame);
    require(cached_target.has_value() && *cached_target == *sought &&
                session->take_cache_hit_count() >= 1,
            "The requested long-seek frame was not cached.");
    static_cast<void>(session->take_cache_hit_count());
    const auto uncached_intermediate = session->decode_frame_at(target_frame - 1);
    require(uncached_intermediate.has_value() && *uncached_intermediate != nullptr &&
                session->take_cache_hit_count() == 0,
            "Long seek decode cached an intermediate frame.");

    auto cancelled_session = media::VideoPlaybackSession::open(path);
    require(cancelled_session->decode_next_frame().has_value(),
            "The long-seek cancellation test could not initialize its decoder.");
    int cancellation_checks = 0;
    metrics.reset();
    const auto cancelled = cancelled_session->decode_frame_at(
        target_frame,
        [&cancellation_checks]() { return ++cancellation_checks >= 5; });
    const auto cancelled_snapshot = metrics.takeSnapshotAndReset();
    require(!cancelled.has_value() &&
                cancelled_session->current_frame_index() < target_frame &&
                cancelled_snapshot.pixel_conversion.count == 0,
            "A cancelled long seek materialized or returned an obsolete target frame.");

    auto fallback_session = media::VideoPlaybackSession::open(path);
    require(fallback_session->decode_next_frame().has_value(),
            "The failed-seek fallback test could not initialize its decoder.");
    metrics.reset();
    const auto out_of_range = fallback_session->decode_frame_at(
        std::numeric_limits<std::int64_t>::max());
    const auto fallback_snapshot = metrics.takeSnapshotAndReset();
    require(!out_of_range.has_value() && fallback_session->at_end() &&
                fallback_session->current_frame_index() > 0 &&
                fallback_snapshot.pixel_conversion.count == 0,
            "A rejected seek did not continue safely from the valid sequential position.");
}

void validateReference(const std::filesystem::path& path) {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.setEnabled(true);
    metrics.reset();

    validateForwardDecode(path, metrics);
    metrics.reset();

    auto session = media::VideoPlaybackSession::open(path);
    require(session->current_frame_index() == -1, "Session did not start before the first frame.");

    const auto first = session->decode_next_frame();
    require(first.has_value() && *first != nullptr, "The first frame was not decoded.");
    require((*first)->width == 640 && (*first)->height == 360,
            "Unexpected first frame dimensions.");
    const auto first_pixels = (*first)->rgba_pixels;
    require(session->current_frame_index() == 0, "First frame index is incorrect.");

    const auto second = session->decode_next_frame();
    require(second.has_value() && *second != nullptr, "The second frame was not decoded.");
    require(session->current_frame_index() == 1, "Second frame index is incorrect.");

    const auto first_seek = session->decode_frame_at(0);
    require(first_seek.has_value() && *first_seek != nullptr,
            "Seeking to the first frame failed.");
    require(session->current_frame_index() == 0, "Previous frame index is incorrect.");
    require((*first_seek)->width == 640 && (*first_seek)->height == 360,
            "The first seek frame dimensions are incorrect.");
    require((*first_seek)->rgba_pixels == first_pixels,
            "Seeking back to the first frame changed the converted RGBA pixels.");
    require(*first_seek == *first,
            "A cache hit did not return the original shared video frame.");
    require(session->take_cache_hit_count() >= 1,
            "A previously decoded frame was not reused from the cache.");

    const auto intermediate = session->decode_frame_at(30);
    require(intermediate.has_value() && *intermediate != nullptr,
            "The intermediate frame could not be decoded.");
    require(session->current_frame_index() == 30, "Intermediate frame index is incorrect.");
    require((*intermediate)->width == 640 && (*intermediate)->height == 360,
            "The intermediate frame dimensions are incorrect.");

    const auto after_intermediate = session->decode_next_frame();
    require(after_intermediate.has_value() && *after_intermediate != nullptr,
            "Decoding did not continue after seeking.");
    require(session->current_frame_index() == 31,
            "The frame after an intermediate seek has the wrong index.");
    require(session->take_cache_hit_count() == 0,
            "Sequential decoding unexpectedly reported a cache hit.");

    bool negative_seek_rejected = false;
    try {
        static_cast<void>(session->decode_frame_at(-1));
    } catch (const media::MediaError& error) {
        require(std::string(error.what()).find("negative") != std::string::npos,
                "Unexpected negative seek error.");
        negative_seek_rejected = true;
    }
    require(negative_seek_rejected, "Negative seek was not rejected.");

    session->reset();
    std::int64_t final_frame_index = -1;
    std::size_t decoded_frames = 0;
    while (const auto frame = session->decode_next_frame()) {
        require(*frame != nullptr && (*frame)->width == 640 && (*frame)->height == 360,
                "A sequential frame has unexpected dimensions.");
        final_frame_index = session->current_frame_index();
        ++decoded_frames;
    }
    require(decoded_frames >= 2, "The session did not decode a complete sequence.");
    require(final_frame_index >= 0, "The reference video has no final frame.");
    require(session->at_end(), "The session did not report end-of-file.");

    session->reset();
    const auto final_frame = session->decode_frame_at(final_frame_index);
    require(final_frame.has_value() && *final_frame != nullptr,
            "Seeking to the final frame failed.");
    require(session->current_frame_index() == final_frame_index,
            "Final seek frame index is incorrect.");
    require((*final_frame)->width == 640 && (*final_frame)->height == 360,
            "The final seek frame dimensions are incorrect.");

    const auto after_final = session->decode_next_frame();
    require(!after_final.has_value(), "A frame was decoded after the final frame.");
    require(session->at_end(), "The final-frame seek did not reach end-of-file.");

    const auto outside_range = session->decode_frame_at(10000);
    require(!outside_range.has_value(), "An out-of-range frame was decoded.");
    require(session->at_end(), "Out-of-range seeking did not reach end-of-file.");

    session->reset();
    require(session->current_frame_index() == -1, "Reset did not restore the initial index.");

    const auto snapshot = metrics.takeSnapshotAndReset();
    require(snapshot.frame_cache_copy.count == 0,
            "The shared playback path still copied pixels into the frame cache.");
    metrics.setEnabled(false);
}

} // namespace

int main(int argc, char* argv[]) {
    const auto directory = uniqueTestDirectory();

    try {
        std::filesystem::create_directories(directory);
        expectMediaError(directory / "missing.mkv", "Input is not a readable regular file");

        const auto empty_file = directory / "empty.mkv";
        std::ofstream(empty_file, std::ios::binary).close();
        expectMediaError(empty_file, "Opening media for playback");

        if (argc == 2) {
            validateReference(argv[1]);
            const auto interframe_path = createInterframeTestVideo(
                directory / "interframe-seek.mkv");
            auto& metrics = rendering::PreviewPerformanceMetrics::instance();
            metrics.setEnabled(true);
            validateLongSeekDecode(interframe_path, metrics);
            metrics.setEnabled(false);
        }
    } catch (const std::exception& error) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(directory, cleanup_error);
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::error_code cleanup_error;
    std::filesystem::remove_all(directory, cleanup_error);
    return cleanup_error ? 1 : 0;
}
