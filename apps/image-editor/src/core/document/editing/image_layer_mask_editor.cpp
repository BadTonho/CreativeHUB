#include "editing/image_layer_mask_editor.h"

#include "rendering/image_document_geometry.h"
#include "image_document_utils.h"

#include <QUuid>

#include <algorithm>
#include <cmath>
#include <utility>

namespace image_editor {
namespace {

void assignError(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
}

} // namespace

std::optional<ImageOperation> ImageLayerMaskEditor::prepareStroke(
    const ImageDocumentData& document,
    const QString& layer_id,
    const QSize& canvas_size,
    const QVector<QPointF>& points,
    const QColor& color,
    int diameter,
    std::optional<QPainterPath> clipping_path,
    ImageLayerMaskStrokeKind kind,
    QString* error) {
    if (error != nullptr) error->clear();

    const auto* layer = findLayer(document, layer_id);
    if (layer == nullptr || layer->background || !layer->mask.has_value()) {
        assignError(error, QStringLiteral(
            "Select a raster layer with a mask before painting its mask."));
        return {};
    }
    if (!color.isValid() || points.isEmpty() ||
        points.size() > ImageDocumentStore::kMaximumPaintStrokePoints ||
        diameter < 1 || diameter > ImageDocumentStore::kMaximumPaintBrushDiameter ||
        layer->mask->operations.size() >= ImageDocumentStore::kMaximumOperations) {
        assignError(error, QStringLiteral(
            "The mask stroke color, size, or operation count is invalid."));
        return {};
    }

    if (clipping_path.has_value() &&
        !ImageDocumentGeometry::isValidStrokeClipPath(*clipping_path)) {
        assignError(error, QStringLiteral(
            "The mask selection has invalid or excessive geometry."));
        return {};
    }

    const auto* parent = findGroup(document, layer->parent_group_id);
    for (const QPointF& point : points) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y()) ||
            point.x() < 0.0 || point.y() < 0.0 ||
            point.x() >= canvas_size.width() || point.y() >= canvas_size.height()) {
            assignError(error, QStringLiteral(
                "The mask stroke contains a point outside the canvas."));
            return {};
        }
    }

    QVector<QPointF> local_points = points;
    ImageDocumentGeometry::mapStrokeGeometryThroughGroup(
        &local_points, &clipping_path, parent, canvas_size);
    local_points.erase(std::remove_if(local_points.begin(), local_points.end(),
        [&canvas_size](const QPointF& point) {
            return point.x() < 0.0 || point.y() < 0.0 ||
                point.x() >= canvas_size.width() ||
                point.y() >= canvas_size.height();
        }), local_points.end());
    if (clipping_path.has_value()) {
        clipping_path = clipping_path->intersected(
            ImageDocumentGeometry::imageBoundsPath(canvas_size));
        if (!ImageDocumentGeometry::isValidStrokeClipPath(*clipping_path)) {
            return {};
        }
    }
    if (color.alpha() == 0 || local_points.isEmpty()) return {};

    const QString stroke_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ImageOperation operation;
    if (kind == ImageLayerMaskStrokeKind::Erase) {
        operation.kind = OperationKind::EraseStroke;
        operation.erase_stroke.id = stroke_id;
        operation.erase_stroke.points = std::move(local_points);
        operation.erase_stroke.diameter = diameter;
        operation.erase_stroke.clipping_path = std::move(clipping_path);
    } else {
        operation.kind = OperationKind::PaintStroke;
        operation.paint_stroke.id = stroke_id;
        operation.paint_stroke.points = std::move(local_points);
        const int gray = qGray(color.rgb());
        operation.paint_stroke.color = QColor(gray, gray, gray, color.alpha());
        operation.paint_stroke.diameter = diameter;
        operation.paint_stroke.clipping_path = std::move(clipping_path);
    }
    return operation;
}

} // namespace image_editor
