#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace playback::detail {

[[nodiscard]] inline std::int64_t timelineFrameFromAudioElapsedUsecs(
    std::int64_t origin_frame,
    std::int64_t elapsed_usecs,
    double timeline_frame_rate) noexcept {
    if (elapsed_usecs <= 0 || !std::isfinite(timeline_frame_rate) ||
        timeline_frame_rate <= 0.0) {
        return origin_frame;
    }
    const auto offset = std::floor(
        static_cast<long double>(elapsed_usecs) * timeline_frame_rate / 1'000'000.0L);
    const auto target = static_cast<long double>(origin_frame) + offset;
    if (!std::isfinite(offset) || !std::isfinite(target) ||
        target >= std::ldexp(1.0L, 63)) {
        return std::numeric_limits<std::int64_t>::max();
    }
    if (target < -std::ldexp(1.0L, 63)) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return static_cast<std::int64_t>(target);
}

[[nodiscard]] inline std::optional<std::int64_t> audioSegmentEndSample(
    std::int64_t source_start_frame,
    double source_frame_rate,
    std::int64_t timeline_duration_frames,
    double timeline_frame_rate,
    std::int64_t sample_rate) noexcept {
    if (source_start_frame < 0 || timeline_duration_frames <= 0 ||
        sample_rate <= 0 || !std::isfinite(source_frame_rate) ||
        source_frame_rate <= 0.0 || !std::isfinite(timeline_frame_rate) ||
        timeline_frame_rate <= 0.0) {
        return std::nullopt;
    }
    const auto end_sample = std::ceil((
        static_cast<long double>(source_start_frame) / source_frame_rate +
        static_cast<long double>(timeline_duration_frames) / timeline_frame_rate) *
        sample_rate);
    if (!std::isfinite(end_sample) || end_sample < 0.0L ||
        end_sample >= std::ldexp(1.0L, 63)) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(end_sample);
}

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
