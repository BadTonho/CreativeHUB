#include "media/timeline_audio_mix.h"
#include "media/audio_ducking.h"
#include "media/audio_filter_session.h"

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

media::AudioPcmChunk constantChunk(
    std::int64_t first_sample,
    std::int16_t value,
    int sample_count = 32) {
    media::AudioPcmChunk chunk;
    chunk.sample_rate = 48000;
    chunk.channel_count = 2;
    chunk.first_sample_index = first_sample;
    chunk.samples.resize(static_cast<std::size_t>(sample_count) * 2U, value);
    return chunk;
}

void testMixesEveryActiveTrack() {
    const std::vector<media::TimelineAudioMixClip> clips{
        {0, 0, 0, timeline::ClipKind::Video, true,
         0, 100, 0, 30.0, 0.5, 1.0, false, false},
        {1, 1, 0, timeline::ClipKind::Video, true,
         50, 100, 0, 24.0, 1.0, 1.0, false, false},
    };
    const auto spans = media::planTimelineAudioMix(
        clips, {}, 30.0, 30, 50, 10);
    require(spans.size() == 2,
            "Audio from both active tracks was not scheduled, including the obscured clip.");
    require(spans[0].source_index == 0 && spans[0].source_start_sample == 50 &&
                spans[1].source_index == 1 && spans[1].source_start_sample == 0,
            "Audio source positions did not follow each clip's timeline and source rates.");

    std::vector<float> mixed(20, 0.0F);
    media::accumulateTimelineAudioChunk(
        spans[0], constantChunk(50, 30000), mixed, 2);
    media::accumulateTimelineAudioChunk(
        spans[1], constantChunk(0, 30000), mixed, 2);
    for (const auto sample : mixed) {
        require(std::abs(sample - (45000.0F / 32768.0F)) < 1.0e-6F && sample > 1.0F,
                "Overlapping audio sources were not summed before output clipping.");
    }
    std::vector<float> first_track(20, 0.0F);
    std::vector<float> second_track(20, 0.0F);
    media::accumulateTimelineAudioChunk(
        spans[0], constantChunk(50, 30000), first_track, 2);
    media::accumulateTimelineAudioChunk(
        spans[1], constantChunk(0, 30000), second_track, 2);
    require(std::abs(media::timelineAudioPeak(first_track) -
                     15000.0F / 32768.0F) < 1.0e-6F &&
            std::abs(media::timelineAudioPeak(second_track) -
                     30000.0F / 32768.0F) < 1.0e-6F &&
            media::timelineAudioPeak(mixed) > 1.0F,
            "Track and output meters must use post-gain, pre-clipping mix peaks.");
    require(media::timelineAudioPeak(std::vector<float>(20, 0.0F)) == 0.0F &&
            media::timelineAudioPeak(std::vector<float>{
                std::numeric_limits<float>::quiet_NaN(), -0.75F}) == 0.75F,
            "Meter peaks must remain stable for silence and invalid samples.");
}

void testMuteGainAndGaps() {
    auto clip = media::TimelineAudioMixClip{
        0, 0, 0, timeline::ClipKind::Video, true,
        10, 10, 0, 30.0, 0.5, 0.5, false, false};
    const auto audible = media::planTimelineAudioMix(
        std::span<const media::TimelineAudioMixClip>(&clip, 1), {},
        30.0, 30, 10, 4);
    require(audible.size() == 1 && audible[0].gain == 0.25,
            "Track and clip gains were not combined.");

    clip.track_muted = true;
    require(media::planTimelineAudioMix(
                std::span<const media::TimelineAudioMixClip>(&clip, 1), {},
                30.0, 30, 10, 4).empty(),
            "A muted track still contributed audio.");
    clip.track_muted = false;
    clip.clip_muted = true;
    require(media::planTimelineAudioMix(
                std::span<const media::TimelineAudioMixClip>(&clip, 1), {},
                30.0, 30, 10, 4).empty(),
            "A muted clip still contributed audio.");
    clip.clip_muted = false;

    require(media::planTimelineAudioMix(
                std::span<const media::TimelineAudioMixClip>(&clip, 1), {},
                30.0, 30, 0, 4).empty(),
            "A Timeline gap produced an audio span.");
}

void testTrimmedSourcePositionUsesTimelineElapsedTime() {
    const media::TimelineAudioMixClip trimmed{
        0, 0, 0, timeline::ClipKind::Video, true,
        10, 40, 30, 30.0, 1.0, 1.0, false, false};
    const auto spans = media::planTimelineAudioMix(
        std::span<const media::TimelineAudioMixClip>(&trimmed, 1), {},
        24.0, 48000, 68000, 960);
    require(spans.size() == 1 && spans[0].destination_start_sample == 0 &&
                spans[0].source_start_sample == 96000,
            "A trimmed 30 fps audio source did not map one Timeline second from a 24 fps Timeline.");
}

void testCrossDissolveAudioStartsAtTheOriginalCut() {
    const media::TimelineAudioMixClip incoming{
        0, 2, 1, timeline::ClipKind::Video, true,
        72, 40, 0, 24.0, 1.0, 1.0, false, false};
    const media::TimelineAudioMixTransition transition{
        2, 1, 80, timeline::TransitionKind::CrossDissolve};

    const auto before_cut = media::planTimelineAudioMix(
        std::span<const media::TimelineAudioMixClip>(&incoming, 1),
        std::span<const media::TimelineAudioMixTransition>(&transition, 1),
        24.0, 48000, 150000, 10000);
    require(before_cut.empty(),
            "Incoming Cross Dissolve audio started before the original cut.");

    const auto after_cut = media::planTimelineAudioMix(
        std::span<const media::TimelineAudioMixClip>(&incoming, 1),
        std::span<const media::TimelineAudioMixTransition>(&transition, 1),
        24.0, 48000, 160000, 10000);
    require(after_cut.size() == 1 &&
                after_cut[0].source_start_sample == 16000 &&
                after_cut[0].destination_start_sample == 0,
            "Incoming Cross Dissolve audio did not start at source frame D at the original cut.");
}

void testAudioOnlyClipUsesMicrosecondSourceTiming() {
    const media::TimelineAudioMixClip audio{
        7, 3, 0, timeline::ClipKind::Audio, true,
        30, 62, 0, 0.0, 0.5, 0.5, false, false,
        500000, 1000000};
    const media::TimelineAudioMixTransition invalid_audio_transition{
        3, 0, 60, timeline::TransitionKind::CrossDissolve};
    const auto spans = media::planTimelineAudioMix(
        std::span<const media::TimelineAudioMixClip>(&audio, 1),
        std::span<const media::TimelineAudioMixTransition>(
            &invalid_audio_transition, 1),
        30.0, 48000, 48000, 48000);
    require(spans.size() == 1 && spans[0].source_index == 7 &&
                spans[0].source_start_sample == 24000 &&
                spans[0].destination_start_sample == 0 &&
                spans[0].sample_count == 48000 && spans[0].gain == 0.25,
            "An audio-only clip did not use its microsecond source range and clip gains.");

    const auto after_source_end = media::planTimelineAudioMix(
        std::span<const media::TimelineAudioMixClip>(&audio, 1), {},
        30.0, 48000, 96000, 512);
    require(after_source_end.empty(),
            "Frame rounding scheduled audio samples beyond the exact source duration.");

    auto muted = audio;
    muted.track_muted = true;
    require(media::planTimelineAudioMix(
                std::span<const media::TimelineAudioMixClip>(&muted, 1), {},
                30.0, 48000, 48000, 512).empty(),
            "A muted audio-only track still contributed samples.");
}

void testNonVideoAndMissingAudioAreExcluded() {
    const std::vector<media::TimelineAudioMixClip> clips{
        {0, 0, 0, timeline::ClipKind::Text, true,
         0, 10, 0, 30.0, 1.0, 1.0, false, false},
        {1, 1, 0, timeline::ClipKind::Video, false,
         0, 10, 0, 30.0, 1.0, 1.0, false, false},
    };
    require(media::planTimelineAudioMix(clips, {}, 30.0, 48000, 0, 1024).empty(),
            "Text or video-only media created an audio source.");
}

void testExternalizedVideoAudioIsMixedOnlyFromItsCompanion() {
    auto video = media::TimelineAudioMixClip{
        0, 0, 0, timeline::ClipKind::Video, true,
        0, 60, 0, 30.0, 1.0, 1.0, false, false};
    video.audio_extracted = true;
    const media::TimelineAudioMixClip companion{
        1, 1, 0, timeline::ClipKind::Audio, true,
        0, 60, 0, 0.0, 0.5, 0.75, false, false,
        125000, 1000000};
    const std::vector<media::TimelineAudioMixClip> clips{video, companion};
    const auto spans = media::planTimelineAudioMix(
        clips, {}, 30.0, 48000, 6000, 4800);
    require(spans.size() == 1 && spans.front().source_index == 1 &&
                spans.front().source_start_sample == 12000 &&
                spans.front().gain == 0.375,
            "Externalized video audio was duplicated or the Audio companion was not scheduled.");

    auto muted_companion = companion;
    muted_companion.clip_muted = true;
    const std::vector<media::TimelineAudioMixClip> muted_clips{
        video, muted_companion};
    require(media::planTimelineAudioMix(
                muted_clips, {}, 30.0, 48000, 6000, 4800).empty(),
            "Muting an externalized companion fell back to the embedded video stream.");
}

void testAudioEnvelopeIsAppliedPerSample() {
    auto clip = media::TimelineAudioMixClip{
        0, 0, 0, timeline::ClipKind::Audio, true,
        0, 30, 0, 0.0, 1.0, 1.0, false, false,
        0, 1'000'000};
    clip.audio_gain_keyframes = {{0, 0.0}, {30, 2.0}};
    const auto spans = media::planTimelineAudioMix(
        std::span<const media::TimelineAudioMixClip>(&clip, 1), {},
        30.0, 30, 0, 30);
    require(spans.size() == 1,
            "An automated Audio clip was not scheduled for mixing.");
    std::vector<float> mixed(60, 0.0F);
    media::accumulateTimelineAudioChunk(
        spans.front(), constantChunk(0, 16384, 30), mixed, 2);
    require(std::abs(mixed[0]) < 1.0e-6F &&
                std::abs(mixed[30] - (16384.0F / 32768.0F)) < 1.0e-5F &&
                mixed[58] > mixed[30],
            "The mixer did not interpolate audio gain for individual output samples.");
}

void testAudioCrossfadeUsesEqualPowerAndExistingGainControls() {
    media::TimelineAudioMixClip outgoing{
        0, 3, 0, timeline::ClipKind::Audio, true,
        0, 60, 0, 0.0, 0.5, 0.5, false, false,
        0, 2'000'000};
    media::TimelineAudioMixClip incoming{
        1, 3, 1, timeline::ClipKind::Audio, true,
        45, 60, 0, 0.0, 0.5, 0.5, false, false,
        0, 2'000'000};
    outgoing.audio_gain_keyframes = {{0, 2.0}, {60, 2.0}};
    incoming.audio_gain_keyframes = {{0, 2.0}, {60, 2.0}};
    const std::vector<media::TimelineAudioMixClip> clips{outgoing, incoming};
    const media::TimelineAudioMixTransition crossfade{
        3, 1, 60, timeline::TransitionKind::AudioCrossfade, 0, 15};
    const auto spans = media::planTimelineAudioMix(
        clips, std::span<const media::TimelineAudioMixTransition>(&crossfade, 1),
        30.0, 60, 90, 30);
    require(spans.size() == 2 && spans[0].transition_fades.size() == 1 &&
                spans[1].transition_fades.size() == 1 &&
                !spans[0].transition_fades.front().incoming &&
                spans[1].transition_fades.front().incoming,
            "The Audio Crossfade did not attach outgoing and incoming ramps to its clips.");

    const auto outgoing_chunk = constantChunk(
        spans[0].source_start_sample, 16384, 32);
    const auto incoming_chunk = constantChunk(
        spans[1].source_start_sample, 16384, 32);
    std::vector<float> outgoing_mix(60, 0.0F);
    std::vector<float> incoming_mix(60, 0.0F);
    std::vector<float> correlated_mix(60, 0.0F);
    media::accumulateTimelineAudioChunk(
        spans[0], outgoing_chunk, outgoing_mix, 2);
    media::accumulateTimelineAudioChunk(
        spans[1], incoming_chunk, incoming_mix, 2);
    media::accumulateTimelineAudioChunk(
        spans[0], outgoing_chunk, correlated_mix, 2);
    media::accumulateTimelineAudioChunk(
        spans[1], incoming_chunk, correlated_mix, 2);
    const auto middle = std::size_t{15} * 2U;
    const auto expected_component = std::sqrt(0.5F) * 0.25F;
    require(std::abs(outgoing_mix[middle] - expected_component) < 1.0e-5F &&
                std::abs(incoming_mix[middle] - expected_component) < 1.0e-5F &&
                correlated_mix[middle] > outgoing_mix[middle] + incoming_mix[middle] - 1.0e-6F,
            "The per-sample equal-power curve did not multiply envelope and clip/track gain.");
    require(std::abs(outgoing_mix[0] - 0.25F) < 1.0e-5F &&
                std::abs(incoming_mix[0]) < 1.0e-6F &&
                incoming_mix[(29U * 2U)] > incoming_mix[middle],
            "The equal-power crossfade did not start/end on the correct endpoints.");

    auto muted_outgoing = outgoing;
    muted_outgoing.clip_muted = true;
    const std::vector<media::TimelineAudioMixClip> muted_clips{
        muted_outgoing, incoming};
    const auto muted_spans = media::planTimelineAudioMix(
        muted_clips, std::span<const media::TimelineAudioMixTransition>(&crossfade, 1),
        30.0, 60, 90, 30);
    require(muted_spans.size() == 1 && muted_spans.front().source_index == 1,
            "Muting one crossfade endpoint incorrectly muted the other endpoint.");
}

media::TimelineAudioMixResult renderMix(
    const std::vector<media::TimelineAudioMixClip>& clips,
    media::TimelineAudioMixConfiguration configuration,
    std::int16_t sample = 8192) {
    const auto spans = media::planTimelineAudioMix(
        clips, {}, 30.0, 30, 0, 4);
    media::TimelineAudioMixAccumulator accumulator(4, 2,
        std::move(configuration));
    for (const auto& span : spans) {
        accumulator.accumulate(span, clips.at(span.source_index),
            constantChunk(span.source_start_sample, sample, 4));
    }
    return accumulator.finish();
}

void testTrackRoutesAndPrePostSends() {
    auto clip = media::TimelineAudioMixClip{
        0, 0, 0, timeline::ClipKind::Video, true,
        0, 4, 0, 30.0, 0.5, 1.0, false, false};
    clip.track_id = 11;
    clip.output_bus_id = 101;
    clip.audio_sends = {{202, 1.0, timeline::AudioSendTap::PostFader}};
    media::TimelineAudioMixConfiguration configuration;
    configuration.buses = {{101, "Submix", 1.0, false, 0.0},
                           {202, "Send", 1.0, false, 0.0}};
    auto post = renderMix({clip}, configuration);
    const auto busPeak = [](const auto& result, timeline::AudioBusId id) {
        const auto found = std::find_if(result.bus_peaks.begin(), result.bus_peaks.end(),
            [id](const auto& peak) { return peak.first == id; });
        return found == result.bus_peaks.end() ? -1.0F : found->second;
    };
    require(std::abs(post.samples.front() - 0.25F) < 1.0e-5F &&
                post.bus_peaks.size() == 2 &&
                std::abs(busPeak(post, 101) - 0.125F) < 1.0e-5F &&
                std::abs(busPeak(post, 202) - 0.125F) < 1.0e-5F,
            "The main bus route or post-fader send did not use the track fader.");

    auto preview_configuration = configuration;
    preview_configuration.apply_solo = true;
    auto export_configuration = configuration;
    export_configuration.apply_solo = false;
    const auto preview_mix = renderMix({clip}, preview_configuration);
    const auto export_mix = renderMix({clip}, export_configuration);
    require(preview_mix.samples == export_mix.samples,
            "Preview and export did not produce the same saved mix without solo state.");

    clip.audio_sends.front().tap = timeline::AudioSendTap::PreFader;
    const auto pre = renderMix({clip}, configuration);
    require(std::abs(pre.samples.front() - 0.375F) < 1.0e-5F &&
                std::abs(busPeak(pre, 202) - 0.25F) < 1.0e-5F,
            "A pre-fader send did not bypass the track fader while reaching Master.");

    clip.track_gain = 0.0;
    const auto zero_fader_pre = renderMix({clip}, configuration);
    require(std::abs(zero_fader_pre.samples.front() - 0.25F) < 1.0e-5F,
            "A pre-fader send was lost when the track fader was at zero.");
}

void testPanSoloMuteAndMasterMix() {
    auto mono = media::TimelineAudioMixClip{
        0, 0, 0, timeline::ClipKind::Video, true,
        0, 4, 0, 30.0, 1.0, 1.0, false, false};
    mono.track_id = 1;
    mono.source_channel_count = 1;
    mono.track_pan = -1.0;
    auto left = renderMix({mono}, {});
    require(std::abs(left.samples[0] - 0.25F) < 1.0e-5F &&
                std::abs(left.samples[1]) < 1.0e-6F,
            "Mono equal-power pan did not place a source on the selected front channel.");
    mono.track_pan = 0.0;
    const auto centered = renderMix({mono}, {});
    const auto equal_power_center = 0.25F * std::sqrt(0.5F);
    require(std::abs(centered.samples[0] - equal_power_center) < 1.0e-5F &&
                std::abs(centered.samples[1] - equal_power_center) < 1.0e-5F,
            "Mono center pan did not use the equal-power law.");

    auto second = mono;
    second.source_index = 1;
    second.track_id = 2;
    second.source_channel_count = 1;
    second.track_pan = 0.0;
    std::vector<media::TimelineAudioMixClip> tracks{mono, second};
    media::TimelineAudioMixConfiguration configuration;
    configuration.solo_track_ids = {1};
    const auto solo_one = renderMix(tracks, configuration);
    configuration.apply_solo = false;
    const auto export_ignores_solo = renderMix(tracks, configuration);
    require(std::abs(solo_one.samples[0] - equal_power_center) < 1.0e-5F &&
                std::abs(export_ignores_solo.samples[0] -
                         2.0F * equal_power_center) < 1.0e-5F,
            "Solo must affect Preview while the saved export mix includes all tracks.");

    configuration.apply_solo = true;
    configuration.solo_track_ids = {1, 2};
    tracks[0].track_muted = true;
    const auto mute_wins = renderMix(tracks, configuration);
    require(std::abs(mute_wins.samples[0] - equal_power_center) < 1.0e-5F,
            "A muted track remained audible while multiple tracks were soloed.");

    tracks[0].track_muted = false;
    tracks[0].track_pan = 0.0;
    configuration.solo_track_ids.clear();
    configuration.buses = {{303, "Muted", 1.0, true, 0.0}};
    tracks[0].output_bus_id = 303;
    tracks[1].track_muted = true;
    configuration.solo_bus_ids = {303};
    configuration.buses.front().audio_muted = false;
    const auto bus_solo = renderMix(tracks, configuration);
    require(std::abs(bus_solo.samples[0] - equal_power_center) < 1.0e-5F,
            "Soloing a bus did not audition its routed track input.");
    configuration.buses.front().audio_pan = -1.0;
    const auto bus_pan = renderMix(tracks, configuration);
    require(std::abs(bus_pan.samples[0] - equal_power_center) < 1.0e-5F &&
                std::abs(bus_pan.samples[1]) < 1.0e-6F,
            "Bus pan did not balance the summed signal.");
    configuration.buses.front().audio_pan = 0.0;
    configuration.buses.front().audio_muted = true;
    const auto muted_bus = renderMix(tracks, configuration);
    require(std::abs(muted_bus.master_peak) < 1.0e-6F,
            "A muted bus still contributed to the Master output.");

    configuration.buses.front().audio_muted = false;
    configuration.master.audio_gain = 0.5;
    tracks[1].track_muted = true;
    tracks[0].track_pan = -1.0;
    const auto master_gain = renderMix(tracks, configuration);
    require(std::abs(master_gain.samples[0] - 0.125F) < 1.0e-5F,
            "Master gain was not applied after bus summing.");
    configuration.master.audio_muted = true;
    const auto master_mute = renderMix(tracks, configuration);
    require(std::abs(master_mute.master_peak) < 1.0e-6F,
            "Master mute did not silence the final output.");
}

void testStereoBalanceAndMultichannelPassThrough() {
    auto clip = media::TimelineAudioMixClip{
        0, 0, 0, timeline::ClipKind::Video, true,
        0, 1, 0, 30.0, 1.0, 1.0, false, false};
    clip.track_id = 7;
    clip.track_pan = 0.5;
    const auto spans = media::planTimelineAudioMix(
        std::span<const media::TimelineAudioMixClip>(&clip, 1), {},
        30.0, 30, 0, 1);
    media::AudioPcmChunk stereo;
    stereo.sample_rate = 30;
    stereo.channel_count = 2;
    stereo.samples = {8192, 4096};
    media::TimelineAudioMixAccumulator stereo_mix(1, 2, {});
    stereo_mix.accumulate(spans.front(), clip, stereo);
    const auto stereo_result = stereo_mix.finish();
    require(std::abs(stereo_result.samples[0] - 0.125F) < 1.0e-5F &&
                std::abs(stereo_result.samples[1] - 0.125F) < 1.0e-5F,
            "Stereo pan did not preserve its source width while applying balance.");

    clip.track_pan = 1.0;
    media::AudioPcmChunk surround;
    surround.sample_rate = 30;
    surround.channel_count = 4;
    surround.samples = {8192, 4096, 2048, 1024};
    media::TimelineAudioMixAccumulator surround_mix(1, 4, {});
    surround_mix.accumulate(spans.front(), clip, surround);
    const auto surround_result = surround_mix.finish();
    require(std::abs(surround_result.samples[0]) < 1.0e-6F &&
                std::abs(surround_result.samples[1] - 0.125F) < 1.0e-5F &&
                std::abs(surround_result.samples[2] - 0.0625F) < 1.0e-5F &&
                std::abs(surround_result.samples[3] - 0.03125F) < 1.0e-5F,
            "Multichannel pan changed channels outside the front pair.");
}

std::vector<float> renderEffectTone(
    double frequency_hz,
    std::vector<timeline::AudioEffectInstance> effects) {
    constexpr int sample_rate = 48000;
    constexpr int frame_count = 9600;
    media::TimelineAudioMixClip clip{
        0, 0, 0, timeline::ClipKind::Audio, true,
        0, 6, 0, 30.0, 1.0, 1.0, false, false};
    clip.source_duration_time_us = 200000;
    clip.track_id = 90;
    clip.source_channel_count = 2;
    clip.audio_effects = std::move(effects);
    const auto spans = media::planTimelineAudioMix(
        std::span<const media::TimelineAudioMixClip>(&clip, 1), {},
        30.0, sample_rate, 0, frame_count);
    require(spans.size() == 1,
            "The test tone did not produce one audio mix span.");
    media::AudioPcmChunk chunk;
    chunk.sample_rate = sample_rate;
    chunk.channel_count = 2;
    chunk.first_sample_index = spans.front().source_start_sample;
    chunk.samples.resize(static_cast<std::size_t>(frame_count) * 2U);
    for (int frame = 0; frame < frame_count; ++frame) {
        const auto value = static_cast<std::int16_t>(std::lround(
            8000.0 * std::sin(2.0 * std::numbers::pi * frequency_hz *
                frame / sample_rate)));
        chunk.samples[static_cast<std::size_t>(frame) * 2U] = value;
        chunk.samples[static_cast<std::size_t>(frame) * 2U + 1U] = value;
    }
    media::TimelineAudioMixAccumulator accumulator(frame_count, 2, {});
    accumulator.accumulate(spans.front(), clip, chunk);
    return accumulator.finish().samples;
}

std::vector<float> renderEffectTone(
    double frequency_hz,
    timeline::AudioEffectInstance effect) {
    return renderEffectTone(frequency_hz,
        std::vector<timeline::AudioEffectInstance>{std::move(effect)});
}

std::vector<float> renderClipEffectTone(
    double frequency_hz,
    std::vector<timeline::AudioEffectInstance> effects,
    double clip_gain = 1.0,
    double amplitude = 8000.0) {
    constexpr int sample_rate = 48000;
    constexpr int frame_count = 9600;
    media::TimelineAudioMixClip clip{
        0, 0, 0, timeline::ClipKind::Audio, true,
        0, 6, 0, 30.0, 1.0, clip_gain, false, false};
    clip.source_duration_time_us = 200000;
    clip.track_id = 90;
    clip.clip_id = 901;
    clip.source_channel_count = 2;
    clip.clip_audio_effects = std::move(effects);
    const auto spans = media::planTimelineAudioMix(
        std::span<const media::TimelineAudioMixClip>(&clip, 1), {},
        30.0, sample_rate, 0, frame_count);
    require(spans.size() == 1,
            "The test clip tone did not produce one audio mix span.");
    media::AudioPcmChunk chunk;
    chunk.sample_rate = sample_rate;
    chunk.channel_count = 2;
    chunk.first_sample_index = spans.front().source_start_sample;
    chunk.samples.resize(static_cast<std::size_t>(frame_count) * 2U);
    for (int frame = 0; frame < frame_count; ++frame) {
        const auto value = static_cast<std::int16_t>(std::lround(
            amplitude * std::sin(2.0 * std::numbers::pi * frequency_hz *
                frame / sample_rate)));
        chunk.samples[static_cast<std::size_t>(frame) * 2U] = value;
        chunk.samples[static_cast<std::size_t>(frame) * 2U + 1U] = value;
    }
    media::TimelineAudioMixAccumulator accumulator(frame_count, 2, {});
    accumulator.accumulate(spans.front(), clip, chunk);
    return accumulator.finish().samples;
}

double steadyRms(const std::vector<float>& samples, std::size_t first_frame);

void testClipEffectResponseAndPreEnvelopeOrder() {
    timeline::AudioEffectInstance flat;
    flat.enabled = true;
    const auto dry = renderClipEffectTone(16000.0, {});
    const auto flat_enabled = renderClipEffectTone(16000.0, {flat});
    require(dry == flat_enabled,
            "A flat clip EQ changed the source signal.");

    auto eq = flat;
    auto eq_parameters = timeline::AudioEqualizerParameters{};
    eq_parameters.high_shelf_gain_db = 6.0;
    eq.parameters = eq_parameters;
    const auto boosted = renderClipEffectTone(16000.0, {eq});
    require(steadyRms(boosted, 4000) > steadyRms(dry, 4000) * 1.6,
            "The clip EQ did not process the selected source before track mixing.");

    timeline::AudioEffectInstance compressor;
    compressor.enabled = true;
    compressor.parameters = timeline::AudioCompressorParameters{
        -18.0, 4.0, 0.1, 100.0, 0.0};
    const auto clip_compressed = renderClipEffectTone(
        10000.0, {compressor}, 0.1, 16000.0);
    const auto clip_dry = renderClipEffectTone(10000.0, {}, 0.1, 16000.0);
    require(steadyRms(clip_compressed, 4000) <
                steadyRms(clip_dry, 4000) * 0.8,
            "The clip compressor ran after clip gain instead of before the clip envelope stage.");
}

double steadyRms(const std::vector<float>& samples, std::size_t first_frame) {
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t frame = first_frame;
         frame < samples.size() / 2U; ++frame) {
        const auto value = static_cast<double>(samples[frame * 2U]);
        sum += value * value;
        ++count;
    }
    return count == 0 ? 0.0 : std::sqrt(sum / count);
}

void testEqualizerResponseAndFlatCompatibility() {
    timeline::AudioEffectInstance flat;
    flat.enabled = true;
    const auto plain = renderEffectTone(10000.0, timeline::AudioEffectInstance{
        timeline::AudioEqualizerParameters{}, false});
    const auto flat_enabled = renderEffectTone(10000.0, flat);
    require(plain == flat_enabled,
            "A flat EQ changed the existing mixer output.");

    auto boosted = flat;
    auto eq = timeline::AudioEqualizerParameters{};
    eq.high_shelf_gain_db = 6.0;
    boosted.parameters = eq;
    const auto high_boost = renderEffectTone(16000.0, boosted);
    const auto low_control = renderEffectTone(100.0, boosted);
    require(steadyRms(high_boost, 4000) >
                steadyRms(renderEffectTone(16000.0,
                    timeline::AudioEffectInstance{}), 4000) * 1.6 &&
            std::abs(steadyRms(low_control, 4000) -
                steadyRms(renderEffectTone(100.0,
                    timeline::AudioEffectInstance{}), 4000)) < 0.002,
            "The high shelf did not boost its band while preserving low frequencies.");

    auto compressor = timeline::AudioEffectInstance{};
    compressor.enabled = true;
    compressor.parameters = timeline::AudioCompressorParameters{
        -18.0, 4.0, 0.1, 100.0, 0.0};
    const auto eq_then_compressor = renderEffectTone(
        10000.0, {boosted, compressor});
    const auto compressor_then_eq = renderEffectTone(
        10000.0, {compressor, boosted});
    require(eq_then_compressor != compressor_then_eq,
            "The saved effect-chain order did not change processing order.");
}

std::vector<float> renderChangedEqBlock(
    media::TimelineAudioMixState& state,
    std::int64_t block_start_sample,
    double high_shelf_gain_db) {
    constexpr int sample_rate = 48000;
    constexpr int frame_count = 4800;
    media::TimelineAudioMixClip clip{
        0, 0, 0, timeline::ClipKind::Audio, true,
        0, 120, 0, 30.0, 1.0, 1.0, false, false};
    clip.source_duration_time_us = 4'000'000;
    clip.track_id = 93;
    clip.source_channel_count = 2;
    timeline::AudioEffectInstance eq;
    eq.enabled = true;
    auto parameters = timeline::AudioEqualizerParameters{};
    parameters.high_shelf_gain_db = high_shelf_gain_db;
    eq.parameters = parameters;
    clip.audio_effects = {eq};
    const auto spans = media::planTimelineAudioMix(
        std::span<const media::TimelineAudioMixClip>(&clip, 1), {},
        30.0, sample_rate, block_start_sample, frame_count);
    require(spans.size() == 1,
            "The EQ transition block did not produce one audio mix span.");
    media::AudioPcmChunk chunk;
    chunk.sample_rate = sample_rate;
    chunk.channel_count = 2;
    chunk.first_sample_index = spans.front().source_start_sample;
    chunk.samples.resize(static_cast<std::size_t>(frame_count) * 2U);
    for (int frame = 0; frame < frame_count; ++frame) {
        const auto source_sample = chunk.first_sample_index + frame;
        const auto value = static_cast<std::int16_t>(std::lround(
            8000.0 * std::sin(2.0 * std::numbers::pi * 10000.0 *
                source_sample / sample_rate)));
        chunk.samples[static_cast<std::size_t>(frame) * 2U] = value;
        chunk.samples[static_cast<std::size_t>(frame) * 2U + 1U] = value;
    }
    media::TimelineAudioMixAccumulator accumulator(
        frame_count, 2, {}, &state);
    accumulator.accumulate(spans.front(), clip, chunk);
    return accumulator.finish().samples;
}

double rmsRange(const std::vector<float>& samples,
                std::size_t first_frame,
                std::size_t end_frame) {
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t frame = first_frame;
         frame < std::min(end_frame, samples.size() / 2U); ++frame) {
        const auto value = static_cast<double>(samples[frame * 2U]);
        sum += value * value;
        ++count;
    }
    return count == 0 ? 0.0 : std::sqrt(sum / count);
}

void testEffectParameterChangeIsSmoothedAcrossBlocks() {
    media::TimelineAudioMixState state;
    const auto dry = renderChangedEqBlock(state, 0, 0.0);
    const auto changed = renderChangedEqBlock(state, 4800, 6.0);
    const auto dry_level = rmsRange(dry, 500, 4500);
    const auto transition_level = rmsRange(changed, 0, 240);
    const auto processed_level = rmsRange(changed, 1000, 4500);
    require(transition_level > dry_level * 0.9 &&
                transition_level < processed_level * 0.9,
            "Changing effect parameters did not crossfade between old and new processing.");
}

std::vector<float> renderCompressorBlock(
    media::TimelineAudioMixState& state,
    std::int64_t block_start_sample,
    std::int16_t value,
    bool include_compressor) {
    constexpr int sample_rate = 48000;
    constexpr int frame_count = 4800;
    media::TimelineAudioMixClip clip{
        0, 0, 0, timeline::ClipKind::Audio, true,
        0, 120, 0, 30.0, 1.0, 1.0, false, false};
    clip.source_duration_time_us = 4'000'000;
    clip.track_id = 91;
    clip.source_channel_count = 2;
    if (include_compressor) {
        timeline::AudioEffectInstance compressor;
        compressor.enabled = true;
        compressor.parameters = timeline::AudioCompressorParameters{
            -18.0, 4.0, 0.1, 100.0, 0.0};
        clip.audio_effects = {compressor};
    }
    const auto spans = media::planTimelineAudioMix(
        std::span<const media::TimelineAudioMixClip>(&clip, 1), {},
        30.0, sample_rate, block_start_sample, frame_count);
    require(spans.size() == 1,
            "The compressor block did not produce one audio mix span.");
    media::TimelineAudioMixAccumulator accumulator(
        frame_count, 2, {}, &state);
    accumulator.accumulate(spans.front(), clip,
        constantChunk(spans.front().source_start_sample, value, frame_count));
    return accumulator.finish().samples;
}

std::vector<float> renderClipCompressorBlock(
    media::TimelineAudioMixState& state,
    std::int64_t block_start_sample,
    std::int16_t value,
    bool include_compressor) {
    constexpr int sample_rate = 48000;
    constexpr int frame_count = 4800;
    media::TimelineAudioMixClip clip{
        0, 0, 0, timeline::ClipKind::Audio, true,
        0, 120, 0, 30.0, 1.0, 1.0, false, false};
    clip.source_duration_time_us = 4'000'000;
    clip.track_id = 93;
    clip.clip_id = 9301;
    clip.source_channel_count = 2;
    if (include_compressor) {
        timeline::AudioEffectInstance compressor;
        compressor.enabled = true;
        compressor.parameters = timeline::AudioCompressorParameters{
            -18.0, 4.0, 0.1, 100.0, 0.0};
        clip.clip_audio_effects = {compressor};
    }
    const auto spans = media::planTimelineAudioMix(
        std::span<const media::TimelineAudioMixClip>(&clip, 1), {},
        30.0, sample_rate, block_start_sample, frame_count);
    require(spans.size() == 1,
            "The per-clip compressor test did not produce an audio span.");
    media::TimelineAudioMixAccumulator accumulator(
        frame_count, 2, {}, &state);
    accumulator.accumulate(spans.front(), clip,
        constantChunk(spans.front().source_start_sample, value, frame_count));
    return accumulator.finish().samples;
}

void testCompressorAndPersistentState() {
    media::TimelineAudioMixState continuing_state;
    static_cast<void>(renderCompressorBlock(
        continuing_state, 0, 20000, true));
    const auto continued = renderCompressorBlock(
        continuing_state, 4800, 1000, true);
    media::TimelineAudioMixState reset_state;
    const auto reset = renderCompressorBlock(reset_state, 0, 1000, true);
    require(steadyRms(continued, 3000) < steadyRms(reset, 3000) * 0.8,
            "The compressor detector state did not continue across audio blocks.");

    media::TimelineAudioMixState plain_state;
    const auto uncompressed = renderCompressorBlock(
        plain_state, 0, 20000, false);
    const auto compressed = renderCompressorBlock(
        reset_state, 4800, 20000, true);
    require(steadyRms(compressed, 3000) < steadyRms(uncompressed, 3000) * 0.8,
            "The compressor did not reduce sustained audio above its threshold.");
    reset_state.reset();
    const auto after_reset = renderCompressorBlock(reset_state, 0, 1000, true);
    require(steadyRms(after_reset, 3000) > steadyRms(continued, 3000),
            "Resetting the compressor state did not clear its release envelope.");

    media::TimelineAudioMixState clip_state;
    const auto clip_loud = renderClipCompressorBlock(
        clip_state, 0, 20000, true);
    const auto clip_continued = renderClipCompressorBlock(
        clip_state, 4800, 1000, true);
    const auto clip_reset = renderClipCompressorBlock(
        clip_state, 0, 1000, true);
    require(steadyRms(clip_loud, 3000) > 0.0 &&
                steadyRms(clip_continued, 3000) < steadyRms(clip_reset, 3000) * 0.8,
            "Per-clip compressor state did not persist across blocks or reset at a discontinuity.");
}

void testProcessedPreviewExportParityAndSendTaps() {
    auto clip = media::TimelineAudioMixClip{
        0, 0, 0, timeline::ClipKind::Audio, true,
        0, 4, 0, 30.0, 1.0, 1.0, false, false};
    clip.source_duration_time_us = 133333;
    clip.track_id = 92;
    clip.clip_id = 9201;
    clip.audio_sends = {{404, 0.5, timeline::AudioSendTap::PreFader},
                        {405, 0.5, timeline::AudioSendTap::PostFader}};
    clip.track_gain = 0.5;
    clip.track_pan = 0.25;
    timeline::AudioEffectInstance compressor;
    compressor.enabled = true;
    compressor.parameters = timeline::AudioCompressorParameters{
        -24.0, 2.0, 1.0, 100.0, 0.0};
    clip.audio_effects = {compressor};
    timeline::AudioEffectInstance clip_eq;
    clip_eq.enabled = true;
    auto clip_eq_parameters = timeline::AudioEqualizerParameters{};
    clip_eq_parameters.low_shelf_gain_db = 2.0;
    clip_eq.parameters = clip_eq_parameters;
    clip.clip_audio_effects = {clip_eq};
    auto configuration = media::TimelineAudioMixConfiguration{};
    configuration.buses = {{404, "Pre", 1.0, false, 0.0},
                           {405, "Post", 1.0, false, 0.0}};
    const auto preview = renderMix({clip}, configuration, 20000);
    configuration.apply_solo = false;
    const auto exported = renderMix({clip}, configuration, 20000);
    require(preview.samples == exported.samples &&
                preview.bus_peaks == exported.bus_peaks,
            "Preview and export did not match with the saved effects chain.");
    const auto peakFor = [](const auto& result, timeline::AudioBusId id) {
        const auto found = std::find_if(result.bus_peaks.begin(),
            result.bus_peaks.end(), [id](const auto& peak) {
                return peak.first == id;
            });
        return found == result.bus_peaks.end() ? -1.0F : found->second;
    };
    require(peakFor(preview, 404) > peakFor(preview, 405),
            "A pre-fader send did not tap the processed signal before track gain and pan.");
}


std::vector<float> renderAdvanced(timeline::AudioEffectInstance effect, bool track,
    std::size_t block_size, const std::function<float(std::int64_t)>& signal, int channels = 2) {
    media::TimelineAudioMixClip clip;
    clip.kind = timeline::ClipKind::Audio; clip.has_audio = true;
    clip.track_id = 1; clip.clip_id = 2; clip.duration_frames = 30;
    clip.source_duration_time_us = 1000000; clip.source_channel_count = channels;
    if (track) clip.audio_effects = {effect}; else clip.clip_audio_effects = {effect};
    media::TimelineAudioMixState state;
    const auto reader = [&](std::size_t, std::int64_t start, std::size_t count) {
        std::vector<float> input(count * channels);
        for (std::size_t frame = 0; frame < count; ++frame)
            for (int channel = 0; channel < channels; ++channel)
                input[frame * channels + channel] = signal(start + frame);
        return input;
    };
    std::vector<float> result;
    for (std::size_t start = 0; start < 48000; start += block_size) {
        auto block = media::TimelineAudioMixAccumulator::render(std::span(&clip, 1), {}, 30, 48000, channels,
            start, std::min(block_size, 48000 - start), {}, state, reader);
        result.insert(result.end(), block.samples.begin(), block.samples.end());
    }
    return result;
}
double advancedRms(const std::vector<float>& samples, std::size_t begin = 10000) {
    double energy = 0;
    for (auto index = begin; index < samples.size(); ++index) energy += samples[index] * samples[index];
    return std::sqrt(energy / (samples.size() - begin));
}
void testAdvancedProcessing() {
    const auto tone = [](double frequency) {
        return [frequency](std::int64_t sample) { return static_cast<float>(.25 * std::sin(2 * std::numbers::pi * frequency * sample / 48000)); };
    };
    timeline::AudioEffectInstance cleanup{timeline::AudioDialogueCleanupParameters{150, 0}, true};
    const auto bass = renderAdvanced(cleanup, false, 997, tone(40));
    const auto voice = renderAdvanced(cleanup, false, 997, tone(1000));
    require(advancedRms(bass) < advancedRms(voice) * .12, "Dialogue cleanup did not attenuate low-frequency rumble.");
    require(voice == renderAdvanced(cleanup, false, 4096, tone(1000)), "Cleanup changed with mixer block boundaries.");
    timeline::AudioEffectInstance noise{timeline::AudioNoiseReductionParameters{24, -30}, true};
    const auto hiss = [](std::int64_t sample) {
        auto value = static_cast<std::uint32_t>(sample) * 747796405U + 2891336453U;
        value = ((value >> ((value >> 28U) + 4U)) ^ value) * 277803737U;
        value = (value >> 22U) ^ value;
        return static_cast<float>((value / 4294967295.0 - .5) * .04);
    };
    const auto denoised = renderAdvanced(noise, false, 997, hiss);
    auto bypassed = noise; bypassed.enabled = false;
    const auto original = renderAdvanced(bypassed, false, 997, hiss);
    require(advancedRms(denoised) < advancedRms(original) * .8, "Noise reduction did not attenuate continuous noise.");
    require(denoised == renderAdvanced(noise, false, 4096, hiss), "Noise reduction lost state across blocks.");
    const auto impulse = renderAdvanced(noise, true, 997, [](std::int64_t index) { return index == 12345 ? .8F : 0.0F; });
    const auto peak = std::max_element(impulse.begin(), impulse.end(), [](float a, float b) { return std::abs(a) < std::abs(b); });
    require(std::abs(static_cast<std::int64_t>((peak - impulse.begin()) / 2) - 12345) <= 1,
        "Noise reduction left uncompensated latency on the timeline.");
    timeline::AudioEffectInstance repair{timeline::AudioRepairParameters{60, false, 2}, true};
    require(advancedRms(renderAdvanced(repair, true, 997, tone(60)), 48000) < .005,
        "Hum repair did not remove the selected mains frequency.");
    repair.parameters = timeline::AudioRepairParameters{0, true, 2};
    const auto clicks = [](std::int64_t index) { return index % 4000 == 1000 ? .8F : static_cast<float>(.03 * std::sin(index * .13)); };
    const auto repaired = renderAdvanced(repair, false, 997, clicks);
    require(repaired.size() == 96000 && repaired == renderAdvanced(repair, false, 4096, clicks),
        "Click repair changed duration or depended on block size.");
    for (float sample : repaired) require(std::isfinite(sample), "Click repair emitted a non-finite sample.");
    const auto mono = renderAdvanced(cleanup, false, 997, tone(1000), 1);
    require(mono.size() == 48000, "Mono cleanup changed channel count or duration.");
}
void testDuckingAndCurveGeneration() {
    timeline::AudioDuckingSettings settings;
    settings.mode = timeline::AudioDuckingMode::Automatic; settings.source_track_id = 10;
    settings.attack_ms = 5; settings.hold_ms = 20; settings.release_ms = 30;
    media::AudioDuckingDetector detector; media::AudioDuckingCurveBuilder builder;
    std::vector<double> reductions;
    for (std::int64_t sample = 0; sample < 48000; ++sample) {
        const float voice[] = {sample < 24000 ? .4F : 0.0F, sample < 24000 ? .4F : 0.0F};
        const auto reduction = detector.process(voice, 48000, settings);
        reductions.push_back(reduction);
        builder.append(static_cast<std::int64_t>(std::llround(sample * 1000000.0 / 48000)), reduction);
    }
    const auto curve = builder.finish();
    require(curve.size() < 1000 && reductions[12000] > 11.9 && reductions.back() < .01,
        "Ducking detector or bounded curve simplification failed.");
    for (std::size_t sample = 0; sample < reductions.size(); ++sample) {
        const auto time = static_cast<std::int64_t>(std::llround(sample * 1000000.0 / 48000));
        const auto actual = timeline::audioDuckingReductionAt(curve, time);
        if (std::abs(actual - reductions[sample]) > .101) {
            std::cerr << "Curve error at " << time << ": " << actual << " vs " << reductions[sample] << ", points=" << curve.size() << '\n';
            for (const auto& point : curve) std::cerr << point.time_us << ':' << point.reduction_db << ' ';
            std::cerr << '\n';
            throw std::runtime_error("Generated ducking curve exceeded the 0.1 dB error bound.");
        }
    }
    std::vector<media::TimelineAudioMixClip> clips(2);
    for (std::size_t index = 0; index < clips.size(); ++index) {
        auto& clip = clips[index]; clip.kind = timeline::ClipKind::Audio; clip.has_audio = true;
        clip.source_index = index; clip.track_id = index == 0 ? 10 : 20; clip.clip_id = index + 1;
        clip.duration_frames = 30; clip.source_duration_time_us = 1000000;
    }
    clips[1].audio_ducking = settings;
    const auto reader = [](std::size_t index, std::int64_t start, std::size_t count) {
        std::vector<float> output(count * 2);
        for (std::size_t frame = 0; frame < count; ++frame)
            output[frame * 2] = output[frame * 2 + 1] = index == 0 ? (start + frame < 24000 ? .4F : 0.0F) : .2F;
        return output;
    };
    media::TimelineAudioMixState live, reversed;
    media::TimelineAudioMixConfiguration config; config.solo_track_ids = {20};
    const auto automatic = media::TimelineAudioMixAccumulator::render(clips, {}, 30, 48000, 2, 0, 48000, config, live, reader);
    std::reverse(clips.begin(), clips.end());
    const auto swapped = media::TimelineAudioMixAccumulator::render(clips, {}, 30, 48000, 2, 0, 48000, config, reversed, reader);
    require(automatic.samples == swapped.samples && automatic.samples[24000] < .06F && automatic.samples.back() > .19F,
        "Automatic ducking depended on track order, soloed away its source, or failed to release.");
    clips.front().audio_ducking.mode = timeline::AudioDuckingMode::Curve; clips.front().audio_ducking.curve = curve;
    media::TimelineAudioMixState baked;
    const auto curve_mix = media::TimelineAudioMixAccumulator::render(clips, {}, 30, 48000, 2, 0, 48000, config, baked, reader);
    require(std::abs(curve_mix.samples[24000] - automatic.samples[24000]) < .001F,
        "Curve ducking was applied twice or at a different mix stage.");
}

} // namespace

int main() {
    try {
        testAdvancedProcessing();
        testDuckingAndCurveGeneration();
        testMixesEveryActiveTrack();
        testMuteGainAndGaps();
        testTrimmedSourcePositionUsesTimelineElapsedTime();
        testCrossDissolveAudioStartsAtTheOriginalCut();
        testAudioOnlyClipUsesMicrosecondSourceTiming();
        testNonVideoAndMissingAudioAreExcluded();
        testExternalizedVideoAudioIsMixedOnlyFromItsCompanion();
        testAudioEnvelopeIsAppliedPerSample();
        testAudioCrossfadeUsesEqualPowerAndExistingGainControls();
        testTrackRoutesAndPrePostSends();
        testPanSoloMuteAndMasterMix();
        testStereoBalanceAndMultichannelPassThrough();
        testEqualizerResponseAndFlatCompatibility();
        testClipEffectResponseAndPreEnvelopeOrder();
        testEffectParameterChangeIsSmoothedAcrossBlocks();
        testCompressorAndPersistentState();
        testProcessedPreviewExportParityAndSendTaps();
        std::cout << "timeline audio mix tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
