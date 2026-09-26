#include "rendering/offline_export_renderer.h"

#include "logging/logger.h"
#include "media/audio_playback.h"
#include "media/still_image_decoder.h"
#include "media/video_playback.h"
#include "media/video_probe.h"
#include "rendering/frame_compositor.h"
#include "rendering/text_renderer.h"
#include "timeline/timeline_transform.h"
#include "ui/workspace/pages/render/render_output_capabilities.h"

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

#include <QString>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace rendering {
namespace {

struct FormatInputDeleter {
    void operator()(AVFormatContext* value) const noexcept {
        if (value != nullptr) avformat_close_input(&value);
    }
};
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

using InputPtr = std::unique_ptr<AVFormatContext, FormatInputDeleter>;
using OutputPtr = std::unique_ptr<AVFormatContext, FormatOutputDeleter>;
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
    throw ExportError(std::string(operation) + ": " + ffmpegMessage(code), code);
}

void check(int result, const char* operation) {
    if (result < 0) failFfmpeg(result, operation);
}

AVRational frameRateRational(double value) {
    if (!std::isfinite(value) || value <= 0.0 || value > 1000.0) {
        throw std::runtime_error("The output frame rate is invalid.");
    }
    return av_d2q(value, 1001000);
}

void checkCanceled(const std::atomic_bool& canceled) {
    if (canceled.load(std::memory_order_acquire)) throw ExportCanceled{};
}

struct RenderClip {
    const project::ProjectClip* clip = nullptr;
    const project::ProjectTrack* track = nullptr;
    std::size_t track_index = 0;
    std::size_t clip_index = 0;
    double source_fps = 30.0;
    std::unique_ptr<media::VideoPlaybackSession> video;
    std::optional<media::VideoFrame> still;
    std::optional<media::VideoFrame> text;
    std::unique_ptr<media::AudioPlaybackSession> audio;
};

struct RenderTransition {
    std::size_t track_index = 0;
    std::size_t from_clip_index = 0;
    std::size_t to_clip_index = 0;
    std::int64_t boundary = 0;
    std::int64_t duration = 0;
    timeline::TransitionKind kind = timeline::TransitionKind::CrossDissolve;
};

std::filesystem::path clipPath(const project::ProjectClip& clip) {
    if (clip.image_editor_variant.has_value() &&
        !clip.image_editor_variant->published_output_path.empty()) {
        std::error_code error;
        if (std::filesystem::is_regular_file(
                clip.image_editor_variant->published_output_path, error) && !error) {
            return clip.image_editor_variant->published_output_path;
        }
    }
    return clip.source_path;
}

double validFrameRate(const std::optional<double>& value, double fallback) {
    return value.has_value() && std::isfinite(*value) && *value > 0.0 && *value <= 1000.0
        ? *value
        : fallback;
}

void setFrameOpacity(timeline::Transform2D& transform, double multiplier) {
    transform.opacity = std::clamp(transform.opacity * multiplier, 0.0, 1.0);
}

struct ClipRequest {
    std::size_t clip_index = 0;
    double opacity = 1.0;
    std::int64_t local_frame = -1;
};

std::vector<ClipRequest> activeClipRequests(
    const std::vector<RenderClip>& clips,
    const std::vector<RenderTransition>& transitions,
    std::int64_t frame) {
    std::vector<ClipRequest> requests;
    for (std::size_t index = 0; index < clips.size(); ++index) {
        const auto& clip = *clips[index].clip;
        if (frame >= clip.timeline_start_frame &&
            frame < clip.timeline_start_frame + clip.duration_frames) {
            requests.push_back({index, 1.0, frame - clip.timeline_start_frame});
        }
    }

    const auto removeClip = [&requests](std::size_t index) {
        requests.erase(std::remove_if(requests.begin(), requests.end(), [index](const auto& item) {
            return item.clip_index == index;
        }), requests.end());
    };
    const auto addClip = [&requests](std::size_t index, double opacity, std::int64_t local_frame) {
        if (opacity > 0.0) requests.push_back({index, std::clamp(opacity, 0.0, 1.0), local_frame});
    };

    for (const auto& transition : transitions) {
        if (transition.duration <= 0) continue;
        const auto from = std::find_if(clips.begin(), clips.end(), [&transition](const auto& item) {
            return item.track_index == transition.track_index &&
                item.clip_index == transition.from_clip_index;
        });
        const auto to = std::find_if(clips.begin(), clips.end(), [&transition](const auto& item) {
            return item.track_index == transition.track_index &&
                item.clip_index == transition.to_clip_index;
        });
        if (from == clips.end() || to == clips.end()) continue;
        const auto from_index = static_cast<std::size_t>(std::distance(clips.begin(), from));
        const auto to_index = static_cast<std::size_t>(std::distance(clips.begin(), to));

        if (transition.kind == timeline::TransitionKind::CrossDissolve &&
            frame >= transition.boundary &&
            frame < transition.boundary + transition.duration) {
            const auto offset = frame - transition.boundary;
            const double blend = transition.duration == 1
                ? 1.0
                : static_cast<double>(offset + 1) /
                    static_cast<double>(transition.duration);
            removeClip(from_index);
            removeClip(to_index);
            addClip(from_index, 1.0, clips[from_index].clip->duration_frames - 1);
            addClip(to_index, blend, frame - transition.boundary);
        } else if (transition.kind == timeline::TransitionKind::FadeToBlack &&
                   frame >= transition.boundary - transition.duration &&
                   frame < transition.boundary) {
            const auto offset = frame - (transition.boundary - transition.duration);
            removeClip(from_index);
            addClip(from_index, 1.0 - static_cast<double>(offset + 1) /
                                      static_cast<double>(transition.duration),
                    frame - clips[from_index].clip->timeline_start_frame);
        } else if (transition.kind == timeline::TransitionKind::FadeToBlack &&
                   frame >= transition.boundary &&
                   frame < transition.boundary + transition.duration) {
            const auto offset = frame - transition.boundary;
            const double opacity = transition.duration == 1
                ? 0.0
                : static_cast<double>(offset) /
                    static_cast<double>(transition.duration - 1);
            removeClip(to_index);
            addClip(to_index, opacity, frame - transition.boundary);
        }
    }

    std::sort(requests.begin(), requests.end(), [&clips](const auto& left, const auto& right) {
        const auto& a = clips[left.clip_index];
        const auto& b = clips[right.clip_index];
        if (a.track_index != b.track_index) return a.track_index > b.track_index;
        if (a.clip->kind != b.clip->kind) {
            return timeline::isMediaClipKind(a.clip->kind);
        }
        return a.clip_index < b.clip_index;
    });
    return requests;
}

std::optional<media::VideoFrame> composeFrame(
    std::vector<RenderClip>& clips,
    const std::vector<RenderTransition>& transitions,
    std::int64_t timeline_frame,
    timeline::FrameRate timeline_frame_rate,
    int width,
    int height,
    const std::atomic_bool& canceled) {
    std::vector<rendering::CompositionLayer> layers;
    std::vector<media::VideoFrame> decoded_frames;
    const auto requests = activeClipRequests(clips, transitions, timeline_frame);
    decoded_frames.reserve(requests.size());
    layers.reserve(requests.size());
    for (const auto& request : requests) {
        checkCanceled(canceled);
        auto& render_clip = clips[request.clip_index];
        const auto& clip = *render_clip.clip;
        const auto local_frame = request.local_frame >= 0
            ? request.local_frame
            : timeline_frame - clip.timeline_start_frame;
        const auto local_transform = timeline::evaluateTransform(
            clip.transform, clip.keyframes, local_frame);
        auto transform = local_transform;
        setFrameOpacity(transform, request.opacity);

        if (clip.kind == timeline::ClipKind::Text) {
            if (!render_clip.text.has_value()) {
                auto text = rendering::renderText(clip.text);
                if (!text.has_value()) {
                    throw std::runtime_error("A text clip could not be rasterized.");
                }
                render_clip.text = std::move(*text);
            }
            layers.push_back({&*render_clip.text, transform, {}});
        } else if (clip.kind == timeline::ClipKind::Image) {
            if (!render_clip.still.has_value()) {
                throw std::runtime_error("An image clip has no decoded source frame.");
            }
            layers.push_back({&*render_clip.still, transform, {}});
        } else {
            const auto source_offset = timeline::sourceFrameOffsetForTimelineFrame(
                local_frame, render_clip.source_fps, timeline_frame_rate,
                clip.source_duration_frames);
            if (!source_offset.has_value() || *source_offset < 0 ||
                clip.source_start_frame > std::numeric_limits<std::int64_t>::max() -
                    *source_offset) {
                throw std::runtime_error("A source frame index exceeded the supported range.");
            }
            const auto source_frame = clip.source_start_frame + *source_offset;
            auto decoded = render_clip.video->decode_frame_at(
                source_frame,
                [&canceled] { return canceled.load(std::memory_order_acquire); });
            if (!decoded.has_value() || *decoded == nullptr) {
                checkCanceled(canceled);
                throw std::runtime_error(
                    "A video frame could not be decoded from " + pathUtf8(clipPath(clip)));
            }
            decoded_frames.push_back(**decoded);
            layers.push_back({&decoded_frames.back(), transform, {}});
        }
    }
    return FrameCompositor::compose(width, height, layers);
}

std::filesystem::path makeTemporaryPath(const std::filesystem::path& target, std::uint64_t id) {
    auto name = target.stem();
    const auto unique_stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto thread_token = std::hash<std::thread::id>{}(std::this_thread::get_id());
    const auto suffix_utf8 = ".rendering-" + std::to_string(id) + "-" +
        std::to_string(unique_stamp) + "-" + std::to_string(thread_token);
    const auto suffix = std::u8string(
        reinterpret_cast<const char8_t*>(suffix_utf8.data()), suffix_utf8.size());
    name += std::filesystem::path(suffix);
    name += target.extension();
    return target.parent_path() / name;
}

bool publishFile(const std::filesystem::path& temporary,
                 const std::filesystem::path& target) {
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(), target.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    return !error;
#endif
}

class OutputEncoder final {
public:
    OutputEncoder(const ui::RenderJob& job,
                  const std::filesystem::path& output_path,
                  double frame_rate,
                  bool with_audio)
        : job_(job), frame_rate_(frameRateRational(frame_rate)) {
        const auto output_utf8 = pathUtf8(output_path);
        AVFormatContext* raw_output = nullptr;
        check(avformat_alloc_output_context2(
                  &raw_output, nullptr,
                  job.settings.container_name.toUtf8().constData(),
                  output_utf8.c_str()),
              "Creating the output container");
        if (raw_output == nullptr) throw std::runtime_error("FFmpeg did not create an output container.");
        output_.reset(raw_output);
        video_stream_ = createVideoStream(job);
        if (with_audio) audio_stream_ = createAudioStream(job);
        if ((output_->oformat->flags & AVFMT_NOFILE) == 0) {
            check(avio_open(&output_->pb, output_utf8.c_str(), AVIO_FLAG_WRITE),
                  "Opening the temporary output file");
            io_open_ = true;
        }
        check(avformat_write_header(output_.get(), nullptr), "Writing the output header");
        header_written_ = true;
    }

    ~OutputEncoder() {
        if (io_open_ && output_ != nullptr) avio_closep(&output_->pb);
    }

    void writeVideo(const media::VideoFrame& source, std::int64_t output_frame) {
        check(av_frame_make_writable(video_frame_.get()), "Preparing an output video frame");
        const std::uint8_t* source_data[4]{source.rgba_pixels.data(), nullptr, nullptr, nullptr};
        const int source_lines[4]{source.stride, 0, 0, 0};
        const int rows = sws_scale(
            scaler_.get(), source_data, source_lines, 0, source.height,
            video_frame_->data, video_frame_->linesize);
        if (rows != job_.settings.height) throw std::runtime_error("Converting an output video frame failed.");
        video_frame_->pts = output_frame;
        encode(video_codec_.get(), video_stream_, video_frame_.get());
    }

    void writeAudio(const std::vector<float>& stereo_samples,
                    int sample_count) {
        if (audio_codec_ == nullptr || sample_count <= 0) return;
        check(av_frame_make_writable(audio_frame_.get()), "Preparing an output audio frame");
        std::vector<std::int16_t> pcm(static_cast<std::size_t>(sample_count) * 2U);
        for (std::size_t index = 0; index < pcm.size(); ++index) {
            const auto value = std::clamp(stereo_samples[index], -1.0F, 1.0F);
            pcm[index] = static_cast<std::int16_t>(std::lrint(value * 32767.0F));
        }
        const std::uint8_t* input_data[1]{reinterpret_cast<const std::uint8_t*>(pcm.data())};
        const int output_samples = audio_codec_->frame_size > 0
            ? audio_codec_->frame_size
            : 1024;
        const int converted = swr_convert(
            audio_resampler_.get(), audio_frame_->data, output_samples,
            input_data, sample_count);
        check(converted, "Converting the output audio samples");
        if (converted < output_samples) {
            check(av_samples_set_silence(
                      audio_frame_->data, converted, output_samples - converted,
                      audio_codec_->ch_layout.nb_channels, audio_codec_->sample_fmt),
                  "Padding the output audio frame");
        }
        audio_frame_->nb_samples = output_samples;
        audio_frame_->pts = next_audio_pts_;
        next_audio_pts_ += output_samples;
        audio_input_samples_written_ += sample_count;
        encode(audio_codec_.get(), audio_stream_, audio_frame_.get());
    }

    [[nodiscard]] int nextAudioInputSampleCount() const {
        if (audio_codec_ == nullptr) return 1024;
        const auto frame_size = audio_codec_->frame_size > 0
            ? audio_codec_->frame_size
            : 1024;
        const auto input_end = av_rescale_rnd(
            next_audio_pts_ + frame_size, 48000, audio_sample_rate_, AV_ROUND_UP);
        return static_cast<int>(std::max<std::int64_t>(
            1, input_end - audio_input_samples_written_));
    }

    void finish() {
        if (finished_) return;
        encode(video_codec_.get(), video_stream_, nullptr);
        if (audio_codec_ != nullptr) {
            encode(audio_codec_.get(), audio_stream_, nullptr);
        }
        if (header_written_) check(av_write_trailer(output_.get()), "Finishing the output container");
        if (io_open_) {
            check(avio_closep(&output_->pb), "Closing the temporary output file");
            io_open_ = false;
        }
        finished_ = true;
    }

private:
    AVStream* createVideoStream(const ui::RenderJob& job) {
        const auto encoder_name = job.settings.video_encoder_name.toUtf8();
        const AVCodec* codec = avcodec_find_encoder_by_name(encoder_name.constData());
        if (codec == nullptr || codec->type != AVMEDIA_TYPE_VIDEO) {
            throw std::runtime_error("The selected video encoder is unavailable.");
        }
        video_codec_.reset(avcodec_alloc_context3(codec));
        if (video_codec_ == nullptr) throw std::runtime_error("Allocating the video encoder failed.");
        video_codec_->codec_type = AVMEDIA_TYPE_VIDEO;
        video_codec_->width = job.settings.width;
        video_codec_->height = job.settings.height;
        video_codec_->time_base = av_inv_q(frame_rate_);
        video_codec_->framerate = frame_rate_;
        video_codec_->bit_rate = static_cast<std::int64_t>(
            std::llround(job.settings.video_bitrate_mbps * 1000000.0));
        video_codec_->gop_size = std::max(1, av_q2d(frame_rate_) > 0.0
            ? static_cast<int>(std::lround(av_q2d(frame_rate_) * 2.0))
            : 60);
        video_codec_->max_b_frames = 0;
        if (output_->oformat->flags & AVFMT_GLOBALHEADER) {
            video_codec_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        const void* pixel_config = nullptr;
        int pixel_config_count = 0;
        check(avcodec_get_supported_config(
                  video_codec_.get(), codec, AV_CODEC_CONFIG_PIX_FORMAT, 0,
                  &pixel_config, &pixel_config_count),
              "Querying supported encoder pixel formats");
        video_codec_->pix_fmt = AV_PIX_FMT_YUV420P;
        if (pixel_config != nullptr && pixel_config_count > 0) {
            const auto* formats = static_cast<const AVPixelFormat*>(pixel_config);
            bool selected = false;
            for (int index = 0; index < pixel_config_count; ++index) {
                if (formats[index] == AV_PIX_FMT_YUV420P) {
                    video_codec_->pix_fmt = formats[index];
                    selected = true;
                    break;
                }
            }
            if (!selected) {
                for (int index = 0; index < pixel_config_count; ++index) {
                    const auto* description = av_pix_fmt_desc_get(formats[index]);
                    if (description != nullptr &&
                        (description->flags & AV_PIX_FMT_FLAG_HWACCEL) == 0) {
                        video_codec_->pix_fmt = formats[index];
                        selected = true;
                        break;
                    }
                }
            }
            if (!selected) video_codec_->pix_fmt = formats[0];
        }
        AVDictionary* options = nullptr;
        if (encoder_name == QByteArray("libx264")) {
            av_dict_set(&options, "preset", "medium", 0);
        }
        const int open_result = avcodec_open2(video_codec_.get(), codec, &options);
        av_dict_free(&options);
        check(open_result, "Opening the selected video encoder");
        auto* stream = avformat_new_stream(output_.get(), nullptr);
        if (stream == nullptr) throw std::runtime_error("Creating the output video stream failed.");
        stream->time_base = video_codec_->time_base;
        check(avcodec_parameters_from_context(stream->codecpar, video_codec_.get()),
              "Writing video encoder parameters");
        video_frame_.reset(av_frame_alloc());
        if (video_frame_ == nullptr) throw std::runtime_error("Allocating an output video frame failed.");
        video_frame_->format = video_codec_->pix_fmt;
        video_frame_->width = video_codec_->width;
        video_frame_->height = video_codec_->height;
        check(av_frame_get_buffer(video_frame_.get(), 32), "Allocating output video pixels");
        scaler_.reset(sws_getContext(
            job.settings.width, job.settings.height, AV_PIX_FMT_RGBA,
            job.settings.width, job.settings.height, video_codec_->pix_fmt,
            SWS_BICUBIC, nullptr, nullptr, nullptr));
        if (scaler_ == nullptr) throw std::runtime_error("Creating the video pixel converter failed.");
        return stream;
    }

    AVStream* createAudioStream(const ui::RenderJob& job) {
        const auto encoder_name = job.settings.audio_encoder_name.toUtf8();
        const AVCodec* codec = avcodec_find_encoder_by_name(encoder_name.constData());
        if (codec == nullptr || codec->type != AVMEDIA_TYPE_AUDIO) {
            throw std::runtime_error("The selected audio encoder is unavailable.");
        }
        audio_codec_.reset(avcodec_alloc_context3(codec));
        if (audio_codec_ == nullptr) throw std::runtime_error("Allocating the audio encoder failed.");
        const void* rate_config = nullptr;
        int rate_config_count = 0;
        check(avcodec_get_supported_config(
                  audio_codec_.get(), codec, AV_CODEC_CONFIG_SAMPLE_RATE, 0,
                  &rate_config, &rate_config_count),
              "Querying supported audio sample rates");
        audio_sample_rate_ = 48000;
        if (rate_config != nullptr && rate_config_count > 0) {
            const auto* rates = static_cast<const int*>(rate_config);
            audio_sample_rate_ = rates[0];
            for (int index = 0; index < rate_config_count; ++index) {
                if (rates[index] == 48000) {
                    audio_sample_rate_ = rates[index];
                    break;
                }
            }
        }
        audio_codec_->sample_rate = audio_sample_rate_;
        av_channel_layout_default(&audio_codec_->ch_layout, 2);
        const void* sample_config = nullptr;
        int sample_config_count = 0;
        check(avcodec_get_supported_config(
                  audio_codec_.get(), codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                  &sample_config, &sample_config_count),
              "Querying supported audio sample formats");
        audio_codec_->sample_fmt = sample_config != nullptr && sample_config_count > 0
            ? static_cast<const AVSampleFormat*>(sample_config)[0]
            : AV_SAMPLE_FMT_FLTP;
        audio_codec_->time_base = AVRational{1, audio_sample_rate_};
        audio_codec_->bit_rate = static_cast<std::int64_t>(job.settings.audio_bitrate_kbps) * 1000;
        if (output_->oformat->flags & AVFMT_GLOBALHEADER) {
            audio_codec_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        check(avcodec_open2(audio_codec_.get(), codec, nullptr), "Opening the selected audio encoder");
        auto* stream = avformat_new_stream(output_.get(), nullptr);
        if (stream == nullptr) throw std::runtime_error("Creating the output audio stream failed.");
        stream->time_base = audio_codec_->time_base;
        check(avcodec_parameters_from_context(stream->codecpar, audio_codec_.get()),
              "Writing audio encoder parameters");

        AVChannelLayout input_layout{};
        av_channel_layout_default(&input_layout, 2);
        SwrContext* raw_resampler = nullptr;
        const int result = swr_alloc_set_opts2(
            &raw_resampler, &audio_codec_->ch_layout, audio_codec_->sample_fmt,
            audio_codec_->sample_rate, &input_layout, AV_SAMPLE_FMT_S16,
            48000, 0, nullptr);
        av_channel_layout_uninit(&input_layout);
        check(result, "Creating the output audio converter");
        if (raw_resampler == nullptr) throw std::runtime_error("Creating the output audio converter failed.");
        audio_resampler_.reset(raw_resampler);
        check(swr_init(audio_resampler_.get()), "Initializing the output audio converter");
        audio_frame_.reset(av_frame_alloc());
        if (audio_frame_ == nullptr) throw std::runtime_error("Allocating an output audio frame failed.");
        audio_frame_->format = audio_codec_->sample_fmt;
        audio_frame_->sample_rate = audio_codec_->sample_rate;
        check(av_channel_layout_copy(&audio_frame_->ch_layout, &audio_codec_->ch_layout),
              "Copying the output audio layout");
        audio_frame_->nb_samples = audio_codec_->frame_size > 0 ? audio_codec_->frame_size : 1024;
        check(av_frame_get_buffer(audio_frame_.get(), 0), "Allocating output audio samples");
        return stream;
    }

    void encode(AVCodecContext* codec, AVStream* stream, AVFrame* frame) {
        check(avcodec_send_frame(codec, frame), frame == nullptr ? "Flushing an output encoder" : "Encoding output media");
        while (true) {
            PacketPtr packet(av_packet_alloc());
            if (packet == nullptr) throw std::runtime_error("Allocating an output packet failed.");
            const int result = avcodec_receive_packet(codec, packet.get());
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
            check(result, "Receiving an encoded output packet");
            packet->stream_index = stream->index;
            av_packet_rescale_ts(packet.get(), codec->time_base, stream->time_base);
            check(av_interleaved_write_frame(output_.get(), packet.get()), "Writing an output packet");
        }
    }

    const ui::RenderJob& job_;
    AVRational frame_rate_{0, 1};
    OutputPtr output_;
    CodecPtr video_codec_;
    CodecPtr audio_codec_;
    AVStream* video_stream_ = nullptr;
    AVStream* audio_stream_ = nullptr;
    FramePtr video_frame_;
    FramePtr audio_frame_;
    ScalerPtr scaler_;
    ResamplerPtr audio_resampler_;
    int audio_sample_rate_ = 48000;
    std::int64_t next_audio_pts_ = 0;
    std::int64_t audio_input_samples_written_ = 0;
    bool io_open_ = false;
    bool header_written_ = false;
    bool finished_ = false;
};

std::vector<RenderClip> prepareClips(
    const project::ProjectDocument& document,
    double& timeline_fps,
    const ui::RenderJob& job,
    const std::atomic_bool& canceled) {
    std::vector<RenderClip> clips;
    if (!timeline::validFrameRate(document.timeline_frame_rate)) {
        throw std::runtime_error("The project timeline frame rate is invalid.");
    }
    timeline_fps = document.timeline_frame_rate.asDouble();

    for (std::size_t track_index = 0; track_index < document.timeline_tracks.size(); ++track_index) {
        const auto& track = document.timeline_tracks[track_index];
        for (std::size_t clip_index = 0; clip_index < track.clips.size(); ++clip_index) {
            checkCanceled(canceled);
            const auto& clip = track.clips[clip_index];
            if (clip.duration_frames <= 0 || clip.timeline_start_frame < 0) continue;
            RenderClip entry;
            entry.clip = &clip;
            entry.track = &track;
            entry.track_index = track_index;
            entry.clip_index = clip_index;
            if (clip.kind == timeline::ClipKind::Text) {
                clips.push_back(std::move(entry));
                continue;
            }
            const auto path = clipPath(clip);
            if (path.empty()) {
                throw std::runtime_error("A timeline clip has no source path.");
            }
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error) || error) {
                throw std::runtime_error("Media is offline or unreadable: " + pathUtf8(path));
            }
            if (clip.kind == timeline::ClipKind::Image) {
                entry.source_fps = media::kStillImageFrameRate;
                entry.still = media::StillImageDecoder{}.decode_first_frame(path);
            } else {
                const auto metadata = media::VideoProbe{}.probe(path);
                entry.source_fps = validFrameRate(metadata.frame_rate, timeline_fps);
                entry.video = media::VideoPlaybackSession::open(path);
                if (job.settings.export_audio) {
                    entry.audio = media::AudioPlaybackSession::open(path, {48000, 2});
                }
            }
            clips.push_back(std::move(entry));
        }
    }
    // Store transition data with the clip collection for the frame loop.
    // Kept separately by the caller to avoid mutating the immutable project snapshot.
    return clips;
}

std::vector<RenderTransition> collectTransitions(const project::ProjectDocument& document) {
    std::vector<RenderTransition> result;
    for (std::size_t track_index = 0; track_index < document.timeline_tracks.size(); ++track_index) {
        const auto& track = document.timeline_tracks[track_index];
        for (const auto& transition : track.transitions) {
            if (transition.from_clip_index >= track.clips.size() ||
                transition.to_clip_index >= track.clips.size()) continue;
            const auto& from = track.clips[transition.from_clip_index];
            if (from.duration_frames <= 0 || from.timeline_start_frame < 0 ||
                from.timeline_start_frame > std::numeric_limits<std::int64_t>::max() -
                    from.duration_frames) continue;
            result.push_back(RenderTransition{
                track_index, transition.from_clip_index, transition.to_clip_index,
                from.timeline_start_frame + from.duration_frames,
                transition.duration_frames, transition.kind});
        }
    }
    return result;
}

std::int64_t timelineDuration(const project::ProjectDocument& document) {
    std::int64_t duration = 0;
    for (const auto& track : document.timeline_tracks) {
        for (const auto& clip : track.clips) {
            if (clip.duration_frames <= 0 || clip.timeline_start_frame < 0 ||
                clip.timeline_start_frame > std::numeric_limits<std::int64_t>::max() - clip.duration_frames) {
                continue;
            }
            duration = std::max(duration, clip.timeline_start_frame + clip.duration_frames);
        }
    }
    return duration;
}

void mixAudioBlock(
    std::vector<RenderClip>& clips,
    double timeline_fps,
    std::int64_t block_start,
    int sample_count,
    std::vector<float>& mixed,
    const std::atomic_bool& canceled) {
    constexpr int sample_rate = 48000;
    const auto block_end = block_start + sample_count;
    mixed.assign(static_cast<std::size_t>(sample_count) * 2U, 0.0F);
    for (auto& render_clip : clips) {
        if (render_clip.clip->kind != timeline::ClipKind::Video ||
            !render_clip.audio || !render_clip.audio->has_audio() ||
            render_clip.track->audio_muted || render_clip.clip->audio_muted) continue;
        checkCanceled(canceled);
        const auto& clip = *render_clip.clip;
        const auto clip_start = static_cast<std::int64_t>(std::llround(
            static_cast<long double>(clip.timeline_start_frame) * sample_rate / timeline_fps));
        const auto clip_end = static_cast<std::int64_t>(std::llround(
            static_cast<long double>(clip.timeline_start_frame + clip.duration_frames) *
            sample_rate / timeline_fps));
        const auto overlap_start = std::max(block_start, clip_start);
        const auto overlap_end = std::min(block_end, clip_end);
        if (overlap_start >= overlap_end) continue;
        const auto local_sample = overlap_start - clip_start;
        const auto source_start_sample = static_cast<std::int64_t>(std::llround(
            static_cast<long double>(clip.source_start_frame) * sample_rate /
            render_clip.source_fps));
        const auto requested_source_sample = source_start_sample + local_sample;
        render_clip.audio->seek_to_sample_index(requested_source_sample);
        auto remaining = static_cast<std::size_t>(overlap_end - overlap_start);
        auto source_cursor = requested_source_sample;
        const auto gain = static_cast<float>(std::clamp(
            render_clip.track->audio_gain * clip.audio_gain, 0.0, 16.0));
        while (remaining > 0) {
            auto chunk = render_clip.audio->decode_samples(
                std::min<std::size_t>(remaining + 2048, 8192),
                [&canceled] { return canceled.load(std::memory_order_acquire); });
            if (!chunk.has_value() || chunk->sampleCount() == 0) break;
            const auto chunk_begin = chunk->first_sample_index;
            const auto chunk_end = chunk_begin + static_cast<std::int64_t>(chunk->sampleCount());
            const auto source_begin = std::max(source_cursor, chunk_begin);
            const auto destination_begin = overlap_start + (source_begin - requested_source_sample);
            const auto source_end = std::min(
                chunk_end,
                requested_source_sample + (overlap_end - overlap_start));
            for (auto source_index = source_begin; source_index < source_end; ++source_index) {
                const auto source_offset = static_cast<std::size_t>(source_index - chunk_begin) * 2U;
                const auto destination_offset = static_cast<std::size_t>(
                    destination_begin + (source_index - source_begin) - block_start) * 2U;
                mixed[destination_offset] +=
                    static_cast<float>(chunk->samples[source_offset]) / 32768.0F * gain;
                mixed[destination_offset + 1] +=
                    static_cast<float>(chunk->samples[source_offset + 1]) / 32768.0F * gain;
            }
            const auto consumed = static_cast<std::size_t>(std::max<std::int64_t>(0, source_end - source_cursor));
            if (consumed == 0) break;
            remaining -= std::min(remaining, consumed);
            source_cursor = source_end;
        }
    }
}

bool verifyOutput(const std::filesystem::path& path, bool expect_audio) {
    AVFormatContext* raw = nullptr;
    const auto utf8 = pathUtf8(path);
    if (avformat_open_input(&raw, utf8.c_str(), nullptr, nullptr) < 0) {
        if (raw != nullptr) avformat_close_input(&raw);
        return false;
    }
    InputPtr input(raw);
    if (avformat_find_stream_info(input.get(), nullptr) < 0) return false;
    if (av_find_best_stream(input.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0) < 0) {
        return false;
    }
    return !expect_audio ||
        av_find_best_stream(input.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) >= 0;
}

}  // namespace

void OfflineExportRenderer::render(
    const ui::RenderJob& job,
    const std::atomic_bool& cancel_requested,
    ProgressCallback report_progress) {
    const auto target_utf8 = job.settings.output_path.toUtf8();
    auto target_u8 = std::u8string(
        reinterpret_cast<const char8_t*>(target_utf8.constData()),
        static_cast<std::size_t>(target_utf8.size()));
    std::filesystem::path target(target_u8);
    std::error_code path_error;
    target = std::filesystem::absolute(target, path_error);
    if (path_error) throw std::runtime_error("The output path could not be resolved.");
    if (target.empty()) throw std::runtime_error("The output path is empty.");
    if (job.settings.width <= 0 || job.settings.height <= 0 ||
        !std::isfinite(job.settings.frame_rate) || job.settings.frame_rate <= 0.0 ||
        !std::isfinite(job.settings.video_bitrate_mbps) || job.settings.video_bitrate_mbps <= 0.0) {
        throw std::runtime_error("The render settings are invalid.");
    }
    if (!std::filesystem::exists(target.parent_path())) {
        throw std::runtime_error("The output folder does not exist: " + pathUtf8(target.parent_path()));
    }
    const auto temporary = makeTemporaryPath(target, job.id);
    std::error_code remove_error;
    std::filesystem::remove(temporary, remove_error);
    if (remove_error) throw std::runtime_error("A stale temporary output file could not be removed.");
    bool published = false;
    int last_reported_progress = -1;
    const auto reportProgress = [&report_progress, &last_reported_progress](int progress) {
        progress = std::clamp(progress, 0, 100);
        if (report_progress && progress != last_reported_progress) {
            last_reported_progress = progress;
            report_progress(progress);
        }
    };
    try {
        double timeline_fps = 30.0;
        auto clips = prepareClips(job.project_snapshot, timeline_fps, job, cancel_requested);
        const auto transitions = collectTransitions(job.project_snapshot);
        const auto duration_frames = timelineDuration(job.project_snapshot);
        if (duration_frames <= 0) throw std::runtime_error("The project timeline has no renderable duration.");
        const long double duration_seconds = static_cast<long double>(duration_frames) / timeline_fps;
        const auto output_frame_count = static_cast<std::int64_t>(std::ceil(
            duration_seconds * job.settings.frame_rate - 1.0e-9L));
        if (output_frame_count <= 0 || output_frame_count > 100000000) {
            throw std::runtime_error("The calculated output duration is outside the supported range.");
        }
        OutputEncoder encoder(job, temporary,
                              job.settings.frame_rate, job.settings.export_audio);
        for (std::int64_t frame_index = 0; frame_index < output_frame_count; ++frame_index) {
            checkCanceled(cancel_requested);
            const auto timeline_frame = static_cast<std::int64_t>(std::floor(
                static_cast<long double>(frame_index) * timeline_fps /
                job.settings.frame_rate + 1.0e-9L));
            auto frame = composeFrame(
                clips, transitions, timeline_frame,
                job.project_snapshot.timeline_frame_rate,
                job.settings.width, job.settings.height, cancel_requested);
            if (!frame.has_value()) throw std::runtime_error("Composing an output frame failed.");
            encoder.writeVideo(*frame, frame_index);
            reportProgress(static_cast<int>(
                (frame_index + 1) * (job.settings.export_audio ? 80 : 99) /
                output_frame_count));
        }

        if (job.settings.export_audio) {
            constexpr int output_sample_rate = 48000;
            const auto total_samples = static_cast<std::int64_t>(std::ceil(
                duration_seconds * output_sample_rate));
            std::vector<float> mixed;
            std::int64_t sample = 0;
            while (sample < total_samples) {
                checkCanceled(cancel_requested);
                const auto block_count = encoder.nextAudioInputSampleCount();
                mixAudioBlock(clips, timeline_fps,
                              sample, block_count, mixed, cancel_requested);
                encoder.writeAudio(mixed, block_count);
                sample += block_count;
                if (total_samples > 0) {
                    reportProgress(static_cast<int>(std::min<std::int64_t>(
                        99, 80 + sample * 19 / total_samples)));
                }
            }
        }
        checkCanceled(cancel_requested);
        encoder.finish();
        if (!verifyOutput(temporary, job.settings.export_audio)) {
            throw std::runtime_error("FFmpeg could not verify the completed output file and its configured streams.");
        }
        checkCanceled(cancel_requested);
        if (!publishFile(temporary, target)) {
            throw std::runtime_error("The completed output could not replace the destination file.");
        }
        published = true;
        reportProgress(100);
    } catch (...) {
        if (!published) {
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
        }
        throw;
    }
}

}  // namespace rendering
