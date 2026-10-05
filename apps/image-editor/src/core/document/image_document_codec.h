#pragma once

#include "image_document_store.h"

#include <QJsonObject>

namespace image_editor {

// Internal, stateless translation between document data and versioned JSON.
class ImageDocumentCodec final {
public:
    [[nodiscard]] static bool encodeDocument(
        const ImageDocumentData& document,
        const QString& document_path,
        QJsonObject* encoded,
        QString* error = nullptr);

    [[nodiscard]] static bool decodeDocument(
        const QJsonObject& encoded,
        const QString& document_path,
        ImageDocumentData* document,
        QString* error = nullptr);

    [[nodiscard]] static bool isValidCanvasSize(const QSize& size) noexcept;
    [[nodiscard]] static bool isValidRaster(
        const ImageRasterData& raster, QString* error = nullptr);
    [[nodiscard]] static bool isValidShape(
        const ImageShapeData& shape,
        const QSize& canvas_size,
        QString* error = nullptr);
    [[nodiscard]] static bool isValidText(
        const ImageTextData& text,
        const QSize& canvas_size,
        QString* error = nullptr);
};

} // namespace image_editor
