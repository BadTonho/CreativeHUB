#pragma once

#include "image_document_store.h"

#include <QColor>
#include <QImage>
#include <QString>

#include <atomic>
#include <functional>

namespace image_editor {

enum class ImageExportScope {
    Composite,
    SelectedLayer
};

struct ImageExportOptions {
    int jpeg_quality = 95;
    QColor jpeg_background = Qt::white;
    ImageExportScope scope = ImageExportScope::Composite;
};

struct ImageExportSnapshot {
    QImage source_image;
    ImageDocumentData document;
    QString selected_layer_id;
};

enum class ImageExportPhase {
    Rendering,
    Encoding,
    Finalizing
};

enum class ImageExportStatus {
    Succeeded,
    Cancelled,
    Failed
};

struct ImageExportResult {
    ImageExportStatus status = ImageExportStatus::Failed;
    QString error;
};

using ImageExportProgressCallback = std::function<void(ImageExportPhase)>;

[[nodiscard]] ImageExportResult exportImageSnapshot(
    const ImageExportSnapshot& snapshot,
    const QString& output_path,
    const ImageExportOptions& options = {},
    const std::atomic_bool* cancellation_requested = nullptr,
    const ImageExportProgressCallback& progress = {});

} // namespace image_editor
