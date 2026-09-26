#include "playback_frame_mailbox.h"

#include <utility>

namespace playback {

std::optional<PlaybackFramePacket> PlaybackFrameMailbox::publish(
    PlaybackFramePacket packet) {
    std::lock_guard lock(mutex_);
    auto replaced = std::move(pending_packet_);
    pending_packet_ = std::move(packet);
    return replaced;
}

std::optional<PlaybackFramePacket> PlaybackFrameMailbox::take() {
    std::lock_guard lock(mutex_);
    if (!pending_packet_.has_value()) return std::nullopt;

    auto packet = std::move(pending_packet_);
    pending_packet_.reset();
    return packet;
}

bool PlaybackFrameMailbox::acquireDispatch() {
    std::lock_guard lock(mutex_);
    if (dispatch_scheduled_) return false;
    dispatch_scheduled_ = true;
    return true;
}

bool PlaybackFrameMailbox::finishDispatch() {
    std::lock_guard lock(mutex_);
    if (pending_packet_.has_value()) return true;
    dispatch_scheduled_ = false;
    return false;
}

std::optional<PlaybackFramePacket> PlaybackFrameMailbox::clearPending() {
    std::lock_guard lock(mutex_);
    auto pending = std::move(pending_packet_);
    pending_packet_.reset();
    return pending;
}

} // namespace playback
