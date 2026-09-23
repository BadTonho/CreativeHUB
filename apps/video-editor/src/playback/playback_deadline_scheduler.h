#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

namespace playback::detail {

// Keeps playback cadence on an absolute monotonic timeline. The helper is
// deliberately independent of Qt so its frame and deadline arithmetic can be
// tested deterministically.
class PlaybackDeadlineScheduler final {
public:
    using Clock = std::chrono::steady_clock;

    void start(
        Clock::time_point started_at,
        std::int64_t origin_frame,
        double frame_rate) noexcept {
        reset();
        if (!std::isfinite(frame_rate) || frame_rate <= 0.0) return;

        started_at_ = started_at;
        origin_frame_ = origin_frame;
        frame_rate_ = frame_rate;
        next_frame_offset_ = 1;
        active_ = true;
    }

    void reset() noexcept {
        active_ = false;
        started_at_ = Clock::time_point{};
        origin_frame_ = 0;
        frame_rate_ = 0.0;
        next_frame_offset_ = 0;
    }

    [[nodiscard]] bool active() const noexcept { return active_; }

    [[nodiscard]] std::int64_t targetFrame(
        Clock::time_point now) const noexcept {
        if (!active_ || now <= started_at_) return origin_frame_;

        const auto elapsed_seconds = std::chrono::duration<double>(
            now - started_at_).count();
        const double frame_offset_value = elapsed_seconds * frame_rate_;
        if (!std::isfinite(frame_offset_value) ||
            frame_offset_value >= static_cast<double>(
                std::numeric_limits<std::int64_t>::max())) {
            return saturatingAdd(
                origin_frame_,
                std::numeric_limits<std::int64_t>::max());
        }

        const auto frame_offset = static_cast<std::int64_t>(std::floor(
            std::max(0.0, frame_offset_value)));
        return saturatingAdd(origin_frame_, frame_offset);
    }

    [[nodiscard]] Clock::time_point nextDeadline() const noexcept {
        if (!active_) return Clock::time_point{};
        return deadlineForOffset(next_frame_offset_);
    }

    // Move the scheduler past every frame that was due at this callback. This
    // prevents a long decode stall from causing one immediate timer callback
    // per missed frame.
    void advanceAfterTarget(std::int64_t target_frame) noexcept {
        if (!active_ || target_frame < origin_frame_) return;

        const auto frame_offset = target_frame - origin_frame_;
        if (frame_offset >= next_frame_offset_) {
            next_frame_offset_ = frame_offset ==
                    std::numeric_limits<std::int64_t>::max()
                ? frame_offset
                : frame_offset + 1;
        }
    }

    [[nodiscard]] std::chrono::milliseconds delayUntil(
        Clock::time_point now) const noexcept {
        if (!active_) return std::chrono::milliseconds(0);

        const auto deadline = nextDeadline();
        if (now >= deadline) return std::chrono::milliseconds(0);

        const auto remaining = deadline - now;
        const auto max_delay = std::chrono::duration_cast<Clock::duration>(
            std::chrono::milliseconds(std::numeric_limits<int>::max()));
        if (remaining >= max_delay) {
            return std::chrono::milliseconds(std::numeric_limits<int>::max());
        }

        auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
            remaining);
        if (std::chrono::duration_cast<Clock::duration>(delay) < remaining) {
            ++delay;
        }
        return delay;
    }

private:
    [[nodiscard]] static std::int64_t saturatingAdd(
        std::int64_t left,
        std::int64_t right) noexcept {
        if (right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) {
            return std::numeric_limits<std::int64_t>::max();
        }
        if (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right) {
            return std::numeric_limits<std::int64_t>::min();
        }
        return left + right;
    }

    [[nodiscard]] Clock::time_point deadlineForOffset(
        std::int64_t frame_offset) const noexcept {
        if (frame_offset <= 0) return started_at_;

        const auto seconds = std::chrono::duration<double>(
            static_cast<double>(frame_offset) / frame_rate_);
        auto duration = std::chrono::duration_cast<Clock::duration>(seconds);
        if (std::chrono::duration<double>(duration) < seconds) {
            ++duration;
        }
        return started_at_ + duration;
    }

    Clock::time_point started_at_{};
    std::int64_t origin_frame_ = 0;
    double frame_rate_ = 0.0;
    std::int64_t next_frame_offset_ = 0;
    bool active_ = false;
};

} // namespace playback::detail
