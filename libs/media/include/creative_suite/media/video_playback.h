#pragma once

#include <creative_suite/media/media_error.h>
#include <creative_suite/media/video_frame.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace creative_suite::media {

struct ForwardDecodeDiagnostics {
    bool collected = false;
    bool attempted = false;
    bool completed = false;
    bool cancelled = false;
    std::int64_t starting_frame = -1;
    std::int64_t requested_frame = -1;
    std::uint64_t discarded_intermediate_frames = 0;
    std::uint64_t elapsed_nanoseconds = 0;
    std::uint64_t packet_io_nanoseconds = 0;
    std::uint64_t decoder_receive_nanoseconds = 0;
    std::uint64_t target_pixel_conversion_nanoseconds = 0;
};

enum class DecodeTimingStage {
    PacketIo,
    DecoderReceive,
    PixelConversion,
};

enum class DecodeSeekResult : std::uint8_t {
    Succeeded,
    Failed,
};

// Optional application instrumentation. The media library remains usable
// without an observer and does not depend on any application's metrics system.
// The session does not own the observer; it must outlive the session. Callbacks
// run synchronously on the thread making the decode call.
class DecodeObserver {
public:
    virtual ~DecodeObserver() = default;
    [[nodiscard]] virtual bool is_enabled() const noexcept = 0;
    virtual void record_timing(
        DecodeTimingStage stage,
        std::uint64_t nanoseconds) noexcept = 0;
    virtual void record_discarded_frame() noexcept = 0;
    // Reports an actual FFmpeg timestamp-seek call. A default implementation
    // keeps existing consumers source-compatible when they do not need this
    // diagnostic.
    virtual void record_timestamp_seek(
        DecodeSeekResult result,
        std::uint64_t nanoseconds) noexcept {
        (void)result;
        (void)nanoseconds;
    }
};

class VideoPlaybackSession final {
public:
    using CancellationPredicate = std::function<bool()>;

    struct CacheSnapshot {
        std::uint64_t entries = 0;
        std::uint64_t bytes = 0;
    };

    // A session owns mutable decoder/cache state and is not safe for concurrent
    // calls. Returned frame pointers share immutable, owned RGBA frame data.
    // Throws MediaError when the input cannot be opened or initialized.
    static std::unique_ptr<VideoPlaybackSession> open(
        const std::filesystem::path& source_path,
        DecodeObserver* observer = nullptr);

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
    struct Impl;

    explicit VideoPlaybackSession(std::unique_ptr<Impl> impl);

    static std::unique_ptr<Impl> openImpl(
        const std::filesystem::path& source_path,
        DecodeObserver* observer);
    static bool decodeRawNextFrame(
        Impl& impl,
        ForwardDecodeDiagnostics* diagnostics = nullptr);
    static bool decodeNextFrame(
        Impl& impl,
        VideoFramePtr* output_frame,
        ForwardDecodeDiagnostics* diagnostics = nullptr);
    static bool discardNextFrame(
        Impl& impl,
        ForwardDecodeDiagnostics* diagnostics = nullptr);
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

} // namespace creative_suite::media
