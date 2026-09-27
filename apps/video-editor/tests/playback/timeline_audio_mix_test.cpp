#include "media/timeline_audio_mix.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
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

} // namespace

int main() {
    try {
        testMixesEveryActiveTrack();
        testMuteGainAndGaps();
        testTrimmedSourcePositionUsesTimelineElapsedTime();
        testCrossDissolveAudioStartsAtTheOriginalCut();
        testNonVideoAndMissingAudioAreExcluded();
        std::cout << "timeline audio mix tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
