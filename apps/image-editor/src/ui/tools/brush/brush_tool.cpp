#include "brush_tool.h"

#include <QPen>
#include <QTransform>

#include <algorithm>
#include <utility>

namespace image_editor {

void BrushTool::updateCursor(const BrushToolContext& context, bool visible) {
    cursor_position_ = context.widget_position;
    cursor_visible_ = visible && context.image_target.contains(context.widget_position);
}

void BrushTool::beginStroke(const BrushToolContext& context) {
    points_.clear();
    drawing_ = true;
    appendStrokePoint(context.image_position);
    updateCursor(context, true);
}

void BrushTool::appendStrokePoint(const QPointF& point) {
    if (!points_.isEmpty() && points_.back() == point) return;
    if (points_.size() >= kMaximumPreviewPoints) {
        points_.last() = point;
        return;
    }
    points_.append(point);
}

QVector<QPointF> BrushTool::finishStroke(const BrushToolContext& context) {
    appendStrokePoint(context.image_position);
    drawing_ = false;
    updateCursor(context, true);
    return std::exchange(points_, {});
}

void BrushTool::cancelStroke() noexcept {
    drawing_ = false;
    points_.clear();
}

void BrushTool::paintStrokeOverlay(QPainter& painter,
                                   const BrushToolContext& context,
                                   const QColor& color) const {
    if (!drawing_ || points_.isEmpty() || context.zoom <= 0.0) return;

    const auto toWidget = [&context](const QPointF& point) {
        return QPointF(context.image_target.left() + point.x() * context.zoom,
                       context.image_target.top() + point.y() * context.zoom);
    };

    QPainterPath path;
    path.moveTo(toWidget(points_.front()));
    for (qsizetype i = 1; i < points_.size(); ++i) {
        path.lineTo(toWidget(points_.at(i)));
    }

    painter.save();
    painter.setClipRect(context.image_target);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (context.area_selection.has_value()) {
        QTransform image_to_widget;
        image_to_widget.translate(context.image_target.left(), context.image_target.top());
        image_to_widget.scale(context.zoom, context.zoom);
        painter.setClipPath(image_to_widget.map(*context.area_selection), Qt::IntersectClip);
    }

    QPen pen(color, context.diameter * context.zoom, Qt::SolidLine,
             Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    if (points_.size() == 1) {
        const QPointF center = toWidget(points_.front());
        const qreal radius = context.diameter * context.zoom / 2.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(center, radius, radius);
    } else {
        painter.drawPath(path);
    }
    painter.restore();
}

void BrushTool::paintCursor(QPainter& painter, const BrushToolContext& context) const {
    if (!cursor_visible_ || !context.image_target.contains(cursor_position_)) return;

    const qreal diameter = std::max<qreal>(3.0, context.diameter * context.zoom);
    const QRectF cursor(cursor_position_.x() - diameter / 2.0,
                        cursor_position_.y() - diameter / 2.0,
                        diameter, diameter);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(18, 19, 22), 3.0));
    painter.drawEllipse(cursor);
    painter.setPen(QPen(QColor(242, 244, 248), 1.0));
    painter.drawEllipse(cursor);
}

} // namespace image_editor
