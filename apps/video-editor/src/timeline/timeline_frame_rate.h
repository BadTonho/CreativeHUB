#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>

namespace timeline {

struct FrameRate {
    std::int64_t numerator = 30;
    std::int64_t denominator = 1;

    [[nodiscard]] constexpr double asDouble() const noexcept {
        return denominator > 0
            ? static_cast<double>(numerator) / static_cast<double>(denominator)
            : 0.0;
    }

    friend bool operator==(const FrameRate&, const FrameRate&) = default;
};

// Rates are stored as a reduced rational to preserve fractional broadcast FPS.
[[nodiscard]] inline bool validFrameRate(FrameRate rate) noexcept {
    if (rate.numerator <= 0 || rate.denominator <= 0 ||
        rate.numerator > 1'000'000'000 || rate.denominator > 1'000'000) {
        return false;
    }
    const auto value = rate.asDouble();
    return std::isfinite(value) && value > 0.0 && value <= 1000.0;
}

[[nodiscard]] inline FrameRate reducedFrameRate(FrameRate rate) noexcept {
    if (rate.numerator <= 0 || rate.denominator <= 0) return {};
    const auto divisor = std::gcd(rate.numerator, rate.denominator);
    return {rate.numerator / divisor, rate.denominator / divisor};
}

[[nodiscard]] inline std::optional<FrameRate> frameRateFromDouble(
    double value) noexcept {
    if (!std::isfinite(value) || value <= 0.0 || value > 1000.0) {
        return std::nullopt;
    }

    constexpr FrameRate common_rates[] = {
        {24000, 1001}, {30000, 1001}, {60000, 1001}, {120000, 1001},
        {24, 1}, {25, 1}, {30, 1}, {48, 1}, {50, 1}, {60, 1},
        {100, 1}, {120, 1}, {240, 1},
    };
    for (const auto candidate : common_rates) {
        if (std::abs(value - candidate.asDouble()) < 0.00005) {
            return candidate;
        }
    }

    constexpr std::int64_t precision = 1'000'000;
    const auto scaled = static_cast<long double>(value) * precision;
    if (!std::isfinite(scaled) ||
        scaled > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    const auto rounded = static_cast<std::int64_t>(std::llround(scaled));
    if (rounded <= 0) return std::nullopt;
    const auto result = reducedFrameRate({rounded, precision});
    return validFrameRate(result) ? std::optional<FrameRate>(result) : std::nullopt;
}

// Maps a local Timeline-frame offset to the nearest source frame. If a source
// duration is supplied, the result is clamped to the final frame in that range.
[[nodiscard]] inline std::optional<std::int64_t> sourceFrameOffsetForTimelineFrame(
    std::int64_t timeline_frame_offset,
    double source_frame_rate,
    FrameRate timeline_frame_rate,
    std::int64_t source_duration_frames = 0) noexcept {
    if (timeline_frame_offset < 0 || !validFrameRate(timeline_frame_rate) ||
        !std::isfinite(source_frame_rate) || source_frame_rate <= 0.0 ||
        source_frame_rate > 1000.0) {
        return std::nullopt;
    }
    const long double raw =
        static_cast<long double>(timeline_frame_offset) * source_frame_rate *
        static_cast<long double>(timeline_frame_rate.denominator) /
        static_cast<long double>(timeline_frame_rate.numerator);
    if (!std::isfinite(raw) ||
        raw > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    auto result = static_cast<std::int64_t>(std::llround(raw));
    if (source_duration_frames > 0) {
        result = std::min(result, source_duration_frames - 1);
    }
    return result;
}

// Converts the full source duration to Timeline frames, rounding up so the
// converted clip does not end before the source media duration.
[[nodiscard]] inline std::optional<std::int64_t> timelineFramesForSourceDuration(
    std::int64_t source_duration_frames,
    double source_frame_rate,
    FrameRate timeline_frame_rate) noexcept {
    if (source_duration_frames <= 0 || !validFrameRate(timeline_frame_rate) ||
        !std::isfinite(source_frame_rate) || source_frame_rate <= 0.0 ||
        source_frame_rate > 1000.0) {
        return std::nullopt;
    }
    const long double raw =
        static_cast<long double>(source_duration_frames) *
        static_cast<long double>(timeline_frame_rate.numerator) /
        (static_cast<long double>(timeline_frame_rate.denominator) * source_frame_rate);
    const auto exclusive_max = std::ldexp(1.0L, 63);
    if (!std::isfinite(raw) || raw >= exclusive_max) return std::nullopt;
    const auto result = static_cast<std::int64_t>(std::ceil(raw - 1.0e-12L));
    return std::max<std::int64_t>(1, result);
}

// Maps a source-frame position to its nearest local Timeline-frame position.
[[nodiscard]] inline std::optional<std::int64_t> timelineFrameOffsetForSourceFrame(
    std::int64_t source_frame_offset,
    double source_frame_rate,
    FrameRate timeline_frame_rate) noexcept {
    if (source_frame_offset < 0 || !validFrameRate(timeline_frame_rate) ||
        !std::isfinite(source_frame_rate) || source_frame_rate <= 0.0 ||
        source_frame_rate > 1000.0) {
        return std::nullopt;
    }
    const long double raw =
        static_cast<long double>(source_frame_offset) *
        static_cast<long double>(timeline_frame_rate.numerator) /
        (static_cast<long double>(timeline_frame_rate.denominator) * source_frame_rate);
    const auto exclusive_max = std::ldexp(1.0L, 63);
    if (!std::isfinite(raw) || raw >= exclusive_max) return std::nullopt;
    return static_cast<std::int64_t>(std::llround(raw));
}

// Maximum Timeline offset whose mapped source offset cannot pass the source
// frame boundary. This is used to limit backwards trims at source frame zero.
[[nodiscard]] inline std::optional<std::int64_t> timelineFrameCapacityForSourceFrames(
    std::int64_t source_frame_count,
    double source_frame_rate,
    FrameRate timeline_frame_rate) noexcept {
    if (source_frame_count < 0 || !validFrameRate(timeline_frame_rate) ||
        !std::isfinite(source_frame_rate) || source_frame_rate <= 0.0 ||
        source_frame_rate > 1000.0) {
        return std::nullopt;
    }
    const long double raw =
        static_cast<long double>(source_frame_count) *
        static_cast<long double>(timeline_frame_rate.numerator) /
        (static_cast<long double>(timeline_frame_rate.denominator) * source_frame_rate);
    const auto exclusive_max = std::ldexp(1.0L, 63);
    if (!std::isfinite(raw) || raw >= exclusive_max) return std::nullopt;
    return static_cast<std::int64_t>(std::floor(raw + 1.0e-12L));
}

// Converts an edited Timeline duration back to source-frame storage, rounding
// up to keep the full edited Timeline interval available.
[[nodiscard]] inline std::optional<std::int64_t> sourceFramesForTimelineDuration(
    std::int64_t timeline_duration_frames,
    double source_frame_rate,
    FrameRate timeline_frame_rate) noexcept {
    if (timeline_duration_frames <= 0 || !validFrameRate(timeline_frame_rate) ||
        !std::isfinite(source_frame_rate) || source_frame_rate <= 0.0 ||
        source_frame_rate > 1000.0) {
        return std::nullopt;
    }
    const long double raw =
        static_cast<long double>(timeline_duration_frames) * source_frame_rate *
        static_cast<long double>(timeline_frame_rate.denominator) /
        static_cast<long double>(timeline_frame_rate.numerator);
    const auto exclusive_max = std::ldexp(1.0L, 63);
    if (!std::isfinite(raw) || raw >= exclusive_max) return std::nullopt;
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(raw - 1.0e-12L)));
}

} // namespace timeline
