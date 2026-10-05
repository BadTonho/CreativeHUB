#pragma once

#include "image_exporter.h"

#include <QObject>

#include <atomic>
#include <memory>

namespace image_editor {

class ImageExportWorker final : public QObject {
    Q_OBJECT

public:
    ImageExportWorker(ImageExportSnapshot snapshot,
                      QString output_path,
                      ImageExportOptions options,
                      std::shared_ptr<std::atomic_bool> cancellation_requested);

public slots:
    void run();

signals:
    void phaseChanged(const QString& text);
    void finished(bool succeeded, bool cancelled, const QString& error);

private:
    ImageExportSnapshot snapshot_;
    QString output_path_;
    ImageExportOptions options_;
    std::shared_ptr<std::atomic_bool> cancellation_requested_;
};

} // namespace image_editor
