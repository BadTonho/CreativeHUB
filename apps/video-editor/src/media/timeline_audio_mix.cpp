#include "timeline_audio_mix.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>

namespace media {
namespace {

std::optional<std::int64_t> sampleAtTimelineFrame(
    std::int64_t frame,
    double timeline_frame_rate,
    int sample_rate) noexcept {
    if (frame < 0 || !std::isfinite(timeline_frame_rate) ||
        timeline_frame_rate <= 0.0 || sample_rate <= 0) {
        return std::nullopt;
    }
    const auto sample = std::round(
        static_cast<long double>(frame) * sample_rate / timeline_frame_rate);
    if (!std::isfinite(sample) ||
        sample > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(sample);
}

std::optional<std::int64_t> sampleAtSourceFrame(
    std::int64_t frame,
    double source_frame_rate,
    int sample_rate) noexcept {
    if (frame < 0 || !std::isfinite(source_frame_rate) ||
        source_frame_rate <= 0.0 || sample_rate <= 0) {
        return std::nullopt;
    }
    const auto sample = std::round(
        static_cast<long double>(frame) * sample_rate / source_frame_rate);
    if (!std::isfinite(sample) ||
        sample > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(sample);
}

std::optional<std::int64_t> sampleAtSourceTimeUs(
    std::int64_t time_us,
    int sample_rate) noexcept {
    if (time_us < 0 || sample_rate <= 0) return std::nullopt;
    const auto sample = std::round(
        static_cast<long double>(time_us) * sample_rate / 1000000.0L);
    if (!std::isfinite(sample) ||
        sample > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(sample);
}

} // namespace

std::vector<TimelineAudioMixSpan> planTimelineAudioMix(
    std::span<const TimelineAudioMixClip> clips,
    std::span<const TimelineAudioMixTransition> transitions,
    double timeline_frame_rate,
    int sample_rate,
    std::int64_t block_start_sample,
    std::int64_t sample_count) {
    std::vector<TimelineAudioMixSpan> spans;
    if (!std::isfinite(timeline_frame_rate) || timeline_frame_rate <= 0.0 ||
        timeline_frame_rate > 1000.0 || sample_rate <= 0 ||
        block_start_sample < 0 || sample_count <= 0 ||
        block_start_sample > std::numeric_limits<std::int64_t>::max() - sample_count) {
        return spans;
    }

    const auto block_end_sample = block_start_sample + sample_count;
    spans.reserve(clips.size());
    for (const auto& clip : clips) {
        const bool supported_audio_kind =
            clip.kind == timeline::ClipKind::Video ||
            clip.kind == timeline::ClipKind::Audio;
        if (!supported_audio_kind || !clip.has_audio ||
            (clip.kind == timeline::ClipKind::Video && clip.audio_extracted) ||
            clip.track_muted || clip.clip_muted || clip.duration_frames <= 0 ||
            clip.timeline_start_frame < 0 || clip.source_start_frame < 0 ||
            clip.timeline_start_frame >
                std::numeric_limits<std::int64_t>::max() - clip.duration_frames ||
            (clip.kind == timeline::ClipKind::Video &&
             (!std::isfinite(clip.source_frame_rate) || clip.source_frame_rate <= 0.0)) ||
            (clip.kind == timeline::ClipKind::Audio &&
             (clip.source_start_time_us < 0 || clip.source_duration_time_us <= 0))) {
            continue;
        }

        const auto gain = std::clamp(clip.track_gain * clip.clip_gain, 0.0, 16.0);
        if (!std::isfinite(gain) || gain <= 0.0) continue;

        auto audio_start_frame = clip.timeline_start_frame;
        for (const auto& transition : transitions) {
            if (clip.kind == timeline::ClipKind::Video &&
                transition.kind == timeline::TransitionKind::CrossDissolve &&
                transition.track_index == clip.track_index &&
                transition.to_clip_index == clip.clip_index) {
                audio_start_frame = std::max(audio_start_frame, transition.boundary_frame);
            }
        }

        const auto audio_start_sample = sampleAtTimelineFrame(
            audio_start_frame, timeline_frame_rate, sample_rate);
        const auto timeline_origin_sample = sampleAtTimelineFrame(
            clip.timeline_start_frame, timeline_frame_rate, sample_rate);
        const auto clip_end_sample = sampleAtTimelineFrame(
            clip.timeline_start_frame + clip.duration_frames,
            timeline_frame_rate,
            sample_rate);
        const auto source_start_sample = clip.kind == timeline::ClipKind::Audio
            ? sampleAtSourceTimeUs(clip.source_start_time_us, sample_rate)
            : sampleAtSourceFrame(
                  clip.source_start_frame, clip.source_frame_rate, sample_rate);
        const auto source_duration_sample = clip.kind == timeline::ClipKind::Audio
            ? sampleAtSourceTimeUs(clip.source_duration_time_us, sample_rate)
            : std::optional<std::int64_t>{};
        if (!audio_start_sample.has_value() || !timeline_origin_sample.has_value() ||
            !clip_end_sample.has_value() || !source_start_sample.has_value() ||
            (clip.kind == timeline::ClipKind::Audio &&
             !source_duration_sample.has_value()) ||
            *audio_start_sample < *timeline_origin_sample ||
            *source_start_sample > std::numeric_limits<std::int64_t>::max() -
                (*audio_start_sample - *timeline_origin_sample)) {
            continue;
        }

        const auto overlap_start = std::max(block_start_sample, *audio_start_sample);
        auto effective_clip_end = *clip_end_sample;
        if (source_duration_sample.has_value()) {
            if (*timeline_origin_sample > std::numeric_limits<std::int64_t>::max() -
                    *source_duration_sample) {
                continue;
            }
            effective_clip_end = std::min(
                effective_clip_end,
                *timeline_origin_sample + *source_duration_sample);
        }
        const auto overlap_end = std::min(block_end_sample, effective_clip_end);
        if (overlap_start >= overlap_end) continue;
        const auto preroll_samples = *audio_start_sample - *timeline_origin_sample;
        const auto local_sample = overlap_start - *audio_start_sample;
        if (*source_start_sample > std::numeric_limits<std::int64_t>::max() -
                preroll_samples ||
            *source_start_sample + preroll_samples >
                std::numeric_limits<std::int64_t>::max() - local_sample) {
            continue;
        }
        const auto source_sample_begin =
            *source_start_sample + preroll_samples + local_sample;
        const auto span_sample_count = overlap_end - overlap_start;
        if (source_sample_begin > std::numeric_limits<std::int64_t>::max() -
                span_sample_count) {
            continue;
        }

        const auto local_frame_at_overlap = static_cast<double>(
            static_cast<long double>(overlap_start - *timeline_origin_sample) *
            timeline_frame_rate / sample_rate);
        TimelineAudioMixSpan span;
        span.source_index = clip.source_index;
        span.source_start_sample = source_sample_begin;
        span.destination_start_sample = overlap_start - block_start_sample;
        span.sample_count = span_sample_count;
        span.gain = gain;
        span.local_frame_at_destination_start = local_frame_at_overlap;
        span.frames_per_sample = timeline_frame_rate / sample_rate;
        span.audio_gain_keyframes = clip.audio_gain_keyframes;
        span.timeline_start_sample = block_start_sample;
        if (clip.kind == timeline::ClipKind::Audio) {
            for (const auto& transition : transitions) {
                if (transition.kind != timeline::TransitionKind::AudioCrossfade ||
                    transition.track_index != clip.track_index ||
                    transition.duration_frames <= 0 ||
                    (transition.from_clip_index != clip.clip_index &&
                     transition.to_clip_index != clip.clip_index) ||
                    transition.boundary_frame < transition.duration_frames) {
                    continue;
                }
                const auto fade_start = sampleAtTimelineFrame(
                    transition.boundary_frame - transition.duration_frames,
                    timeline_frame_rate, sample_rate);
                const auto fade_end = sampleAtTimelineFrame(
                    transition.boundary_frame, timeline_frame_rate, sample_rate);
                if (!fade_start.has_value() || !fade_end.has_value() ||
                    *fade_end <= *fade_start) {
                    continue;
                }
                span.transition_fades.push_back(TimelineAudioMixFade{
                    *fade_start, *fade_end,
                    transition.to_clip_index == clip.clip_index});
            }
        }
        spans.push_back(std::move(span));
    }
    return spans;
}

void accumulateTimelineAudioChunk(
    const TimelineAudioMixSpan& span,
    const AudioPcmChunk& chunk,
    std::span<float> mixed,
    int output_channel_count) {
    if (span.sample_count <= 0 || span.source_start_sample < 0 ||
        span.destination_start_sample < 0 || chunk.channel_count <= 0 ||
        chunk.channel_count != output_channel_count || output_channel_count <= 0 ||
        chunk.first_sample_index < 0) {
        return;
    }
    const auto destination_sample_count =
        mixed.size() / static_cast<std::size_t>(output_channel_count);
    if (static_cast<std::uint64_t>(span.destination_start_sample) >=
        destination_sample_count) {
        return;
    }

    const auto chunk_sample_count = static_cast<std::int64_t>(chunk.sampleCount());
    if (chunk_sample_count <= 0 ||
        chunk.first_sample_index > std::numeric_limits<std::int64_t>::max() -
            chunk_sample_count ||
        span.source_start_sample > std::numeric_limits<std::int64_t>::max() -
            span.sample_count) {
        return;
    }
    const auto source_begin = std::max(span.source_start_sample, chunk.first_sample_index);
    const auto source_end = std::min(
        span.source_start_sample + span.sample_count,
        chunk.first_sample_index + chunk_sample_count);
    if (source_begin >= source_end) return;

    const auto destination_begin = span.destination_start_sample +
        (source_begin - span.source_start_sample);
    const auto count = source_end - source_begin;
    if (destination_begin < 0 ||
        destination_begin > static_cast<std::int64_t>(destination_sample_count) - count) {
        return;
    }

    for (std::int64_t index = 0; index < count; ++index) {
        const auto source_offset = static_cast<std::size_t>(
            source_begin - chunk.first_sample_index + index) *
            static_cast<std::size_t>(output_channel_count);
        const auto destination_offset = static_cast<std::size_t>(
            destination_begin + index) * static_cast<std::size_t>(output_channel_count);
        for (int channel = 0; channel < output_channel_count; ++channel) {
            const auto local_frame = span.local_frame_at_destination_start +
                static_cast<double>(destination_begin + index -
                    span.destination_start_sample) * span.frames_per_sample;
            const auto envelope_gain = timeline::evaluateAudioGainEnvelope(
                span.audio_gain_keyframes, local_frame);
            const auto timeline_sample = span.timeline_start_sample +
                destination_begin + index;
            double transition_gain = 1.0;
            for (const auto& fade : span.transition_fades) {
                if (timeline_sample < fade.transition_start_sample ||
                    timeline_sample >= fade.transition_end_sample) {
                    continue;
                }
                const auto sample_duration = fade.transition_end_sample -
                    fade.transition_start_sample;
                if (sample_duration <= 0) continue;
                const auto fraction = std::clamp(
                    static_cast<double>(timeline_sample -
                        fade.transition_start_sample) /
                        static_cast<double>(sample_duration),
                    0.0, 1.0);
                const auto angle = fraction * std::numbers::pi / 2.0;
                transition_gain *= fade.incoming
                    ? std::sin(angle)
                    : std::cos(angle);
            }
            mixed[destination_offset + static_cast<std::size_t>(channel)] +=
                static_cast<float>(chunk.samples[source_offset +
                    static_cast<std::size_t>(channel)]) / 32768.0F *
                static_cast<float>(span.gain * envelope_gain * transition_gain);
        }
    }
}

} // namespace media
