#pragma once

#include <array>
#include <cstdint>
#include <limits>

namespace motion::ui::detail {

inline constexpr std::array<double, 22> kTimelineZoomLevels = {
    0.25, 0.50, 0.75, 1.00, 1.25, 1.50,
    2.00, 3.00, 4.00, 6.00, 8.00, 12.00,
    18.00, 27.00, 40.00, 60.00, 90.00, 135.00,
    200.00, 300.00, 400.00, 512.00};

inline constexpr int kTimelineZoomDefaultIndex = 3;
inline constexpr int kTimelineScrollResolution = 1'000'000;

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
