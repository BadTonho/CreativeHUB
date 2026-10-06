#include "../src/model/storage_manager.h"
#include <cassert>
#include <iostream>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

using namespace creative_suite::hub;

int main() {
    // 1. formatBytes tests
    assert(StorageManager::formatBytes(0) == QStringLiteral("0 B"));
    assert(StorageManager::formatBytes(512) == QStringLiteral("512 B"));
    assert(StorageManager::formatBytes(1024) == QStringLiteral("1.0 KB"));
    assert(StorageManager::formatBytes(1024 * 1024) == QStringLiteral("1.0 MB"));
    assert(StorageManager::formatBytes(1024ULL * 1024 * 1024) == QStringLiteral("1.0 GB"));

    // 2. Directory size calculation and clearing
    QTemporaryDir tempDir;
    assert(tempDir.isValid());

    const QString testDirPath = tempDir.filePath(QStringLiteral("cache_dir"));
    QDir().mkpath(testDirPath);

    assert(StorageManager::calculateDirectorySize(testDirPath) == 0);

    const QString f1Path = QDir(testDirPath).filePath(QStringLiteral("cache1.dat"));
    const QString f2Path = QDir(testDirPath).filePath(QStringLiteral("cache2.dat"));

    {
        QFile f1(f1Path);
        f1.open(QIODevice::WriteOnly);
        f1.write(QByteArray(1000, 'A'));
        f1.close();

        QFile f2(f2Path);
        f2.open(QIODevice::WriteOnly);
        f2.write(QByteArray(2000, 'B'));
        f2.close();
    }

    const quint64 calculatedSize = StorageManager::calculateDirectorySize(testDirPath);
    assert(calculatedSize == 3000);

    // 3. Clear directory
    const bool cleared = StorageManager::clearDirectory(testDirPath);
    assert(cleared);
    assert(StorageManager::calculateDirectorySize(testDirPath) == 0);
    assert(QDir(testDirPath).exists());

    // 4. Suite cache paths
    const auto paths = StorageManager::suiteCachePaths();
    assert(paths.size() >= 4);

    std::cout << "All StorageManager tests passed successfully.\n";
    return 0;
}
