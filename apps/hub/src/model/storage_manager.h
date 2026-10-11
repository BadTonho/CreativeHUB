#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <system_error>

namespace creative_suite::hub {

struct CacheCleanupFailure {
    QString path;
    QString operation;
    QString cause;
    int error_code{0};
};

struct CacheCleanupResult {
    // Logical bytes of regular files successfully removed, not allocated disk blocks.
    qint64 removed_bytes{0};
    qint64 removed_entries{0};
    qint64 remaining_bytes{0};
    bool remaining_size_known{true};
    QVector<CacheCleanupFailure> failures;
    [[nodiscard]] bool succeeded() const noexcept { return failures.isEmpty(); }
};

class StorageManager {
public:
    static QString defaultCachePath();
    static QStringList suiteCachePaths();

    static qint64 calculateDirectorySize(const QString& path);
    static qint64 calculateTotalCacheSize();
    static qint64 calculateCacheSize(const QStringList& paths);

    // Synchronous cleanup of caller-owned cache roots, retaining the roots.
    // Missing/empty paths are harmless. Directory links are never traversed.
    // Failures are logged before return; remaining size is a post-cleanup snapshot.
    // An optional remover must behave like filesystem::remove (including error
    // codes) and remove only the supplied entry; tests can simulate failures.
    using RemoveEntry = std::function<bool(const QString&, std::error_code&)>;
    [[nodiscard]] static CacheCleanupResult clearCachePaths(
        const QStringList& paths, RemoveEntry remove = {});

    static bool clearDirectory(const QString& path);
    static bool clearSuiteCache();

    static QString formatBytes(qint64 bytes);
};

} // namespace creative_suite::hub
