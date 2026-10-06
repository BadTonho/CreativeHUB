#pragma once

#include <QString>
#include <QStringList>

namespace creative_suite::hub {

class StorageManager {
public:
    static QString defaultCachePath();
    static QStringList suiteCachePaths();

    static qint64 calculateDirectorySize(const QString& path);
    static qint64 calculateTotalCacheSize();

    static bool clearDirectory(const QString& path);
    static bool clearSuiteCache();

    static QString formatBytes(qint64 bytes);
};

} // namespace creative_suite::hub
