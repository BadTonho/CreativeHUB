#include "timeline_audio_mix.h"

#include <algorithm>
#include <array>
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

struct BiquadCoefficients {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

struct BiquadDelay {
    double x1 = 0.0;
    double x2 = 0.0;
    double y1 = 0.0;
    double y2 = 0.0;
};

struct TrackDspState {
    std::array<std::vector<BiquadDelay>, 3> equalizer_delays;
    double compressor_reduction_db = 0.0;
    int sample_rate = 0;
    int channel_count = 0;
    std::int64_t next_source_sample_index = -1;
    std::vector<timeline::AudioEffectInstance> configured_effects;
};

bool hasActiveAudioEffectProcessing(
    const std::vector<timeline::AudioEffectInstance>& effects) noexcept {
    return std::any_of(effects.begin(), effects.end(),
        [](const timeline::AudioEffectInstance& effect) {
            if (!effect.enabled) return false;
            if (const auto* eq = std::get_if<
                    timeline::AudioEqualizerParameters>(&effect.parameters)) {
                return eq->low_shelf_gain_db != 0.0 ||
                    eq->mid_gain_db != 0.0 || eq->high_shelf_gain_db != 0.0;
            }
            if (const auto* compressor = std::get_if<
                    timeline::AudioCompressorParameters>(&effect.parameters)) {
                return compressor->ratio != 1.0 ||
                    compressor->makeup_gain_db != 0.0;
            }
            return false;
        });
}

BiquadCoefficients normalizeBiquad(
    double b0, double b1, double b2,
    double a0, double a1, double a2) noexcept {
    if (!std::isfinite(a0) || std::abs(a0) < 1.0e-12) return {};
    const auto inverse_a0 = 1.0 / a0;
    return {b0 * inverse_a0, b1 * inverse_a0, b2 * inverse_a0,
            a1 * inverse_a0, a2 * inverse_a0};
}

double safeFrequency(double requested, int sample_rate) noexcept {
    if (sample_rate <= 0) return 20.0;
    return std::clamp(requested, 1.0,
        std::max(1.0, static_cast<double>(sample_rate) * 0.45));
}

BiquadCoefficients peakingCoefficients(
    double frequency, double gain_db, double q, int sample_rate) noexcept {
    const auto f = safeFrequency(frequency, sample_rate);
    const auto a = std::pow(10.0, gain_db / 40.0);
    const auto omega = 2.0 * std::numbers::pi * f / sample_rate;
    const auto cosine = std::cos(omega);
    const auto alpha = std::sin(omega) / (2.0 * q);
    return normalizeBiquad(1.0 + alpha * a, -2.0 * cosine,
        1.0 - alpha * a, 1.0 + alpha / a, -2.0 * cosine,
        1.0 - alpha / a);
}

BiquadCoefficients shelfCoefficients(
    double frequency, double gain_db, int sample_rate, bool high_shelf) noexcept {
    const auto f = safeFrequency(frequency, sample_rate);
    const auto a = std::pow(10.0, gain_db / 40.0);
    const auto omega = 2.0 * std::numbers::pi * f / sample_rate;
    const auto cosine = std::cos(omega);
    const auto sine = std::sin(omega);
    constexpr double slope = 1.0;
    const auto alpha = sine / 2.0 * std::sqrt(
        (a + 1.0 / a) * (1.0 / slope - 1.0) + 2.0);
    const auto beta = 2.0 * std::sqrt(a) * alpha;
    if (!high_shelf) {
        return normalizeBiquad(
            a * ((a + 1.0) - (a - 1.0) * cosine + beta),
            2.0 * a * ((a - 1.0) - (a + 1.0) * cosine),
            a * ((a + 1.0) - (a - 1.0) * cosine - beta),
            (a + 1.0) + (a - 1.0) * cosine + beta,
            -2.0 * ((a - 1.0) + (a + 1.0) * cosine),
            (a + 1.0) + (a - 1.0) * cosine - beta);
    }
    return normalizeBiquad(
        a * ((a + 1.0) + (a - 1.0) * cosine + beta),
        -2.0 * a * ((a - 1.0) + (a + 1.0) * cosine),
        a * ((a + 1.0) + (a - 1.0) * cosine - beta),
        (a + 1.0) - (a - 1.0) * cosine + beta,
        2.0 * ((a - 1.0) - (a + 1.0) * cosine),
        (a + 1.0) - (a - 1.0) * cosine - beta);
}

float applyBiquad(float input, const BiquadCoefficients& coefficients,
                  BiquadDelay& delay) noexcept {
    const auto output = coefficients.b0 * input + coefficients.b1 * delay.x1 +
        coefficients.b2 * delay.x2 - coefficients.a1 * delay.y1 -
        coefficients.a2 * delay.y2;
    delay.x2 = delay.x1;
    delay.x1 = input;
    delay.y2 = delay.y1;
    delay.y1 = output;
    return std::isfinite(output) ? static_cast<float>(output) : 0.0F;
}

void processEqualizer(
    std::vector<float>& samples,
    int channel_count,
    int sample_rate,
    const timeline::AudioEqualizerParameters& parameters,
    TrackDspState& state) {
    const bool low_active = parameters.low_shelf_gain_db != 0.0;
    const bool mid_active = parameters.mid_gain_db != 0.0;
    const bool high_active = parameters.high_shelf_gain_db != 0.0;
    if (!low_active && !mid_active && !high_active) return;
    for (auto& delays : state.equalizer_delays) {
        if (delays.size() != static_cast<std::size_t>(channel_count))
            delays.assign(static_cast<std::size_t>(channel_count), {});
    }
    const auto low = shelfCoefficients(parameters.low_shelf_frequency_hz,
        parameters.low_shelf_gain_db, sample_rate, false);
    const auto mid = peakingCoefficients(parameters.mid_frequency_hz,
        parameters.mid_gain_db, parameters.mid_q, sample_rate);
    const auto high = shelfCoefficients(parameters.high_shelf_frequency_hz,
        parameters.high_shelf_gain_db, sample_rate, true);
    const auto frame_count = samples.size() /
        static_cast<std::size_t>(channel_count);
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        for (int channel = 0; channel < channel_count; ++channel) {
            const auto offset = frame * static_cast<std::size_t>(channel_count) +
                static_cast<std::size_t>(channel);
            auto value = samples[offset];
            if (low_active)
                value = applyBiquad(value, low,
                    state.equalizer_delays[0][static_cast<std::size_t>(channel)]);
            if (mid_active)
                value = applyBiquad(value, mid,
                    state.equalizer_delays[1][static_cast<std::size_t>(channel)]);
            if (high_active)
                value = applyBiquad(value, high,
                    state.equalizer_delays[2][static_cast<std::size_t>(channel)]);
            samples[offset] = value;
        }
    }
}

void processCompressor(
    std::vector<float>& samples,
    int channel_count,
    int sample_rate,
    const timeline::AudioCompressorParameters& parameters,
    TrackDspState& state) noexcept {
    if (channel_count <= 0 || sample_rate <= 0) return;
    const auto attack = std::exp(-1.0 /
        (parameters.attack_ms * 0.001 * sample_rate));
    const auto release = std::exp(-1.0 /
        (parameters.release_ms * 0.001 * sample_rate));
    const auto frame_count = samples.size() /
        static_cast<std::size_t>(channel_count);
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        const auto offset = frame * static_cast<std::size_t>(channel_count);
        double peak = 0.0;
        for (int channel = 0; channel < channel_count; ++channel) {
            peak = std::max(peak, std::abs(static_cast<double>(
                samples[offset + static_cast<std::size_t>(channel)])));
        }
        const auto level_db = 20.0 * std::log10(std::max(peak, 1.0e-12));
        const auto over_db = std::max(0.0, level_db - parameters.threshold_db);
        const auto target_reduction = over_db * (1.0 - 1.0 / parameters.ratio);
        const auto coefficient = target_reduction > state.compressor_reduction_db
            ? attack : release;
        state.compressor_reduction_db = coefficient *
            state.compressor_reduction_db +
            (1.0 - coefficient) * target_reduction;
        const auto gain = std::pow(10.0,
            (parameters.makeup_gain_db - state.compressor_reduction_db) / 20.0);
        for (int channel = 0; channel < channel_count; ++channel) {
            const auto sample_index = offset + static_cast<std::size_t>(channel);
            samples[sample_index] = static_cast<float>(samples[sample_index] * gain);
        }
    }
}

void runTrackEffectChain(
    std::vector<float>& samples,
    int channel_count,
    int sample_rate,
    const std::vector<timeline::AudioEffectInstance>& effects,
    TrackDspState& state) {
    for (const auto& effect : effects) {
        if (!effect.enabled) continue;
        if (const auto* eq = std::get_if<timeline::AudioEqualizerParameters>(
                &effect.parameters)) {
            processEqualizer(samples, channel_count, sample_rate, *eq, state);
        } else if (const auto* compressor =
                       std::get_if<timeline::AudioCompressorParameters>(
                           &effect.parameters)) {
            if (compressor->ratio != 1.0 || compressor->makeup_gain_db != 0.0)
                processCompressor(samples, channel_count, sample_rate,
                                  *compressor, state);
        }
    }
}

void processEffectChain(
    std::vector<float>& samples,
    int channel_count,
    int sample_rate,
    const std::vector<timeline::AudioEffectInstance>& effects,
    TrackDspState& state) {
    const bool matching_format = state.sample_rate == sample_rate &&
        state.channel_count == channel_count;
    if (matching_format && state.configured_effects != effects) {
        auto previous_state = state;
        auto previous_samples = samples;
        runTrackEffectChain(previous_samples, channel_count, sample_rate,
            previous_state.configured_effects, previous_state);

        state = {};
        state.sample_rate = sample_rate;
        state.channel_count = channel_count;
        state.configured_effects = effects;
        runTrackEffectChain(samples, channel_count, sample_rate, effects, state);

        constexpr double transition_seconds = 0.01;
        const auto transition_frames = std::min(
            samples.size() / static_cast<std::size_t>(channel_count),
            std::max<std::size_t>(1, static_cast<std::size_t>(
                std::lround(sample_rate * transition_seconds))));
        for (std::size_t frame = 0; frame < transition_frames; ++frame) {
            const auto new_weight = static_cast<float>(frame + 1) /
                static_cast<float>(transition_frames);
            const auto old_weight = 1.0F - new_weight;
            const auto offset = frame * static_cast<std::size_t>(channel_count);
            for (int channel = 0; channel < channel_count; ++channel) {
                const auto index = offset + static_cast<std::size_t>(channel);
                samples[index] = previous_samples[index] * old_weight +
                    samples[index] * new_weight;
            }
        }
        return;
    }
    if (!matching_format) state = {};
    state.sample_rate = sample_rate;
    state.channel_count = channel_count;
    state.configured_effects = effects;
    runTrackEffectChain(samples, channel_count, sample_rate, effects, state);
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

struct TimelineAudioMixState::Impl {
    std::unordered_map<timeline::TrackId, TrackDspState> tracks;
    std::unordered_map<timeline::ClipId, TrackDspState> clips;
};

TimelineAudioMixState::TimelineAudioMixState()
    : impl_(std::make_unique<Impl>()) {}

TimelineAudioMixState::~TimelineAudioMixState() = default;
TimelineAudioMixState::TimelineAudioMixState(TimelineAudioMixState&&) noexcept = default;
TimelineAudioMixState& TimelineAudioMixState::operator=(
    TimelineAudioMixState&&) noexcept = default;

void TimelineAudioMixState::reset() noexcept {
    if (impl_ != nullptr) {
        impl_->tracks.clear();
        impl_->clips.clear();
    }
}

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
            clip.duration_frames <= 0 ||
            clip.timeline_start_frame < 0 || clip.source_start_frame < 0 ||
            clip.timeline_start_frame >
                std::numeric_limits<std::int64_t>::max() - clip.duration_frames ||
            (clip.kind == timeline::ClipKind::Video &&
             (!std::isfinite(clip.source_frame_rate) || clip.source_frame_rate <= 0.0)) ||
            (clip.kind == timeline::ClipKind::Audio &&
             (clip.source_start_time_us < 0 || clip.source_duration_time_us <= 0))) {
            continue;
        }

        const bool clip_effect_processing =
            hasActiveAudioEffectProcessing(clip.clip_audio_effects);
        const bool track_effect_processing =
            hasActiveAudioEffectProcessing(clip.audio_effects);
        if ((clip.track_muted || clip.clip_muted) &&
            !clip_effect_processing && !track_effect_processing) {
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
            (gain <= 0.0 && !has_audible_pre_fader_send &&
             !clip_effect_processing) ||
            (clip.clip_gain <= 0.0 && !clip_effect_processing)) {
            continue;
        }

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
    TimelineAudioMixConfiguration configuration,
    TimelineAudioMixState* persistent_state)
    : sample_count_(sample_count),
      channel_count_(channel_count),
      configuration_(std::move(configuration)),
      any_solo_(configuration_.apply_solo &&
          (!configuration_.solo_track_ids.empty() ||
           !configuration_.solo_bus_ids.empty())),
      master_samples_(sample_count * static_cast<std::size_t>(
          std::max(channel_count, 0)), 0.0F) {
    if (persistent_state == nullptr) {
        owned_state_ = std::make_unique<TimelineAudioMixState>();
        persistent_state_ = owned_state_.get();
    } else {
        persistent_state_ = persistent_state;
    }
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
    if (channel_count_ <= 0 || sample_count_ == 0 ||
        span.destination_start_sample < 0 ||
        static_cast<std::uint64_t>(span.destination_start_sample) >= sample_count_) {
        return;
    }
    if (chunk.sample_rate <= 0 || chunk.channel_count != channel_count_) return;
    if (sample_rate_ == 0) sample_rate_ = chunk.sample_rate;
    if (sample_rate_ != chunk.sample_rate) return;

    const auto chunk_sample_count = static_cast<std::int64_t>(chunk.sampleCount());
    if (chunk_sample_count <= 0 || chunk.first_sample_index < 0 ||
        chunk.first_sample_index > std::numeric_limits<std::int64_t>::max() -
            chunk_sample_count || span.source_start_sample < 0 ||
        span.sample_count <= 0 || span.source_start_sample >
            std::numeric_limits<std::int64_t>::max() - span.sample_count) {
        return;
    }
    const auto source_begin = std::max(
        span.source_start_sample, chunk.first_sample_index);
    const auto source_end = std::min(
        span.source_start_sample + span.sample_count,
        chunk.first_sample_index + chunk_sample_count);
    if (source_begin >= source_end) return;

    std::vector<float> processed_clip_samples;
    bool has_processed_clip_samples = false;
    auto clip_dsp = persistent_state_->impl_->clips.find(clip.clip_id);
    const bool has_clip_dsp = clip_dsp != persistent_state_->impl_->clips.end();
    if (!clip.clip_audio_effects.empty() || has_clip_dsp) {
        auto& state = persistent_state_->impl_->clips[clip.clip_id];
        if (state.next_source_sample_index >= 0 &&
            state.next_source_sample_index != source_begin) {
            state = {};
        }
        const bool format_changed = state.sample_rate != chunk.sample_rate ||
            state.channel_count != channel_count_;
        const bool chain_changed = state.configured_effects !=
            clip.clip_audio_effects;
        const bool process_effects = hasActiveAudioEffectProcessing(
            clip.clip_audio_effects);
        if (process_effects || chain_changed || format_changed) {
            const auto sample_count = static_cast<std::size_t>(source_end - source_begin);
            const auto sample_offset = static_cast<std::size_t>(
                source_begin - chunk.first_sample_index) *
                static_cast<std::size_t>(channel_count_);
            processed_clip_samples.resize(sample_count *
                static_cast<std::size_t>(channel_count_));
            for (std::size_t index = 0; index < processed_clip_samples.size(); ++index) {
                processed_clip_samples[index] = static_cast<float>(
                    chunk.samples[sample_offset + index]) / 32768.0F;
            }
            processEffectChain(processed_clip_samples, channel_count_,
                chunk.sample_rate, clip.clip_audio_effects, state);
            has_processed_clip_samples = true;
        } else {
            state.sample_rate = chunk.sample_rate;
            state.channel_count = channel_count_;
            state.configured_effects = clip.clip_audio_effects;
        }
        state.next_source_sample_index = source_end;
    }

    std::vector<float> pre_fader(
        sample_count_ * static_cast<std::size_t>(channel_count_), 0.0F);
    auto pre_span = span;
    pre_span.gain = span.clip_gain;
    if (!clip.clip_muted) {
        if (has_processed_clip_samples) {
            accumulateTimelineAudioFloatSamples(
                pre_span, processed_clip_samples, source_begin,
                pre_fader, channel_count_);
        } else {
            accumulateTimelineAudioChunk(pre_span, chunk, pre_fader, channel_count_);
        }
    }

    std::vector<float> post_fader = pre_fader;
    auto track_buffer = track_samples_.try_emplace(
        clip.track_id, TrackBuffers{}).first;
    if (track_buffer->second.pre_fader.empty()) {
        track_buffer->second.pre_fader.assign(pre_fader.size(), 0.0F);
        track_buffer->second.post_fader.assign(post_fader.size(), 0.0F);
        track_buffer->second.gain = clip.track_gain;
        track_buffer->second.pan = clip.track_pan;
        track_buffer->second.muted = clip.track_muted;
        track_buffer->second.source_channel_count = clip.source_channel_count;
        track_buffer->second.output_bus_id = clip.output_bus_id;
        track_buffer->second.sends = clip.audio_sends;
        track_buffer->second.effects = clip.audio_effects;
    } else if (track_buffer->second.source_channel_count !=
               clip.source_channel_count) {
        track_buffer->second.source_channel_count = std::max(
            2, std::max(track_buffer->second.source_channel_count,
                        clip.source_channel_count));
    }
    const auto frames = std::min(sample_count_, pre_fader.size() /
        static_cast<std::size_t>(channel_count_));
    for (std::size_t frame_index = 0; frame_index < frames; ++frame_index) {
        const auto offset = frame_index * static_cast<std::size_t>(channel_count_);
        applyTrackGainAndPan(
            post_fader.data() + offset, channel_count_, clip.source_channel_count,
            clip.track_gain, clip.track_pan);
        for (int channel = 0; channel < channel_count_; ++channel) {
            const auto sample_offset = offset + static_cast<std::size_t>(channel);
            track_buffer->second.pre_fader[sample_offset] +=
                pre_fader[sample_offset];
            track_buffer->second.post_fader[sample_offset] +=
                post_fader[sample_offset];
        }
    }
}

TimelineAudioMixResult TimelineAudioMixAccumulator::finish() {
    TimelineAudioMixResult result;
    if (channel_count_ <= 0) {
        result.samples = std::move(master_samples_);
        return result;
    }

    result.track_peaks.reserve(track_samples_.size());
    for (auto& [track_id, track] : track_samples_) {
        const bool track_solo = any_solo_ && trackSoloed(track_id);
        const bool include_main_route = !any_solo_ || track_solo ||
            (track.output_bus_id.has_value() &&
             busSoloed(*track.output_bus_id));
        const bool include_any_send = std::any_of(
            track.sends.begin(), track.sends.end(),
            [this, track_solo](const timeline::AudioSend& send) {
                return send.gain > 0.0 && (!any_solo_ || track_solo ||
                    busSoloed(send.destination_bus_id));
            });
        const bool has_effect_processing =
            hasActiveAudioEffectProcessing(track.effects);
        auto& dsp_state = persistent_state_->impl_->tracks[track_id];
        const bool effect_chain_changed = dsp_state.configured_effects != track.effects;
        if (has_effect_processing || effect_chain_changed) {
            processEffectChain(track.pre_fader, channel_count_, sample_rate_,
                track.effects, dsp_state);
            track.post_fader = track.pre_fader;
            for (std::size_t offset = 0; offset +
                     static_cast<std::size_t>(channel_count_) <=
                     track.post_fader.size();
                 offset += static_cast<std::size_t>(channel_count_)) {
                applyTrackGainAndPan(track.post_fader.data() + offset,
                    channel_count_, track.source_channel_count,
                    track.gain, track.pan);
            }
        } else {
            // Keep the pre-effects mixer path bit-identical for existing projects.
            if (dsp_state.sample_rate != sample_rate_ ||
                dsp_state.channel_count != channel_count_) {
                dsp_state = {};
                dsp_state.sample_rate = sample_rate_;
                dsp_state.channel_count = channel_count_;
                dsp_state.configured_effects = track.effects;
            }
        }

        if (track.muted || (!include_main_route && !include_any_send)) continue;

        result.track_peaks.emplace_back(
            track_id, timelineAudioPeak(track.post_fader));
        const auto& post_fader = track.post_fader;
        for (std::size_t sample_index = 0;
             sample_index < post_fader.size(); ++sample_index) {
            if (include_main_route && !track.output_bus_id.has_value()) {
                master_samples_[sample_index] += post_fader[sample_index];
            } else if (include_main_route && track.output_bus_id.has_value()) {
                busBuffer(*track.output_bus_id)[sample_index] +=
                    post_fader[sample_index];
            }
        }
        for (const auto& send : track.sends) {
            const bool send_solo = !any_solo_ || track_solo ||
                busSoloed(send.destination_bus_id);
            if (!send_solo || send.gain <= 0.0) continue;
            auto& destination = busBuffer(send.destination_bus_id);
            const auto& source = send.tap == timeline::AudioSendTap::PreFader
                ? track.pre_fader : track.post_fader;
            for (std::size_t sample_index = 0; sample_index < source.size();
                 ++sample_index) {
                destination[sample_index] += static_cast<float>(
                    source[sample_index] * send.gain);
            }
        }
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

namespace {

template <typename Sample, typename Normalize>
void accumulateTimelineAudioSamples(
    const TimelineAudioMixSpan& span,
    std::span<const Sample> source_samples,
    std::int64_t first_source_sample,
    std::span<float> mixed,
    int output_channel_count,
    Normalize normalize) {
    if (span.sample_count <= 0 || span.source_start_sample < 0 ||
        span.destination_start_sample < 0 || output_channel_count <= 0 ||
        first_source_sample < 0 ||
        source_samples.size() % static_cast<std::size_t>(output_channel_count) != 0) {
        return;
    }
    const auto destination_sample_count =
        mixed.size() / static_cast<std::size_t>(output_channel_count);
    if (static_cast<std::uint64_t>(span.destination_start_sample) >=
        destination_sample_count) {
        return;
    }
    const auto source_sample_count = static_cast<std::int64_t>(
        source_samples.size() / static_cast<std::size_t>(output_channel_count));
    if (source_sample_count <= 0 ||
        first_source_sample > std::numeric_limits<std::int64_t>::max() -
            source_sample_count ||
        span.source_start_sample > std::numeric_limits<std::int64_t>::max() -
            span.sample_count) {
        return;
    }
    const auto source_begin = std::max(span.source_start_sample, first_source_sample);
    const auto source_end = std::min(
        span.source_start_sample + span.sample_count,
        first_source_sample + source_sample_count);
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
            source_begin - first_source_sample + index) *
            static_cast<std::size_t>(output_channel_count);
        const auto destination_offset = static_cast<std::size_t>(
            destination_begin + index) * static_cast<std::size_t>(output_channel_count);
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
                static_cast<double>(timeline_sample - fade.transition_start_sample) /
                    static_cast<double>(sample_duration),
                0.0, 1.0);
            const auto angle = fraction * std::numbers::pi / 2.0;
            transition_gain *= fade.incoming ? std::sin(angle) : std::cos(angle);
        }
        const auto gain = static_cast<float>(
            span.gain * envelope_gain * transition_gain);
        for (int channel = 0; channel < output_channel_count; ++channel) {
            const auto source_index = source_offset +
                static_cast<std::size_t>(channel);
            const auto destination_index = destination_offset +
                static_cast<std::size_t>(channel);
            mixed[destination_index] += normalize(source_samples[source_index]) * gain;
        }
    }
}

}  // namespace

void accumulateTimelineAudioChunk(
    const TimelineAudioMixSpan& span,
    const AudioPcmChunk& chunk,
    std::span<float> mixed,
    int output_channel_count) {
    if (chunk.channel_count != output_channel_count || chunk.first_sample_index < 0) {
        return;
    }
    accumulateTimelineAudioSamples(
        span, std::span<const std::int16_t>(chunk.samples),
        chunk.first_sample_index, mixed, output_channel_count,
        [](std::int16_t sample) {
            return static_cast<float>(sample) / 32768.0F;
        });
}

void accumulateTimelineAudioFloatSamples(
    const TimelineAudioMixSpan& span,
    std::span<const float> samples,
    std::int64_t first_sample_index,
    std::span<float> mixed,
    int output_channel_count) {
    accumulateTimelineAudioSamples(
        span, samples, first_sample_index, mixed, output_channel_count,
        [](float sample) { return sample; });
}
} // namespace media
