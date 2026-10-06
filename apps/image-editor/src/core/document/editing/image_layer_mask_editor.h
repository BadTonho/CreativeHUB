#pragma once

#include "image_document_store.h"

#include <optional>

namespace image_editor {

enum class ImageLayerMaskStrokeKind {
    Paint,
    Erase,
};

// Prepares mask operations without mutating the document or edit history.
class ImageLayerMaskEditor final {
public:
    [[nodiscard]] static std::optional<ImageOperation> prepareStroke(
        const ImageDocumentData& document,
        const QString& layer_id,
        const QSize& canvas_size,
        const QVector<QPointF>& points,
        const QColor& color,
        int diameter,
        std::optional<QPainterPath> clipping_path,
        ImageLayerMaskStrokeKind kind,
        QString* error = nullptr);
};

} // namespace image_editor
