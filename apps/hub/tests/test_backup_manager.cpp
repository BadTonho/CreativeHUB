#include "../src/model/backup_manager.h"
#include "../../../cmake/test_support/test_check.h"
#include <iostream>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>

using namespace creative_suite::hub;

int main() {
    QTemporaryDir tempDir;
    CS_TEST_CHECK(tempDir.isValid());

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
        file.write("project dummy payload content");
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

    std::cout << "All BackupManager tests passed successfully.\n";
    return 0;
}
