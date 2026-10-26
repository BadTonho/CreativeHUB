#pragma once

#include "audio_frame.h"
#include "video_metadata.h"

#include <cstdint>
#include <algorithm>
#include <stdexcept>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace media {

class AudioPlaybackSession final {
public:
    struct OutputSpec {
        int sample_rate = 48000;
        int channel_count = 2;
    };

    using CancellationPredicate = std::function<bool()>;

    static std::unique_ptr<AudioPlaybackSession> open(
        const std::filesystem::path& source_path);
    static std::unique_ptr<AudioPlaybackSession> open(
        const std::filesystem::path& source_path,
        OutputSpec output);

    ~AudioPlaybackSession();

    AudioPlaybackSession(AudioPlaybackSession&&) noexcept;
    AudioPlaybackSession& operator=(AudioPlaybackSession&&) noexcept;

    AudioPlaybackSession(const AudioPlaybackSession&) = delete;
    AudioPlaybackSession& operator=(const AudioPlaybackSession&) = delete;

    [[nodiscard]] bool has_audio() const noexcept;
    [[nodiscard]] int source_channel_count() const noexcept;
    [[nodiscard]] const OutputSpec& output_spec() const noexcept;
    [[nodiscard]] std::int64_t current_sample_index() const noexcept;
    [[nodiscard]] bool at_end() const noexcept;

    void seek_to_source_frame(
        std::int64_t source_frame,
        double video_frame_rate);
    void seek_to_sample_index(std::int64_t sample_index);
    // maximum_samples limits output sample frames per channel, not codec input.
    // Unreturned PCM is retained until the next call; EOF includes resampler
    // draining. Seek/reset discard buffered PCM, cancellation consumes none.
    [[nodiscard]] std::optional<AudioPcmChunk> decode_samples(
        std::size_t maximum_samples = 4096,
        const CancellationPredicate& should_cancel = {});
    void reset();

private:
    struct Impl;

    explicit AudioPlaybackSession(std::unique_ptr<Impl> impl);
    static std::unique_ptr<Impl> openImpl(
        const std::filesystem::path& source_path,
        OutputSpec output);

    std::unique_ptr<Impl> impl_;
};

// Decoder-independent block adapter used by Preview, export and analysis.
// Unavailable samples at EOF remain silence; errors and cancellation propagate.
inline std::vector<float> readNormalizedAudio(AudioPlaybackSession& session,
    std::int64_t start, std::size_t count,
    const AudioPlaybackSession::CancellationPredicate& canceled = {}) {
    const int channels = session.output_spec().channel_count;
    std::vector<float> result(count * channels, 0.0F);
    if (session.current_sample_index() != start) session.seek_to_sample_index(start);
    std::size_t written = 0;
    while (written < count) {
        if (canceled && canceled()) throw std::runtime_error("Audio operation cancelled.");
        auto chunk = session.decode_samples(std::min<std::size_t>(8192, count - written), canceled);
        if (!chunk || chunk->sampleCount() == 0) break;
        const auto begin = std::max(start, chunk->first_sample_index);
        const auto end = std::min(start + static_cast<std::int64_t>(count),
            chunk->first_sample_index + static_cast<std::int64_t>(chunk->sampleCount()));
        if (end <= begin) break;
        for (auto frame = begin; frame < end; ++frame)
            for (int channel = 0; channel < channels; ++channel)
                result[static_cast<std::size_t>(frame - start) * channels + channel] =
                    chunk->samples[static_cast<std::size_t>(frame - chunk->first_sample_index) * channels + channel] / 32768.0F;
        const auto next = static_cast<std::size_t>(end - start);
        if (next <= written) break;
        written = next;
    }
    return result;
}

} // namespace media
