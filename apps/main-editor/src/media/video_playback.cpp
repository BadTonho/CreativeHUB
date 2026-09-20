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
#include <cmath>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

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

bool isValidRational(AVRational value) noexcept {
    return value.num > 0 && value.den > 0;
}

std::optional<std::int64_t> timestampForFrame(
    std::int64_t frame_index,
    AVRational frame_rate,
    AVRational time_base,
    std::int64_t stream_start_time) {
    if (frame_index < 0 || !isValidRational(frame_rate) || !isValidRational(time_base) ||
        stream_start_time == AV_NOPTS_VALUE) {
        return std::nullopt;
    }

    const auto relative_timestamp = av_rescale_q(
        frame_index,
        AVRational{frame_rate.den, frame_rate.num},
        time_base);
    if (relative_timestamp > 0 &&
        stream_start_time > std::numeric_limits<std::int64_t>::max() - relative_timestamp) {
        return std::nullopt;
    }
    if (relative_timestamp < 0 &&
        stream_start_time < std::numeric_limits<std::int64_t>::min() - relative_timestamp) {
        return std::nullopt;
    }
    return stream_start_time + relative_timestamp;
}

std::optional<std::int64_t> frameIndexForTimestamp(
    std::int64_t timestamp,
    AVRational frame_rate,
    AVRational time_base,
    std::int64_t stream_start_time) {
    if (timestamp == AV_NOPTS_VALUE || !isValidRational(frame_rate) ||
        !isValidRational(time_base) || stream_start_time == AV_NOPTS_VALUE) {
        return std::nullopt;
    }

    const long double relative_timestamp =
        static_cast<long double>(timestamp) -
        static_cast<long double>(stream_start_time);
    const long double seconds = relative_timestamp * av_q2d(time_base);
    const long double frame_position = seconds * av_q2d(frame_rate);
    if (!std::isfinite(static_cast<double>(frame_position)) || frame_position < 0.0L ||
        frame_position > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }

    return static_cast<std::int64_t>(std::llround(frame_position));
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
    struct CachedFrame {
        std::int64_t frame_index = -1;
        std::shared_ptr<const VideoFrame> frame;
    };

    std::filesystem::path source_path;
    FormatContextPtr format;
    CodecContextPtr decoder;
    PacketPtr packet;
    FramePtr frame;
    int stream_index = -1;
    AVRational stream_time_base{0, 1};
    AVRational frame_rate{0, 1};
    std::int64_t stream_start_time = AV_NOPTS_VALUE;
    bool flush_sent = false;
    bool end_reached = false;
    bool decoder_position_invalid = false;
    bool cache_decoded_frames = true;
    std::int64_t current_frame_index = -1;
    std::int64_t last_decoded_timestamp = AV_NOPTS_VALUE;
    std::deque<CachedFrame> frame_cache;
    std::size_t cached_bytes = 0;
};

constexpr std::size_t max_cached_frames = 8;
constexpr std::size_t max_cached_bytes = 64U * 1024U * 1024U;

void VideoPlaybackSession::cacheFrame(
    VideoPlaybackSession::Impl& impl,
    std::int64_t frame_index,
    const VideoFrame& frame) {
    for (auto iterator = impl.frame_cache.begin(); iterator != impl.frame_cache.end(); ++iterator) {
        if (iterator->frame_index != frame_index) continue;
        impl.cached_bytes -= iterator->frame != nullptr ? iterator->frame->rgba_pixels.size() : 0;
        impl.frame_cache.erase(iterator);
        break;
    }

    auto cached = std::make_shared<const VideoFrame>(frame);
    impl.cached_bytes += cached->rgba_pixels.size();
    impl.frame_cache.push_back({frame_index, std::move(cached)});

    while (impl.frame_cache.size() > 1 &&
           (impl.frame_cache.size() > max_cached_frames || impl.cached_bytes > max_cached_bytes)) {
        const auto& oldest = impl.frame_cache.front();
        if (oldest.frame != nullptr) impl.cached_bytes -= oldest.frame->rgba_pixels.size();
        impl.frame_cache.pop_front();
    }
}

std::shared_ptr<const VideoFrame> VideoPlaybackSession::takeCachedFrame(
    VideoPlaybackSession::Impl& impl,
    std::int64_t frame_index) {
    for (auto iterator = impl.frame_cache.begin(); iterator != impl.frame_cache.end(); ++iterator) {
        if (iterator->frame_index != frame_index) continue;
        auto frame = iterator->frame;
        auto entry = std::move(*iterator);
        impl.frame_cache.erase(iterator);
        impl.frame_cache.push_back(std::move(entry));
        return frame;
    }
    return nullptr;
}

void VideoPlaybackSession::resetDecoderPosition(VideoPlaybackSession::Impl& impl) {
    avcodec_flush_buffers(impl.decoder.get());
    av_packet_unref(impl.packet.get());
    av_frame_unref(impl.frame.get());
    impl.flush_sent = false;
    impl.end_reached = false;
    impl.decoder_position_invalid = false;
    impl.cache_decoded_frames = true;
    impl.current_frame_index = -1;
    impl.last_decoded_timestamp = AV_NOPTS_VALUE;
}

bool VideoPlaybackSession::seekToTimestamp(
    VideoPlaybackSession::Impl& impl,
    std::int64_t frame_index) {
    const auto timestamp = timestampForFrame(
        frame_index,
        impl.frame_rate,
        impl.stream_time_base,
        impl.stream_start_time);
    if (!timestamp.has_value()) return false;

    const int seek_result = avformat_seek_file(
        impl.format.get(),
        impl.stream_index,
        std::numeric_limits<std::int64_t>::min(),
        *timestamp,
        *timestamp,
        AVSEEK_FLAG_BACKWARD);
    if (seek_result < 0) return false;

    resetDecoderPosition(impl);
    return true;
}

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

    impl->stream_time_base = stream->time_base;
    impl->stream_start_time = stream->start_time;
    impl->frame_rate = av_guess_frame_rate(impl->format.get(), stream, nullptr);

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
            impl.last_decoded_timestamp = impl.frame->best_effort_timestamp;
            auto decoded_frame = copyRgbaFrame(*impl.frame);
            if (impl.cache_decoded_frames) {
                VideoPlaybackSession::cacheFrame(
                    impl,
                    impl.current_frame_index,
                    decoded_frame);
            }
            return decoded_frame;
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
        if (impl_->decoder_position_invalid && impl_->current_frame_index >= 0) {
            return decode_frame_at(impl_->current_frame_index + 1);
        }
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
    return decode_frame_at(frame_index, {});
}

std::optional<VideoFrame> VideoPlaybackSession::decode_frame_at(
    std::int64_t frame_index,
    const CancellationPredicate& should_cancel) {
    try {
        if (frame_index < 0) {
            throw MediaError("The requested frame index is negative.");
        }

        const auto cancelled = [&should_cancel]() {
            return should_cancel && should_cancel();
        };

        if (cancelled()) return std::nullopt;

        if (const auto cached = takeCachedFrame(*impl_, frame_index); cached != nullptr) {
            impl_->current_frame_index = frame_index;
            impl_->end_reached = false;
            impl_->decoder_position_invalid = true;
            return *cached;
        }

        const auto decodeFromBeginning = [&]() -> std::optional<VideoFrame> {
            reset();
            std::optional<VideoFrame> frame;
            for (std::int64_t index = 0; index <= frame_index; ++index) {
                if (cancelled()) return std::nullopt;
                frame = decodeNextFrame(*impl_);
                if (!frame.has_value()) return std::nullopt;
            }
            return frame;
        };

        if (!seekToTimestamp(*impl_, frame_index)) {
            return decodeFromBeginning();
        }

        impl_->cache_decoded_frames = false;
        const auto fallbackToBeginning = [&]() {
            impl_->cache_decoded_frames = true;
            return decodeFromBeginning();
        };

        while (true) {
            if (cancelled()) {
                impl_->cache_decoded_frames = true;
                return std::nullopt;
            }

            auto frame = decodeNextFrame(*impl_);
            if (!frame.has_value()) {
                impl_->cache_decoded_frames = true;
                return std::nullopt;
            }

            const auto decoded_index = frameIndexForTimestamp(
                impl_->last_decoded_timestamp,
                impl_->frame_rate,
                impl_->stream_time_base,
                impl_->stream_start_time);
            if (!decoded_index.has_value() || *decoded_index > frame_index) {
                return fallbackToBeginning();
            }

            impl_->current_frame_index = *decoded_index;
            cacheFrame(*impl_, *decoded_index, *frame);
            if (*decoded_index == frame_index) {
                impl_->cache_decoded_frames = true;
                impl_->decoder_position_invalid = false;
                return frame;
            }
        }
    } catch (const MediaError& error) {
        impl_->cache_decoded_frames = true;
        logFailure(impl_->source_path, "playback_seek", error);
        throw;
    } catch (const std::exception& error) {
        impl_->cache_decoded_frames = true;
        logFailure(impl_->source_path, "playback_seek", error);
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
