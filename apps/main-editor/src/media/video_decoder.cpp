#include "video_decoder.h"

#include "../logging/logger.h"
#include "video_metadata.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <system_error>

namespace media {
namespace {

struct FormatContextDeleter {
    void operator()(AVFormatContext* context) const noexcept {
        if (context != nullptr) avformat_close_input(&context);
    }
};

struct CodecContextDeleter {
    void operator()(AVCodecContext* context) const noexcept {
        if (context != nullptr) avcodec_free_context(&context);
    }
};

struct PacketDeleter {
    void operator()(AVPacket* packet) const noexcept {
        if (packet != nullptr) av_packet_free(&packet);
    }
};

struct FrameDeleter {
    void operator()(AVFrame* frame) const noexcept {
        if (frame != nullptr) av_frame_free(&frame);
    }
};

struct SwsContextDeleter {
    void operator()(SwsContext* context) const noexcept {
        if (context != nullptr) sws_freeContext(context);
    }
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;

std::string toUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::string safePathForLog(const std::filesystem::path& path) noexcept {
    try {
        return toUtf8(path);
    } catch (...) {
        return "<unavailable>";
    }
}

std::string ffmpegError(int result) {
    char message[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(result, message, sizeof(message)) == 0) return message;
    return "Unknown FFmpeg error (" + std::to_string(result) + ")";
}

[[noreturn]] void throwFfmpegError(int result, const std::string& operation) {
    throw MediaError(operation + ": " + ffmpegError(result), result);
}

void validateInputFile(const std::filesystem::path& source_path) {
    if (source_path.empty()) throw MediaError("Media path is empty.");

    std::error_code file_error;
    if (!std::filesystem::is_regular_file(source_path, file_error) || file_error) {
        const auto code = file_error ? std::optional<int>(file_error.value()) : std::nullopt;
        throw MediaError("Input is not a readable regular file: " + toUtf8(source_path), code);
    }
}

VideoFrame copyRgbaFrame(const AVFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0) {
        throw MediaError("Decoded video frame has invalid dimensions.");
    }

    const auto width = static_cast<std::size_t>(frame.width);
    const auto height = static_cast<std::size_t>(frame.height);
    constexpr std::size_t bytes_per_pixel = 4;
    if (width > std::numeric_limits<std::size_t>::max() / bytes_per_pixel ||
        width * bytes_per_pixel > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw MediaError("Decoded video frame is too large.");
    }

    const std::size_t stride = width * bytes_per_pixel;
    if (height > std::numeric_limits<std::size_t>::max() / stride) {
        throw MediaError("Decoded video frame is too large.");
    }

    VideoFrame result;
    result.width = frame.width;
    result.height = frame.height;
    result.stride = static_cast<int>(stride);
    result.rgba_pixels.resize(stride * height);

    SwsContextPtr scaler(sws_getContext(
        frame.width,
        frame.height,
        static_cast<AVPixelFormat>(frame.format),
        frame.width,
        frame.height,
        AV_PIX_FMT_RGBA,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr));
    if (!scaler) throw MediaError("Could not create the video color conversion context.");

    std::uint8_t* destination_data[4] = {result.rgba_pixels.data(), nullptr, nullptr, nullptr};
    int destination_linesize[4] = {result.stride, 0, 0, 0};
    const int scaled_height = sws_scale(
        scaler.get(),
        frame.data,
        frame.linesize,
        0,
        frame.height,
        destination_data,
        destination_linesize);
    if (scaled_height != frame.height) {
        throw MediaError("Could not convert the decoded video frame to RGBA.");
    }

    return result;
}

} // namespace

VideoFrame VideoDecoder::decode_first_frame(const std::filesystem::path& source_path) const {
    try {
        validateInputFile(source_path);

        AVFormatContext* raw_format = nullptr;
        const std::string input_path = toUtf8(source_path);
        const int open_result = avformat_open_input(&raw_format, input_path.c_str(), nullptr, nullptr);
        if (open_result < 0) {
            if (raw_format != nullptr) avformat_close_input(&raw_format);
            throwFfmpegError(open_result, "Opening media for preview");
        }
        FormatContextPtr format(raw_format);

        const int stream_info_result = avformat_find_stream_info(format.get(), nullptr);
        if (stream_info_result < 0) {
            throwFfmpegError(stream_info_result, "Reading media stream information for preview");
        }

        const AVCodec* codec = nullptr;
        const int stream_index = av_find_best_stream(
            format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (stream_index < 0 || codec == nullptr) {
            throw MediaError("No supported video stream was found for preview.", stream_index);
        }

        AVStream* stream = format->streams[stream_index];
        if (stream == nullptr || stream->codecpar == nullptr) {
            throw MediaError("The video stream has no codec parameters for preview.");
        }

        CodecContextPtr decoder(avcodec_alloc_context3(codec));
        if (!decoder) throw MediaError("Could not allocate the video decoder context.");

        const int parameters_result = avcodec_parameters_to_context(decoder.get(), stream->codecpar);
        if (parameters_result < 0) {
            throwFfmpegError(parameters_result, "Reading video codec parameters for preview");
        }

        const int decoder_result = avcodec_open2(decoder.get(), codec, nullptr);
        if (decoder_result < 0) throwFfmpegError(decoder_result, "Opening video decoder for preview");

        PacketPtr packet(av_packet_alloc());
        if (!packet) throw MediaError("Could not allocate a video packet.");

        FramePtr frame(av_frame_alloc());
        if (!frame) throw MediaError("Could not allocate a decoded video frame.");

        bool flush_sent = false;
        while (true) {
            const int read_result = av_read_frame(format.get(), packet.get());
            if (read_result == AVERROR_EOF) {
                if (flush_sent) {
                    throw MediaError("The video ended before a frame could be decoded.", read_result);
                }

                const int flush_result = avcodec_send_packet(decoder.get(), nullptr);
                flush_sent = true;
                if (flush_result < 0 && flush_result != AVERROR_EOF) {
                    throwFfmpegError(flush_result, "Flushing video decoder");
                }
            } else if (read_result < 0) {
                throwFfmpegError(read_result, "Reading video packet for preview");
            } else {
                if (packet->stream_index == stream_index) {
                    const int send_result = avcodec_send_packet(decoder.get(), packet.get());
                    av_packet_unref(packet.get());
                    if (send_result < 0 && send_result != AVERROR(EAGAIN)) {
                        throwFfmpegError(send_result, "Sending video packet to decoder");
                    }
                } else {
                    av_packet_unref(packet.get());
                    continue;
                }
            }

            const int receive_result = avcodec_receive_frame(decoder.get(), frame.get());
            if (receive_result == 0) return copyRgbaFrame(*frame);
            if (receive_result == AVERROR(EAGAIN)) continue;
            if (receive_result == AVERROR_EOF) {
                throw MediaError("The video ended before a frame could be decoded.", receive_result);
            }
            throwFfmpegError(receive_result, "Receiving decoded video frame");
        }
    } catch (const MediaError& error) {
        try {
            logging::Context context{{"path", safePathForLog(source_path)}};
            if (error.error_code().has_value()) {
                context.emplace_back("error_code", std::to_string(*error.error_code()));
            }
            logging::Logger::instance().log(
                logging::Level::Error,
                "media",
                "decode_first_frame",
                error.what(),
                context);
        } catch (...) {
            // Preserve the original media error even if diagnostic context allocation fails.
        }
        throw;
    } catch (const std::exception& error) {
        try {
            logging::Logger::instance().log(
                logging::Level::Error,
                "media",
                "decode_first_frame",
                error.what(),
                {{"path", safePathForLog(source_path)}});
        } catch (...) {
            // Preserve the original exception even if diagnostic context allocation fails.
        }
        throw;
    }
}

} // namespace media
