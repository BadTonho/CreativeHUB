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

namespace creative_suite::media::test_support {
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
