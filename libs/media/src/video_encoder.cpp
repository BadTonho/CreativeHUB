#include <creative_suite/media/video_encoder.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace creative_suite::media {
namespace {

struct FormatOutputDeleter {
    void operator()(AVFormatContext* value) const noexcept {
        if (value != nullptr) avformat_free_context(value);
    }
};
struct CodecDeleter {
    void operator()(AVCodecContext* value) const noexcept {
        if (value != nullptr) avcodec_free_context(&value);
    }
};
struct PacketDeleter {
    void operator()(AVPacket* value) const noexcept {
        if (value != nullptr) av_packet_free(&value);
    }
};
struct FrameDeleter {
    void operator()(AVFrame* value) const noexcept {
        if (value != nullptr) av_frame_free(&value);
    }
};
struct ResamplerDeleter {
    void operator()(SwrContext* value) const noexcept {
        if (value != nullptr) swr_free(&value);
    }
};
struct ScalerDeleter {
    void operator()(SwsContext* value) const noexcept {
        if (value != nullptr) sws_freeContext(value);
    }
};

using FormatPtr = std::unique_ptr<AVFormatContext, FormatOutputDeleter>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using ResamplerPtr = std::unique_ptr<SwrContext, ResamplerDeleter>;
using ScalerPtr = std::unique_ptr<SwsContext, ScalerDeleter>;

std::string pathUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::string ffmpegMessage(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(code, buffer, sizeof(buffer)) == 0) return buffer;
    return "FFmpeg error " + std::to_string(code);
}

[[noreturn]] void failFfmpeg(int code, const char* operation) {
    throw VideoEncodingError(std::string(operation) + ": " + ffmpegMessage(code), code);
}

void check(int result, const char* operation) {
    if (result < 0) failFfmpeg(result, operation);
}

const AVOutputFormat* findFormat(std::string_view name) {
    void* iterator = nullptr;
    const AVOutputFormat* format = nullptr;
    while ((format = av_muxer_iterate(&iterator)) != nullptr) {
        if (format->name == nullptr) continue;
        std::string_view names(format->name);
        while (!names.empty()) {
            const auto comma = names.find(',');
            if (names.substr(0, comma) == name) return format;
            if (comma == std::string_view::npos) break;
            names.remove_prefix(comma + 1);
        }
    }
    return nullptr;
}

std::string firstName(std::string_view names) {
    const auto comma = names.find(',');
    return std::string(names.substr(0, comma));
}

std::vector<VideoEncoderOption> compatibleEncoders(
    const AVOutputFormat* format,
    AVMediaType media_type) {
    std::vector<VideoEncoderOption> result;
    if (format == nullptr) return result;
    void* iterator = nullptr;
    const AVCodec* codec = nullptr;
    while ((codec = av_codec_iterate(&iterator)) != nullptr) {
        if (!av_codec_is_encoder(codec) || codec->type != media_type ||
            codec->id == AV_CODEC_ID_NONE || codec->name == nullptr ||
            avformat_query_codec(format, codec->id, FF_COMPLIANCE_NORMAL) <= 0) {
            continue;
        }
        result.push_back({codec->name,
            codec->long_name != nullptr ? codec->long_name : codec->name,
            static_cast<int>(codec->id)});
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.display_name != right.display_name) {
            return left.display_name < right.display_name;
        }
        return left.name < right.name;
    });
    return result;
}

}  // namespace

std::vector<VideoContainerOption> availableVideoContainers() {
    std::vector<VideoContainerOption> result;
    std::set<std::string> seen_names;
    void* iterator = nullptr;
    const AVOutputFormat* format = nullptr;
    while ((format = av_muxer_iterate(&iterator)) != nullptr) {
        if (format->name == nullptr) continue;
        auto name = firstName(format->name);
        if (name.empty() || seen_names.contains(name)) continue;
        auto video_encoders = compatibleEncoders(format, AVMEDIA_TYPE_VIDEO);
        if (video_encoders.empty()) continue;
        seen_names.insert(name);
        result.push_back({std::move(name),
            format->long_name != nullptr ? format->long_name : firstName(format->name),
            format->extensions != nullptr ? format->extensions : "",
            std::move(video_encoders),
            compatibleEncoders(format, AVMEDIA_TYPE_AUDIO)});
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.display_name != right.display_name) {
            return left.display_name < right.display_name;
        }
        return left.name < right.name;
    });
    return result;
}

bool supportsVideoEncoder(
    const VideoContainerOption& container,
    const std::string& encoder_name) {
    const auto* format = findFormat(container.name);
    const auto* codec = avcodec_find_encoder_by_name(encoder_name.c_str());
    return format != nullptr && codec != nullptr && codec->type == AVMEDIA_TYPE_VIDEO &&
        avformat_query_codec(format, codec->id, FF_COMPLIANCE_NORMAL) > 0;
}

bool supportsAudioEncoder(
    const VideoContainerOption& container,
    const std::string& encoder_name) {
    const auto* format = findFormat(container.name);
    const auto* codec = avcodec_find_encoder_by_name(encoder_name.c_str());
    return format != nullptr && codec != nullptr && codec->type == AVMEDIA_TYPE_AUDIO &&
        avformat_query_codec(format, codec->id, FF_COMPLIANCE_NORMAL) > 0;
}

bool publishEncodedFileAtomically(
    const std::filesystem::path& temporary_path,
    const std::filesystem::path& target_path) noexcept {
#ifdef _WIN32
    return MoveFileExW(temporary_path.c_str(), target_path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary_path, target_path, error);
    return !error;
#endif
}

struct VideoEncoder::Impl {
    explicit Impl(VideoEncodingSettings input) : settings(std::move(input)) {
        if (settings.width <= 0 || settings.height <= 0 ||
            settings.frame_rate_numerator <= 0 || settings.frame_rate_denominator <= 0 ||
            !std::isfinite(settings.video_bitrate_mbps) || settings.video_bitrate_mbps <= 0.0 ||
            settings.video_bitrate_mbps > 500.0 ||
            settings.output_path.empty() || settings.container_name.empty() ||
            settings.video_encoder_name.empty()) {
            throw std::invalid_argument("The video encoding settings are invalid.");
        }
        const auto max = std::numeric_limits<int>::max();
        if (settings.width > max / 4) {
            throw std::invalid_argument("The output video dimensions are not representable.");
        }
        frame_rate = AVRational{
            static_cast<int>(std::min<std::int64_t>(settings.frame_rate_numerator, max)),
            static_cast<int>(std::min<std::int64_t>(settings.frame_rate_denominator, max))};
        if (static_cast<std::int64_t>(frame_rate.num) != settings.frame_rate_numerator ||
            static_cast<std::int64_t>(frame_rate.den) != settings.frame_rate_denominator) {
            const double rate = static_cast<double>(settings.frame_rate_numerator) /
                static_cast<double>(settings.frame_rate_denominator);
            if (!std::isfinite(rate) || rate <= 0.0 || rate > 1000.0) {
                throw std::invalid_argument("The output frame rate is outside the supported range.");
            }
            frame_rate = av_d2q(rate, 1001000);
        }
        if (av_q2d(frame_rate) > 1000.0) {
            throw std::invalid_argument("The output frame rate is outside the supported range.");
        }

        const auto output_utf8 = pathUtf8(settings.output_path);
        AVFormatContext* raw_output = nullptr;
        check(avformat_alloc_output_context2(&raw_output, nullptr,
            settings.container_name.c_str(), output_utf8.c_str()),
            "Creating the output container");
        if (raw_output == nullptr) {
            throw std::runtime_error("FFmpeg did not create an output container.");
        }
        output.reset(raw_output);
        createVideoStream();
        if (settings.audio.has_value()) createAudioStream();
        if ((output->oformat->flags & AVFMT_NOFILE) == 0) {
            check(avio_open(&output->pb, output_utf8.c_str(), AVIO_FLAG_WRITE),
                  "Opening the temporary output file");
            io_open = true;
        }
        check(avformat_write_header(output.get(), nullptr), "Writing the output header");
        header_written = true;
    }

    ~Impl() {
        if (io_open && output != nullptr) avio_closep(&output->pb);
    }

    void createVideoStream() {
        const AVCodec* codec = avcodec_find_encoder_by_name(
            settings.video_encoder_name.c_str());
        if (codec == nullptr || codec->type != AVMEDIA_TYPE_VIDEO) {
            throw std::runtime_error("The selected video encoder is unavailable.");
        }
        video_codec.reset(avcodec_alloc_context3(codec));
        if (video_codec == nullptr) {
            throw std::runtime_error("Allocating the video encoder failed.");
        }
        video_codec->codec_type = AVMEDIA_TYPE_VIDEO;
        video_codec->width = settings.width;
        video_codec->height = settings.height;
        video_codec->time_base = av_inv_q(frame_rate);
        video_codec->framerate = frame_rate;
        video_codec->bit_rate = static_cast<std::int64_t>(
            std::llround(settings.video_bitrate_mbps * 1000000.0));
        video_codec->gop_size = std::max(1, static_cast<int>(std::lround(
            av_q2d(frame_rate) * 2.0)));
        video_codec->max_b_frames = 0;
        if (output->oformat->flags & AVFMT_GLOBALHEADER) {
            video_codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        const void* pixel_config = nullptr;
        int pixel_config_count = 0;
        check(avcodec_get_supported_config(video_codec.get(), codec,
                  AV_CODEC_CONFIG_PIX_FORMAT, 0, &pixel_config, &pixel_config_count),
              "Querying supported encoder pixel formats");
        video_codec->pix_fmt = AV_PIX_FMT_YUV420P;
        if (pixel_config != nullptr && pixel_config_count > 0) {
            const auto* formats = static_cast<const AVPixelFormat*>(pixel_config);
            bool selected = false;
            for (int index = 0; index < pixel_config_count; ++index) {
                if (formats[index] == AV_PIX_FMT_YUV420P) {
                    video_codec->pix_fmt = formats[index];
                    selected = true;
                    break;
                }
            }
            if (!selected) {
                for (int index = 0; index < pixel_config_count; ++index) {
                    const auto* description = av_pix_fmt_desc_get(formats[index]);
                    if (description != nullptr &&
                        (description->flags & AV_PIX_FMT_FLAG_HWACCEL) == 0) {
                        video_codec->pix_fmt = formats[index];
                        selected = true;
                        break;
                    }
                }
            }
            if (!selected) video_codec->pix_fmt = formats[0];
        }
        AVDictionary* options = nullptr;
        if (settings.video_encoder_name == "libx264") {
            av_dict_set(&options, "preset", "medium", 0);
        }
        const int open_result = avcodec_open2(video_codec.get(), codec, &options);
        av_dict_free(&options);
        check(open_result, "Opening the selected video encoder");
        video_stream = avformat_new_stream(output.get(), nullptr);
        if (video_stream == nullptr) {
            throw std::runtime_error("Creating the output video stream failed.");
        }
        video_stream->time_base = video_codec->time_base;
        check(avcodec_parameters_from_context(video_stream->codecpar, video_codec.get()),
              "Writing video encoder parameters");
        video_frame.reset(av_frame_alloc());
        if (video_frame == nullptr) {
            throw std::runtime_error("Allocating an output video frame failed.");
        }
        video_frame->format = video_codec->pix_fmt;
        video_frame->width = video_codec->width;
        video_frame->height = video_codec->height;
        check(av_frame_get_buffer(video_frame.get(), 32), "Allocating output video pixels");
    }

    void createAudioStream() {
        const auto& audio_settings = *settings.audio;
        if (audio_settings.input_sample_rate <= 0 || audio_settings.channels != 2 ||
            audio_settings.bitrate_kbps <= 0 || audio_settings.encoder_name.empty()) {
            throw std::invalid_argument("The audio encoding settings are invalid.");
        }
        const AVCodec* codec = avcodec_find_encoder_by_name(audio_settings.encoder_name.c_str());
        if (codec == nullptr || codec->type != AVMEDIA_TYPE_AUDIO) {
            throw std::runtime_error("The selected audio encoder is unavailable.");
        }
        audio_codec.reset(avcodec_alloc_context3(codec));
        if (audio_codec == nullptr) {
            throw std::runtime_error("Allocating the audio encoder failed.");
        }
        const void* rate_config = nullptr;
        int rate_config_count = 0;
        check(avcodec_get_supported_config(audio_codec.get(), codec,
                  AV_CODEC_CONFIG_SAMPLE_RATE, 0, &rate_config, &rate_config_count),
              "Querying supported audio sample rates");
        audio_sample_rate = audio_settings.input_sample_rate;
        if (rate_config != nullptr && rate_config_count > 0) {
            const auto* rates = static_cast<const int*>(rate_config);
            audio_sample_rate = rates[0];
            for (int index = 0; index < rate_config_count; ++index) {
                if (rates[index] == audio_settings.input_sample_rate) {
                    audio_sample_rate = rates[index];
                    break;
                }
            }
        }
        audio_codec->sample_rate = audio_sample_rate;
        av_channel_layout_default(&audio_codec->ch_layout, audio_settings.channels);
        const void* sample_config = nullptr;
        int sample_config_count = 0;
        check(avcodec_get_supported_config(audio_codec.get(), codec,
                  AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &sample_config, &sample_config_count),
              "Querying supported audio sample formats");
        audio_codec->sample_fmt = sample_config != nullptr && sample_config_count > 0
            ? static_cast<const AVSampleFormat*>(sample_config)[0]
            : AV_SAMPLE_FMT_FLTP;
        audio_codec->time_base = AVRational{1, audio_sample_rate};
        audio_codec->bit_rate = static_cast<std::int64_t>(audio_settings.bitrate_kbps) * 1000;
        if (output->oformat->flags & AVFMT_GLOBALHEADER) {
            audio_codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        check(avcodec_open2(audio_codec.get(), codec, nullptr), "Opening the selected audio encoder");
        audio_stream = avformat_new_stream(output.get(), nullptr);
        if (audio_stream == nullptr) {
            throw std::runtime_error("Creating the output audio stream failed.");
        }
        audio_stream->time_base = audio_codec->time_base;
        check(avcodec_parameters_from_context(audio_stream->codecpar, audio_codec.get()),
              "Writing audio encoder parameters");
        AVChannelLayout input_layout{};
        av_channel_layout_default(&input_layout, audio_settings.channels);
        SwrContext* raw_resampler = nullptr;
        const int result = swr_alloc_set_opts2(&raw_resampler,
            &audio_codec->ch_layout, audio_codec->sample_fmt, audio_codec->sample_rate,
            &input_layout, AV_SAMPLE_FMT_S16, audio_settings.input_sample_rate,
            0, nullptr);
        av_channel_layout_uninit(&input_layout);
        check(result, "Creating the output audio converter");
        if (raw_resampler == nullptr) {
            throw std::runtime_error("Creating the output audio converter failed.");
        }
        audio_resampler.reset(raw_resampler);
        check(swr_init(audio_resampler.get()), "Initializing the output audio converter");
        audio_frame.reset(av_frame_alloc());
        if (audio_frame == nullptr) {
            throw std::runtime_error("Allocating an output audio frame failed.");
        }
        audio_frame->format = audio_codec->sample_fmt;
        audio_frame->sample_rate = audio_codec->sample_rate;
        check(av_channel_layout_copy(&audio_frame->ch_layout, &audio_codec->ch_layout),
              "Copying the output audio layout");
        audio_frame->nb_samples = audio_codec->frame_size > 0
            ? audio_codec->frame_size : 1024;
        check(av_frame_get_buffer(audio_frame.get(), 0), "Allocating output audio samples");
    }

    void encode(AVCodecContext* codec, AVStream* stream, AVFrame* frame) {
        check(avcodec_send_frame(codec, frame),
              frame == nullptr ? "Flushing an output encoder" : "Encoding output media");
        while (true) {
            PacketPtr packet(av_packet_alloc());
            if (packet == nullptr) throw std::runtime_error("Allocating an output packet failed.");
            const int result = avcodec_receive_packet(codec, packet.get());
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
            check(result, "Receiving an encoded output packet");
            packet->stream_index = stream->index;
            av_packet_rescale_ts(packet.get(), codec->time_base, stream->time_base);
            check(av_interleaved_write_frame(output.get(), packet.get()), "Writing an output packet");
        }
    }

    VideoEncodingSettings settings;
    AVRational frame_rate{0, 1};
    FormatPtr output;
    CodecPtr video_codec;
    CodecPtr audio_codec;
    AVStream* video_stream = nullptr;
    AVStream* audio_stream = nullptr;
    FramePtr video_frame;
    FramePtr audio_frame;
    ScalerPtr scaler;
    ResamplerPtr audio_resampler;
    int audio_sample_rate = 48000;
    int source_width = 0;
    int source_height = 0;
    std::int64_t next_audio_pts = 0;
    std::int64_t audio_input_samples_written = 0;
    bool io_open = false;
    bool header_written = false;
    bool finished = false;
};

VideoEncoder::VideoEncoder(VideoEncodingSettings settings)
    : impl_(std::make_unique<Impl>(std::move(settings))) {}
VideoEncoder::~VideoEncoder() = default;
VideoEncoder::VideoEncoder(VideoEncoder&&) noexcept = default;
VideoEncoder& VideoEncoder::operator=(VideoEncoder&&) noexcept = default;

void VideoEncoder::writeVideo(const RgbaFrame& source, std::int64_t output_frame) {
    if (impl_ == nullptr || impl_->finished) {
        throw std::logic_error("The video encoder is not writable.");
    }
    if (source.width <= 0 || source.height <= 0 ||
        source.width > std::numeric_limits<int>::max() / 4 ||
        source.stride < source.width * 4 ||
        static_cast<std::size_t>(source.height) >
            std::numeric_limits<std::size_t>::max() /
                static_cast<std::size_t>(std::max(source.stride, 1)) ||
        source.rgba_pixels.size() < static_cast<std::size_t>(source.stride) *
            static_cast<std::size_t>(source.height) || output_frame < 0) {
        throw std::invalid_argument("The source frame does not match the output settings.");
    }
    if (impl_->source_width != source.width || impl_->source_height != source.height) {
        impl_->scaler.reset(sws_getContext(source.width, source.height, AV_PIX_FMT_RGBA,
            impl_->settings.width, impl_->settings.height, impl_->video_codec->pix_fmt,
            SWS_BICUBIC, nullptr, nullptr, nullptr));
        if (impl_->scaler == nullptr) {
            throw std::runtime_error("Creating the video pixel converter failed.");
        }
        impl_->source_width = source.width;
        impl_->source_height = source.height;
    }
    check(av_frame_make_writable(impl_->video_frame.get()), "Preparing an output video frame");
    const std::uint8_t* source_data[4]{source.rgba_pixels.data(), nullptr, nullptr, nullptr};
    const int source_lines[4]{source.stride, 0, 0, 0};
    const int rows = sws_scale(impl_->scaler.get(), source_data, source_lines, 0,
        source.height, impl_->video_frame->data, impl_->video_frame->linesize);
    if (rows != impl_->settings.height) {
        throw std::runtime_error("Converting an output video frame failed.");
    }
    impl_->video_frame->pts = output_frame;
    impl_->encode(impl_->video_codec.get(), impl_->video_stream, impl_->video_frame.get());
}

void VideoEncoder::writeAudio(
    std::span<const float> interleaved_stereo,
    int sample_count) {
    if (impl_ == nullptr || !impl_->settings.audio.has_value() || sample_count <= 0) return;
    const auto required = static_cast<std::size_t>(sample_count) * 2U;
    if (interleaved_stereo.size() < required) {
        throw std::invalid_argument("The audio input buffer is shorter than its sample count.");
    }
    check(av_frame_make_writable(impl_->audio_frame.get()), "Preparing an output audio frame");
    std::vector<std::int16_t> pcm(required);
    for (std::size_t index = 0; index < required; ++index) {
        const auto value = std::clamp(interleaved_stereo[index], -1.0F, 1.0F);
        pcm[index] = static_cast<std::int16_t>(std::lrint(value * 32767.0F));
    }
    const std::uint8_t* input_data[1]{reinterpret_cast<const std::uint8_t*>(pcm.data())};
    const int output_samples = impl_->audio_codec->frame_size > 0
        ? impl_->audio_codec->frame_size : 1024;
    const int converted = swr_convert(impl_->audio_resampler.get(),
        impl_->audio_frame->data, output_samples, input_data, sample_count);
    check(converted, "Converting the output audio samples");
    if (converted < output_samples) {
        check(av_samples_set_silence(impl_->audio_frame->data, converted,
            output_samples - converted, impl_->audio_codec->ch_layout.nb_channels,
            impl_->audio_codec->sample_fmt), "Padding the output audio frame");
    }
    impl_->audio_frame->nb_samples = output_samples;
    impl_->audio_frame->pts = impl_->next_audio_pts;
    impl_->next_audio_pts += output_samples;
    impl_->audio_input_samples_written += sample_count;
    impl_->encode(impl_->audio_codec.get(), impl_->audio_stream, impl_->audio_frame.get());
}

int VideoEncoder::nextAudioInputSampleCount() const {
    if (impl_ == nullptr || impl_->audio_codec == nullptr) return 1024;
    const auto frame_size = impl_->audio_codec->frame_size > 0
        ? impl_->audio_codec->frame_size : 1024;
    const int input_rate = impl_->settings.audio->input_sample_rate;
    const auto input_end = av_rescale_rnd(
        impl_->next_audio_pts + frame_size, input_rate,
        impl_->audio_sample_rate, AV_ROUND_UP);
    return static_cast<int>(std::max<std::int64_t>(
        1, input_end - impl_->audio_input_samples_written));
}

bool VideoEncoder::hasAudio() const noexcept {
    return impl_ != nullptr && impl_->audio_codec != nullptr;
}

void VideoEncoder::finish() {
    if (impl_ == nullptr || impl_->finished) return;
    impl_->encode(impl_->video_codec.get(), impl_->video_stream, nullptr);
    if (impl_->audio_codec != nullptr) {
        impl_->encode(impl_->audio_codec.get(), impl_->audio_stream, nullptr);
    }
    if (impl_->header_written) check(av_write_trailer(impl_->output.get()), "Finishing the output container");
    if (impl_->io_open) {
        check(avio_closep(&impl_->output->pb), "Closing the temporary output file");
        impl_->io_open = false;
    }
    impl_->finished = true;
}

}  // namespace creative_suite::media
