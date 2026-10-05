#include "image_import_controller.h"

#include "image_export_dialog.h"

#include <QThread>

#include <atomic>
#include <memory>

namespace image_editor {

RasterImportResult ImageImportController::run(
    QWidget* parent,
    const QStringList& paths,
    ImageImportPurpose purpose) {
    auto cancellation_requested = std::make_shared<std::atomic_bool>(false);
    RasterImportResult result;

    ImageExportProgressDialog progress_dialog(parent);
    progress_dialog.setObjectName(QStringLiteral("imageImportProgressDialog"));
    progress_dialog.setWindowTitle(
        purpose == ImageImportPurpose::Relink
            ? QStringLiteral("Relinking Image")
            : QStringLiteral("Importing Images"));
    progress_dialog.setPhaseText(QStringLiteral("Decoding images…"));
    progress_dialog.setCancellationText(QStringLiteral("Cancelling image import…"));

    QObject::connect(&progress_dialog,
                     &ImageExportProgressDialog::cancelRequested,
                     &progress_dialog,
                     [cancellation_requested]() {
        cancellation_requested->store(true, std::memory_order_relaxed);
    });

    auto thread = std::unique_ptr<QThread>(QThread::create(
        [&result, paths, cancellation_requested]() {
            result = prepareRasterImport(paths, cancellation_requested.get());
        }));
    QObject::connect(thread.get(), &QThread::finished,
                     &progress_dialog, &ImageExportProgressDialog::finish);

    thread->start();
    progress_dialog.exec();
    thread->wait();

    if (cancellation_requested->load(std::memory_order_relaxed)) {
        result.status = RasterImportStatus::Cancelled;
        result.images.clear();
        result.cause.clear();
        result.failed_path.clear();
    }
    return result;
}

} // namespace image_editor
