#pragma once

#include "timeline_model.h"

#include <QPointF>
#include <QRectF>
#include <QSizeF>

#include <cstdint>
#include <optional>
#include <vector>

namespace timeline {

class TimelineGeometry final {
public:
    static constexpr double left_margin = 12.0;
    static constexpr double right_margin = 12.0;
    static constexpr double top_margin = 48.0;
    static constexpr double row_gap = 10.0;
    static constexpr double track_header_width = 142.0;
    static constexpr double standard_duration_seconds = 60.0 * 60.0;

    TimelineGeometry(
        const std::vector<TimelineTrack>& tracks,
        QSizeF bounds,
        double row_height,
        double zoom_factor,
        std::optional<std::int64_t> fixed_duration = std::nullopt,
        double timeline_frame_rate = 0.0) noexcept;

    [[nodiscard]] double frameRate() const noexcept;
    [[nodiscard]] std::int64_t totalDuration() const noexcept;
    [[nodiscard]] std::int64_t standardDuration() const noexcept;
    [[nodiscard]] std::int64_t displayDuration() const noexcept;
    [[nodiscard]] double pixelsPerFrame() const noexcept;
    [[nodiscard]] QRectF trackRect(std::size_t index) const noexcept;
    [[nodiscard]] QRectF rulerRect() const noexcept;
    [[nodiscard]] QRectF trackContentRect(std::size_t index) const noexcept;
    [[nodiscard]] QRectF clipRect(
        const TimelineClip& clip,
        std::size_t track_index) const noexcept;
    [[nodiscard]] std::optional<std::int64_t> frameAtContentX(double x) const noexcept;
    [[nodiscard]] std::optional<std::int64_t> playheadFrameAtRulerX(double x) const noexcept;
    [[nodiscard]] double contentXForFrame(std::int64_t frame) const noexcept;

private:
    const std::vector<TimelineTrack>& tracks_;
    QSizeF bounds_;
    double row_height_ = 70.0;
    double zoom_factor_ = 1.0;
    std::optional<std::int64_t> fixed_duration_;
    double timeline_frame_rate_ = 0.0;
};

class TimelineHitTester final {
public:
    [[nodiscard]] static std::optional<std::size_t> trackAt(
        const TimelineGeometry& geometry,
        std::size_t track_count,
        double y) noexcept;
    [[nodiscard]] static std::optional<ClipLocation> clipAt(
        const std::vector<TimelineTrack>& tracks,
        const TimelineGeometry& geometry,
        double x,
        double y,
        const std::optional<ClipEdgeEditPreview>& preview = std::nullopt) noexcept;
    [[nodiscard]] static std::optional<std::pair<std::size_t, std::size_t>>
    transitionPairAt(
        const std::vector<TimelineTrack>& tracks,
        const TimelineGeometry& geometry,
        double x,
        double y,
        const std::optional<ClipEdgeEditPreview>& preview = std::nullopt) noexcept;
};

} // namespace timeline
