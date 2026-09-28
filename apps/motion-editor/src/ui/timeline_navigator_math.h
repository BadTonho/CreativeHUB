#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace motion::ui::detail {

inline constexpr std::array<double, 22> kTimelineZoomLevels = {
    0.25, 0.50, 0.75, 1.00, 1.25, 1.50,
    2.00, 3.00, 4.00, 6.00, 8.00, 12.00,
    18.00, 27.00, 40.00, 60.00, 90.00, 135.00,
    200.00, 300.00, 400.00, 512.00};

inline constexpr int kTimelineZoomDefaultIndex = 3;
inline constexpr int kTimelineScrollResolution = 1'000'000;

[[nodiscard]] inline std::int64_t timelineRulerTickStep(
    std::int64_t frames_per_view,
    int axis_width,
    int label_width) noexcept
{
    if (frames_per_view <= 1 || axis_width <= 0) {
        return 1;
    }
    const int minimum_tick_spacing = std::max(112, label_width + 16);
    const long double target = std::max(1.0L,
        static_cast<long double>(frames_per_view - 1) * minimum_tick_spacing /
            static_cast<long double>(axis_width));
    const long double decade = std::pow(10.0L, std::floor(std::log10(target)));
    const long double scaled = target / decade;
    const long double factor = scaled <= 1.0L ? 1.0L
        : scaled <= 2.0L ? 2.0L
        : scaled <= 5.0L ? 5.0L : 10.0L;
    const long double step = std::max(1.0L, factor * decade);
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    if (step >= static_cast<long double>(maximum)) {
        return maximum;
    }
    return static_cast<std::int64_t>(std::ceil(step));
}

[[nodiscard]] inline std::string formatElapsedTime(
    std::int64_t frame,
    std::int64_t frame_rate_numerator,
    std::int64_t frame_rate_denominator)
{
    if (frame < 0 || frame_rate_numerator <= 0 || frame_rate_denominator <= 0 ||
        frame_rate_denominator > std::numeric_limits<std::int64_t>::max() / 1000) {
        return "00:00:00.000";
    }

    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto milliseconds_per_denominator = frame_rate_denominator * 1000;
    const auto frame_quotient = frame / frame_rate_numerator;
    const auto frame_remainder = frame % frame_rate_numerator;
    std::int64_t whole_seconds = frame_quotient > maximum / frame_rate_denominator
        ? maximum : frame_quotient * frame_rate_denominator;

    std::int64_t fractional_milliseconds = 0;
    if (frame_remainder <= maximum / milliseconds_per_denominator) {
        const auto fractional_product = frame_remainder * milliseconds_per_denominator;
        if (fractional_product <= maximum - frame_rate_numerator / 2) {
            fractional_milliseconds =
                (fractional_product + frame_rate_numerator / 2) /
                frame_rate_numerator;
        } else {
            const auto estimate = std::round(
                static_cast<long double>(frame_remainder) *
                static_cast<long double>(milliseconds_per_denominator) /
                static_cast<long double>(frame_rate_numerator));
            fractional_milliseconds = estimate >= static_cast<long double>(maximum)
                ? maximum : static_cast<std::int64_t>(estimate);
        }
    } else {
        const auto estimate = std::round(
            static_cast<long double>(frame_remainder) *
            static_cast<long double>(milliseconds_per_denominator) /
            static_cast<long double>(frame_rate_numerator));
        fractional_milliseconds = estimate >= static_cast<long double>(maximum)
            ? maximum : static_cast<std::int64_t>(estimate);
    }

    const auto fractional_seconds = fractional_milliseconds / 1000;
    whole_seconds = fractional_seconds > maximum - whole_seconds
        ? maximum : whole_seconds + fractional_seconds;
    const auto hours = whole_seconds / (60 * 60);
    const auto minutes = (whole_seconds / 60) % 60;
    const auto seconds = whole_seconds % 60;
    const auto remainder = fractional_milliseconds % 1000;
    auto result = std::to_string(hours);
    if (result.size() < 2) {
        result.insert(result.begin(), 2 - result.size(), '0');
    }
    result.push_back(':');
    if (minutes < 10) result.push_back('0');
    result += std::to_string(minutes);
    result.push_back(':');
    if (seconds < 10) result.push_back('0');
    result += std::to_string(seconds);
    result.push_back('.');
    if (remainder < 100) result.push_back('0');
    if (remainder < 10) result.push_back('0');
    result += std::to_string(remainder);
    return result;
}

[[nodiscard]] inline std::int64_t saturatingFrameAdd(
    std::int64_t frame,
    std::int64_t increment) noexcept
{
    if (increment <= 0) {
        return frame;
    }
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    return frame > maximum - increment ? maximum : frame + increment;
}

[[nodiscard]] inline std::int64_t framesElapsedForNanoseconds(
    std::int64_t elapsed_nanoseconds,
    std::int64_t frame_rate_numerator,
    std::int64_t frame_rate_denominator) noexcept
{
    if (elapsed_nanoseconds <= 0 || frame_rate_numerator <= 0 ||
        frame_rate_denominator <= 0) {
        return 0;
    }

    constexpr std::int64_t nanoseconds_per_second = 1'000'000'000;
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    if (frame_rate_denominator > maximum / nanoseconds_per_second) return maximum;

    const auto whole_seconds = elapsed_nanoseconds / nanoseconds_per_second;
    const auto remaining_nanoseconds = elapsed_nanoseconds % nanoseconds_per_second;
    const auto grouped_seconds = whole_seconds / frame_rate_denominator;
    if (grouped_seconds > maximum / frame_rate_numerator) return maximum;
    auto frames = grouped_seconds * frame_rate_numerator;

    const auto remainder_seconds = whole_seconds % frame_rate_denominator;
    if (remainder_seconds >
        (maximum - remaining_nanoseconds) / nanoseconds_per_second) {
        return maximum;
    }
    const auto remainder_nanoseconds =
        remainder_seconds * nanoseconds_per_second + remaining_nanoseconds;
    if (remainder_nanoseconds > maximum / frame_rate_numerator) return maximum;
    const auto fractional_frames = (remainder_nanoseconds * frame_rate_numerator) /
        (frame_rate_denominator * nanoseconds_per_second);
    return fractional_frames > maximum - frames ? maximum : frames + fractional_frames;
}

[[nodiscard]] inline std::int64_t extendRangeEndToInclude(
    std::int64_t current_end_frame,
    std::int64_t target_frame,
    std::int64_t extension_frames) noexcept
{
    if (target_frame <= current_end_frame || extension_frames <= 0) {
        return current_end_frame;
    }

    const auto distance = target_frame - current_end_frame;
    const auto complete_extensions = distance / extension_frames;
    const auto extension_count = complete_extensions +
        (distance % extension_frames == 0 ? 0 : 1);
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto available_extensions = (maximum - current_end_frame) / extension_frames;
    if (extension_count > available_extensions) return maximum;
    return current_end_frame + extension_count * extension_frames;
}

[[nodiscard]] inline std::int64_t loopFrameForElapsed(
    std::int64_t start_frame,
    std::int64_t elapsed_frames,
    std::int64_t end_frame_exclusive) noexcept
{
    if (end_frame_exclusive <= 0) return 0;
    const auto bounded_start = std::clamp(
        start_frame, std::int64_t{0}, end_frame_exclusive - 1);
    const auto offset = std::max<std::int64_t>(0, elapsed_frames) % end_frame_exclusive;
    const auto distance_to_wrap = end_frame_exclusive - offset;
    return bounded_start >= distance_to_wrap
        ? bounded_start - distance_to_wrap
        : bounded_start + offset;
}

} // namespace motion::ui::detail
