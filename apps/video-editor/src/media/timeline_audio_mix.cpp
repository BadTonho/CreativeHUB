#include "timeline_audio_mix.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

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
        if (clip.kind != timeline::ClipKind::Video || !clip.has_audio ||
            clip.track_muted || clip.clip_muted || clip.duration_frames <= 0 ||
            clip.timeline_start_frame < 0 || clip.source_start_frame < 0 ||
            clip.timeline_start_frame >
                std::numeric_limits<std::int64_t>::max() - clip.duration_frames ||
            !std::isfinite(clip.source_frame_rate) || clip.source_frame_rate <= 0.0) {
            continue;
        }

        const auto gain = std::clamp(clip.track_gain * clip.clip_gain, 0.0, 16.0);
        if (!std::isfinite(gain) || gain <= 0.0) continue;

        auto audio_start_frame = clip.timeline_start_frame;
        for (const auto& transition : transitions) {
            if (transition.kind == timeline::TransitionKind::CrossDissolve &&
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
        const auto source_start_sample = sampleAtSourceFrame(
            clip.source_start_frame, clip.source_frame_rate, sample_rate);
        if (!audio_start_sample.has_value() || !timeline_origin_sample.has_value() ||
            !clip_end_sample.has_value() || !source_start_sample.has_value() ||
            *audio_start_sample < *timeline_origin_sample ||
            *source_start_sample > std::numeric_limits<std::int64_t>::max() -
                (*audio_start_sample - *timeline_origin_sample)) {
            continue;
        }

        const auto overlap_start = std::max(block_start_sample, *audio_start_sample);
        const auto overlap_end = std::min(block_end_sample, *clip_end_sample);
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

        spans.push_back(TimelineAudioMixSpan{
            clip.source_index,
            source_sample_begin,
            overlap_start - block_start_sample,
            span_sample_count,
            gain});
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
            mixed[destination_offset + static_cast<std::size_t>(channel)] +=
                static_cast<float>(chunk.samples[source_offset +
                    static_cast<std::size_t>(channel)]) / 32768.0F *
                static_cast<float>(span.gain);
        }
    }
}

} // namespace media
