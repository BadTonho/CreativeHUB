#include "image_export_worker.h"

#include <utility>

namespace image_editor {

ImageExportWorker::ImageExportWorker(
    ImageExportSnapshot snapshot,
    QString output_path,
    ImageExportOptions options,
    std::shared_ptr<std::atomic_bool> cancellation_requested)
    : snapshot_(std::move(snapshot)),
      output_path_(std::move(output_path)),
      options_(std::move(options)),
      cancellation_requested_(std::move(cancellation_requested)) {}

void ImageExportWorker::run() {
    const auto result = exportImageSnapshot(
        snapshot_, output_path_, options_, cancellation_requested_.get(),
        [this](ImageExportPhase phase) {
            switch (phase) {
            case ImageExportPhase::Rendering:
                emit phaseChanged(QStringLiteral("Rendering image…"));
                break;
            case ImageExportPhase::Encoding:
                emit phaseChanged(QStringLiteral("Encoding image…"));
                break;
            case ImageExportPhase::Finalizing:
                emit phaseChanged(QStringLiteral("Finalizing export…"));
                break;
            }
        });
    emit finished(result.status == ImageExportStatus::Succeeded,
                  result.status == ImageExportStatus::Cancelled,
                  result.error);
}

} // namespace image_editor
