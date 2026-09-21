#pragma once

#include <array>

namespace timeline {

inline constexpr std::array<double, 22> kTimelineZoomLevels = {
    0.25, 0.50, 0.75, 1.00, 1.25, 1.50,
    2.00, 3.00, 4.00, 6.00, 8.00, 12.00,
    18.00, 27.00, 40.00, 60.00, 90.00, 135.00,
    200.00, 300.00, 400.00, 512.00};

inline constexpr double kMinTimelineZoomFactor = 0.25;
inline constexpr double kMaxTimelineZoomFactor = 512.0;

} // namespace timeline
