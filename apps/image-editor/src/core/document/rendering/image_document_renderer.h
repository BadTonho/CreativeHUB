#pragma once

#include "image_document_store.h"
#include "rendering/image_layer_raster_cache.h"

#include <QHash>
#include <QImage>
#include <QStringList>

#include <atomic>
#include <functional>
#include <array>
#include <optional>
#include <cstdint>

namespace image_editor {

class ImageDocumentRenderer final {
public:
    // Worker-owned rasterization cache support. A nonempty region reconstructs
    // only that region; callers use it exclusively for local drawing operations.
    [[nodiscard]] static QImage rasterize(const QSize& size,
        const QVector<ImageOperation>& operations, const QHash<QString, QImage>& resources,
        bool mask, QImage previous = {}, const QRect& region = {},
        const std::atomic_bool* cancellation_requested = nullptr,
        const std::optional<QPainterPath>& initial_selection = {});
    [[nodiscard]] static QImage maskPixels(const QSize&,const ImageLayerMaskData&,const std::atomic_bool* = nullptr);
    [[nodiscard]] static QImage transformRaster(QImage pixels,
        const QVector<ImageOperation>& operations,
        const std::atomic_bool* cancellation_requested = nullptr);
    [[nodiscard]] static QSize documentSize(
        const ImageDocumentData& document, const QSize& source_size) noexcept;

    [[nodiscard]] static QImage composite(
        const ImageDocumentData& document,
        const QImage& source_image,
        const QHash<QString, QImage>& raster_images,
        const QStringList& excluded_object_ids = {},
        const std::atomic_bool* cancellation_requested = nullptr,
        ImageLayerRasterCache* layer_raster_cache = nullptr);
    using Histogram = std::array<std::uint64_t, 256>;
    // Combined straight RGB distribution before a Levels or Curves layer.
    [[nodiscard]] static std::optional<Histogram> adjustmentInputHistogram(
        const ImageDocumentData& document, const QImage& source_image,
        const QHash<QString, QImage>& raster_images, const QString& layer_id,
        const std::atomic_bool* cancellation_requested = nullptr);
    // Compatibility entry point for the existing Levels model contract.
    [[nodiscard]] static std::optional<Histogram> levelsHistogram(
        const ImageDocumentData& document, const QImage& source_image,
        const QHash<QString, QImage>& raster_images, const QString& layer_id,
        const std::atomic_bool* cancellation_requested = nullptr);
    // Process a base and its clipping chain before base/group opacity or group transforms.
    // Optional borrowed input supplies masked/styled member pixels on the same worker;
    // it must honor cancellation and return null on failure. No callback is retained.
    [[nodiscard]] static QImage layerChain(const ImageDocumentData& document,
        const QImage& source, const QHash<QString, QImage>& resources, const QString& base_id,
        const std::atomic_bool* cancel = nullptr,
        const std::function<QImage(const ImageLayerData&)>& input = {});
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
        const QSize& maximum_size,
        const std::atomic_bool* cancellation_requested = nullptr);
    [[nodiscard]] static QImage groupThumbnail(
        const ImageDocumentData& document,
        const QHash<QString, QImage>& raster_images,
        const ImageGroupData& group,
        const QSize& maximum_size,
        const std::atomic_bool* cancellation_requested = nullptr);
    [[nodiscard]] static QHash<QString, QImage> maskThumbnails(
        const ImageDocumentData& document,
        const QImage& source_image,
        const QHash<QString, QImage>& raster_images,
        const QSize& maximum_size);
};

} // namespace image_editor
