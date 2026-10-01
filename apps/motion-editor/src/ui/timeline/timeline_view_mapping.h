#pragma once

#include "timeline_navigator_math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace motion::ui {

inline constexpr int kTimelineHeaderWidth = 180;
inline constexpr int kRulerHorizontalPadding = 20;

[[nodiscard]] inline int rulerAxisLeft(int width, int header_width) noexcept
{
    return std::min(std::max(0, header_width + kRulerHorizontalPadding),
                    std::max(0, width - kRulerHorizontalPadding));
}

[[nodiscard]] inline int rulerAxisRight(int width) noexcept
{
    return std::max(0, width - kRulerHorizontalPadding);
}

struct TimelineViewMapping {
    int width = 0;
    int header_width = 0;
    std::int64_t range_end_frame = 0;
    std::int64_t start_frame = 0;
    std::int64_t frames_per_view = 1;

    [[nodiscard]] int axisLeft() const noexcept
    {
        return rulerAxisLeft(width, header_width);
    }

    [[nodiscard]] int axisRight() const noexcept
    {
        return rulerAxisRight(width);
    }

    [[nodiscard]] int axisWidth() const noexcept
    {
        return std::max(0, axisRight() - axisLeft());
    }

    [[nodiscard]] std::int64_t viewEndFrame() const noexcept
    {
        const auto bounded_start = std::clamp(start_frame, std::int64_t{0}, range_end_frame);
        const auto last_offset = std::max<std::int64_t>(0, frames_per_view - 1);
        return std::min(range_end_frame,
            detail::saturatingFrameAdd(bounded_start, last_offset));
    }

    [[nodiscard]] std::int64_t frameAtX(int x) const noexcept
    {
        const auto bounded_start = std::clamp(start_frame, std::int64_t{0}, range_end_frame);
        const int left = axisLeft();
        const int span = axisWidth();
        if (span <= 0 || x <= left || frames_per_view <= 1) {
            return bounded_start;
        }
        const auto available = range_end_frame - bounded_start;
        if (x >= axisRight()) {
            return std::min(range_end_frame, viewEndFrame());
        }
        const long double fraction = static_cast<long double>(x - left) /
            static_cast<long double>(span);
        const long double offset_value = std::floor(
            fraction * static_cast<long double>(frames_per_view - 1) + 0.5L);
        if (offset_value >= static_cast<long double>(available)) {
            return range_end_frame;
        }
        const auto offset = static_cast<std::int64_t>(offset_value);
        return bounded_start + offset;
    }

    [[nodiscard]] int xForFrame(std::int64_t frame) const noexcept
    {
        const int left = axisLeft();
        const int span = axisWidth();
        if (span <= 0 || frames_per_view <= 1) {
            return left;
        }
        const auto bounded = std::clamp(frame, std::int64_t{0}, range_end_frame);
        if (bounded <= start_frame) {
            return left;
        }
        if (bounded - start_frame >= frames_per_view - 1) {
            return axisRight();
        }
        const auto offset = bounded - start_frame;
        const long double fraction = static_cast<long double>(offset) /
            static_cast<long double>(frames_per_view - 1);
        return left + static_cast<int>(std::llround(fraction * span));
    }
};

} // namespace motion::ui
