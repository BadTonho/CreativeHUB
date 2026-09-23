#pragma once

#include "../media/video_frame.h"

#include <QtGlobal>

#include <mutex>
#include <optional>

namespace playback {

struct PlaybackFramePacket {
    media::VideoFramePtr frame;
    qint64 frame_index = 0;
    quint64 generation = 0;
};

// A one-slot, latest-frame mailbox used between the playback thread and the UI.
// The mailbox never copies frame pixels; only the shared pointer is replaced.
class PlaybackFrameMailbox final {
public:
    // Returns true when an older pending packet was replaced.
    bool publish(PlaybackFramePacket packet);

    [[nodiscard]] std::optional<PlaybackFramePacket> take();

    // Returns true exactly once while a UI drain callback is outstanding.
    bool acquireDispatch();

    // Keeps the dispatch reservation when another packet arrived. Returns true
    // when the caller must enqueue another UI drain callback.
    bool finishDispatch();

    void clearPending();

private:
    std::mutex mutex_;
    std::optional<PlaybackFramePacket> pending_packet_;
    bool dispatch_scheduled_ = false;
};

} // namespace playback
