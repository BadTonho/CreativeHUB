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

} // namespace motion::ui::detail
