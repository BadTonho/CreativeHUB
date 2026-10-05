#include "shape_tool.h"

#include <QBrush>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace image_editor {

void ShapeTool::beginGesture(const QPointF& image_position) noexcept {
    gesture_shape_ = style_;
    gesture_shape_.id.clear();
    gesture_shape_.start = image_position;
    gesture_shape_.end = image_position;
    gesture_active_ = true;
}

void ShapeTool::updateGesture(const QPointF& image_position, bool constrain) noexcept {
    if (!gesture_active_) return;
    gesture_shape_.end = constrainPoint(
        image_position, gesture_shape_.start, gesture_shape_.kind, constrain);
}

std::optional<ImageShapeData> ShapeTool::finishGesture(
    const QPointF& image_position, bool constrain) noexcept {
    if (!gesture_active_) return std::nullopt;
    updateGesture(image_position, constrain);
    const ImageShapeData completed = gesture_shape_;
    gesture_shape_ = {};
    gesture_active_ = false;
    if (!hasValidGeometry(completed)) return std::nullopt;
    return completed;
}

bool ShapeTool::cancelGesture() noexcept {
    if (!gesture_active_) return false;
    gesture_shape_ = {};
    gesture_active_ = false;
    return true;
}

std::optional<ImageShapeData> ShapeTool::preview() const {
    if (!gesture_active_) return std::nullopt;
    return gesture_shape_;
}

void ShapeTool::paintOverlay(QPainter& painter,
                             const ShapeToolRenderContext& context) const {
    if (gesture_active_) paintShape(painter, gesture_shape_, context);
}

void ShapeTool::paintShape(QPainter& painter,
                           const ImageShapeData& shape,
                           const ShapeToolRenderContext& context,
                           int opacity) {
    const QRectF& target = context.image_target;
    const qreal zoom = context.zoom;
    const auto toWidget = [&target, zoom](const QPointF& point) {
        return QPointF(target.left() + point.x() * zoom,
                       target.top() + point.y() * zoom);
    };
    const QPointF start = toWidget(shape.start);
    const QPointF end = toWidget(shape.end);

    painter.save();
    painter.setClipRect(target);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setOpacity(std::clamp(opacity, 0, 100) / 100.0);
    if (shape.kind == ImageShapeKind::Line) {
        if (shape.stroke_enabled) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(shape.stroke_color, shape.stroke_width * zoom,
                                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawLine(start, end);
        }
    } else {
        painter.setPen(shape.stroke_enabled
            ? QPen(shape.stroke_color, shape.stroke_width * zoom, Qt::SolidLine,
                   Qt::SquareCap, Qt::MiterJoin)
            : QPen(Qt::NoPen));
        painter.setBrush(shape.fill_enabled ? QBrush(shape.fill_color)
                                           : QBrush(Qt::NoBrush));
        const QRectF bounds(start, end);
        if (shape.kind == ImageShapeKind::Rectangle) painter.drawRect(bounds.normalized());
        else painter.drawEllipse(bounds.normalized());
    }
    painter.restore();
}

QPointF ShapeTool::constrainPoint(const QPointF& point,
                                  const QPointF& anchor,
                                  ImageShapeKind kind,
                                  bool constrain) noexcept {
    if (!constrain) return point;
    const QPointF delta = point - anchor;
    if (kind == ImageShapeKind::Line) {
        const qreal length = std::hypot(delta.x(), delta.y());
        const qreal angle = std::atan2(delta.y(), delta.x());
        const qreal step = std::acos(-1.0) / 4.0;
        const qreal snapped = std::round(angle / step) * step;
        return anchor + QPointF(std::cos(snapped) * length,
                                std::sin(snapped) * length);
    }
    const qreal side = std::max(std::abs(delta.x()), std::abs(delta.y()));
    return anchor + QPointF((delta.x() < 0.0 ? -1.0 : 1.0) * side,
                            (delta.y() < 0.0 ? -1.0 : 1.0) * side);
}

bool ShapeTool::hasValidGeometry(const ImageShapeData& shape) noexcept {
    if (shape.start == shape.end) return false;
    return shape.kind == ImageShapeKind::Line ||
        (shape.start.x() != shape.end.x() && shape.start.y() != shape.end.y());
}

} // namespace image_editor
