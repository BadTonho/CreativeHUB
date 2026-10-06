#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <vector>

namespace creative_suite::hub {

struct BackupRecord {
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

    bool createBackup(const QString& projectFilePath, QString* outBackupPath = nullptr, QString* outError = nullptr);
    [[nodiscard]] std::vector<BackupRecord> listBackups() const;
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
