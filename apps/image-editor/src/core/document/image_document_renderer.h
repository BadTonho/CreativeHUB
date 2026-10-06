#pragma once

#include "image_document_store.h"
#include "image_layer_raster_cache.h"

#include <QHash>
#include <QImage>
#include <QStringList>

#include <atomic>

namespace image_editor {

class ImageDocumentRenderer final {
public:
    [[nodiscard]] static QSize documentSize(
        const ImageDocumentData& document, const QSize& source_size) noexcept;

    [[nodiscard]] static QImage composite(
        const ImageDocumentData& document,
        const QImage& source_image,
        const QHash<QString, QImage>& raster_images,
        const QStringList& excluded_object_ids = {},
        const std::atomic_bool* cancellation_requested = nullptr,
        ImageLayerRasterCache* layer_raster_cache = nullptr);
    [[nodiscard]] static QImage selectedLayer(
        const ImageDocumentData& document,
        const QImage& source_image,
        const QHash<QString, QImage>& raster_images,
        const QString& layer_id,
        const std::atomic_bool* cancellation_requested = nullptr);
    [[nodiscard]] static QImage selectedGroup(
        const ImageDocumentData& document,
        const QImage& source_image,
        const QHash<QString, QImage>& raster_images,
        const QString& group_id,
        const std::atomic_bool* cancellation_requested = nullptr);
    // Renders the selected layer's editable pixels before visibility, opacity,
    // group composition, or its content mask is applied. Mask targets start white.
    [[nodiscard]] static QImage editableLayerTarget(
        const ImageDocumentData& document,
        const QImage& source_image,
        const QHash<QString, QImage>& raster_images,
        const QString& layer_id,
        bool mask_target);

    [[nodiscard]] static QImage layerThumbnail(
        const ImageDocumentData& document,
        const QImage& source_image,
        const QHash<QString, QImage>& raster_images,
        const ImageLayerData& layer,
        const QSize& maximum_size);
    [[nodiscard]] static QImage groupThumbnail(
        const ImageDocumentData& document,
        const QHash<QString, QImage>& raster_images,
        const ImageGroupData& group,
        const QSize& maximum_size);
    [[nodiscard]] static QHash<QString, QImage> maskThumbnails(
        const ImageDocumentData& document,
        const QImage& source_image,
        const QHash<QString, QImage>& raster_images,
        const QSize& maximum_size);
};

} // namespace image_editor
