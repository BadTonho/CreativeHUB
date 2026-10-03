#include "audio_waveform.h"

#include "video_metadata.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <utility>

namespace media {
namespace {

constexpr std::size_t kMaximumWaveformPeakCount =
    kAudioWaveformCacheByteLimit / sizeof(AudioWaveformPeak);
constexpr std::size_t kDecodeChunkSamples = 8192;

bool cancelled(const AudioPlaybackSession::CancellationPredicate& predicate) {
    return predicate && predicate();
}

std::uint16_t magnitude(std::int16_t sample) noexcept {
    const auto promoted = static_cast<int>(sample);
    return static_cast<std::uint16_t>(promoted < 0 ? -promoted : promoted);
}

std::uint8_t quantizePeak(std::uint16_t peak) noexcept {
    return static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(peak) * 255U + 16384U) / 32768U);
}

} // namespace

std::optional<AudioWaveform> decodeAudioWaveform(
    const std::filesystem::path& source_path,
    const AudioPlaybackSession::CancellationPredicate& should_cancel) {
    if (cancelled(should_cancel)) return std::nullopt;

    auto session = AudioPlaybackSession::open(
        source_path,
        AudioPlaybackSession::OutputSpec{kAudioWaveformSampleRate, 2});
    if (!session->has_audio()) return std::nullopt;

    AudioWaveform waveform;
    waveform.source_channel_count = session->source_channel_count();
    while (!cancelled(should_cancel)) {
        const auto chunk = session->decode_samples(kDecodeChunkSamples, should_cancel);
        if (cancelled(should_cancel)) return std::nullopt;
        if (!chunk.has_value()) break;
        if (chunk->channel_count != 2 || chunk->sample_rate != kAudioWaveformSampleRate) {
            throw MediaError("The audio decoder returned an unsupported waveform format.");
        }

        for (std::size_t sample_offset = 0;
             sample_offset < chunk->sampleCount(); ++sample_offset) {
            if (sample_offset > static_cast<std::size_t>(
                    std::numeric_limits<std::int64_t>::max()) ||
                chunk->first_sample_index > std::numeric_limits<std::int64_t>::max() -
                    static_cast<std::int64_t>(sample_offset)) {
                throw MediaError("The decoded audio position exceeds the waveform range.");
            }
            const auto source_sample = chunk->first_sample_index +
                static_cast<std::int64_t>(sample_offset);
            if (source_sample < 0) continue;

            const auto bucket = static_cast<std::uint64_t>(source_sample) /
                kAudioWaveformSamplesPerBucket;
            if (bucket >= kMaximumWaveformPeakCount) {
                throw MediaError("The audio source exceeds the waveform memory limit.");
            }
            const auto bucket_index = static_cast<std::size_t>(bucket);
            if (waveform.peaks.size() <= bucket_index) {
                waveform.peaks.resize(bucket_index + 1, AudioWaveformPeak{});
            }

            const auto channel_offset = sample_offset * 2;
            auto& bucket_peak = waveform.peaks[bucket_index];
            bucket_peak.left = std::max(
                bucket_peak.left,
                quantizePeak(magnitude(chunk->samples[channel_offset])));
            bucket_peak.right = std::max(
                bucket_peak.right,
                quantizePeak(magnitude(chunk->samples[channel_offset + 1])));
        }
    }

    if (cancelled(should_cancel)) return std::nullopt;
    if (waveform.peaks.empty()) return std::nullopt;
    waveform.peaks.shrink_to_fit();
    return waveform;
}

} // namespace media
