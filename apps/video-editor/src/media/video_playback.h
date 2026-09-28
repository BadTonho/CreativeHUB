#pragma once

#include <creative_suite/media/video_playback.h>

#include <filesystem>
#include <memory>
#include <optional>

namespace media {

using ForwardDecodeDiagnostics = creative_suite::media::ForwardDecodeDiagnostics;
using VideoFramePtr = creative_suite::media::VideoFramePtr;

class VideoPlaybackSession final {
public:
    using CancellationPredicate =
        creative_suite::media::VideoPlaybackSession::CancellationPredicate;
    using CacheSnapshot =
        creative_suite::media::VideoPlaybackSession::CacheSnapshot;

    static std::unique_ptr<VideoPlaybackSession> open(
        const std::filesystem::path& source_path);

    ~VideoPlaybackSession();

    VideoPlaybackSession(const VideoPlaybackSession&) = delete;
    VideoPlaybackSession& operator=(const VideoPlaybackSession&) = delete;
    VideoPlaybackSession(VideoPlaybackSession&&) noexcept;
    VideoPlaybackSession& operator=(VideoPlaybackSession&&) noexcept;

    std::optional<VideoFramePtr> decode_next_frame();
    std::optional<VideoFramePtr> decode_forward_to(
        std::int64_t frame_index,
        const CancellationPredicate& should_cancel = {},
        ForwardDecodeDiagnostics* diagnostics = nullptr);
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
    using SharedSession = creative_suite::media::VideoPlaybackSession;

    explicit VideoPlaybackSession(std::unique_ptr<SharedSession> session);

    std::unique_ptr<SharedSession> session_;
};

} // namespace media
