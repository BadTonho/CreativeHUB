#include "../src/model/storage_manager.h"
#include "../../../cmake/test_support/test_check.h"
#include <iostream>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

using namespace creative_suite::hub;

int main() {
    // 1. formatBytes tests
    CS_TEST_CHECK(StorageManager::formatBytes(0) == QStringLiteral("0 B"));
    CS_TEST_CHECK(StorageManager::formatBytes(512) == QStringLiteral("512 B"));
    CS_TEST_CHECK(StorageManager::formatBytes(1024) == QStringLiteral("1.0 KB"));
    CS_TEST_CHECK(StorageManager::formatBytes(1024 * 1024) == QStringLiteral("1.0 MB"));
    CS_TEST_CHECK(StorageManager::formatBytes(1024ULL * 1024 * 1024) == QStringLiteral("1.0 GB"));

    // 2. Directory size calculation and clearing
    QTemporaryDir tempDir;
    CS_TEST_CHECK(tempDir.isValid());

    const QString testDirPath = tempDir.filePath(QStringLiteral("cache_dir"));
    QDir().mkpath(testDirPath);

    CS_TEST_CHECK(StorageManager::calculateDirectorySize(testDirPath) == 0);

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
    CS_TEST_CHECK(calculatedSize == 3000);

    // 3. Clear directory
    const bool cleared = StorageManager::clearDirectory(testDirPath);
    CS_TEST_CHECK(cleared);
    CS_TEST_CHECK(StorageManager::calculateDirectorySize(testDirPath) == 0);
    CS_TEST_CHECK(QDir(testDirPath).exists());

    // 4. Suite cache paths
    const auto paths = StorageManager::suiteCachePaths();
    CS_TEST_CHECK(paths.size() >= 4);

    std::cout << "All StorageManager tests passed successfully.\n";
    return 0;
}
