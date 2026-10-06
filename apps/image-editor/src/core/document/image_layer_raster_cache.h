#pragma once

#include "image_document_store.h"

#include <QHash>
#include <QImage>
#include <QSize>
#include <QVector>

#include <cstdint>
#include <optional>

namespace image_editor {

class ImageLayerRasterCache final {
public:
    enum class LookupState {
        Hit,
        Miss,
        Bypass,
    };

    struct LookupResult {
        LookupState state = LookupState::Miss;
        QImage pixels;
    };

    static constexpr std::uint64_t maximum_bytes = 64ULL * 1024ULL * 1024ULL;

    [[nodiscard]] LookupResult lookup(
        const ImageLayerData& layer,
        const QSize& canvas_size,
        const QHash<QString, QImage>& raster_images);
    void insert(const ImageLayerData& layer,
                const QSize& canvas_size,
                const QHash<QString, QImage>& raster_images,
                const QImage& pixels);
    void synchronize(const QVector<ImageLayerData>& layers,
                     const QSize& canvas_size,
                     const QHash<QString, QImage>& raster_images);
    void invalidateLayer(const QString& layer_id) noexcept;
    void clear() noexcept;

    [[nodiscard]] std::uint64_t retainedBytes() const noexcept {
        return retained_bytes_;
    }

private:
    struct RasterResourceSignature {
        QString id;
        qint64 cache_key = 0;

        bool operator==(const RasterResourceSignature&) const = default;
    };

    struct Entry {
        QVector<ImageOperation> operations;
        std::optional<ImageLayerMaskData> mask;
        QVector<RasterResourceSignature> raster_resources;
        QSize canvas_size;
        QImage pixels;
        std::uint64_t bytes = 0;
    };

    [[nodiscard]] static QVector<RasterResourceSignature> resourceSignatures(
        const ImageLayerData& layer,
        const QHash<QString, QImage>& raster_images);
    [[nodiscard]] static bool sameOperations(
        const QVector<ImageOperation>& left,
        const QVector<ImageOperation>& right) noexcept;
    [[nodiscard]] static bool sameMask(
        const std::optional<ImageLayerMaskData>& left,
        const std::optional<ImageLayerMaskData>& right) noexcept;
    void removeEntry(QHash<QString, Entry>::iterator entry) noexcept;

    QHash<QString, Entry> entries_;
    std::uint64_t retained_bytes_ = 0;
};

} // namespace image_editor
