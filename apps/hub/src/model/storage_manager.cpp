#include "storage_manager.h"
#include "../diagnostics/hub_logger.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStandardPaths>

namespace creative_suite::hub {

QString StorageManager::defaultCachePath() {
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return QDir(cacheDir).filePath(QStringLiteral("creative-suite"));
}

QStringList StorageManager::suiteCachePaths() {
    QStringList paths;

    // 1. Primary suite cache & app caches
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    paths.append(defaultCachePath());
    paths.append(QDir(cacheDir).filePath(QStringLiteral("video-editor")));
    paths.append(QDir(cacheDir).filePath(QStringLiteral("image-editor")));
    paths.append(QDir(cacheDir).filePath(QStringLiteral("motion-editor")));

    // 2. Temp cache folder
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    paths.append(QDir(tempDir).filePath(QStringLiteral("creative-suite")));

    // 3. User data cache
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    paths.append(QDir(appData).filePath(QStringLiteral("cache")));

    return paths;
}

qint64 StorageManager::calculateDirectorySize(const QString& path) {
    if (path.isEmpty() || !QDir(path).exists()) {
        return 0;
    }

    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

qint64 StorageManager::calculateTotalCacheSize() {
    qint64 total = 0;
    const QStringList paths = suiteCachePaths();
    for (const auto& p : paths) {
        total += calculateDirectorySize(p);
    }
    return total;
}

bool StorageManager::clearDirectory(const QString& path) {
    if (path.isEmpty() || !QDir(path).exists()) {
        return true;
    }

    QDir dir(path);
    const QFileInfoList entries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs | QDir::Hidden);
    bool allSuccess = true;

    for (const auto& entry : entries) {
        if (entry.isDir()) {
            QDir subDir(entry.absoluteFilePath());
            if (!subDir.removeRecursively()) {
                allSuccess = false;
            }
        } else {
            if (!QFile::remove(entry.absoluteFilePath())) {
                allSuccess = false;
            }
        }
    }

    return allSuccess;
}

bool StorageManager::clearSuiteCache() {
    bool allSuccess = true;
    const QStringList paths = suiteCachePaths();
    for (const auto& p : paths) {
        if (!clearDirectory(p)) {
            allSuccess = false;
        }
    }

    HubLogger::instance().logInfo(
        QStringLiteral("StorageManager"),
        QStringLiteral("clearSuiteCache"),
        QStringLiteral("Limpeza de cache da suíte executada")
    );

    return allSuccess;
}

QString StorageManager::formatBytes(qint64 bytes) {
    if (bytes <= 0) {
        return QStringLiteral("0 B");
    }

    constexpr qint64 kKiB = 1024;
    constexpr qint64 kMiB = kKiB * 1024;
    constexpr qint64 kGiB = kMiB * 1024;

    if (bytes < kKiB) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < kMiB) {
        const double val = static_cast<double>(bytes) / static_cast<double>(kKiB);
        return QStringLiteral("%1 KB").arg(QString::number(val, 'f', 1));
    }
    if (bytes < kGiB) {
        const double val = static_cast<double>(bytes) / static_cast<double>(kMiB);
        return QStringLiteral("%1 MB").arg(QString::number(val, 'f', 1));
    }

    const double val = static_cast<double>(bytes) / static_cast<double>(kGiB);
    return QStringLiteral("%1 GB").arg(QString::number(val, 'f', 1));
}

} // namespace creative_suite::hub
