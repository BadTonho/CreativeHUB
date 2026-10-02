#include "image_raster_import.h"
#include "image_document_store.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>

namespace image_editor {

RasterImportResult prepareRasterImport(const QStringList& paths,
                                       const std::atomic_bool* cancellation_requested) {
    RasterImportResult result;
    const auto cancelled = [cancellation_requested]() {
        return cancellation_requested != nullptr && cancellation_requested->load(std::memory_order_relaxed);
    };
    if (cancelled()) { result.status = RasterImportStatus::Cancelled; return result; }
    if (paths.isEmpty() || paths.size() > ImageDocumentStore::kMaximumLayers) {
        result.cause = QStringLiteral("Choose a valid number of images to import.");
        return result;
    }
    QHash<QString, QImage> decoded;
    for (const auto& path : paths) {
        if (cancelled()) { result.status = RasterImportStatus::Cancelled; result.images.clear(); return result; }
        const QFileInfo file(path);
        const QString absolute = file.exists() ? file.canonicalFilePath()
            : QDir::cleanPath(file.absoluteFilePath());
        result.failed_path = absolute;
        QImage pixels = decoded.value(absolute);
        if (pixels.isNull()) {
            QImageReader reader(absolute);
            reader.setAutoTransform(true);
            reader.setDecideFormatFromContent(true);
            if (!reader.canRead()) {
                result.cause = reader.errorString();
                result.images.clear();
                return result;
            }
            const auto format = reader.format().toLower();
            if (format != "png" && format != "jpeg" && format != "jpg" && format != "bmp" &&
                format != "webp" && format != "tiff" && format != "tif") {
                result.cause = QStringLiteral("Unsupported image format.");
                result.images.clear();
                return result;
            }
            const QSize advertised = reader.size();
            if (advertised.isValid() && !ImageDocumentStore::isValidCanvasSize(advertised)) {
                result.cause = QStringLiteral("The image exceeds the supported dimensions or pixel limit.");
                result.images.clear();
                return result;
            }
            pixels = reader.read();
            if (cancelled()) { result.status = RasterImportStatus::Cancelled; result.images.clear(); return result; }
            if (pixels.isNull() || !ImageDocumentStore::isValidCanvasSize(pixels.size())) {
                result.cause = pixels.isNull() ? reader.errorString()
                    : QStringLiteral("The decoded image exceeds the supported dimensions or pixel limit.");
                result.images.clear();
                return result;
            }
            decoded.insert(absolute, pixels);
        }
        result.images.append({absolute, pixels});
    }
    result.status = RasterImportStatus::Ready;
    result.failed_path.clear();
    return result;
}

} // namespace image_editor
