#pragma once

#include "audio_frame.h"
#include "../timeline/timeline_model.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace media {

// Timeline audio scheduling shared by real-time playback and offline export.
// source_index is owned by the caller and maps a planned span back to its
// decoder/session entry.
struct TimelineAudioMixClip {
    std::size_t source_index = 0;
    std::int64_t track_index = -1;
    std::int64_t clip_index = -1;
    timeline::ClipKind kind = timeline::ClipKind::Video;
    bool has_audio = false;
    std::int64_t timeline_start_frame = 0;
    std::int64_t duration_frames = 0;
    std::int64_t source_start_frame = 0;
    double source_frame_rate = 30.0;
    double track_gain = 1.0;
    double clip_gain = 1.0;
    bool track_muted = false;
    bool clip_muted = false;
    // Audio-only media has no source frame rate. Its trim points stay in
    // microseconds and are converted directly to samples at the mix rate.
    std::int64_t source_start_time_us = 0;
    std::int64_t source_duration_time_us = 0;
    // The video's embedded stream is suppressed when its audio is represented
    // by a linked Audio clip elsewhere on the timeline.
    bool audio_extracted = false;
    std::vector<timeline::AudioGainKeyframe> audio_gain_keyframes;
    timeline::TrackId track_id = 0;
    double track_pan = 0.0;
    std::optional<timeline::AudioBusId> output_bus_id;
    std::vector<timeline::AudioSend> audio_sends;
    // The legacy name is retained for track-level processing after clips sum.
    std::vector<timeline::AudioEffectInstance> audio_effects;
    timeline::ClipId clip_id = 0;
    std::vector<timeline::AudioEffectInstance> clip_audio_effects;
    int source_channel_count = 2;
    timeline::AudioDuckingSettings audio_ducking;
};

struct TimelineAudioMixTransition {
    std::int64_t track_index = -1;
    std::int64_t to_clip_index = -1;
    std::int64_t boundary_frame = 0;
    timeline::TransitionKind kind = timeline::TransitionKind::CrossDissolve;
    std::int64_t from_clip_index = -1;
    std::int64_t duration_frames = 0;
};

struct TimelineAudioMixFade {
    std::int64_t transition_start_sample = 0;
    std::int64_t transition_end_sample = 0;
    bool incoming = false;
};

struct TimelineAudioMixSpan {
    std::size_t source_index = 0;
    std::int64_t source_start_sample = 0;
    std::int64_t destination_start_sample = 0;
    std::int64_t sample_count = 0;
    double gain = 1.0;
    double local_frame_at_destination_start = 0.0;
    double frames_per_sample = 0.0;
    std::vector<timeline::AudioGainKeyframe> audio_gain_keyframes;
    std::int64_t timeline_start_sample = 0;
    std::vector<TimelineAudioMixFade> transition_fades;
    double track_gain = 1.0;
    double clip_gain = 1.0;
};

struct TimelineAudioMixConfiguration {
    std::vector<timeline::AudioBus> buses;
    timeline::MasterAudioMix master;
    std::vector<timeline::TrackId> solo_track_ids;
    std::vector<timeline::AudioBusId> solo_bus_ids;
    bool apply_solo = true;
    std::int64_t block_start_sample = 0;
    int sample_rate = 48000;
};

struct TimelineAudioMixResult {
    std::vector<float> samples;
    std::vector<std::pair<timeline::TrackId, float>> track_peaks;
    std::vector<std::pair<timeline::AudioBusId, float>> bus_peaks;
    float master_peak = 0.0F;
    bool processor_bypassed = false;
};

// Persistent per-clip and per-track DSP state shared by consecutive Preview or
// export blocks. Call reset() whenever playback jumps to a different point.
class TimelineAudioMixState final {
public:
    TimelineAudioMixState();
    ~TimelineAudioMixState();
    TimelineAudioMixState(TimelineAudioMixState&&) noexcept;
    TimelineAudioMixState& operator=(TimelineAudioMixState&&) noexcept;
    TimelineAudioMixState(const TimelineAudioMixState&) = delete;
    TimelineAudioMixState& operator=(const TimelineAudioMixState&) = delete;

    void reset() noexcept;

private:
    friend class TimelineAudioMixAccumulator;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Accumulates clip samples through track routes, sends, buses and the Master.
// The same class is used for real-time Preview and offline export.
class TimelineAudioMixAccumulator final {
public:
    TimelineAudioMixAccumulator(
        std::size_t sample_count,
        int channel_count,
        TimelineAudioMixConfiguration configuration,
        TimelineAudioMixState* persistent_state = nullptr);

    using SourceReader = std::function<std::vector<float>(std::size_t, std::int64_t, std::size_t)>;
    // Pull-based block renderer compensates per-node lookahead before envelopes,
    // summation and routing. Reader returns exactly count interleaved frames.
    static TimelineAudioMixResult render(
        std::span<const TimelineAudioMixClip> clips,
        std::span<const TimelineAudioMixTransition> transitions,
        double frame_rate, int sample_rate, int channels,
        std::int64_t start, std::size_t count, TimelineAudioMixConfiguration configuration,
        TimelineAudioMixState& state, const SourceReader& reader, bool bypass_failed_processors = false);
    void accumulate(
        const TimelineAudioMixSpan& span,
        const TimelineAudioMixClip& clip,
        const AudioPcmChunk& chunk);
    [[nodiscard]] TimelineAudioMixResult finish();

private:
    struct TrackBuffers {
        std::vector<float> pre_fader;
        std::vector<float> post_fader;
        double gain = 1.0;
        double pan = 0.0;
        bool muted = false;
        int source_channel_count = 2;
        std::optional<timeline::AudioBusId> output_bus_id;
        std::vector<timeline::AudioSend> sends;
        std::vector<timeline::AudioEffectInstance> effects;
        timeline::AudioDuckingSettings ducking;
    };

    [[nodiscard]] bool trackSoloed(timeline::TrackId track_id) const noexcept;
    [[nodiscard]] bool busSoloed(timeline::AudioBusId bus_id) const noexcept;
    [[nodiscard]] std::vector<float>& busBuffer(timeline::AudioBusId bus_id);

    std::size_t sample_count_ = 0;
    int channel_count_ = 0;
    TimelineAudioMixConfiguration configuration_;
    int sample_rate_ = 0;
    std::unique_ptr<TimelineAudioMixState> owned_state_;
    TimelineAudioMixState* persistent_state_ = nullptr;
    bool any_solo_ = false;
    std::vector<float> master_samples_;
    std::unordered_map<timeline::AudioBusId, std::vector<float>> bus_samples_;
    std::unordered_map<timeline::TrackId, TrackBuffers> track_samples_;
};

[[nodiscard]] std::vector<TimelineAudioMixSpan> planTimelineAudioMix(
    std::span<const TimelineAudioMixClip> clips,
    std::span<const TimelineAudioMixTransition> transitions,
    double timeline_frame_rate,
    int sample_rate,
    std::int64_t block_start_sample,
    std::int64_t sample_count);

// Adds the intersection of a decoded source chunk and a planned span to a
// normalized interleaved float mix buffer. The destination is not clipped;
// each output path applies its final gain and clipping once after summation.
void accumulateTimelineAudioChunk(
    const TimelineAudioMixSpan& span,
    const AudioPcmChunk& chunk,
    std::span<float> mixed,
    int output_channel_count);

// Same envelope/crossfade path for source samples already normalized and
// processed by clip-level DSP.
void accumulateTimelineAudioFloatSamples(
    const TimelineAudioMixSpan& span,
    std::span<const float> samples,
    std::int64_t first_sample_index,
    std::span<float> mixed,
    int output_channel_count);

// Peak of the floating-point mix before monitor gain and output clipping.
[[nodiscard]] float timelineAudioPeak(std::span<const float> samples) noexcept;

} // namespace media
