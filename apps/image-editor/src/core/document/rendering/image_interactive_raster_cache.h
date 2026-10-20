#pragma once
#include "import_export/image_exporter.h"
#include <QRect>
#include <atomic>

namespace image_editor {
struct InteractiveRaster {
    QString identity;
    QImage pixels;
    qint64 previous_key = 0;
    QRect dirty;
    qint64 revision = 0;
};
struct InteractiveLayer {
    InteractiveRaster content, mask;
    int opacity = 100;
};
struct InteractiveNode {
    QVector<InteractiveLayer> layers;
    QVector<ImageOperation> transforms;
    int opacity = 100;
    bool group = false;
};
struct InteractiveComposition {
    QSize size;
    QVector<InteractiveNode> nodes;
};
// One worker owns this cache. Transient requests reuse confirmed pixels but do
// not replace them. The 64 MiB budget covers content, masks and Background.
class ImageInteractiveRasterCache final {
public:
    static constexpr std::uint64_t maximum_bytes = 64ULL * 1024 * 1024;
    [[nodiscard]] InteractiveComposition prepare(const ImageExportSnapshot& snapshot,
        const QString& document_id, bool transient = false,
        const std::atomic_bool* cancel = nullptr);
    [[nodiscard]] static QImage composite(const InteractiveComposition& composition,
        const std::atomic_bool* cancel = nullptr);
    void clear();
    void setByteLimit(std::uint64_t limit);
    [[nodiscard]] std::uint64_t retainedBytes() const noexcept { return bytes_; }
    [[nodiscard]] std::uint64_t reconstructedPixels() const noexcept { return reconstructed_pixels_; }
private:
    struct Entry {
        QVector<ImageOperation> operations;
        QHash<QString, qint64> resources;
        QImage pixels;
        std::uint64_t used = 0;
        qint64 revision = 0;
        qint64 content_revision = 0, mask_revision = 0;
        std::optional<ImageLayerOutlineData> outline;
        std::optional<ImageLayerMaskData> layer_mask;
        std::optional<ImageDocumentData> clipping_signature;
    };
    InteractiveRaster raster(const QString& identity, const QSize& size,
        const QVector<ImageOperation>& operations, const QHash<QString, QImage>& resources,
        bool mask, bool transient, const std::atomic_bool* cancel);
    QHash<QString, Entry> entries_;
    // Semantic revisions survive pixel eviction; unchanged sources need no GPU upload.
    QHash<QString, Entry> revisions_;
    qint64 next_revision_ = 0, background_revision_ = 0;
    ImageExportSnapshot background_snapshot_;
    QImage background_;
    QString document_id_;
    std::uint64_t byte_limit_ = maximum_bytes;
    std::uint64_t bytes_ = 0, clock_ = 0, reconstructed_pixels_ = 0;
};
} // namespace image_editor
