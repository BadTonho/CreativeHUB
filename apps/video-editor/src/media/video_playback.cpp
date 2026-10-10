#include "video_playback.h"

#include "../rendering/preview_performance_metrics.h"

#include <chrono>
#include <memory>
#include <utility>

namespace media {
namespace {

class VideoEditorDecodeObserver final
    : public creative_suite::media::DecodeObserver {
public:
    [[nodiscard]] bool is_enabled() const noexcept override {
        return rendering::PreviewPerformanceMetrics::instance().isEnabled();
    }

    void record_timing(
        creative_suite::media::DecodeTimingStage stage,
        std::uint64_t nanoseconds) noexcept override {
        auto& metrics = rendering::PreviewPerformanceMetrics::instance();
        rendering::PreviewTiming timing = rendering::PreviewTiming::DecodePacket;
        switch (stage) {
        case creative_suite::media::DecodeTimingStage::PacketIo:
            timing = rendering::PreviewTiming::DecodePacket;
            break;
        case creative_suite::media::DecodeTimingStage::DecoderReceive:
            timing = rendering::PreviewTiming::DecodeReceive;
            break;
        case creative_suite::media::DecodeTimingStage::PixelConversion:
            timing = rendering::PreviewTiming::PixelConversion;
            break;
        }
        metrics.recordTiming(timing, std::chrono::nanoseconds(nanoseconds));
    }

    void record_discarded_frame() noexcept override {
        rendering::PreviewPerformanceMetrics::instance().recordDecodeDiscardedFrame();
    }

    void record_timestamp_seek(
        creative_suite::media::DecodeSeekResult,
        std::uint64_t) noexcept override {
        // Preserve the Video Editor's established metrics schema and playback
        // strategy; Motion Studio consumes timestamp-seek detail separately.
    }
};

VideoEditorDecodeObserver& videoEditorDecodeObserver() noexcept {
    static VideoEditorDecodeObserver observer;
    return observer;
}

} // namespace

VideoPlaybackSession::VideoPlaybackSession(std::unique_ptr<SharedSession> session)
    : session_(std::move(session)) {}

VideoPlaybackSession::~VideoPlaybackSession() = default;

VideoPlaybackSession::VideoPlaybackSession(VideoPlaybackSession&&) noexcept = default;

VideoPlaybackSession& VideoPlaybackSession::operator=(VideoPlaybackSession&&) noexcept = default;

std::unique_ptr<VideoPlaybackSession> VideoPlaybackSession::open(
    const std::filesystem::path& source_path, creative_suite::media::DecodeOptions options,
    creative_suite::media::DecodeObserver* observer) {
    return std::unique_ptr<VideoPlaybackSession>(new VideoPlaybackSession(
        SharedSession::open(source_path, options, observer ? observer : &videoEditorDecodeObserver())));
}

std::optional<VideoFramePtr> VideoPlaybackSession::decode_next_frame() {
    return session_->decode_next_frame();
}

std::optional<creative_suite::media::DecodedVideoFrame> VideoPlaybackSession::decodeFrameAtNative(
    std::int64_t frame_index, const CancellationPredicate& cancel) {
    return session_->decode_frame_at_native(frame_index, cancel);
}
std::optional<creative_suite::media::DecodedVideoFrame> VideoPlaybackSession::decodeForwardToNative(
    std::int64_t frame_index, const CancellationPredicate& cancel, ForwardDecodeDiagnostics* diagnostics) {
    return session_->decode_forward_to_native(frame_index, cancel, diagnostics);
}

std::optional<VideoFramePtr> VideoPlaybackSession::decode_forward_to(
    std::int64_t frame_index,
    const CancellationPredicate& should_cancel,
    ForwardDecodeDiagnostics* diagnostics) {
    auto* requested_diagnostics = diagnostics;
    if (diagnostics != nullptr && !videoEditorDecodeObserver().is_enabled()) {
        *diagnostics = {};
        requested_diagnostics = nullptr;
    }
    return session_->decode_forward_to(
        frame_index, should_cancel, requested_diagnostics);
}

std::optional<VideoFramePtr> VideoPlaybackSession::decode_frame_at(
    std::int64_t frame_index) {
    return session_->decode_frame_at(frame_index);
}

std::optional<VideoFramePtr> VideoPlaybackSession::decode_frame_at(
    std::int64_t frame_index,
    const CancellationPredicate& should_cancel) {
    return session_->decode_frame_at(frame_index, should_cancel);
}

std::uint64_t VideoPlaybackSession::take_cache_hit_count() noexcept {
    return session_->take_cache_hit_count();
}

VideoPlaybackSession::CacheSnapshot VideoPlaybackSession::cache_snapshot() const noexcept {
    return session_->cache_snapshot();
}

void VideoPlaybackSession::reset() {
    session_->reset();
}

void VideoPlaybackSession::setDecodeOptions(creative_suite::media::DecodeOptions options) {
    session_->set_decode_options(options);
}

creative_suite::media::DecodeAccelerationDiagnostics VideoPlaybackSession::accelerationDiagnostics() const {
    return session_->acceleration_diagnostics();
}

std::int64_t VideoPlaybackSession::current_frame_index() const noexcept {
    return session_->current_frame_index();
}

bool VideoPlaybackSession::at_end() const noexcept {
    return session_->at_end();
}

} // namespace media
