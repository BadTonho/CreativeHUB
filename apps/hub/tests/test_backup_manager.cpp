#include "../src/model/backup_manager.h"
#include <cassert>
#include <iostream>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>

using namespace creative_suite::hub;

int main() {
    QTemporaryDir tempDir;
    assert(tempDir.isValid());

    const QString backupDir = tempDir.filePath(QStringLiteral("test_backups"));
    BackupManager manager(backupDir);

    assert(manager.backupDirectory() == backupDir);
    assert(manager.totalBackupsCount() == 0);
    assert(manager.totalBackupsSize() == 0);

    // 1. Create a dummy project file
    const QString projectFile = tempDir.filePath(QStringLiteral("trailer_2026.csp"));
    {
        QFile file(projectFile);
        assert(file.open(QIODevice::WriteOnly));
        file.write("project dummy payload content");
        file.close();
    }

    // 2. Create backup
    QString outBackupPath;
    QString outError;
    const bool ok = manager.createBackup(projectFile, &outBackupPath, &outError);
    assert(ok);
    assert(!outBackupPath.isEmpty());
    assert(outError.isEmpty());
    assert(QFile::exists(outBackupPath));

    // Verify backup filename format
    const QString backupFileName = QFileInfo(outBackupPath).fileName();
    assert(backupFileName.startsWith(QStringLiteral("trailer_2026_backup_")));
    assert(backupFileName.endsWith(QStringLiteral(".csp")));

    // 3. Inspect backup list and stats
    assert(manager.totalBackupsCount() == 1);
    assert(manager.totalBackupsSize() > 0);

    const auto allBackups = manager.listBackups();
    assert(allBackups.size() == 1);
    assert(allBackups[0].projectName == QStringLiteral("trailer_2026"));
    assert(allBackups[0].fileSizeBytes == QFileInfo(projectFile).size());

    // 4. Project-specific backup filter
    const auto projectBackups = manager.backupsForProject(projectFile);
    assert(projectBackups.size() == 1);
    assert(projectBackups[0].backupFilePath == outBackupPath);

    const auto unrelatedBackups = manager.backupsForProject(QStringLiteral("other_project.csp"));
    assert(unrelatedBackups.empty());

    // 5. Attempt backup of non-existent project file
    QString badBackupPath;
    QString badError;
    const bool failResult = manager.createBackup(QStringLiteral("/path/does/not/exist.csp"), &badBackupPath, &badError);
    assert(!failResult);
    assert(!badError.isEmpty());
    assert(badBackupPath.isEmpty());

    std::cout << "All BackupManager tests passed successfully.\n";
    return 0;
}
