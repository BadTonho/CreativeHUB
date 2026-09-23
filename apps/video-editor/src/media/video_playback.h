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

    struct CacheSnapshot {
        std::uint64_t entries = 0;
        std::uint64_t bytes = 0;
    };

    static std::unique_ptr<VideoPlaybackSession> open(
        const std::filesystem::path& source_path);

    ~VideoPlaybackSession();

    VideoPlaybackSession(const VideoPlaybackSession&) = delete;
    VideoPlaybackSession& operator=(const VideoPlaybackSession&) = delete;
    VideoPlaybackSession(VideoPlaybackSession&&) noexcept;
    VideoPlaybackSession& operator=(VideoPlaybackSession&&) noexcept;

    std::optional<VideoFramePtr> decode_next_frame();
    // Advances an already-valid sequential decoder state without seeking.
    // Only the requested final frame is materialized; cancelled, backward,
    // initial, or invalid decoder-position requests return no frame.
    std::optional<VideoFramePtr> decode_forward_to(
        std::int64_t frame_index,
        const CancellationPredicate& should_cancel = {});
    std::optional<VideoFramePtr> decode_frame_at(std::int64_t frame_index);
    std::optional<VideoFramePtr> decode_frame_at(
        std::int64_t frame_index,
        const CancellationPredicate& should_cancel);
    [[nodiscard]] std::uint64_t take_cache_hit_count() noexcept;
    [[nodiscard]] CacheSnapshot cache_snapshot() const noexcept;
    void reset();

    [[nodiscard]] std::int64_t current_frame_index() const noexcept;
    [[nodiscard]] bool at_end() const noexcept;

private:
    struct Impl;

    explicit VideoPlaybackSession(std::unique_ptr<Impl> impl);

    static std::unique_ptr<Impl> openImpl(const std::filesystem::path& source_path);
    static bool decodeNextFrame(Impl& impl, VideoFramePtr* output_frame);
    static bool discardNextFrame(Impl& impl);
    static void cacheFrame(
        Impl& impl,
        std::int64_t frame_index,
        const VideoFramePtr& frame);
    static VideoFramePtr takeCachedFrame(
        Impl& impl,
        std::int64_t frame_index);
    static void resetDecoderPosition(Impl& impl);
    static bool seekToTimestamp(Impl& impl, std::int64_t frame_index);

    std::unique_ptr<Impl> impl_;
};

} // namespace media
