#include "rendering/image_document_geometry.h"

#include <cmath>

namespace image_editor {

QPointF ImageDocumentGeometry::transformPoint(
    QPointF point, const ImageOperation& operation,
    const QSize& canvas_size, bool inverse) {
    const QRect bounds = operation.transform_bounds.isValid() &&
        !operation.transform_bounds.isEmpty()
        ? operation.transform_bounds : QRect(QPoint(), canvas_size);
    if (operation.kind == OperationKind::FlipHorizontal) {
        point.setX(bounds.x() + bounds.width() - 1.0 - point.x());
    } else if (operation.kind == OperationKind::FlipVertical) {
        point.setY(bounds.y() + bounds.height() - 1.0 - point.y());
    } else if (operation.kind == OperationKind::Rotate) {
        QTransform transform;
        const qreal center_x = bounds.x() + bounds.width() / 2.0;
        const qreal center_y = bounds.y() + bounds.height() / 2.0;
        transform.translate(center_x, center_y);
        transform.rotate((inverse ? -operation.quarter_turns
                                  : operation.quarter_turns) * 90.0);
        transform.translate(-center_x, -center_y);
        point = transform.map(point);
    }
    return point;
}

QTransform ImageDocumentGeometry::operationTransform(
    const ImageOperation& operation, const QSize& canvas_size, bool inverse) {
    const QPointF origin = transformPoint({}, operation, canvas_size, inverse);
    const QPointF x_axis = transformPoint({1.0, 0.0}, operation,
                                          canvas_size, inverse) - origin;
    const QPointF y_axis = transformPoint({0.0, 1.0}, operation,
                                          canvas_size, inverse) - origin;
    return QTransform(x_axis.x(), x_axis.y(), y_axis.x(), y_axis.y(),
                      origin.x(), origin.y());
}

void ImageDocumentGeometry::mapStrokeGeometryThroughGroup(
    QVector<QPointF>* points,
    std::optional<QPainterPath>* clipping_path,
    const ImageGroupData* parent,
    const QSize& canvas_size) {
    if (parent == nullptr) return;
    for (auto operation = parent->operations.crbegin();
         operation != parent->operations.crend(); ++operation) {
        if (points != nullptr) {
            for (QPointF& point : *points) {
                point = transformPoint(point, *operation, canvas_size, true);
            }
        }
        if (clipping_path != nullptr && clipping_path->has_value()) {
            clipping_path->value() = operationTransform(
                *operation, canvas_size, true).map(clipping_path->value());
        }
    }
}

bool ImageDocumentGeometry::isValidStrokeClipPath(const QPainterPath& path) {
    if (path.isEmpty() || path.elementCount() < 1 ||
        path.elementCount() > ImageDocumentStore::kMaximumStrokeClipPathElements) {
        return false;
    }
    for (int index = 0; index < path.elementCount(); ++index) {
        const auto element = path.elementAt(index);
        if (!std::isfinite(element.x) || !std::isfinite(element.y) ||
            std::abs(element.x) > 1'000'000.0 ||
            std::abs(element.y) > 1'000'000.0) {
            return false;
        }
    }
    return true;
}

QPainterPath ImageDocumentGeometry::imageBoundsPath(const QSize& size) {
    QPainterPath path;
    path.addRect(QRectF(QPointF(0.0, 0.0), QSizeF(size)));
    return path;
}

} // namespace image_editor
