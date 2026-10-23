#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <vector>

namespace creative_suite::hub {

struct BackupRecord {
    // Canonical absolute source path; empty only for legacy flat snapshots.
    QString originalFilePath;
    QString backupFilePath;
    QString projectName;
    QDateTime timestamp{QDateTime::currentDateTime()};
    qint64 fileSizeBytes{0};
};

class BackupManager : public QObject {
    Q_OBJECT

public:
    static BackupManager& instance();

    explicit BackupManager(QObject* parent = nullptr);
    explicit BackupManager(const QString& customBackupDir, QObject* parent = nullptr);

    [[nodiscard]] static QString defaultBackupDirectory();
    [[nodiscard]] QString backupDirectory() const;
    void setBackupDirectory(const QString& dir);

    // Snapshot supported project formats with relative references resolved from
    // the source document directory. External media remains referenced; a
    // Video Editor Color Gallery sidecar is copied beside the snapshot. Returns
    // no output path on failure and never replaces an existing backup.
    // Each new snapshot owns a unique vault directory and a versioned identity record.
    bool createBackup(const QString& projectFilePath, QString* outBackupPath = nullptr, QString* outError = nullptr);
    [[nodiscard]] std::vector<BackupRecord> listBackups() const;
    // Matches persisted source paths, not names; legacy snapshots cannot be attributed.
    [[nodiscard]] std::vector<BackupRecord> backupsForProject(const QString& projectFilePath) const;
    [[nodiscard]] int totalBackupsCount() const;
    [[nodiscard]] qint64 totalBackupsSize() const;
    bool openBackupDirectory() const;

signals:
    void backupCreated(const BackupRecord& record);

private:
    void loadSettings();
    void saveSettings() const;

    QString m_backupDirectory;
};

} // namespace creative_suite::hub
