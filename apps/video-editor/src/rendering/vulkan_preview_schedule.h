#pragma once

#include "preview_frame_payload.h"
#include <optional>

namespace rendering {

// GUI-owned latest-frame mailbox. GPU completion is supplied by the window;
// this state never polls a device or owns a native surface.
class VulkanPreviewSchedule final {
public:
    struct Frame {
        creative_suite::composition::GpuTextureFramePtr image;
        quint64 trace = 0, epoch = 0;
    };

    quint64 replace(Frame frame) {
        const auto dropped = pending_ ? pending_->trace : 0;
        pending_ = std::move(frame);
        invalidate();
        return dropped;
    }
    quint64 clear() {
        const auto dropped = pending_ ? pending_->trace : 0;
        pending_.reset();
        last_ = {};
        return dropped;
    }
    quint64 setEpoch(quint64 epoch) {
        if (epoch_ == epoch) return 0;
        epoch_ = epoch;
        const auto dropped = pending_ ? pending_->trace : 0;
        pending_.reset();
        return dropped;
    }
    void invalidate() noexcept { ++revision_; }
    void setSuspended(bool suspended) noexcept {
        if (suspended_ == suspended) return;
        suspended_ = suspended;
        if (!suspended) invalidate();
    }
    bool ready() const noexcept {
        return !in_flight_ && !suspended_ && revision_ != submitted_revision_ &&
            candidate().image && candidate().epoch == epoch_;
    }
    const Frame& candidate() const noexcept { return pending_ ? *pending_ : last_; }
    const Frame& lastSubmitted() const noexcept { return last_; }
    void submitted() {
        last_ = candidate();
        in_flight_trace_ = pending_ ? pending_->trace : 0;
        in_flight_epoch_ = last_.epoch;
        pending_.reset();
        submitted_revision_ = revision_;
        in_flight_ = true;
    }
    // Stale completions release ownership, but cannot credit a new generation.
    quint64 completed() noexcept {
        in_flight_ = false;
        const auto trace = std::exchange(in_flight_trace_, 0);
        return in_flight_epoch_ == epoch_ ? trace : 0;
    }
    quint64 pendingTrace() const noexcept { return pending_ ? pending_->trace : 0; }
    quint64 retireInFlightTrace() noexcept { return std::exchange(in_flight_trace_, 0); }
    bool inFlight() const noexcept { return in_flight_; }

private:
    std::optional<Frame> pending_;
    Frame last_;
    quint64 epoch_ = 0, revision_ = 0, submitted_revision_ = 0;
    quint64 in_flight_trace_ = 0, in_flight_epoch_ = 0;
    bool in_flight_ = false, suspended_ = false;
};

} // namespace rendering
