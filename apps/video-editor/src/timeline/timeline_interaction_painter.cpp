#include "timeline_interaction_painter.h"

#include "timeline_drop_validation.h"

#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QRectF>

#include <algorithm>
#include <array>
#include <limits>

namespace timeline {
namespace {

QRectF previewRect(
    const TimelineGeometry& geometry,
    std::size_t track_index,
    std::int64_t start_frame,
    std::int64_t duration_frames) {
    const auto content = geometry.trackContentRect(track_index);
    if (duration_frames <= 0 || content.isEmpty()) return {};
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto start = std::max<std::int64_t>(0, start_frame);
    const auto end = start > maximum - duration_frames ? maximum : start + duration_frames;
    const auto left = geometry.contentXForFrame(start);
    const auto right = geometry.contentXForFrame(end);
    return {left, content.top(), std::max(2.0, right - left), content.height()};
}

} // namespace

void TimelineInteractionPainter::paint(
    QPainter& painter,
    const TimelineInteractionPaintState& state) {
    if (state.tracks == nullptr || state.geometry == nullptr) return;
    const auto& tracks = *state.tracks;
    const auto& geometry = *state.geometry;
    const std::array<QColor, 4> track_colors{
        QColor("#3c75ae"), QColor("#357f70"),
        QColor("#6d5ca8"), QColor("#9b6943")};

    const auto draw_ghost = [&](std::size_t track_index,
                                std::int64_t start_frame,
                                std::int64_t duration_frames,
                                const QString& label,
                                bool valid) {
        if (track_index >= tracks.size()) return;
        const auto ghost = previewRect(geometry, track_index, start_frame, duration_frames);
        if (ghost.isEmpty()) return;
        const auto base = valid
            ? track_colors[track_index % track_colors.size()]
            : QColor("#d85a5a");
        painter.save();
        painter.setPen(QPen(valid ? QColor("#9ed8ff") : QColor("#ff7777"), 2.0, Qt::DashLine));
        painter.setBrush(QColor(base.red(), base.green(), base.blue(), valid ? 92 : 115));
        painter.drawRoundedRect(ghost, 3.0, 3.0);
        if (!label.isEmpty()) {
            painter.setPen(QColor(244, 247, 251, 210));
            painter.drawText(
                ghost.adjusted(6.0, 0.0, -6.0, 0.0), Qt::AlignVCenter,
                QFontMetrics(painter.font()).elidedText(
                    label, Qt::ElideRight, std::max(1, static_cast<int>(ghost.width() - 12.0))));
        }
        painter.restore();
    };

    const auto draw_invalid_marker = [&](const QPointF& position) {
        if (tracks.empty()) return;
        const auto content = geometry.trackContentRect(0);
        const auto x = std::clamp(position.x(), content.left(), content.right());
        painter.save();
        painter.setPen(QPen(QColor("#ff7777"), 2.0, Qt::DashLine));
        painter.drawLine(
            QPointF(x, geometry.trackRect(0).top() - 4.0),
            QPointF(x, geometry.trackRect(tracks.size() - 1).bottom() + 4.0));
        painter.restore();
    };

    if (state.moving_clip.has_value()) {
        const auto location = *state.moving_clip;
        if (location.track_index < tracks.size() &&
            location.clip_index < tracks[location.track_index].clips.size()) {
            const auto& clip = tracks[location.track_index].clips[location.clip_index];
            if (state.move_target_track.has_value()) {
                const auto target = *state.move_target_track;
                const auto valid = !TimelineDropValidator::overlaps(
                    tracks, target, state.move_target_frame,
                    clip.timeline_duration_frames, location);
                const auto label = (clip.kind == ClipKind::Text
                    ? QStringLiteral("[Text] ") : QString{}) +
                    QString::fromUtf8(clip.display_name.data(),
                                      static_cast<int>(clip.display_name.size()));
                draw_ghost(target, state.move_target_frame,
                           clip.timeline_duration_frames, label, valid);
            } else {
                draw_invalid_marker(state.invalid_marker_position);
            }
        }
    }

    if (state.media_drop_hovered) {
        if (state.drop_hover_track.has_value() && state.drop_hover_frame.has_value()) {
            draw_ghost(*state.drop_hover_track, *state.drop_hover_frame,
                       state.drop_duration_frames, state.drop_label, state.drop_valid);
        } else {
            draw_invalid_marker(state.invalid_marker_position);
        }
    } else if (state.drop_hover_track.has_value() && state.drop_hover_frame.has_value() &&
               geometry.displayDuration() > 0 && *state.drop_hover_track < tracks.size()) {
        const auto content = geometry.trackContentRect(*state.drop_hover_track);
        const auto x = geometry.contentXForFrame(*state.drop_hover_frame);
        painter.setPen(QPen(QColor("#9ed8ff"), 2.0, Qt::DashLine));
        painter.drawLine(
            QPointF(x, content.top() - 4.0), QPointF(x, content.bottom() + 4.0));
        painter.setBrush(QColor("#9ed8ff"));
        painter.drawEllipse(QPointF(x, content.top() - 5.0), 3.0, 3.0);
    }

    std::optional<std::size_t> snap_track;
    if (state.moving_clip.has_value() && state.move_target_track.has_value()) {
        snap_track = state.move_target_track;
    } else if (state.media_drop_hovered && state.drop_hover_track.has_value()) {
        snap_track = state.drop_hover_track;
    }
    if (snap_track.has_value() && state.snap_guide_frame.has_value() &&
        *snap_track < tracks.size()) {
        const auto row = geometry.trackRect(*snap_track);
        const auto content = geometry.trackContentRect(*snap_track);
        const auto x = std::clamp(
            geometry.contentXForFrame(*state.snap_guide_frame), content.left(), content.right());
        painter.save();
        painter.setPen(QPen(QColor("#c7efff"), 1.5, Qt::DashLine));
        painter.drawLine(QPointF(x, row.top() - 5.0), QPointF(x, row.bottom() + 5.0));
        painter.setBrush(QColor("#c7efff"));
        painter.drawEllipse(QPointF(x, row.top() - 6.0), 2.5, 2.5);
        painter.restore();
    }
}

} // namespace timeline
