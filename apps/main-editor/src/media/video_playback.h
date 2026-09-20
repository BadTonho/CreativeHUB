#pragma once

#include "video_frame.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace media {

class VideoPlaybackSession final {
public:
    using CancellationPredicate = std::function<bool()>;

    static std::unique_ptr<VideoPlaybackSession> open(
        const std::filesystem::path& source_path);

    ~VideoPlaybackSession();

    VideoPlaybackSession(const VideoPlaybackSession&) = delete;
    VideoPlaybackSession& operator=(const VideoPlaybackSession&) = delete;
    VideoPlaybackSession(VideoPlaybackSession&&) noexcept;
    VideoPlaybackSession& operator=(VideoPlaybackSession&&) noexcept;

    std::optional<VideoFrame> decode_next_frame();
    std::optional<VideoFrame> decode_frame_at(std::int64_t frame_index);
    std::optional<VideoFrame> decode_frame_at(
        std::int64_t frame_index,
        const CancellationPredicate& should_cancel);
    void reset();

    [[nodiscard]] std::int64_t current_frame_index() const noexcept;
    [[nodiscard]] bool at_end() const noexcept;

private:
    struct Impl;

    explicit VideoPlaybackSession(std::unique_ptr<Impl> impl);

    static std::unique_ptr<Impl> openImpl(const std::filesystem::path& source_path);
    static std::optional<VideoFrame> decodeNextFrame(Impl& impl);
    static void cacheFrame(Impl& impl, std::int64_t frame_index, const VideoFrame& frame);
    static std::shared_ptr<const VideoFrame> takeCachedFrame(
        Impl& impl,
        std::int64_t frame_index);
    static void resetDecoderPosition(Impl& impl);
    static bool seekToTimestamp(Impl& impl, std::int64_t frame_index);

    std::unique_ptr<Impl> impl_;
};

} // namespace media
