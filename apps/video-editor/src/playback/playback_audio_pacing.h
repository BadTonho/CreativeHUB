#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace playback::detail {

struct AudioPacingDecision {
    std::int64_t target_frame = 0;
    std::uint64_t deadline_catchup_frames = 0;
    std::uint64_t audio_catchup_frames = 0;
    bool audio_correction_applied = false;
};

// Keeps small audio-clock fluctuations from immediately skipping visual
// frames. The policy is intentionally allocation-free and independent of Qt
// so it can be tested with deterministic frame targets.
class AudioPacingPolicy final {
public:
    static constexpr std::int64_t kToleranceFrames = 1;
    static constexpr std::uint32_t kRequiredAheadTicks = 3;
    static constexpr std::int64_t kMaxAdditionalFramesPerTick = 1;

    void reset() noexcept { consecutive_ahead_ticks_ = 0; }

    [[nodiscard]] AudioPacingDecision selectTarget(
        std::int64_t current_frame,
        std::int64_t deadline_frame,
        std::int64_t audio_frame) noexcept {
        AudioPacingDecision decision;
        decision.target_frame = deadline_frame;

        if (audio_frame < deadline_frame) {
            // Keep the audio clock as the master when it is behind the video
            // deadline; this preserves the existing audio-sync behavior.
            consecutive_ahead_ticks_ = 0;
            decision.target_frame = audio_frame;
        } else {
            const auto drift_frames = audio_frame - deadline_frame;
            if (drift_frames > kToleranceFrames) {
                consecutive_ahead_ticks_ = std::min(
                    kRequiredAheadTicks,
                    consecutive_ahead_ticks_ + std::uint32_t{1});
            } else {
                consecutive_ahead_ticks_ = 0;
            }

            if (drift_frames > kToleranceFrames &&
                consecutive_ahead_ticks_ >= kRequiredAheadTicks) {
                const auto bounded_target = saturatingAdd(
                    deadline_frame,
                    kMaxAdditionalFramesPerTick);
                decision.target_frame = std::min(audio_frame, bounded_target);
                decision.audio_correction_applied =
                    decision.target_frame > deadline_frame;
            }
        }

        const auto total_catchup = skippedFrames(
            current_frame,
            decision.target_frame);
        if (!decision.audio_correction_applied) {
            decision.deadline_catchup_frames = total_catchup;
            return decision;
        }

        const auto deadline_catchup = skippedFrames(
            current_frame,
            deadline_frame);
        decision.deadline_catchup_frames = std::min(
            total_catchup,
            deadline_catchup);
        decision.audio_catchup_frames = total_catchup -
            decision.deadline_catchup_frames;
        return decision;
    }

private:
    [[nodiscard]] static std::uint64_t skippedFrames(
        std::int64_t current_frame,
        std::int64_t target_frame) noexcept {
        if (target_frame <= current_frame) return 0;
        const auto distance = target_frame - current_frame;
        return distance <= 1
            ? 0U
            : static_cast<std::uint64_t>(distance - 1);
    }

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

    std::uint32_t consecutive_ahead_ticks_ = 0;
};

} // namespace playback::detail
