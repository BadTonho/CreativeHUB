#include "video_playback.h"

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

void logFailure(const std::filesystem::path& source_path,
                const char* operation,
                const media::MediaError& error) noexcept {
    try {
        logging::Context context{{"path", safePathForLog(source_path)}};
        if (error.error_code().has_value()) {
            context.emplace_back("error_code", std::to_string(*error.error_code()));
        }
        logging::Logger::instance().log(
            logging::Level::Error,
            "media",
            operation,
            error.what(),
            context);
    } catch (...) {
        // Preserve the original media error even if diagnostic context allocation fails.
    }
}

void logFailure(const std::filesystem::path& source_path,
                const char* operation,
                const std::exception& error) noexcept {
    try {
        logging::Logger::instance().log(
            logging::Level::Error,
            "media",
            operation,
            error.what(),
            {{"path", safePathForLog(source_path)}});
    } catch (...) {
        // Preserve the original exception even if diagnostic context allocation fails.
    }
}

} // namespace

struct VideoPlaybackSession::Impl {
    std::filesystem::path source_path;
    FormatContextPtr format;
    CodecContextPtr decoder;
    PacketPtr packet;
    FramePtr frame;
    int stream_index = -1;
    bool flush_sent = false;
    bool end_reached = false;
    std::int64_t current_frame_index = -1;
};

VideoPlaybackSession::VideoPlaybackSession(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

VideoPlaybackSession::~VideoPlaybackSession() = default;

VideoPlaybackSession::VideoPlaybackSession(VideoPlaybackSession&&) noexcept = default;

VideoPlaybackSession& VideoPlaybackSession::operator=(VideoPlaybackSession&&) noexcept = default;

std::unique_ptr<VideoPlaybackSession> VideoPlaybackSession::open(
    const std::filesystem::path& source_path) {
    try {
        return std::unique_ptr<VideoPlaybackSession>(
            new VideoPlaybackSession(openImpl(source_path)));
    } catch (const MediaError& error) {
        logFailure(source_path, "playback_open", error);
        throw;
    } catch (const std::exception& error) {
        logFailure(source_path, "playback_open", error);
        throw;
    }
}

std::unique_ptr<VideoPlaybackSession::Impl> VideoPlaybackSession::openImpl(
    const std::filesystem::path& source_path) {
    validateInputFile(source_path);

    auto impl = std::make_unique<Impl>();
    impl->source_path = source_path;

    AVFormatContext* raw_format = nullptr;
    const std::string input_path = toUtf8(source_path);
    const int open_result = avformat_open_input(&raw_format, input_path.c_str(), nullptr, nullptr);
    if (open_result < 0) {
        if (raw_format != nullptr) avformat_close_input(&raw_format);
        throwFfmpegError(open_result, "Opening media for playback");
    }
    impl->format.reset(raw_format);

    const int stream_info_result = avformat_find_stream_info(impl->format.get(), nullptr);
    if (stream_info_result < 0) {
        throwFfmpegError(stream_info_result, "Reading media stream information for playback");
    }

    const AVCodec* codec = nullptr;
    impl->stream_index = av_find_best_stream(
        impl->format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (impl->stream_index < 0 || codec == nullptr) {
        throw MediaError("No supported video stream was found for playback.", impl->stream_index);
    }

    AVStream* stream = impl->format->streams[impl->stream_index];
    if (stream == nullptr || stream->codecpar == nullptr) {
        throw MediaError("The video stream has no codec parameters for playback.");
    }

    impl->decoder.reset(avcodec_alloc_context3(codec));
    if (!impl->decoder) throw MediaError("Could not allocate the video decoder context.");

    const int parameters_result = avcodec_parameters_to_context(
        impl->decoder.get(), stream->codecpar);
    if (parameters_result < 0) {
        throwFfmpegError(parameters_result, "Reading video codec parameters for playback");
    }

    const int decoder_result = avcodec_open2(impl->decoder.get(), codec, nullptr);
    if (decoder_result < 0) throwFfmpegError(decoder_result, "Opening video decoder for playback");

    impl->packet.reset(av_packet_alloc());
    if (!impl->packet) throw MediaError("Could not allocate a video packet.");

    impl->frame.reset(av_frame_alloc());
    if (!impl->frame) throw MediaError("Could not allocate a decoded video frame.");

    return impl;
}

std::optional<VideoFrame> VideoPlaybackSession::decodeNextFrame(Impl& impl) {
    if (impl.end_reached) return std::nullopt;

    while (true) {
        if (!impl.flush_sent) {
            while (true) {
                const int read_result = av_read_frame(impl.format.get(), impl.packet.get());
                if (read_result == AVERROR_EOF) {
                    const int flush_result = avcodec_send_packet(impl.decoder.get(), nullptr);
                    impl.flush_sent = true;
                    if (flush_result < 0 && flush_result != AVERROR_EOF) {
                        throwFfmpegError(flush_result, "Flushing video decoder");
                    }
                    break;
                }
                if (read_result < 0) {
                    throwFfmpegError(read_result, "Reading video packet for playback");
                }

                if (impl.packet->stream_index != impl.stream_index) {
                    av_packet_unref(impl.packet.get());
                    continue;
                }

                const int send_result = avcodec_send_packet(impl.decoder.get(), impl.packet.get());
                av_packet_unref(impl.packet.get());
                if (send_result < 0 && send_result != AVERROR(EAGAIN)) {
                    throwFfmpegError(send_result, "Sending video packet to decoder");
                }
                break;
            }
        }

        const int receive_result = avcodec_receive_frame(impl.decoder.get(), impl.frame.get());
        if (receive_result == 0) {
            ++impl.current_frame_index;
            return copyRgbaFrame(*impl.frame);
        }
        if (receive_result == AVERROR(EAGAIN)) {
            if (impl.flush_sent) {
                impl.end_reached = true;
                return std::nullopt;
            }
            continue;
        }
        if (receive_result == AVERROR_EOF) {
            impl.end_reached = true;
            return std::nullopt;
        }
        throwFfmpegError(receive_result, "Receiving decoded video frame");
    }
}

std::optional<VideoFrame> VideoPlaybackSession::decode_next_frame() {
    try {
        return decodeNextFrame(*impl_);
    } catch (const MediaError& error) {
        logFailure(impl_->source_path, "playback_decode", error);
        throw;
    } catch (const std::exception& error) {
        logFailure(impl_->source_path, "playback_decode", error);
        throw;
    }
}

std::optional<VideoFrame> VideoPlaybackSession::decode_frame_at(std::int64_t frame_index) {
    if (frame_index < 0) return std::nullopt;

    try {
        reset();
        std::optional<VideoFrame> frame;
        for (std::int64_t index = 0; index <= frame_index; ++index) {
            frame = decodeNextFrame(*impl_);
            if (!frame.has_value()) return std::nullopt;
        }
        return frame;
    } catch (const MediaError& error) {
        logFailure(impl_->source_path, "playback_decode_frame_at", error);
        throw;
    } catch (const std::exception& error) {
        logFailure(impl_->source_path, "playback_decode_frame_at", error);
        throw;
    }
}

void VideoPlaybackSession::reset() {
    const auto source_path = impl_->source_path;
    impl_ = openImpl(source_path);
}

std::int64_t VideoPlaybackSession::current_frame_index() const noexcept {
    return impl_->current_frame_index;
}

bool VideoPlaybackSession::at_end() const noexcept {
    return impl_->end_reached;
}

} // namespace media
