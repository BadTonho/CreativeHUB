#include "../src/model/backup_manager.h"
#include "../src/model/activity_manager.h"
#include "../src/diagnostics/hub_logger.h"
#include "../../../cmake/test_support/test_check.h"
#include <iostream>
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>

using namespace creative_suite::hub;

namespace {

QByteArray readBytes(const QString& path) {
    QFile file(path);
    const bool opened = file.open(QIODevice::ReadOnly);
    CS_TEST_CHECK(opened);
    return file.readAll();
}

void writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly);
    CS_TEST_CHECK(opened);
    const auto written = file.write(bytes);
    CS_TEST_CHECK(written == bytes.size());
}

void testImageRelativeMediaBackup(const QString& root) {
    const QString projectDir = QDir(root).filePath(QStringLiteral("original project"));
    const bool created = QDir().mkpath(projectDir);
    CS_TEST_CHECK(created);
    const QString imagePath = QDir(projectDir).filePath(QStringLiteral("source image.png"));
    writeBytes(imagePath, "original raster bytes");
    const QString projectPath = QDir(projectDir).filePath(QStringLiteral("image.cimg"));
    const QJsonObject document{
        {"format", "creative-suite-image-document"}, {"version", 16},
        {"base", QJsonObject{{"kind", "source_image"}, {"path", "source image.png"},
                             {"width", 2}, {"height", 2}}},
        {"operations", QJsonArray{}}, {"layers", QJsonArray{}},
        {"contributor_note", "Keep this unknown field"}};
    const auto original = QJsonDocument(document).toJson();
    writeBytes(projectPath, original);

    BackupManager manager(QDir(root).filePath(QStringLiteral("image vault")));
    QString backupPath;
    QString error;
    const bool backedUp = manager.createBackup(projectPath, &backupPath, &error);
    CS_TEST_CHECK(backedUp);
    const auto backedUpDocument = QJsonDocument::fromJson(readBytes(backupPath)).object();
    CS_TEST_CHECK(backedUpDocument.value("base").toObject().value("path").toString() == imagePath);
    CS_TEST_CHECK(backedUpDocument.value("version") == document.value("version"));
    CS_TEST_CHECK(backedUpDocument.value("contributor_note") == document.value("contributor_note"));
    CS_TEST_CHECK(readBytes(projectPath) == original);
    CS_TEST_CHECK(readBytes(imagePath) == "original raster bytes");

    auto absoluteDocument = document;
    auto base = absoluteDocument.value("base").toObject();
    base.insert("path", imagePath);
    absoluteDocument.insert("base", base);
    const auto absoluteBytes = QJsonDocument(absoluteDocument).toJson(QJsonDocument::Compact) + "\n\n";
    const auto absolutePath = QDir(projectDir).filePath("recognized.other");
    writeBytes(absolutePath, absoluteBytes);
    const bool absoluteBackedUp = manager.createBackup(absolutePath, &backupPath, &error);
    CS_TEST_CHECK(absoluteBackedUp);
    CS_TEST_CHECK(readBytes(backupPath) == absoluteBytes);

    base.insert("path", "missing image.png");
    absoluteDocument.insert("base", base);
    const auto missingPath = QDir(projectDir).filePath("offline.cimg");
    writeBytes(missingPath, QJsonDocument(absoluteDocument).toJson());
    const bool offlineBackedUp = manager.createBackup(missingPath, &backupPath, &error);
    CS_TEST_CHECK(offlineBackedUp);
    CS_TEST_CHECK(QJsonDocument::fromJson(readBytes(backupPath)).object()
        .value("base").toObject().value("path").toString() == QDir(projectDir).filePath("missing image.png"));
}

void testRejectedBackups(const QString& root) {
    const QString vault = QDir(root).filePath("rejection vault");
    BackupManager manager(vault);
    int notifications = 0;
    QObject::connect(&manager, &BackupManager::backupCreated, [&notifications](const BackupRecord&) { ++notifications; });
    const std::vector<std::pair<QString, QByteArray>> invalid{
        {"corrupt.cimg", "not JSON"},
        {"future.csp", R"({"format":"creative-suite.main-editor","version":25})"},
        {"future.motion", R"({"format":"creative-suite.motion-studio","version":7})"},
        {"future.csie", R"({"format":"creative-suite-image-document","version":17})"},
        {"fractional.cimg", R"({"format":"creative-suite-image-document","version":1.5})"},
        {"bad-path.cimg", R"({"format":"creative-suite-image-document","version":16,"base":{"kind":"source_image","path":42}})"},
        {"bad-media.csve", R"({"format":"creative-suite.main-editor","version":24,"media":{}})"},
        {"escape.csme", R"({"format":"creative-suite.motion-studio","version":6,"media_pool":{"items":[{"path":"../outside.png"}]}})"},
        {"wrong-format.csp", "{}"},
    };
    for (const auto& [name, bytes] : invalid) {
        const auto path = QDir(root).filePath(name);
        writeBytes(path, bytes);
        QString output = "stale output";
        QString error = "stale error";
        HubLogger::instance().clear();
        const bool backedUp = manager.createBackup(path, &output, &error);
        CS_TEST_CHECK(!backedUp);
        CS_TEST_CHECK(output.isEmpty());
        CS_TEST_CHECK(!error.isEmpty() && error != "stale error");
        CS_TEST_CHECK(manager.totalBackupsCount() == 0);
        CS_TEST_CHECK(readBytes(path) == bytes);
        const auto logs = HubLogger::instance().entries();
        CS_TEST_CHECK(!logs.empty());
        CS_TEST_CHECK(logs.back().level == LogLevel::Error);
        CS_TEST_CHECK(logs.back().subsystem == "BackupManager");
        CS_TEST_CHECK(logs.back().operation == "createBackup");
        CS_TEST_CHECK(logs.back().message == error);
        CS_TEST_CHECK(logs.back().context.contains(path));
        CS_TEST_CHECK(logs.back().timestamp.isValid());
    }
    CS_TEST_CHECK(notifications == 0);
    CS_TEST_CHECK(QDir(vault).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());

    const QString unknownPath = QDir(root).filePath("opaque.data");
    const QByteArray opaque("bytes\0\xff", 7);
    writeBytes(unknownPath, opaque);
    QString output;
    QString error = "previous failure";
    const bool copied = manager.createBackup(unknownPath, &output, &error);
    CS_TEST_CHECK(copied);
    CS_TEST_CHECK(error.isEmpty());
    CS_TEST_CHECK(readBytes(output) == opaque);
    CS_TEST_CHECK(notifications == 1);

    const QString blockedVault = QDir(root).filePath("not a directory");
    writeBytes(blockedVault, "keep these bytes");
    BackupManager blocked(blockedVault);
    output = "stale";
    const bool written = blocked.createBackup(unknownPath, &output, &error);
    CS_TEST_CHECK(!written);
    CS_TEST_CHECK(output.isEmpty() && !error.isEmpty());
    CS_TEST_CHECK(blocked.totalBackupsCount() == 0);
    CS_TEST_CHECK(readBytes(blockedVault) == "keep these bytes");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir tempDir;
    CS_TEST_CHECK(tempDir.isValid());
    ActivityManager::instance().setStoragePath(tempDir.filePath("activities.json"));
    HubLogger::instance().setLogFilePath(tempDir.filePath("hub.log"));

    const QString backupDir = tempDir.filePath(QStringLiteral("test_backups"));
    BackupManager manager(backupDir);

    CS_TEST_CHECK(manager.backupDirectory() == backupDir);
    CS_TEST_CHECK(manager.totalBackupsCount() == 0);
    CS_TEST_CHECK(manager.totalBackupsSize() == 0);

    // 1. Create a dummy project file
    const QString projectFile = tempDir.filePath(QStringLiteral("trailer_2026.csp"));
    {
        QFile file(projectFile);
        const bool opened = file.open(QIODevice::WriteOnly);
        CS_TEST_CHECK(opened);
        file.write(R"({"format":"creative-suite.main-editor","version":24,"media":[],"timeline":{"tracks":[]}})");
        file.close();
    }

    // 2. Create backup
    QString outBackupPath;
    QString outError;
    const bool ok = manager.createBackup(projectFile, &outBackupPath, &outError);
    CS_TEST_CHECK(ok);
    CS_TEST_CHECK(!outBackupPath.isEmpty());
    CS_TEST_CHECK(outError.isEmpty());
    CS_TEST_CHECK(QFile::exists(outBackupPath));

    // Verify backup filename format
    const QString backupFileName = QFileInfo(outBackupPath).fileName();
    CS_TEST_CHECK(backupFileName.startsWith(QStringLiteral("trailer_2026_backup_")));
    CS_TEST_CHECK(backupFileName.endsWith(QStringLiteral(".csp")));

    // 3. Inspect backup list and stats
    CS_TEST_CHECK(manager.totalBackupsCount() == 1);
    CS_TEST_CHECK(manager.totalBackupsSize() > 0);

    const auto allBackups = manager.listBackups();
    CS_TEST_CHECK(allBackups.size() == 1);
    CS_TEST_CHECK(allBackups[0].projectName == QStringLiteral("trailer_2026"));
    CS_TEST_CHECK(allBackups[0].fileSizeBytes == QFileInfo(projectFile).size());

    // 4. Project-specific backup filter
    const auto projectBackups = manager.backupsForProject(projectFile);
    CS_TEST_CHECK(projectBackups.size() == 1);
    CS_TEST_CHECK(projectBackups[0].backupFilePath == outBackupPath);

    const auto unrelatedBackups = manager.backupsForProject(QStringLiteral("other_project.csp"));
    CS_TEST_CHECK(unrelatedBackups.empty());

    // 5. Attempt backup of non-existent project file
    QString badBackupPath;
    QString badError;
    const bool failResult = manager.createBackup(QStringLiteral("/path/does/not/exist.csp"), &badBackupPath, &badError);
    CS_TEST_CHECK(!failResult);
    CS_TEST_CHECK(!badError.isEmpty());
    CS_TEST_CHECK(badBackupPath.isEmpty());

    testImageRelativeMediaBackup(tempDir.path());
    testRejectedBackups(tempDir.path());
    std::cout << "All BackupManager tests passed successfully.\n";
    return 0;
}
