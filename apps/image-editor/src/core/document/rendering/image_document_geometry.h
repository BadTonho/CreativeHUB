#pragma once

#include "image_document_store.h"

#include <QPainterPath>
#include <QTransform>

namespace image_editor {

// Stateless coordinate and clipping helpers shared by document edit paths.
class ImageDocumentGeometry final {
public:
    [[nodiscard]] static QPointF transformPoint(
        QPointF point, const ImageOperation& operation,
        const QSize& canvas_size, bool inverse = false);

    [[nodiscard]] static QTransform operationTransform(
        const ImageOperation& operation, const QSize& canvas_size,
        bool inverse = false);

    static void mapStrokeGeometryThroughGroup(
        QVector<QPointF>* points,
        std::optional<QPainterPath>* clipping_path,
        const ImageGroupData* parent,
        const QSize& canvas_size);

    [[nodiscard]] static bool isValidStrokeClipPath(
        const QPainterPath& path);

    [[nodiscard]] static QPainterPath imageBoundsPath(const QSize& size);
};

} // namespace image_editor
