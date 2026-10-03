#pragma once

#include "audio_frame.h"
#include "../timeline/timeline_model.h"

#include <cstddef>
#include <cstdint>
#include <span>
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
};

struct TimelineAudioMixTransition {
    std::int64_t track_index = -1;
    std::int64_t to_clip_index = -1;
    std::int64_t boundary_frame = 0;
    timeline::TransitionKind kind = timeline::TransitionKind::CrossDissolve;
};

struct TimelineAudioMixSpan {
    std::size_t source_index = 0;
    std::int64_t source_start_sample = 0;
    std::int64_t destination_start_sample = 0;
    std::int64_t sample_count = 0;
    double gain = 1.0;
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

} // namespace media
