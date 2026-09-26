#pragma once

#include "timeline_frame_rate.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace timeline {

[[nodiscard]] inline long double timelineTimeSeconds(
    std::int64_t frame,
    FrameRate rate) noexcept {
    if (frame < 0 || !validFrameRate(rate)) return 0.0L;
    return static_cast<long double>(frame) *
        static_cast<long double>(rate.denominator) /
        static_cast<long double>(rate.numerator);
}

[[nodiscard]] inline std::string formatTimelineTimecode(
    std::int64_t frame,
    FrameRate rate) {
    if (!validFrameRate(rate)) rate = FrameRate{30, 1};
    const auto milliseconds_value = std::clamp<long double>(
        std::round(
            static_cast<long double>(std::max<std::int64_t>(0, frame)) *
            1000.0L * static_cast<long double>(rate.denominator) /
            static_cast<long double>(rate.numerator)),
        0.0L,
        static_cast<long double>(std::numeric_limits<std::int64_t>::max()));
    const auto milliseconds = static_cast<std::int64_t>(milliseconds_value);
    const auto hours = milliseconds / (60 * 60 * 1000);
    const auto minutes = (milliseconds / (60 * 1000)) % 60;
    const auto seconds = (milliseconds / 1000) % 60;
    const auto remainder = milliseconds % 1000;
    auto result = std::to_string(hours);
    if (result.size() < 2) result.insert(result.begin(), 2 - result.size(), '0');
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

} // namespace timeline
