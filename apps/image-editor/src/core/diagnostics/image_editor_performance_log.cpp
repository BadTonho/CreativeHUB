#include "image_editor_performance_log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>

#include <utility>

namespace image_editor {

ImageEditorPerformanceLog::ImageEditorPerformanceLog(QString log_directory)
    : log_directory_(std::move(log_directory)) {
    if (log_directory_.isEmpty()) {
        log_directory_ = QDir(QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("logs"));
    }
}

QString ImageEditorPerformanceLog::logPath() const {
    return QDir(log_directory_).filePath(
        QStringLiteral("image-editor-performance.jsonl"));
}

bool ImageEditorPerformanceLog::rotateIfNeeded(
    qint64 incoming_bytes, QString* error) const {
    const QFileInfo current(logPath());
    if (!current.exists() ||
        (current.size() + incoming_bytes <= maximum_file_size_bytes)) return true;

    const QString oldest = logPath() + QStringLiteral(".2");
    if (QFileInfo::exists(oldest) && !QFile::remove(oldest)) {
        if (error != nullptr) *error = QStringLiteral("Could not remove the oldest performance log.");
        return false;
    }
    for (int index = maximum_file_count - 2; index >= 1; --index) {
        const QString from = logPath() + QStringLiteral(".%1").arg(index);
        const QString to = logPath() + QStringLiteral(".%1").arg(index + 1);
        if (QFileInfo::exists(from) && !QFile::rename(from, to)) {
            if (error != nullptr) *error = QStringLiteral("Could not rotate a performance log.");
            return false;
        }
    }
    if (!QFile::rename(logPath(), logPath() + QStringLiteral(".1"))) {
        if (error != nullptr) *error = QStringLiteral("Could not rotate the current performance log.");
        return false;
    }
    return true;
}

bool ImageEditorPerformanceLog::append(
    const QJsonObject& entry, QString* error) const noexcept {
    if (error != nullptr) error->clear();
    try {
        if (!QDir().mkpath(log_directory_)) {
            if (error != nullptr) *error = QStringLiteral("Could not create the performance log directory.");
            return false;
        }
        QByteArray line = QJsonDocument(entry).toJson(QJsonDocument::Compact);
        line.append('\n');
        if (line.size() > maximum_file_size_bytes) {
            if (error != nullptr) *error = QStringLiteral("Performance log entry exceeds the per-file size limit.");
            return false;
        }
        if (!rotateIfNeeded(line.size(), error)) return false;

        QFile file(logPath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            if (error != nullptr) *error = file.errorString();
            return false;
        }
        if (file.write(line) != line.size()) {
            if (error != nullptr) *error = file.errorString();
            return false;
        }
        return true;
    } catch (...) {
        if (error != nullptr) *error = QStringLiteral("Unexpected failure while writing performance metrics.");
        return false;
    }
}

} // namespace image_editor
