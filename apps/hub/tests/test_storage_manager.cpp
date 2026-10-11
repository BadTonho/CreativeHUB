#include "../src/model/storage_manager.h"
#include "../src/diagnostics/hub_logger.h"
#include "../../../cmake/test_support/test_check.h"
#include <iostream>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QStandardPaths>
#include <filesystem>

using namespace creative_suite::hub;

static void writeBytes(const QString& path, int size) {
    const bool created = QDir().mkpath(QFileInfo(path).absolutePath());
    CS_TEST_CHECK(created);
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly);
    CS_TEST_CHECK(opened);
    CS_TEST_CHECK(file.write(QByteArray(size, 'c')) == size);
}

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    application.setApplicationName("hub-storage-manager-test");
    QStandardPaths::setTestModeEnabled(true);
    // 1. formatBytes tests
    CS_TEST_CHECK(StorageManager::formatBytes(0) == QStringLiteral("0 B"));
    CS_TEST_CHECK(StorageManager::formatBytes(512) == QStringLiteral("512 B"));
    CS_TEST_CHECK(StorageManager::formatBytes(1024) == QStringLiteral("1.0 KB"));
    CS_TEST_CHECK(StorageManager::formatBytes(1024 * 1024) == QStringLiteral("1.0 MB"));
    CS_TEST_CHECK(StorageManager::formatBytes(1024ULL * 1024 * 1024) == QStringLiteral("1.0 GB"));

    // 2. Directory size calculation and clearing
    QTemporaryDir tempDir;
    CS_TEST_CHECK(tempDir.isValid());
    HubLogger::instance().setLogFilePath(tempDir.filePath("hub.log"));

    const QString testDirPath = tempDir.filePath(QStringLiteral("cache_dir"));
    QDir().mkpath(testDirPath);

    CS_TEST_CHECK(StorageManager::calculateDirectorySize(testDirPath) == 0);

    const QString f1Path = QDir(testDirPath).filePath(QStringLiteral("cache1.dat"));
    const QString f2Path = QDir(testDirPath).filePath(QStringLiteral("cache2.dat"));

    writeBytes(f1Path, 1000);
    writeBytes(f2Path, 2000);

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

    const QString nested = QDir(testDirPath).filePath("nested");
    const QString blocked = QDir(nested).filePath("blocked.dat");
    const QString zero = QDir(nested).filePath("zero.dat");
    writeBytes(blocked, 2000);
    writeBytes(zero, 0);
    writeBytes(f1Path, 1000);
    writeBytes(QDir(nested).filePath(".hidden"), 50);
    const QString secondCache = tempDir.filePath("second-cache");
    writeBytes(QDir(secondCache).filePath("other.dat"), 75);
    const QString outside = tempDir.filePath("original/project.dat");
    writeBytes(outside, 5000);
    const QStringList roots{testDirPath, nested, testDirPath, secondCache};
    CS_TEST_CHECK(StorageManager::calculateCacheSize(roots) == 3125);
    HubLogger::instance().clear();
    // Simulate denied deletion deterministically on every platform/privilege level,
    // while all other removals and directory operations use the real filesystem.
    const auto denyOne = [&](const QString& path, std::error_code& error) {
        if (path == blocked) {
            error = std::make_error_code(std::errc::permission_denied);
            return false;
        }
        return std::filesystem::remove(QFileInfo(path).filesystemFilePath(), error);
    };
    const auto partial = StorageManager::clearCachePaths(roots, denyOne);
    CS_TEST_CHECK(!partial.succeeded());
    CS_TEST_CHECK(partial.removed_bytes == 1125);
    CS_TEST_CHECK(partial.remaining_bytes == 2000 && partial.remaining_size_known);
    CS_TEST_CHECK(partial.failures.size() == 1);
    CS_TEST_CHECK(partial.failures.front().path == blocked);
    CS_TEST_CHECK(partial.failures.front().error_code == static_cast<int>(std::errc::permission_denied));
    CS_TEST_CHECK(!partial.failures.front().cause.isEmpty());
    CS_TEST_CHECK(QDir(testDirPath).exists() && QDir(secondCache).exists());
    CS_TEST_CHECK(QFileInfo::exists(blocked) && QFileInfo(outside).size() == 5000);
    const auto logged = HubLogger::instance().entries();
    CS_TEST_CHECK(!logged.empty() && logged.front().level == LogLevel::Error);
    CS_TEST_CHECK(logged.front().timestamp.isValid());
    CS_TEST_CHECK(logged.front().context == blocked);
    CS_TEST_CHECK(logged.front().operation == "remove_cache_entry");
    CS_TEST_CHECK(logged.front().message.contains("code"));
    QFile log(tempDir.filePath("hub.log"));
    const bool logOpened = log.open(QIODevice::ReadOnly);
    CS_TEST_CHECK(logOpened);
    CS_TEST_CHECK(log.readAll().contains(blocked.toUtf8()));
    log.close();

    HubLogger::instance().clear();
    const auto retry = StorageManager::clearCachePaths(roots);
    CS_TEST_CHECK(retry.succeeded() && retry.removed_bytes == 2000 && retry.remaining_bytes == 0);
    CS_TEST_CHECK(!QFileInfo::exists(nested) && QDir(testDirPath).exists());
    for (const auto& entry : HubLogger::instance().entries()) CS_TEST_CHECK(entry.level != LogLevel::Error);

    writeBytes(zero, 0);
    const auto zeroFailure = StorageManager::clearCachePaths({testDirPath},
        [&](const QString& path, std::error_code& error) {
            error = std::make_error_code(std::errc::permission_denied);
            CS_TEST_CHECK(path == zero);
            return false;
        });
    CS_TEST_CHECK(!zeroFailure.succeeded() && zeroFailure.removed_bytes == 0 && zeroFailure.remaining_bytes == 0);
    CS_TEST_CHECK(zeroFailure.failures.size() == 1); // An empty but locked file is still a failure.
    const auto zeroRemoved = StorageManager::clearCachePaths({testDirPath});
    CS_TEST_CHECK(zeroRemoved.succeeded() && !QFileInfo::exists(zero) && zeroRemoved.removed_entries > 0);

    const auto missing = StorageManager::clearCachePaths({"", tempDir.filePath("missing"), testDirPath});
    CS_TEST_CHECK(missing.succeeded() && missing.removed_bytes == 0 && missing.remaining_bytes == 0);
    CS_TEST_CHECK(StorageManager::clearDirectory(""));
    const auto invalid = StorageManager::clearCachePaths({outside});
    CS_TEST_CHECK(!invalid.succeeded() && !invalid.remaining_size_known);
    CS_TEST_CHECK(invalid.removed_bytes == 0 && QFileInfo(outside).size() == 5000);
    CS_TEST_CHECK(!StorageManager::clearDirectory(outside));

    // Newly written cache is retained and reported, rather than counted as freed.
    writeBytes(f1Path, 10);
    const auto concurrent = StorageManager::clearCachePaths({testDirPath},
        [&](const QString& path, std::error_code& error) {
            const bool removed = std::filesystem::remove(QFileInfo(path).filesystemFilePath(), error);
            if (path == f1Path && removed) writeBytes(QDir(testDirPath).filePath("new.dat"), 20);
            return removed;
        });
    CS_TEST_CHECK(!concurrent.succeeded() && concurrent.removed_bytes == 10 && concurrent.remaining_bytes == 20);
    CS_TEST_CHECK(concurrent.failures.front().operation == "verify_cache");
    const bool finalCleared = StorageManager::clearDirectory(testDirPath);
    CS_TEST_CHECK(finalCleared);

    writeBytes(f1Path, 10);
    const auto concurrentZero = StorageManager::clearCachePaths({testDirPath},
        [&](const QString& path, std::error_code& error) {
            const bool removed = std::filesystem::remove(QFileInfo(path).filesystemFilePath(), error);
            if (path == f1Path && removed) writeBytes(QDir(testDirPath).filePath("new-zero.dat"), 0);
            return removed;
        });
    CS_TEST_CHECK(!concurrentZero.succeeded() && concurrentZero.removed_bytes == 10 && concurrentZero.remaining_bytes == 0);
    CS_TEST_CHECK(concurrentZero.failures.front().operation == "verify_cache");
    const bool zeroCleared = StorageManager::clearDirectory(testDirPath);
    CS_TEST_CHECK(zeroCleared);

    const auto linked = QDir(testDirPath).filePath("linked-original");
    std::error_code linkError;
    std::filesystem::create_directory_symlink(QFileInfo(outside).dir().filesystemPath(),
        QFileInfo(linked).filesystemFilePath(), linkError);
    if (!linkError) {
        CS_TEST_CHECK(StorageManager::calculateDirectorySize(testDirPath) == 0);
        const auto rootLink = StorageManager::clearCachePaths({linked});
        CS_TEST_CHECK(!rootLink.succeeded() && QFileInfo::exists(outside));
        const auto childLink = StorageManager::clearCachePaths({testDirPath});
        CS_TEST_CHECK(childLink.succeeded() && childLink.removed_bytes == 0);
        CS_TEST_CHECK(!QFileInfo::exists(linked) && QFileInfo(outside).size() == 5000);
    } else {
        std::cout << "Directory symlink check pending: " << linkError.message() << '\n';
    }

    std::cout << "All StorageManager tests passed successfully.\n";
    return 0;
}
