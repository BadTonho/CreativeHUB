#pragma once

#include "audio_frame.h"
#include "video_metadata.h"

#include <cstdint>
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
        const std::filesystem::path& source_path,
        OutputSpec output = {});

    ~AudioPlaybackSession();

    AudioPlaybackSession(AudioPlaybackSession&&) noexcept;
    AudioPlaybackSession& operator=(AudioPlaybackSession&&) noexcept;

    AudioPlaybackSession(const AudioPlaybackSession&) = delete;
    AudioPlaybackSession& operator=(const AudioPlaybackSession&) = delete;

    [[nodiscard]] bool has_audio() const noexcept;
    [[nodiscard]] const OutputSpec& output_spec() const noexcept;
    [[nodiscard]] std::int64_t current_sample_index() const noexcept;
    [[nodiscard]] bool at_end() const noexcept;

    void seek_to_source_frame(
        std::int64_t source_frame,
        double video_frame_rate);
    void seek_to_sample_index(std::int64_t sample_index);
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

} // namespace media
