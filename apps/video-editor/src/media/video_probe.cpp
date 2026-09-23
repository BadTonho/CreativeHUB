#include "video_probe.h"

#include "../logging/logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
}

#include <cmath>
#include <limits>
#include <memory>
#include <sstream>
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

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;

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

std::optional<double> secondsFromTimestamp(int64_t timestamp, AVRational time_base) {
    if (timestamp == AV_NOPTS_VALUE || timestamp < 0 || time_base.num <= 0 || time_base.den <= 0) {
        return std::nullopt;
    }

    const double seconds = static_cast<double>(timestamp) * av_q2d(time_base);
    if (!std::isfinite(seconds) || seconds < 0.0) return std::nullopt;
    return seconds;
}

std::optional<double> frameRateFromStream(AVStream* stream, AVFormatContext* format) {
    const AVRational rate = av_guess_frame_rate(format, stream, nullptr);
    if (rate.num <= 0 || rate.den <= 0) return std::nullopt;

    const double frames_per_second = av_q2d(rate);
    if (!std::isfinite(frames_per_second) || frames_per_second <= 0.0) {
        return std::nullopt;
    }
    return frames_per_second;
}

} // namespace

VideoMetadata VideoProbe::probe(const std::filesystem::path& source_path) const {
    try {
        if (source_path.empty()) throw MediaError("Media path is empty.");

        std::error_code file_error;
        if (!std::filesystem::is_regular_file(source_path, file_error) || file_error) {
            const auto code = file_error ? std::optional<int>(file_error.value()) : std::nullopt;
            throw MediaError("Input is not a readable regular file: " + toUtf8(source_path), code);
        }

        AVFormatContext* raw_format = nullptr;
        const std::string input_path = toUtf8(source_path);
        const int open_result = avformat_open_input(&raw_format, input_path.c_str(), nullptr, nullptr);
        if (open_result < 0) {
            if (raw_format != nullptr) avformat_close_input(&raw_format);
            throwFfmpegError(open_result, "Opening media");
        }
        FormatContextPtr format(raw_format);

        const int stream_info_result = avformat_find_stream_info(format.get(), nullptr);
        if (stream_info_result < 0) throwFfmpegError(stream_info_result, "Reading media stream information");

        const AVCodec* codec = nullptr;
        const int stream_index = av_find_best_stream(
            format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (stream_index < 0 || codec == nullptr) {
            throw MediaError("No supported video stream was found.", stream_index);
        }

        AVStream* stream = format->streams[stream_index];
        if (stream == nullptr || stream->codecpar == nullptr) {
            throw MediaError("The video stream has no codec parameters.");
        }

        CodecContextPtr decoder(avcodec_alloc_context3(codec));
        if (!decoder) throw MediaError("Could not allocate the video decoder context.");

        const int parameters_result = avcodec_parameters_to_context(decoder.get(), stream->codecpar);
        if (parameters_result < 0) {
            throwFfmpegError(parameters_result, "Reading video codec parameters");
        }

        const int decoder_result = avcodec_open2(decoder.get(), codec, nullptr);
        if (decoder_result < 0) throwFfmpegError(decoder_result, "Opening video decoder");

        VideoMetadata metadata;
        metadata.source_path = source_path;
        metadata.display_name = toUtf8(source_path.filename());
        if (metadata.display_name.empty()) metadata.display_name = input_path;
        metadata.container_format = format->iformat != nullptr && format->iformat->long_name != nullptr
            ? format->iformat->long_name
            : (format->iformat != nullptr && format->iformat->name != nullptr
                ? format->iformat->name
                : "Unknown");
        metadata.video_codec = codec->long_name != nullptr
            ? codec->long_name
            : (codec->name != nullptr ? codec->name : "Unknown");
        metadata.width = decoder->width;
        metadata.height = decoder->height;
        metadata.frame_rate = frameRateFromStream(stream, format.get());

        metadata.duration_seconds = secondsFromTimestamp(stream->duration, stream->time_base);
        if (!metadata.duration_seconds.has_value()) {
            metadata.duration_seconds = secondsFromTimestamp(format->duration, AVRational{1, AV_TIME_BASE});
        }

        if (stream->nb_frames > 0) {
            metadata.frame_count = stream->nb_frames;
        } else if (metadata.duration_seconds.has_value() && metadata.frame_rate.has_value()) {
            const double estimated_frames = *metadata.duration_seconds * *metadata.frame_rate;
            if (estimated_frames <= static_cast<double>(std::numeric_limits<int64_t>::max())) {
                metadata.frame_count = static_cast<int64_t>(std::llround(estimated_frames));
            }
        }

        const AVCodec* audio_codec = nullptr;
        const int audio_stream_index = av_find_best_stream(
            format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &audio_codec, 0);
        if (audio_stream_index >= 0 && audio_codec != nullptr) {
            AVStream* audio_stream = format->streams[audio_stream_index];
            if (audio_stream != nullptr && audio_stream->codecpar != nullptr) {
                AudioMetadata audio;
                audio.codec = audio_codec->long_name != nullptr
                    ? audio_codec->long_name
                    : (audio_codec->name != nullptr ? audio_codec->name : "Unknown");
                audio.sample_rate = audio_stream->codecpar->sample_rate;
                audio.channel_count = audio_stream->codecpar->ch_layout.nb_channels;
                audio.duration_seconds = secondsFromTimestamp(
                    audio_stream->duration,
                    audio_stream->time_base);
                if (!audio.duration_seconds.has_value()) {
                    audio.duration_seconds = metadata.duration_seconds;
                }
                metadata.audio = std::move(audio);
            }
        }

        return metadata;
    } catch (const MediaError& error) {
        try {
            logging::Context context{{"path", safePathForLog(source_path)}};
            if (error.error_code().has_value()) {
                context.emplace_back("error_code", std::to_string(*error.error_code()));
            }
            logging::Logger::instance().log(
                logging::Level::Error,
                "media",
                "probe",
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
                "probe",
                error.what(),
                {{"path", safePathForLog(source_path)}});
        } catch (...) {
            // Preserve the original exception even if diagnostic context allocation fails.
        }
        throw;
    }
}

} // namespace media
