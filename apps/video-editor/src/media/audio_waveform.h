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

struct AudioWaveform {
    // Each value is a combined-channel peak in the inclusive 0..255 range.
    std::vector<std::uint8_t> peaks;

    [[nodiscard]] std::size_t memoryBytes() const noexcept {
        return peaks.capacity() * sizeof(std::uint8_t);
    }
};

[[nodiscard]] std::optional<AudioWaveform> decodeAudioWaveform(
    const std::filesystem::path& source_path,
    const AudioPlaybackSession::CancellationPredicate& should_cancel = {});

} // namespace media
