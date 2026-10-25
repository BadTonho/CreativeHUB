#include "timeline_audio_mix.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <unordered_set>
#include <utility>

namespace media {

float timelineAudioPeak(std::span<const float> samples) noexcept {
    float peak = 0.0F;
    for (const float sample : samples) {
        if (std::isfinite(sample)) peak = std::max(peak, std::abs(sample));
    }
    return peak;
}
namespace {

void applyTrackGainAndPan(
    float* frame,
    int channel_count,
    int source_channel_count,
    double gain,
    double pan) noexcept {
    if (frame == nullptr || channel_count <= 0) return;
    for (int channel = 0; channel < channel_count; ++channel) {
        frame[channel] = static_cast<float>(frame[channel] * gain);
    }
    if (channel_count < 2 || (pan == 0.0 && source_channel_count > 1)) return;
    const auto normalized_pan = std::clamp(pan, -1.0, 1.0);
    if (source_channel_count <= 1) {
        // FFmpeg has already expanded a mono source to the device layout. Fold
        // the duplicated front pair back to mono before applying the pan law.
        const auto mono = (frame[0] + frame[1]) * 0.5F;
        const auto angle = (normalized_pan + 1.0) * std::numbers::pi / 4.0;
        frame[0] = mono * static_cast<float>(std::cos(angle));
        frame[1] = mono * static_cast<float>(std::sin(angle));
        return;
    }
    if (normalized_pan > 0.0) {
        frame[0] *= static_cast<float>(1.0 - normalized_pan);
    } else if (normalized_pan < 0.0) {
        frame[1] *= static_cast<float>(1.0 + normalized_pan);
    }
}

void applyBusPan(float* frame, int channel_count, double gain, double pan) noexcept {
    if (frame == nullptr || channel_count <= 0) return;
    for (int channel = 0; channel < channel_count; ++channel) {
        frame[channel] = static_cast<float>(frame[channel] * gain);
    }
    if (channel_count < 2 || pan == 0.0) return;
    if (pan > 0.0) frame[0] *= static_cast<float>(1.0 - pan);
    else if (pan < 0.0) frame[1] *= static_cast<float>(1.0 + pan);
}

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
        const bool has_audible_pre_fader_send = std::any_of(
            clip.audio_sends.begin(), clip.audio_sends.end(),
            [](const timeline::AudioSend& send) {
                return send.tap == timeline::AudioSendTap::PreFader &&
                    send.gain > 0.0;
            });
        if (!std::isfinite(gain) ||
            (gain <= 0.0 && !has_audible_pre_fader_send) ||
            clip.clip_gain <= 0.0) continue;

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
        span.track_gain = clip.track_gain;
        span.clip_gain = clip.clip_gain;
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

TimelineAudioMixAccumulator::TimelineAudioMixAccumulator(
    std::size_t sample_count,
    int channel_count,
    TimelineAudioMixConfiguration configuration)
    : sample_count_(sample_count),
      channel_count_(channel_count),
      configuration_(std::move(configuration)),
      any_solo_(configuration_.apply_solo &&
          (!configuration_.solo_track_ids.empty() ||
           !configuration_.solo_bus_ids.empty())),
      master_samples_(sample_count * static_cast<std::size_t>(
          std::max(channel_count, 0)), 0.0F) {
    for (const auto& bus : configuration_.buses) {
        bus_samples_.try_emplace(bus.bus_id,
            sample_count_ * static_cast<std::size_t>(std::max(channel_count_, 0)),
            0.0F);
    }
}

bool TimelineAudioMixAccumulator::trackSoloed(
    timeline::TrackId track_id) const noexcept {
    return std::find(configuration_.solo_track_ids.begin(),
                     configuration_.solo_track_ids.end(), track_id) !=
        configuration_.solo_track_ids.end();
}

bool TimelineAudioMixAccumulator::busSoloed(
    timeline::AudioBusId bus_id) const noexcept {
    return std::find(configuration_.solo_bus_ids.begin(),
                     configuration_.solo_bus_ids.end(), bus_id) !=
        configuration_.solo_bus_ids.end();
}

std::vector<float>& TimelineAudioMixAccumulator::busBuffer(
    timeline::AudioBusId bus_id) {
    auto found = bus_samples_.find(bus_id);
    if (found == bus_samples_.end()) {
        found = bus_samples_.try_emplace(
            bus_id, sample_count_ * static_cast<std::size_t>(
                std::max(channel_count_, 0)), 0.0F).first;
    }
    return found->second;
}

void TimelineAudioMixAccumulator::accumulate(
    const TimelineAudioMixSpan& span,
    const TimelineAudioMixClip& clip,
    const AudioPcmChunk& chunk) {
    if (channel_count_ <= 0 || sample_count_ == 0 || clip.track_muted ||
        clip.clip_muted || span.destination_start_sample < 0 ||
        static_cast<std::uint64_t>(span.destination_start_sample) >= sample_count_) {
        return;
    }
    const bool track_solo = any_solo_ && trackSoloed(clip.track_id);
    const bool main_route_solo = !any_solo_ || track_solo ||
        (clip.output_bus_id.has_value() && busSoloed(*clip.output_bus_id));
    bool has_audible_route = main_route_solo;
    for (const auto& send : clip.audio_sends) {
        if (send.gain > 0.0 && (!any_solo_ || track_solo ||
                busSoloed(send.destination_bus_id))) {
            has_audible_route = true;
            break;
        }
    }
    if (!has_audible_route) return;

    std::vector<float> pre_fader(
        sample_count_ * static_cast<std::size_t>(channel_count_), 0.0F);
    auto pre_span = span;
    pre_span.gain = span.clip_gain;
    accumulateTimelineAudioChunk(pre_span, chunk, pre_fader, channel_count_);

    std::vector<float> post_fader = pre_fader;
    auto track_buffer = track_samples_.try_emplace(
        clip.track_id, pre_fader.size(), 0.0F).first;
    const bool include_main_route = main_route_solo;
    const bool include_any_send = std::any_of(
        clip.audio_sends.begin(), clip.audio_sends.end(),
        [this, track_solo, any_solo = any_solo_](const timeline::AudioSend& send) {
            return send.gain > 0.0 && (!any_solo || track_solo ||
                busSoloed(send.destination_bus_id));
        });
    const auto frames = std::min(sample_count_, pre_fader.size() /
        static_cast<std::size_t>(channel_count_));
    for (std::size_t frame_index = 0; frame_index < frames; ++frame_index) {
        const auto offset = frame_index * static_cast<std::size_t>(channel_count_);
        applyTrackGainAndPan(
            post_fader.data() + offset, channel_count_, clip.source_channel_count,
            clip.track_gain, clip.track_pan);
        for (int channel = 0; channel < channel_count_; ++channel) {
            const auto sample_offset = offset + static_cast<std::size_t>(channel);
            if (include_main_route || include_any_send) {
                track_buffer->second[sample_offset] += post_fader[sample_offset];
            }
            if (include_main_route && !clip.output_bus_id.has_value()) {
                master_samples_[sample_offset] += post_fader[sample_offset];
            }
            if (include_main_route && clip.output_bus_id.has_value()) {
                busBuffer(*clip.output_bus_id)[sample_offset] += post_fader[sample_offset];
            }
        }
    }

    for (const auto& send : clip.audio_sends) {
        const bool send_solo = !any_solo_ || track_solo ||
            busSoloed(send.destination_bus_id);
        if (!send_solo || send.gain <= 0.0) continue;
        auto& destination = busBuffer(send.destination_bus_id);
        const auto& source = send.tap == timeline::AudioSendTap::PreFader
            ? pre_fader : post_fader;
        for (std::size_t sample_index = 0; sample_index < source.size(); ++sample_index) {
            destination[sample_index] += static_cast<float>(
                source[sample_index] * send.gain);
        }
    }
}

TimelineAudioMixResult TimelineAudioMixAccumulator::finish() {
    TimelineAudioMixResult result;
    if (channel_count_ <= 0) {
        result.samples = std::move(master_samples_);
        return result;
    }
    for (auto& [bus_id, samples] : bus_samples_) {
        const auto bus = std::find_if(
            configuration_.buses.begin(), configuration_.buses.end(),
            [bus_id](const timeline::AudioBus& value) {
                return value.bus_id == bus_id;
            });
        if (bus == configuration_.buses.end()) continue;
        if (bus->audio_muted) {
            std::fill(samples.begin(), samples.end(), 0.0F);
        } else {
            for (std::size_t offset = 0; offset < samples.size();
                 offset += static_cast<std::size_t>(channel_count_)) {
                applyBusPan(samples.data() + offset, channel_count_,
                            bus->audio_gain, bus->audio_pan);
            }
        }
        result.bus_peaks.emplace_back(bus_id, timelineAudioPeak(samples));
        for (std::size_t index = 0;
             index < std::min(master_samples_.size(), samples.size()); ++index) {
            master_samples_[index] += samples[index];
        }
    }

    result.track_peaks.reserve(track_samples_.size());
    for (const auto& [track_id, samples] : track_samples_) {
        result.track_peaks.emplace_back(track_id, timelineAudioPeak(samples));
    }
    if (configuration_.master.audio_muted) {
        std::fill(master_samples_.begin(), master_samples_.end(), 0.0F);
    } else {
        const auto master_gain = configuration_.master.audio_gain;
        for (auto& sample : master_samples_) {
            sample = static_cast<float>(sample * master_gain);
        }
    }
    result.master_peak = timelineAudioPeak(master_samples_);
    result.samples = std::move(master_samples_);
    return result;
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
