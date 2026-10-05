#pragma once

#include "image_document_store.h"

#include <optional>

namespace image_editor {

struct ImageDocumentObjectEditResult {
    ImageDocumentData document;
    QString selected_layer_id;
    QString selected_group_id;
    QString object_id;
};

// Prepares object edits without accessing session state, history, or resources.
class ImageDocumentObjectEditor final {
public:
    static void transformGeometry(ImageOperation* operation,
                                  const ImageOperation& transform,
                                  const QSize& canvas_size,
                                  bool inverse = false);

    [[nodiscard]] static std::optional<ImageDocumentObjectEditResult> addShape(
        const ImageDocumentData& document,
        ImageShapeData shape,
        const QSize& canvas_size,
        const QString& selected_layer_id,
        const QString& selected_group_id,
        QString* error = nullptr);
    [[nodiscard]] static std::optional<ImageDocumentObjectEditResult> addText(
        const ImageDocumentData& document,
        ImageTextData text,
        const QSize& canvas_size,
        const QString& selected_layer_id,
        const QString& selected_group_id,
        QString* error = nullptr);
    [[nodiscard]] static std::optional<ImageDocumentObjectEditResult> updateText(
        const ImageDocumentData& document,
        const ImageTextData& text,
        const QSize& canvas_size,
        const QString& selected_layer_id,
        const QString& selected_group_id,
        QString* error = nullptr);
    [[nodiscard]] static std::optional<ImageDocumentObjectEditResult> updateShape(
        const ImageDocumentData& document,
        const ImageShapeData& shape,
        const QSize& canvas_size,
        const QString& selected_layer_id,
        const QString& selected_group_id,
        QString* error = nullptr);
    [[nodiscard]] static std::optional<ImageDocumentObjectEditResult> updateShapeRendered(
        const ImageDocumentData& document,
        const ImageShapeData& rendered_shape,
        const QSize& canvas_size,
        const QString& selected_layer_id,
        const QString& selected_group_id,
        QString* error = nullptr);
    [[nodiscard]] static std::optional<ImageDocumentObjectEditResult> updateObjectsRendered(
        const ImageDocumentData& document,
        const QVector<ImageObjectPlacement>& objects,
        const QSize& canvas_size,
        const QString& selected_layer_id,
        const QString& selected_group_id,
        QString* error = nullptr);
    [[nodiscard]] static std::optional<ImageDocumentObjectEditResult> updateShapeStyles(
        const ImageDocumentData& document,
        const QStringList& shape_ids,
        const ImageShapeData& style,
        const QSize& canvas_size,
        const QString& selected_layer_id,
        const QString& selected_group_id,
        QString* error = nullptr);
    [[nodiscard]] static std::optional<ImageDocumentObjectEditResult> deleteShape(
        const ImageDocumentData& document,
        const QString& shape_id,
        const QString& selected_layer_id,
        const QString& selected_group_id);
    [[nodiscard]] static std::optional<ImageDocumentObjectEditResult> deleteObjects(
        const ImageDocumentData& document,
        const QStringList& object_ids,
        const QString& selected_layer_id,
        const QString& selected_group_id);
};

} // namespace image_editor
