#pragma once

#include "timeline_geometry.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace timeline {

struct SnapPlacement {
    std::int64_t start_frame = 0;
    std::optional<std::int64_t> guide_frame;
};

class TimelineDropValidator final {
public:
    [[nodiscard]] static bool overlaps(
        const std::vector<TimelineTrack>& tracks,
        std::size_t track_index,
        std::int64_t start_frame,
        std::int64_t duration_frames,
        std::optional<ClipLocation> excluded = std::nullopt) noexcept;

    [[nodiscard]] static SnapPlacement snap(
        const std::vector<TimelineTrack>& tracks,
        const TimelineGeometry& geometry,
        bool enabled,
        std::size_t track_index,
        std::int64_t raw_start_frame,
        std::int64_t duration_frames,
        std::optional<ClipLocation> excluded = std::nullopt) noexcept;
};

} // namespace timeline
