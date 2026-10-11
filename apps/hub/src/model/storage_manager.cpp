#include "storage_manager.h"
#include "../diagnostics/hub_logger.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>
#include <filesystem>
#include <vector>

namespace creative_suite::hub {
namespace {
namespace fs = std::filesystem;

QString entryPath(const fs::path& path) {
#ifdef Q_OS_WIN
    return QDir::fromNativeSeparators(QString::fromStdWString(path.native()));
#else
    return QString::fromUtf8(path.native().c_str());
#endif
}

QStringList uniqueRoots(const QStringList& paths) {
    QStringList roots;
    for (const auto& path : paths) {
        if (!path.isEmpty()) roots.append(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    }
    std::sort(roots.begin(), roots.end(), [](const QString& a, const QString& b) {
        return a.size() < b.size();
    });
    QStringList result;
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    for (const auto& root : roots) {
        bool covered = false;
        for (const auto& parent : result) {
            const auto prefix = parent.endsWith('/') ? parent : parent + '/';
            if (root.compare(parent, sensitivity) == 0 ||
                (root.startsWith(prefix, sensitivity) && QFileInfo(parent).isDir() &&
                 !QFileInfo(parent).isSymLink())) {
                covered = true;
                break;
            }
        }
        if (!covered) result.append(root);
    }
    return result;
}

struct SizeSnapshot {
    qint64 bytes{0};
    bool known{true};
    bool empty{true};
};

SizeSnapshot measureDirectory(const fs::path& path) {
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found) return {};
    if (error || !fs::is_directory(status)) return {0, false};
    SizeSnapshot size;
    fs::directory_iterator it(path, error), end;
    if (error) return {0, false};
    while (it != end) {
        size.empty = false;
        const auto entry = it->path();
        const auto entryStatus = fs::symlink_status(entry, error);
        if (error) {
            size.known = false;
        } else if (fs::is_directory(entryStatus)) {
            const auto child = measureDirectory(entry);
            size.bytes += child.bytes;
            size.known = size.known && child.known;
        } else if (fs::is_regular_file(entryStatus)) {
            const auto bytes = fs::file_size(entry, error);
            if (error) size.known = false;
            else size.bytes += static_cast<qint64>(bytes);
        }
        // Symlinks are entries only; target bytes do not belong to this cache.
        it.increment(error);
        if (error) { size.known = false; break; }
    }
    return size;
}

void recordFailure(CacheCleanupResult& result, const fs::path& path,
                   const QString& operation, const std::error_code& error,
                   const QString& explanation = {}) {
    const QString cause = explanation.isEmpty()
        ? QString::fromLocal8Bit(error.message().c_str()) : explanation;
    result.failures.append({entryPath(path), operation, cause, error.value()});
    HubLogger::instance().logError(QStringLiteral("StorageManager"), operation,
        QStringLiteral("Cache cleanup failed (code %1, %2): %3")
            .arg(error.value()).arg(QString::fromLatin1(error.category().name()), cause),
        entryPath(path));
}

bool cleanDirectory(const fs::path& path, CacheCleanupResult& result,
                    const StorageManager::RemoveEntry& remove) {
    std::error_code error;
    // Finish enumeration before removing entries, so the iterator cannot hold
    // a directory open during its removal on Windows.
    std::vector<fs::path> entries;
    bool complete = true;
    {
        fs::directory_iterator it(path, error), end;
        if (error) {
            recordFailure(result, path, QStringLiteral("list_cache"), error);
            return false;
        }
        while (it != end) {
            entries.push_back(it->path());
            it.increment(error);
            if (error) {
                recordFailure(result, path, QStringLiteral("list_cache"), error);
                complete = false;
                break;
            }
        }
    }
    for (const auto& entry : entries) {
        const auto status = fs::symlink_status(entry, error);
        if (error == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found) continue;
        if (error) {
            recordFailure(result, entry, QStringLiteral("inspect_cache"), error);
            complete = false;
            continue;
        }
        if (fs::is_directory(status) && !cleanDirectory(entry, result, remove)) {
            complete = false;
            continue;
        }
        qint64 bytes = 0;
        if (fs::is_regular_file(status)) {
            const auto measured = fs::file_size(entry, error);
            if (error) {
                recordFailure(result, entry, QStringLiteral("measure_cache"), error);
                complete = false;
                continue;
            }
            bytes = static_cast<qint64>(measured);
        }
        error.clear();
        const bool removed = remove(entryPath(entry), error);
        if (error) {
            recordFailure(result, entry, QStringLiteral("remove_cache_entry"), error);
            complete = false;
        } else if (removed) {
            result.removed_bytes += bytes;
            ++result.removed_entries;
        }
    }
    return complete;
}
} // namespace

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
    return path.isEmpty() ? 0 : measureDirectory(QFileInfo(path).filesystemFilePath()).bytes;
}

qint64 StorageManager::calculateTotalCacheSize() {
    return calculateCacheSize(suiteCachePaths());
}

qint64 StorageManager::calculateCacheSize(const QStringList& paths) {
    qint64 total = 0;
    for (const auto& path : uniqueRoots(paths)) total += calculateDirectorySize(path);
    return total;
}

CacheCleanupResult StorageManager::clearCachePaths(const QStringList& paths, RemoveEntry remove) {
    CacheCleanupResult result;
    if (!remove) {
        remove = [](const QString& path, std::error_code& error) {
            return fs::remove(QFileInfo(path).filesystemFilePath(), error);
        };
    }
    const auto roots = uniqueRoots(paths);
    for (const auto& root : roots) {
        const auto path = QFileInfo(root).filesystemFilePath();
        std::error_code error;
        const auto status = fs::symlink_status(path, error);
        if (error == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found) continue;
        const auto failureCount = result.failures.size();
        if (error) {
            recordFailure(result, path, QStringLiteral("inspect_cache"), error);
        } else if (!fs::is_directory(status)) {
            recordFailure(result, path, QStringLiteral("inspect_cache"),
                std::make_error_code(std::errc::invalid_argument),
                QStringLiteral("The cache root is not a regular directory; it was not removed."));
        } else {
            cleanDirectory(path, result, remove);
        }
        const auto remaining = measureDirectory(path);
        result.remaining_bytes += remaining.bytes;
        result.remaining_size_known = result.remaining_size_known && remaining.known;
        if (result.failures.size() == failureCount && (!remaining.known || !remaining.empty)) {
            recordFailure(result, path, QStringLiteral("verify_cache"),
                std::make_error_code(std::errc::resource_unavailable_try_again),
                QStringLiteral("The cache changed or could not be fully measured after cleanup."));
        }
    }
    HubLogger::instance().logInfo(QStringLiteral("StorageManager"), QStringLiteral("clearCachePaths"),
        QStringLiteral("Cache cleanup result: removed=%1 bytes, remaining=%2 bytes, remaining_size_known=%3, failures=%4, removed_entries=%5")
            .arg(result.removed_bytes).arg(result.remaining_bytes).arg(result.remaining_size_known)
            .arg(result.failures.size()).arg(result.removed_entries));
    return result;
}

bool StorageManager::clearDirectory(const QString& path) {
    return clearCachePaths({path}).succeeded();
}

bool StorageManager::clearSuiteCache() {
    return clearCachePaths(suiteCachePaths()).succeeded();
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
