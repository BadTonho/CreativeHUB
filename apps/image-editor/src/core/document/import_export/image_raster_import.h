#pragma once

#include <QImage>
#include <QStringList>
#include <QVector>
#include <atomic>

namespace image_editor {

struct PreparedRasterImage {
    QString path;
    QImage image;
};

enum class RasterImportStatus { Ready, Cancelled, Failed };

struct RasterImportResult {
    RasterImportStatus status = RasterImportStatus::Failed;
    QVector<PreparedRasterImage> images;
    QString cause;
    QString failed_path;
};

// Safe on a worker thread. Results share decoded pixels for repeated paths.
[[nodiscard]] RasterImportResult prepareRasterImport(
    const QStringList& paths,
    const std::atomic_bool* cancellation_requested = nullptr);

} // namespace image_editor
