#include "audio_ducking_analysis.h"
#include "audio_ducking.h"
#include "audio_playback.h"
#include "timeline_audio_mix.h"
#include "video_probe.h"
#include <unordered_map>
#include <stdexcept>

namespace media {
std::vector<timeline::AudioDuckingKeyframe> generateAudioDuckingCurve(
    const project::ProjectDocument& document, const timeline::AudioDuckingSettings& settings,
    const std::function<bool()>& canceled, const std::function<void(int)>& progress) {
    if (!timeline::validAudioDucking(settings) || !settings.source_track_id)
        throw std::invalid_argument("Ducking analysis needs a valid source track.");
    const auto source = std::find_if(document.timeline_tracks.begin(), document.timeline_tracks.end(),
        [&](const auto& track) { return track.track_id == *settings.source_track_id; });
    if (source == document.timeline_tracks.end()) throw std::invalid_argument("Ducking source track is missing.");
    const auto fps = document.timeline_frame_rate.asDouble();
    constexpr int rate = 48000;
    std::vector<TimelineAudioMixClip> clips;
    std::vector<TimelineAudioMixTransition> transitions;
    std::unordered_map<std::size_t, std::unique_ptr<AudioPlaybackSession>> decoders;
    std::int64_t end_frame = 0;
    for (const auto& track : document.timeline_tracks) for (const auto& clip : track.clips)
        end_frame = std::max(end_frame, clip.timeline_start_frame + clip.duration_frames);
    for (std::size_t index = 0; index < source->clips.size(); ++index) {
        if (canceled && canceled()) return {};
        const auto& clip = source->clips[index];
        if ((clip.kind != timeline::ClipKind::Audio && clip.kind != timeline::ClipKind::Video) || clip.audio_extracted) continue;
        const auto metadata = VideoProbe{}.probe(clip.source_path);
        TimelineAudioMixClip mix;
        mix.source_index = index; mix.clip_index = index; mix.track_index = 0;
        mix.track_id = source->track_id; mix.clip_id = clip.clip_id; mix.kind = clip.kind;
        mix.has_audio = metadata.audio.has_value(); mix.source_channel_count = metadata.audio ? metadata.audio->channel_count : 2;
        mix.timeline_start_frame = clip.timeline_start_frame; mix.duration_frames = clip.duration_frames;
        mix.source_start_frame = clip.source_start_frame; mix.source_frame_rate = metadata.frame_rate.value_or(0.0);
        mix.source_start_time_us = clip.source_start_time_us; mix.source_duration_time_us = clip.source_duration_time_us;
        mix.track_gain = source->audio_gain; mix.track_muted = source->audio_muted; mix.track_pan = source->audio_pan;
        mix.clip_gain = clip.audio_gain; mix.clip_muted = clip.audio_muted;
        mix.audio_gain_keyframes = clip.audio_gain_keyframes; mix.clip_audio_effects = clip.audio_effects;
        mix.audio_effects = source->audio_effects;
        clips.push_back(std::move(mix));
    }
    for (const auto& transition : source->transitions) {
        if (transition.to_clip_index >= source->clips.size()) continue;
        transitions.push_back({0, static_cast<std::int64_t>(transition.to_clip_index),
            source->clips[transition.to_clip_index].timeline_start_frame + transition.duration_frames,
            transition.kind, static_cast<std::int64_t>(transition.from_clip_index), transition.duration_frames});
    }
    const auto reader = [&](std::size_t index, std::int64_t start, std::size_t count) {
        if (canceled && canceled()) return std::vector<float>(count * 2, 0.0F);
        auto& decoder = decoders[index];
        if (!decoder) decoder = AudioPlaybackSession::open(source->clips[index].source_path, {rate, 2});
        return readNormalizedAudio(*decoder, start, count, canceled);
    };
    const auto samples = static_cast<std::int64_t>(std::llround(static_cast<long double>(end_frame) * rate / fps));
    TimelineAudioMixState state;
    TimelineAudioMixConfiguration configuration; configuration.apply_solo = false;
    AudioDuckingDetector detector;
    AudioDuckingCurveBuilder builder;
    int last_progress = -1;
    for (std::int64_t start = 0; start < samples; start += 4096) {
        if (canceled && canceled()) return {};
        const auto count = static_cast<std::size_t>(std::min<std::int64_t>(4096, samples - start));
        const auto block = TimelineAudioMixAccumulator::render(clips, transitions, fps, rate, 2,
            start, count, configuration, state, reader);
        for (std::size_t frame = 0; frame < count; ++frame) {
            const auto reduction = detector.process(std::span<const float>(block.samples).subspan(frame * 2, 2), rate, settings);
            builder.append(static_cast<std::int64_t>(std::llround(static_cast<long double>(start + frame) * 1000000 / rate)), reduction);
        }
        const auto percent = static_cast<int>((start + count) * 100 / std::max<std::int64_t>(1, samples));
        if (progress && percent != last_progress) { progress(percent); last_progress = percent; }
        // Retire decoders after their occurrence to bound resources on long timelines.
        std::erase_if(decoders, [&](const auto& item) {
            const auto& clip = source->clips[item.first];
            return (start + count) > static_cast<long double>(clip.timeline_start_frame + clip.duration_frames) * rate / fps + rate;
        });
    }
    if (canceled && canceled()) return {};
    return builder.finish();
}
} // namespace media
