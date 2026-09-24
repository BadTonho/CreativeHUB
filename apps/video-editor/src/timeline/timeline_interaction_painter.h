#pragma once

#include "timeline_geometry.h"

#include <QPointF>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

class QPainter;

namespace timeline {

struct TimelineInteractionPaintState {
    const std::vector<TimelineTrack>* tracks = nullptr;
    const TimelineGeometry* geometry = nullptr;
    std::optional<ClipLocation> moving_clip;
    std::optional<std::size_t> move_target_track;
    std::int64_t move_target_frame = 0;
    QPointF invalid_marker_position;
    bool media_drop_hovered = false;
    std::optional<std::size_t> drop_hover_track;
    std::optional<std::int64_t> drop_hover_frame;
    std::int64_t drop_duration_frames = 1;
    QString drop_label;
    bool drop_valid = false;
    std::optional<std::int64_t> snap_guide_frame;
};

class TimelineInteractionPainter final {
public:
    static void paint(QPainter& painter, const TimelineInteractionPaintState& state);
};

} // namespace timeline
