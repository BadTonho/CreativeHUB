#pragma once

// Deterministic tagged YUV fixtures shared by CPU and native conversion tests.
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}
#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <cstring>

namespace creative_suite::media::test_support {
inline void remuxWithOrientation(const std::filesystem::path& source, const std::filesystem::path& target,
    const std::array<std::int32_t, 9>& matrix) {
    struct Resources {
        AVFormatContext* input = nullptr; AVFormatContext* output = nullptr; AVPacket* packet = nullptr;
        ~Resources() {
            av_packet_free(&packet); avformat_close_input(&input);
            if (output) { if (output->pb) avio_closep(&output->pb); avformat_free_context(output); }
        }
    } resources;
    auto utf8 = [](const std::filesystem::path& path) { const auto value = path.u8string();
        return std::string(reinterpret_cast<const char*>(value.data()), value.size()); };
    auto check = [](int result) { if (result < 0) throw std::runtime_error("Orientation fixture remux failed: " + std::to_string(result)); };
    check(avformat_open_input(&resources.input, utf8(source).c_str(), nullptr, nullptr));
    check(avformat_find_stream_info(resources.input, nullptr));
    check(avformat_alloc_output_context2(&resources.output, nullptr, "mp4", utf8(target).c_str()));
    auto* stream = avformat_new_stream(resources.output, nullptr);
    if (!stream) throw std::runtime_error("Orientation fixture stream allocation failed.");
    check(avcodec_parameters_copy(stream->codecpar, resources.input->streams[0]->codecpar));
    stream->codecpar->codec_tag = 0;
    const auto* side = av_packet_side_data_new(&stream->codecpar->coded_side_data,
        &stream->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX, sizeof(matrix), 0);
    if (!side) throw std::runtime_error("Orientation fixture matrix allocation failed.");
    std::memcpy(side->data, matrix.data(), sizeof(matrix));
    stream->time_base = resources.input->streams[0]->time_base;
    check(avio_open(&resources.output->pb, utf8(target).c_str(), AVIO_FLAG_WRITE));
    check(avformat_write_header(resources.output, nullptr));
    resources.packet = av_packet_alloc();
    if (!resources.packet) throw std::runtime_error("Orientation fixture packet allocation failed.");
    int result = 0;
    while ((result = av_read_frame(resources.input, resources.packet)) >= 0) {
        av_packet_rescale_ts(resources.packet, resources.input->streams[0]->time_base, stream->time_base);
        resources.packet->stream_index = stream->index; resources.packet->pos = -1;
        check(av_interleaved_write_frame(resources.output, resources.packet)); av_packet_unref(resources.packet);
    }
    if (result != AVERROR_EOF) check(result);
    check(av_write_trailer(resources.output));
}
// Clockwise rotations in top-down pixel coordinates; reflection follows rotation.
inline std::array<std::int32_t, 9> orientationMatrix(int turns, bool mirror = false) {
    constexpr std::array<std::array<int, 4>, 4> values{{{1,0,0,1}, {0,1,-1,0}, {-1,0,0,-1}, {0,-1,1,0}}};
    const auto& value = values[turns];
    return {value[0] * 65536 * (mirror ? -1 : 1), value[1] * 65536, 0,
        value[2] * 65536 * (mirror ? -1 : 1), value[3] * 65536, 0, 0, 0, 1 << 30};
}
template<class Frame> void checkOrientedPixels(const Frame& raw, const Frame& actual, int turns, bool mirror) {
    const int width = turns % 2 ? raw.height : raw.width, height = turns % 2 ? raw.width : raw.height;
    if (actual.width != width || actual.height != height) throw std::runtime_error("Video orientation geometry is incorrect.");
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const int rx = mirror ? width - 1 - x : x;
        const int sx = turns == 0 ? rx : turns == 1 ? y : turns == 2 ? raw.width - 1 - rx : raw.width - 1 - y;
        const int sy = turns == 0 ? y : turns == 1 ? raw.height - 1 - rx : turns == 2 ? raw.height - 1 - y : rx;
        for (int channel = 0; channel < 4; ++channel) {
            if (raw.rgba_pixels[sy * raw.stride + sx * 4 + channel] != actual.rgba_pixels[y * actual.stride + x * 4 + channel])
                throw std::runtime_error("Video orientation mismatch: turns=" + std::to_string(turns) +
                    " mirror=" + std::to_string(mirror) + " x=" + std::to_string(x) + " y=" + std::to_string(y) +
                    " expected=" + std::to_string(raw.rgba_pixels[sy * raw.stride + sx * 4 + channel]) +
                    " actual=" + std::to_string(actual.rgba_pixels[y * actual.stride + x * 4 + channel]));
        }
    }
}
inline constexpr std::array<std::array<int, 3>, 4> patches{{
    {63, 102, 240}, {160, 60, 60}, {100, 230, 120}, {32, 128, 128}}};
inline void checked(int result, const char* operation) {
    if (result >= 0) return;
    char text[AV_ERROR_MAX_STRING_SIZE]{}; av_strerror(result, text, sizeof(text));
    throw std::runtime_error(std::string(operation) + ": " + text);
}
inline void createTaggedVideo(const std::filesystem::path& path, AVColorSpace matrix,
    AVColorRange range, const char* encoder_name = "ffv1") {
    struct Resources {
        AVFormatContext* format = nullptr; AVCodecContext* encoder = nullptr;
        AVFrame* frame = nullptr; AVPacket* packet = nullptr;
        ~Resources() {
            av_frame_free(&frame); av_packet_free(&packet); avcodec_free_context(&encoder);
            if (format) { if (format->pb) avio_closep(&format->pb); avformat_free_context(format); }
        }
    } writer;
    const auto utf8 = path.u8string();
    const std::string name(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    checked(avformat_alloc_output_context2(&writer.format, nullptr, "matroska", name.c_str()), "Creating tagged container");
    const auto* codec = avcodec_find_encoder_by_name(encoder_name);
    if (!codec || !writer.format) throw std::runtime_error("Tagged video encoder is unavailable.");
    auto* stream = avformat_new_stream(writer.format, nullptr);
    writer.encoder = avcodec_alloc_context3(codec);
    if (!stream || !writer.encoder) throw std::runtime_error("Allocating tagged stream failed.");
    auto& encoder = *writer.encoder;
    encoder.width = 320; encoder.height = 180; encoder.pix_fmt = AV_PIX_FMT_YUV420P;
    encoder.time_base = {1, 30}; encoder.framerate = {30, 1};
    encoder.colorspace = matrix; encoder.color_range = range;
    encoder.bit_rate = 4000000; encoder.gop_size = 12; encoder.max_b_frames = 0;
    if (writer.format->oformat->flags & AVFMT_GLOBALHEADER) encoder.flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    checked(avcodec_open2(&encoder, codec, nullptr), "Opening tagged encoder");
    checked(avcodec_parameters_from_context(stream->codecpar, &encoder), "Writing tagged parameters");
    stream->time_base = encoder.time_base;
    checked(avio_open(&writer.format->pb, name.c_str(), AVIO_FLAG_WRITE), "Opening tagged output");
    checked(avformat_write_header(writer.format, nullptr), "Writing tagged header");
    writer.frame = av_frame_alloc(); writer.packet = av_packet_alloc();
    if (!writer.frame || !writer.packet) throw std::runtime_error("Allocating tagged frame failed.");
    writer.frame->width = encoder.width; writer.frame->height = encoder.height;
    writer.frame->format = encoder.pix_fmt; writer.frame->colorspace = matrix; writer.frame->color_range = range;
    checked(av_frame_get_buffer(writer.frame, 32), "Allocating tagged pixels");
    auto drain = [&] {
        while (true) {
            const auto result = avcodec_receive_packet(&encoder, writer.packet);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return;
            checked(result, "Receiving tagged packet");
            av_packet_rescale_ts(writer.packet, encoder.time_base, stream->time_base);
            writer.packet->stream_index = stream->index;
            checked(av_interleaved_write_frame(writer.format, writer.packet), "Writing tagged packet");
            av_packet_unref(writer.packet);
        }
    };
    for (int index = 0; index < 3; ++index) {
        checked(av_frame_make_writable(writer.frame), "Preparing tagged pixels");
        for (int plane = 0; plane < 3; ++plane) {
            const int divisor = plane == 0 ? 1 : 2;
            const int width = encoder.width / divisor, height = encoder.height / divisor;
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
                writer.frame->data[plane][y * writer.frame->linesize[plane] + x] =
                    static_cast<std::uint8_t>(patches[(y >= height / 2) * 2 + (x >= width / 2)][plane]);
        }
        writer.frame->pts = index;
        checked(avcodec_send_frame(&encoder, writer.frame), "Sending tagged frame"); drain();
    }
    checked(avcodec_send_frame(&encoder, nullptr), "Flushing tagged encoder"); drain();
    checked(av_write_trailer(writer.format), "Finishing tagged video");
}
// Independent matrix equations; never derive the expected RGB from the decoder.
inline std::array<int, 3> expectedRgb(int patch, AVColorSpace matrix, AVColorRange range) {
    const double kr = matrix == AVCOL_SPC_BT709 ? .2126 : matrix == AVCOL_SPC_BT2020_NCL ? .2627 : .299;
    const double kb = matrix == AVCOL_SPC_BT709 ? .0722 : matrix == AVCOL_SPC_BT2020_NCL ? .0593 : .114;
    const auto& value = patches[patch];
    const double y = range == AVCOL_RANGE_JPEG ? value[0] : (value[0] - 16) * 255.0 / 219;
    const double u = (value[1] - 128) * (range == AVCOL_RANGE_JPEG ? 1.0 : 255.0 / 224);
    const double v = (value[2] - 128) * (range == AVCOL_RANGE_JPEG ? 1.0 : 255.0 / 224);
    return {std::clamp(int(std::lround(y + 2 * (1 - kr) * v)), 0, 255),
        std::clamp(int(std::lround(y - 2 * kb * (1 - kb) / (1 - kr - kb) * u -
            2 * kr * (1 - kr) / (1 - kr - kb) * v)), 0, 255),
        std::clamp(int(std::lround(y + 2 * (1 - kb) * u)), 0, 255)};
}
template<class Frame> void checkTaggedPixels(const Frame& frame, AVColorSpace matrix, AVColorRange range, int tolerance = 3) {
    for (int patch = 0; patch < 4; ++patch) {
        const auto rgb = expectedRgb(patch, matrix, range);
        const auto offset = (frame.height * (patch / 2 * 2 + 1) / 4) * frame.stride +
            (frame.width * (patch % 2 * 2 + 1) / 4) * 4;
        for (int channel = 0; channel < 3; ++channel) {
            if (std::abs(int(frame.rgba_pixels[offset + channel]) - rgb[channel]) > tolerance)
                throw std::runtime_error("Tagged YUV matrix/range did not match independent RGB values.");
        }
        if (frame.rgba_pixels[offset + 3] != 255) throw std::runtime_error("Tagged conversion changed alpha.");
    }
}
}
