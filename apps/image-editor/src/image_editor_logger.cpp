#include "image_editor_logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTextStream>

#include <utility>

namespace image_editor {

ImageEditorLogger::ImageEditorLogger(QString log_directory)
    : log_directory_(std::move(log_directory)) {
    if (log_directory_.isEmpty()) {
        log_directory_ = QDir(QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("logs"));
    }
}

QString ImageEditorLogger::logPath() const {
    return QDir(log_directory_).filePath(QStringLiteral("image-editor.log"));
}

void ImageEditorLogger::rotateIfNeeded() const noexcept {
    try {
        const QFileInfo current(logPath());
        if (!current.exists() || current.size() < kMaximumLogSize) return;
        QFile::remove(logPath() + QStringLiteral(".%1").arg(kMaximumLogFiles));
        for (int index = kMaximumLogFiles - 1; index >= 1; --index) {
            const QString from = logPath() + QStringLiteral(".%1").arg(index);
            const QString to = logPath() + QStringLiteral(".%1").arg(index + 1);
            if (QFileInfo::exists(from)) QFile::rename(from, to);
        }
        QFile::rename(logPath(), logPath() + QStringLiteral(".1"));
    } catch (...) {
        // Logging must not interrupt error handling.
    }
}

void ImageEditorLogger::logError(const QString& operation,
                                 const QString& cause,
                                 const QString& path,
                                 int error_code) const noexcept {
    try {
        QDir().mkpath(log_directory_);
        rotateIfNeeded();

        QJsonObject entry;
        entry.insert("timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        entry.insert("severity", "error");
        entry.insert("subsystem", "image_editor");
        entry.insert("operation", operation);
        entry.insert("cause", cause);
        if (!path.isEmpty()) entry.insert("path", path);
        if (error_code != 0) entry.insert("error_code", error_code);
        else entry.insert("error_code", QStringLiteral("unavailable"));

        QFile file(logPath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            qWarning("Image Editor could not write its error log.");
            return;
        }
        file.write(QJsonDocument(entry).toJson(QJsonDocument::Compact));
        file.write("\n");
    } catch (...) {
        qWarning("Image Editor error logging failed.");
    }
}

} // namespace image_editor
