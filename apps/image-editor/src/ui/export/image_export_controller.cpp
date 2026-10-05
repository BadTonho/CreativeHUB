#include "image_export_controller.h"

#include "image_export_worker.h"
#include "image_export_dialog.h"

#include <QThread>

#include <atomic>
#include <memory>
#include <utility>

namespace image_editor {

ImageExportResult ImageExportController::run(
    QWidget* parent,
    ImageExportSnapshot snapshot,
    QString output_path,
    ImageExportOptions options) {
    auto cancellation_requested = std::make_shared<std::atomic_bool>(false);
    QThread worker_thread;
    auto* worker = new ImageExportWorker(
        std::move(snapshot), std::move(output_path), std::move(options),
        cancellation_requested);
    worker->moveToThread(&worker_thread);

    ImageExportProgressDialog progress_dialog(parent);
    ImageExportResult result;
    QObject::connect(&worker_thread, &QThread::started,
                     worker, &ImageExportWorker::run);
    QObject::connect(worker, &ImageExportWorker::phaseChanged,
                     &progress_dialog, &ImageExportProgressDialog::setPhaseText);
    QObject::connect(&progress_dialog, &ImageExportProgressDialog::cancelRequested,
                     &progress_dialog, [cancellation_requested]() {
        cancellation_requested->store(true, std::memory_order_relaxed);
    });
    QObject::connect(worker, &ImageExportWorker::finished,
                     &progress_dialog, [&progress_dialog, &result](
                         bool succeeded, bool cancelled, const QString& error) {
        result.status = cancelled ? ImageExportStatus::Cancelled
                                  : succeeded ? ImageExportStatus::Succeeded
                                              : ImageExportStatus::Failed;
        result.error = error;
        progress_dialog.finish();
    });
    QObject::connect(worker, &ImageExportWorker::finished,
                     &worker_thread, &QThread::quit, Qt::DirectConnection);
    QObject::connect(&worker_thread, &QThread::finished,
                     worker, &QObject::deleteLater);

    worker_thread.start();
    progress_dialog.exec();
    worker_thread.wait();
    return result;
}

} // namespace image_editor
