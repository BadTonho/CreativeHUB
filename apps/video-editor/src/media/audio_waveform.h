#pragma once

#include "audio_playback.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace media {

inline constexpr int kAudioWaveformSampleRate = 8000;
inline constexpr std::int64_t kAudioWaveformBucketDurationUs = 10'000;
inline constexpr std::size_t kAudioWaveformSamplesPerBucket = 80;
inline constexpr std::size_t kAudioWaveformCacheByteLimit = 64U * 1024U * 1024U;

struct AudioWaveformPeak {
    std::uint8_t left = 0;
    std::uint8_t right = 0;

    friend bool operator==(const AudioWaveformPeak&, const AudioWaveformPeak&) = default;
};

struct AudioWaveform {
    int source_channel_count = 0;
    // Per-channel peaks in the inclusive 0..255 range, one pair per 10 ms bucket.
    std::vector<AudioWaveformPeak> peaks;

    [[nodiscard]] std::size_t memoryBytes() const noexcept {
        return peaks.capacity() * sizeof(AudioWaveformPeak);
    }
};

[[nodiscard]] std::optional<AudioWaveform> decodeAudioWaveform(
    const std::filesystem::path& source_path,
    const AudioPlaybackSession::CancellationPredicate& should_cancel = {});

} // namespace media
