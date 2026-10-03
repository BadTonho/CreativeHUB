#include "rendering/offline_export_renderer.h"
#include "rendering/export_composition.h"

#include "logging/logger.h"
#include "media/audio_playback.h"
#include "media/timeline_audio_mix.h"
#include "media/still_image_decoder.h"
#include "media/video_playback.h"
#include "media/video_probe.h"
#include "rendering/frame_compositor.h"
#include "rendering/text_renderer.h"
#include "timeline/timeline_transform.h"
#include "ui/workspace/pages/render/render_output_capabilities.h"

#include <creative_suite/media/video_encoder.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
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
using InputPtr = std::unique_ptr<AVFormatContext, FormatInputDeleter>;

std::string pathUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
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
            frame >= transition.boundary - transition.duration &&
            frame < transition.boundary) {
            const auto offset = frame -
                (transition.boundary - transition.duration);
            const double blend = transition.duration == 1
                ? 1.0
                : static_cast<double>(offset + 1) /
                    static_cast<double>(transition.duration);
            removeClip(from_index);
            removeClip(to_index);
            addClip(from_index, 1.0,
                    frame - clips[from_index].clip->timeline_start_frame);
            addClip(to_index, blend,
                    frame - clips[to_index].clip->timeline_start_frame);
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
    detail::ExportComposition& compositor,
    std::int64_t output_frame,
    OfflineExportMetrics& metrics,
    const std::atomic_bool& canceled) {
    std::vector<rendering::CompositionLayer> layers;
    std::vector<media::VideoFramePtr> decoded_frames;
    {
        detail::ExportTimedScope preparation(metrics.preparation_nanoseconds);
        const auto requests = activeClipRequests(clips, transitions, timeline_frame);
        decoded_frames.reserve(requests.size());
        layers.reserve(requests.size());
        for (const auto& request : requests) {
            checkCanceled(canceled);
            auto& render_clip = clips[request.clip_index];
            const auto& clip = *render_clip.clip;
            if (clip.kind == timeline::ClipKind::Audio) continue;
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
                decoded_frames.push_back(*decoded);
                layers.push_back({decoded_frames.back().get(), transform, {}});
            }
        }
        std::uint64_t resident = 0;
        for (const auto& decoded : decoded_frames) resident += decoded->rgba_pixels.size();
        for (const auto& clip : clips) {
            if (clip.still) resident += clip.still->rgba_pixels.size();
            if (clip.text) resident += clip.text->rgba_pixels.size();
        }
        metrics.peak_prepared_source_bytes = std::max(metrics.peak_prepared_source_bytes, resident);
    }
    return compositor.compose(layers, output_frame, timeline_frame, canceled);
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
    return creative_suite::media::publishEncodedFileAtomically(temporary, target);
}

class OutputEncoder final {
public:
    OutputEncoder(const ui::RenderJob& job,
                  const std::filesystem::path& output_path,
                  double frame_rate,
                  bool with_audio)
        : encoder_(makeSettings(job, output_path, frame_rate, with_audio)) {}

    void writeVideo(const media::VideoFrame& source, std::int64_t output_frame) {
        encoder_.writeVideo(source, output_frame);
    }

    void writeAudio(const std::vector<float>& stereo_samples, int sample_count) {
        encoder_.writeAudio(
            std::span<const float>(stereo_samples.data(), stereo_samples.size()), sample_count);
    }

    [[nodiscard]] int nextAudioInputSampleCount() const {
        return encoder_.nextAudioInputSampleCount();
    }

    void finish() { encoder_.finish(); }

private:
    static creative_suite::media::VideoEncodingSettings makeSettings(
        const ui::RenderJob& job,
        const std::filesystem::path& output_path,
        double frame_rate,
        bool with_audio) {
        const auto rate = frameRateRational(frame_rate);
        creative_suite::media::VideoEncodingSettings settings;
        settings.output_path = output_path;
        settings.container_name = job.settings.container_name.toStdString();
        settings.video_encoder_name = job.settings.video_encoder_name.toStdString();
        settings.width = job.settings.width;
        settings.height = job.settings.height;
        settings.frame_rate_numerator = rate.num;
        settings.frame_rate_denominator = rate.den;
        settings.video_bitrate_mbps = job.settings.video_bitrate_mbps;
        if (with_audio) {
            settings.audio = creative_suite::media::AudioEncodingSettings{
                job.settings.audio_encoder_name.toStdString(),
                job.settings.audio_bitrate_kbps,
                48000,
                2};
        }
        return settings;
    }

    creative_suite::media::VideoEncoder encoder_;
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
            } else if (clip.kind == timeline::ClipKind::Audio) {
                if (job.settings.export_audio) {
                    entry.audio = media::AudioPlaybackSession::open(path, {48000, 2});
                }
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

std::vector<media::TimelineAudioMixClip> audioMixClips(
    const std::vector<RenderClip>& clips) {
    std::vector<media::TimelineAudioMixClip> result;
    result.reserve(clips.size());
    for (std::size_t index = 0; index < clips.size(); ++index) {
        const auto& render_clip = clips[index];
        const auto& clip = *render_clip.clip;
        result.push_back(media::TimelineAudioMixClip{
            index,
            static_cast<std::int64_t>(render_clip.track_index),
            static_cast<std::int64_t>(render_clip.clip_index),
            clip.kind,
            render_clip.audio != nullptr && render_clip.audio->has_audio(),
            clip.timeline_start_frame,
            clip.duration_frames,
            clip.source_start_frame,
            render_clip.source_fps,
            render_clip.track->audio_gain,
            clip.audio_gain,
            render_clip.track->audio_muted,
            clip.audio_muted,
            clip.source_start_time_us,
            clip.source_duration_time_us,
            clip.audio_extracted});
    }
    return result;
}

std::vector<media::TimelineAudioMixTransition> audioMixTransitions(
    const std::vector<RenderTransition>& transitions) {
    std::vector<media::TimelineAudioMixTransition> result;
    result.reserve(transitions.size());
    for (const auto& transition : transitions) {
        result.push_back(media::TimelineAudioMixTransition{
            static_cast<std::int64_t>(transition.track_index),
            static_cast<std::int64_t>(transition.to_clip_index),
            transition.boundary,
            transition.kind});
    }
    return result;
}

void mixAudioBlock(
    std::vector<RenderClip>& clips,
    const std::vector<media::TimelineAudioMixClip>& audio_clips,
    const std::vector<media::TimelineAudioMixTransition>& transitions,
    double timeline_fps,
    std::int64_t block_start,
    int sample_count,
    std::vector<float>& mixed,
    const std::atomic_bool& canceled) {
    constexpr int sample_rate = 48000;
    mixed.assign(static_cast<std::size_t>(sample_count) * 2U, 0.0F);
    const auto spans = media::planTimelineAudioMix(
        audio_clips, transitions, timeline_fps, sample_rate,
        block_start, sample_count);
    for (const auto& span : spans) {
        checkCanceled(canceled);
        if (span.source_index >= clips.size()) continue;
        auto& render_clip = clips[span.source_index];
        if (!render_clip.audio || !render_clip.audio->has_audio()) continue;
        render_clip.audio->seek_to_sample_index(span.source_start_sample);
        auto source_cursor = span.source_start_sample;
        const auto source_end = span.source_start_sample + span.sample_count;
        auto remaining = static_cast<std::size_t>(span.sample_count);
        while (remaining > 0) {
            auto chunk = render_clip.audio->decode_samples(
                std::min<std::size_t>(remaining + 2048, 8192),
                [&canceled] { return canceled.load(std::memory_order_acquire); });
            if (!chunk.has_value() || chunk->sampleCount() == 0) break;
            media::accumulateTimelineAudioChunk(span, *chunk, mixed, 2);
            const auto chunk_end = chunk->first_sample_index +
                static_cast<std::int64_t>(chunk->sampleCount());
            const auto consumed_end = std::min(source_end, chunk_end);
            const auto consumed = static_cast<std::size_t>(
                std::max<std::int64_t>(0, consumed_end - source_cursor));
            if (consumed == 0) break;
            remaining -= std::min(remaining, consumed);
            source_cursor = consumed_end;
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
    ProgressCallback report_progress,
    const OfflineExportOptions& options) {
    detail::ExportMetricsScope summary(job, options);
    auto& metrics = summary.metrics;
    if (cancel_requested.load()) { metrics.outcome = ExportOutcome::Canceled; throw ExportCanceled{}; }
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
        auto clips = [&] {
            detail::ExportTimedScope preparation(metrics.preparation_nanoseconds);
            return prepareClips(job.project_snapshot, timeline_fps, job, cancel_requested);
        }();
        detail::ExportComposition compositor(job, options, metrics);
        const auto transitions = collectTransitions(job.project_snapshot);
        const auto audio_clips = audioMixClips(clips);
        const auto audio_transitions = audioMixTransitions(transitions);
        const auto duration_frames = timelineDuration(job.project_snapshot);
        if (duration_frames <= 0) throw std::runtime_error("The project timeline has no renderable duration.");
        const long double duration_seconds = static_cast<long double>(duration_frames) / timeline_fps;
        const auto output_frame_count = static_cast<std::int64_t>(std::ceil(
            duration_seconds * job.settings.frame_rate - 1.0e-9L));
        if (output_frame_count <= 0 || output_frame_count > 100000000) {
            throw std::runtime_error("The calculated output duration is outside the supported range.");
        }
        auto encoder = [&] {
            detail::ExportTimedScope encoding(metrics.encoding_nanoseconds);
            return OutputEncoder(job, temporary, job.settings.frame_rate, job.settings.export_audio);
        }();
        for (std::int64_t frame_index = 0; frame_index < output_frame_count; ++frame_index) {
            checkCanceled(cancel_requested);
            const auto timeline_frame = static_cast<std::int64_t>(std::floor(
                static_cast<long double>(frame_index) * timeline_fps /
                job.settings.frame_rate + 1.0e-9L));
            auto frame = composeFrame(
                clips, transitions, timeline_frame,
                job.project_snapshot.timeline_frame_rate,
                compositor, frame_index, metrics, cancel_requested);
            if (!frame.has_value()) throw std::runtime_error("Composing an output frame failed.");
            checkCanceled(cancel_requested);
            metrics.peak_cpu_frame_bytes = std::max(metrics.peak_cpu_frame_bytes,
                static_cast<std::uint64_t>(frame->rgba_pixels.size()));
            {
                detail::ExportTimedScope encoding(metrics.encoding_nanoseconds);
                encoder.writeVideo(*frame, frame_index);
            }
            ++metrics.encoded_frames;
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
                {
                    detail::ExportTimedScope audio(metrics.audio_nanoseconds);
                    mixAudioBlock(clips, audio_clips, audio_transitions, timeline_fps,
                                  sample, block_count, mixed, cancel_requested);
                }
                {
                    detail::ExportTimedScope encoding(metrics.encoding_nanoseconds);
                    encoder.writeAudio(mixed, block_count);
                }
                sample += block_count;
                if (total_samples > 0) {
                    reportProgress(static_cast<int>(std::min<std::int64_t>(
                        99, 80 + sample * 19 / total_samples)));
                }
            }
        }
        checkCanceled(cancel_requested);
        {
            detail::ExportTimedScope encoding(metrics.encoding_nanoseconds);
            encoder.finish();
        }
        {
            detail::ExportTimedScope finalization(metrics.finalization_nanoseconds);
            if (!verifyOutput(temporary, job.settings.export_audio)) {
                throw std::runtime_error("FFmpeg could not verify the completed output file and its configured streams.");
            }
            checkCanceled(cancel_requested);
            if (!publishFile(temporary, target)) {
                throw std::runtime_error("The completed output could not replace the destination file.");
            }
            published = true;
        }
        reportProgress(100);
        metrics.outcome = ExportOutcome::Completed;
    } catch (const ExportCanceled&) {
        metrics.outcome = ExportOutcome::Canceled;
        if (!published) {
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
        }
        throw;
    } catch (const creative_suite::media::VideoEncodingError& error) {
        if (!published) {
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
        }
        throw ExportError(error.what(), error.errorCode());
    } catch (...) {
        if (!published) {
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
        }
        throw;
    }
}

}  // namespace rendering
