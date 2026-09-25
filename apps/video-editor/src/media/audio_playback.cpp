#include "audio_playback.h"

#include "../logging/logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>
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

struct ResamplerDeleter {
    void operator()(SwrContext* context) const noexcept {
        if (context != nullptr) swr_free(&context);
    }
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using ResamplerPtr = std::unique_ptr<SwrContext, ResamplerDeleter>;

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

void validateOutput(AudioPlaybackSession::OutputSpec output) {
    if (output.sample_rate <= 0 || output.channel_count <= 0 ||
        output.channel_count > 8) {
        throw MediaError("The requested audio output format is invalid.");
    }
}

std::optional<std::int64_t> sampleIndexForTimestamp(
    std::int64_t timestamp,
    AVRational time_base,
    std::int64_t stream_start_time,
    int sample_rate) {
    if (timestamp == AV_NOPTS_VALUE || time_base.num <= 0 || time_base.den <= 0 ||
        sample_rate <= 0) {
        return std::nullopt;
    }
    const auto relative_timestamp = stream_start_time == AV_NOPTS_VALUE
        ? timestamp
        : timestamp - stream_start_time;
    const long double seconds = static_cast<long double>(relative_timestamp) *
        static_cast<long double>(time_base.num) / static_cast<long double>(time_base.den);
    const long double samples = seconds * static_cast<long double>(sample_rate);
    if (!std::isfinite(static_cast<double>(samples))) return std::nullopt;
    if (samples < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
        samples > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(std::llround(samples));
}

void logFailure(const std::filesystem::path& source_path,
                const char* operation,
                const MediaError& error) noexcept {
    try {
        logging::Context context{{"path", safePathForLog(source_path)}};
        if (error.error_code().has_value()) {
            context.emplace_back("error_code", std::to_string(*error.error_code()));
        }
        logging::Logger::instance().log(
            logging::Level::Error,
            "audio",
            operation,
            error.what(),
            context);
    } catch (...) {
    }
}

void logFailure(const std::filesystem::path& source_path,
                const char* operation,
                const std::exception& error) noexcept {
    try {
        logging::Logger::instance().log(
            logging::Level::Error,
            "audio",
            operation,
            error.what(),
            {{"path", safePathForLog(source_path)}});
    } catch (...) {
    }
}

} // namespace

struct AudioPlaybackSession::Impl {
    std::filesystem::path source_path;
    AudioPlaybackSession::OutputSpec output;
    FormatContextPtr format;
    CodecContextPtr decoder;
    PacketPtr packet;
    FramePtr frame;
    ResamplerPtr resampler;
    int stream_index = -1;
    AVRational stream_time_base{0, 1};
    std::int64_t stream_start_time = AV_NOPTS_VALUE;
    bool has_audio = false;
    bool flush_sent = false;
    bool end_reached = false;
    std::int64_t current_sample_index = 0;
    std::int64_t requested_sample_index = 0;
};

AudioPlaybackSession::AudioPlaybackSession(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

AudioPlaybackSession::~AudioPlaybackSession() = default;

AudioPlaybackSession::AudioPlaybackSession(AudioPlaybackSession&&) noexcept = default;

AudioPlaybackSession& AudioPlaybackSession::operator=(AudioPlaybackSession&&) noexcept = default;

std::unique_ptr<AudioPlaybackSession> AudioPlaybackSession::open(
    const std::filesystem::path& source_path,
    OutputSpec output) {
    try {
        return std::unique_ptr<AudioPlaybackSession>(
            new AudioPlaybackSession(openImpl(source_path, output)));
    } catch (const MediaError& error) {
        logFailure(source_path, "open", error);
        throw;
    } catch (const std::exception& error) {
        logFailure(source_path, "open", error);
        throw;
    }
}

std::unique_ptr<AudioPlaybackSession::Impl> AudioPlaybackSession::openImpl(
    const std::filesystem::path& source_path,
    OutputSpec output) {
    validateOutput(output);
    if (source_path.empty()) throw MediaError("Media path is empty.");

    std::error_code file_error;
    if (!std::filesystem::is_regular_file(source_path, file_error) || file_error) {
        const auto code = file_error ? std::optional<int>(file_error.value()) : std::nullopt;
        throw MediaError("Input is not a readable regular file: " + toUtf8(source_path), code);
    }

    auto impl = std::make_unique<Impl>();
    impl->source_path = source_path;
    impl->output = output;

    AVFormatContext* raw_format = nullptr;
    const auto input_path = toUtf8(source_path);
    const int open_result = avformat_open_input(&raw_format, input_path.c_str(), nullptr, nullptr);
    if (open_result < 0) {
        if (raw_format != nullptr) avformat_close_input(&raw_format);
        throwFfmpegError(open_result, "Opening media for audio");
    }
    impl->format.reset(raw_format);

    const int info_result = avformat_find_stream_info(impl->format.get(), nullptr);
    if (info_result < 0) throwFfmpegError(info_result, "Reading audio stream information");

    const AVCodec* codec = nullptr;
    impl->stream_index = av_find_best_stream(
        impl->format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (impl->stream_index < 0 || codec == nullptr) {
        impl->has_audio = false;
        return impl;
    }

    auto* stream = impl->format->streams[impl->stream_index];
    if (stream == nullptr || stream->codecpar == nullptr) {
        throw MediaError("The audio stream has no codec parameters.");
    }
    impl->stream_time_base = stream->time_base;
    impl->stream_start_time = stream->start_time;
    impl->decoder.reset(avcodec_alloc_context3(codec));
    if (!impl->decoder) throw MediaError("Could not allocate the audio decoder context.");

    const int parameters_result = avcodec_parameters_to_context(
        impl->decoder.get(), stream->codecpar);
    if (parameters_result < 0) {
        throwFfmpegError(parameters_result, "Reading audio codec parameters");
    }
    const int decoder_result = avcodec_open2(impl->decoder.get(), codec, nullptr);
    if (decoder_result < 0) throwFfmpegError(decoder_result, "Opening audio decoder");
    if (impl->decoder->sample_rate <= 0 || impl->decoder->ch_layout.nb_channels <= 0) {
        throw MediaError("The audio stream has invalid sample format metadata.");
    }

    AVChannelLayout output_layout{};
    av_channel_layout_default(&output_layout, output.channel_count);
    SwrContext* raw_resampler = nullptr;
    const int resampler_result = swr_alloc_set_opts2(
        &raw_resampler,
        &output_layout,
        AV_SAMPLE_FMT_S16,
        output.sample_rate,
        &impl->decoder->ch_layout,
        impl->decoder->sample_fmt,
        impl->decoder->sample_rate,
        0,
        nullptr);
    av_channel_layout_uninit(&output_layout);
    if (resampler_result < 0 || raw_resampler == nullptr) {
        throwFfmpegError(
            resampler_result < 0 ? resampler_result : AVERROR(ENOMEM),
            "Creating audio resampler");
    }
    impl->resampler.reset(raw_resampler);
    const int init_result = swr_init(impl->resampler.get());
    if (init_result < 0) throwFfmpegError(init_result, "Initializing audio resampler");

    impl->packet.reset(av_packet_alloc());
    if (!impl->packet) throw MediaError("Could not allocate an audio packet.");
    impl->frame.reset(av_frame_alloc());
    if (!impl->frame) throw MediaError("Could not allocate a decoded audio frame.");
    impl->has_audio = true;
    return impl;
}

bool AudioPlaybackSession::has_audio() const noexcept {
    return impl_ != nullptr && impl_->has_audio;
}

const AudioPlaybackSession::OutputSpec& AudioPlaybackSession::output_spec() const noexcept {
    return impl_->output;
}

std::int64_t AudioPlaybackSession::current_sample_index() const noexcept {
    return impl_ != nullptr ? impl_->current_sample_index : 0;
}

bool AudioPlaybackSession::at_end() const noexcept {
    return impl_ == nullptr || impl_->end_reached;
}

void AudioPlaybackSession::seek_to_source_frame(
    std::int64_t source_frame,
    double video_frame_rate) {
    if (source_frame < 0 || !std::isfinite(video_frame_rate) ||
        video_frame_rate <= 0.0) {
        const MediaError error("The requested audio source position is invalid.");
        logFailure(impl_->source_path, "seek", error);
        throw error;
    }
    const auto sample_index = static_cast<std::int64_t>(std::llround(
        static_cast<long double>(source_frame) * impl_->output.sample_rate /
        static_cast<long double>(video_frame_rate)));
    seek_to_sample_index(sample_index);
}

void AudioPlaybackSession::seek_to_sample_index(std::int64_t sample_index) {
    try {
        if (sample_index < 0) {
            throw MediaError("The requested audio source position is invalid.");
        }
        if (!has_audio()) return;

        const auto timestamp = av_rescale_q(
            sample_index,
            AVRational{1, impl_->output.sample_rate},
            impl_->stream_time_base);
        const auto seek_timestamp = impl_->stream_start_time == AV_NOPTS_VALUE
            ? timestamp
            : impl_->stream_start_time + timestamp;
        const int seek_result = avformat_seek_file(
            impl_->format.get(),
            impl_->stream_index,
            std::numeric_limits<std::int64_t>::min(),
            seek_timestamp,
            seek_timestamp,
            AVSEEK_FLAG_BACKWARD);
        if (seek_result < 0) throwFfmpegError(seek_result, "Seeking audio");

        avcodec_flush_buffers(impl_->decoder.get());
        swr_close(impl_->resampler.get());
        const int resampler_result = swr_init(impl_->resampler.get());
        if (resampler_result < 0) {
            throwFfmpegError(resampler_result, "Resetting the audio resampler after a seek");
        }
        av_packet_unref(impl_->packet.get());
        av_frame_unref(impl_->frame.get());
        impl_->flush_sent = false;
        impl_->end_reached = false;
        impl_->requested_sample_index = sample_index;
        impl_->current_sample_index = sample_index;
    } catch (const MediaError& error) {
        logFailure(impl_->source_path, "seek", error);
        throw;
    } catch (const std::exception& error) {
        logFailure(impl_->source_path, "seek", error);
        throw;
    }
}

std::optional<AudioPcmChunk> AudioPlaybackSession::decode_samples(
    std::size_t maximum_samples,
    const CancellationPredicate& should_cancel) {
    try {
        if (!has_audio() || impl_->end_reached) return std::nullopt;
        if (maximum_samples == 0) return std::nullopt;
        const auto cancelled = [&should_cancel]() {
            return should_cancel && should_cancel();
        };
        while (true) {
            if (cancelled()) return std::nullopt;
            if (!impl_->flush_sent) {
                while (true) {
                    const int read_result = av_read_frame(impl_->format.get(), impl_->packet.get());
                    if (read_result == AVERROR_EOF) {
                        const int flush_result = avcodec_send_packet(impl_->decoder.get(), nullptr);
                        impl_->flush_sent = true;
                        if (flush_result < 0 && flush_result != AVERROR_EOF) {
                            throwFfmpegError(flush_result, "Flushing audio decoder");
                        }
                        break;
                    }
                    if (read_result < 0) throwFfmpegError(read_result, "Reading audio packet");
                    if (impl_->packet->stream_index != impl_->stream_index) {
                        av_packet_unref(impl_->packet.get());
                        continue;
                    }
                    const int send_result = avcodec_send_packet(
                        impl_->decoder.get(), impl_->packet.get());
                    av_packet_unref(impl_->packet.get());
                    if (send_result < 0 && send_result != AVERROR(EAGAIN)) {
                        throwFfmpegError(send_result, "Sending audio packet to decoder");
                    }
                    break;
                }
            }

            const int receive_result = avcodec_receive_frame(
                impl_->decoder.get(), impl_->frame.get());
            if (receive_result == AVERROR(EAGAIN)) {
                if (impl_->flush_sent) {
                    impl_->end_reached = true;
                    return std::nullopt;
                }
                continue;
            }
            if (receive_result == AVERROR_EOF) {
                impl_->end_reached = true;
                return std::nullopt;
            }
            if (receive_result < 0) {
                throwFfmpegError(receive_result, "Receiving decoded audio frame");
            }

            const int64_t delay = swr_get_delay(
                impl_->resampler.get(),
                impl_->decoder->sample_rate);
            const int output_capacity = static_cast<int>(std::min<std::int64_t>(
                std::numeric_limits<int>::max(),
                av_rescale_rnd(
                    delay + impl_->frame->nb_samples,
                    impl_->output.sample_rate,
                    impl_->decoder->sample_rate,
                    AV_ROUND_UP)));
            if (output_capacity <= 0) continue;

            AudioPcmChunk chunk;
            chunk.sample_rate = impl_->output.sample_rate;
            chunk.channel_count = impl_->output.channel_count;
            chunk.first_sample_index = impl_->current_sample_index;
            chunk.samples.resize(static_cast<std::size_t>(output_capacity) *
                                 static_cast<std::size_t>(impl_->output.channel_count));
            auto* output_data = reinterpret_cast<std::uint8_t*>(chunk.samples.data());
            std::uint8_t* output_planes[] = {output_data};
            const int converted = swr_convert(
                impl_->resampler.get(),
                output_planes,
                output_capacity,
                const_cast<const std::uint8_t**>(impl_->frame->extended_data),
                impl_->frame->nb_samples);
            if (converted < 0) throwFfmpegError(converted, "Resampling audio frame");
            if (converted == 0) continue;
            chunk.samples.resize(static_cast<std::size_t>(converted) *
                                 static_cast<std::size_t>(impl_->output.channel_count));

            if (chunk.first_sample_index < impl_->requested_sample_index) {
                const auto skip = static_cast<std::size_t>(std::min<std::int64_t>(
                    converted,
                    impl_->requested_sample_index - chunk.first_sample_index));
                const auto sample_offset = skip * static_cast<std::size_t>(chunk.channel_count);
                chunk.samples.erase(chunk.samples.begin(), chunk.samples.begin() +
                    static_cast<std::ptrdiff_t>(sample_offset));
                chunk.first_sample_index += static_cast<std::int64_t>(skip);
            }
            if (chunk.samples.empty()) continue;
            const auto available = chunk.sampleCount();
            const auto limited = std::min(maximum_samples, available);
            chunk.samples.resize(limited * static_cast<std::size_t>(chunk.channel_count));
            impl_->current_sample_index = chunk.first_sample_index +
                static_cast<std::int64_t>(limited);
            return chunk;
        }
    } catch (const MediaError& error) {
        logFailure(impl_->source_path, "decode", error);
        throw;
    } catch (const std::exception& error) {
        logFailure(impl_->source_path, "decode", error);
        throw;
    }
}

void AudioPlaybackSession::reset() {
    const auto source_path = impl_->source_path;
    const auto output = impl_->output;
    impl_ = openImpl(source_path, output);
}

} // namespace media
