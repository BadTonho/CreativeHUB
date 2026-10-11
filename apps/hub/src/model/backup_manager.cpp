#include "backup_manager.h"
#include "activity_manager.h"
#include "project_backup_paths.h"
#include "../diagnostics/hub_logger.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDesktopServices>
#include <QUrl>
#include <QTemporaryFile>
#include <algorithm>

namespace creative_suite::hub {

BackupManager& BackupManager::instance() {
    static BackupManager s_instance;
    return s_instance;
}

QString BackupManager::defaultBackupDirectory() {
    const QString docDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(docDir).filePath(QStringLiteral("CreativeSuite_Backups"));
}

BackupManager::BackupManager(QObject* parent)
    : QObject(parent)
    , m_backupDirectory(defaultBackupDirectory())
{
    loadSettings();
}

BackupManager::BackupManager(const QString& customBackupDir, QObject* parent)
    : QObject(parent)
    , m_backupDirectory(customBackupDir.isEmpty() ? defaultBackupDirectory() : customBackupDir)
{
}

void BackupManager::loadSettings() {
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString settingsFile = QDir(dataDir).filePath(QStringLiteral("backup_settings.json"));
    if (!QFile::exists(settingsFile)) {
        return;
    }

    QFile file(settingsFile);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject()) {
            const QString savedDir = doc.object().value(QStringLiteral("backupDirectory")).toString();
            if (!savedDir.isEmpty()) {
                m_backupDirectory = savedDir;
            }
        }
    }
}

void BackupManager::saveSettings() const {
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataDir);
    const QString settingsFile = QDir(dataDir).filePath(QStringLiteral("backup_settings.json"));

    QFile file(settingsFile);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QJsonObject obj;
        obj.insert(QStringLiteral("backupDirectory"), m_backupDirectory);
        file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    }
}

QString BackupManager::backupDirectory() const {
    return m_backupDirectory;
}

void BackupManager::setBackupDirectory(const QString& dir) {
    if (dir.isEmpty() || m_backupDirectory == dir) {
        return;
    }
    m_backupDirectory = dir;
    saveSettings();
}

bool BackupManager::createBackup(const QString& projectFilePath, QString* outBackupPath, QString* outError) {
    if (outBackupPath) outBackupPath->clear();
    if (outError) outError->clear();
    if (projectFilePath.isEmpty() || !QFile::exists(projectFilePath)) {
        const QString err = QStringLiteral("O arquivo do projeto não existe ou o caminho é inválido: %1").arg(projectFilePath);
        if (outError) *outError = err;
        HubLogger::instance().logError(
            QStringLiteral("BackupManager"),
            QStringLiteral("createBackup"),
            err,
            projectFilePath
        );
        return false;
    }

    const auto fail = [outError, &projectFilePath](const QString& cause, const QString& context) {
        if (outError) *outError = cause;
        HubLogger::instance().logError(QStringLiteral("BackupManager"),
            QStringLiteral("createBackup"), cause, projectFilePath + QStringLiteral(" -> ") + context);
        return false;
    };
    QFile source(projectFilePath);
    if (!QFileInfo(projectFilePath).isFile())
        return fail(QStringLiteral("O caminho do projeto não é um arquivo regular."), projectFilePath);
    if (!source.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Não foi possível ler o projeto (código %1): %2")
            .arg(static_cast<int>(source.error())).arg(source.errorString()), projectFilePath);
    const auto original = source.readAll();
    if (source.error() != QFileDevice::NoError)
        return fail(QStringLiteral("Falha na leitura do projeto (código %1): %2")
            .arg(static_cast<int>(source.error())).arg(source.errorString()), projectFilePath);
    source.close();
    QByteArray prepared;
    QString referenceError;
    if (!prepareProjectBackup(original, projectFilePath, &prepared, &referenceError))
        return fail(referenceError, projectFilePath);

    if (!QDir().mkpath(m_backupDirectory)) {
        const QString err = QStringLiteral("Não foi possível criar o diretório de backups: %1").arg(m_backupDirectory);
        if (outError) *outError = err;
        HubLogger::instance().logError(
            QStringLiteral("BackupManager"),
            QStringLiteral("createBackup"),
            err,
            m_backupDirectory
        );
        return false;
    }

    const QFileInfo srcInfo(projectFilePath);
    const QString baseName = srcInfo.completeBaseName();
    const QString suffix = srcInfo.suffix();
    const QString timestampStr = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));

    const QString targetFileName = suffix.isEmpty()
        ? QStringLiteral("%1_backup_%2").arg(baseName, timestampStr)
        : QStringLiteral("%1_backup_%2.%3").arg(baseName, timestampStr, suffix);

    const QString targetPath = QDir(m_backupDirectory).filePath(targetFileName);

    // Prepare completely in the vault, then rename without replacing snapshots.
    QTemporaryFile pending(QDir(m_backupDirectory).filePath(QStringLiteral(".snapshot-XXXXXX")));
    if (!pending.open() || pending.write(prepared) != prepared.size() || !pending.flush())
        return fail(QStringLiteral("Falha ao gravar o backup (código %1): %2")
            .arg(static_cast<int>(pending.error())).arg(pending.errorString()), targetPath);
    pending.close();
    if (!pending.rename(targetPath))
        return fail(QStringLiteral("Falha ao publicar o backup (código %1): %2")
            .arg(static_cast<int>(pending.error())).arg(pending.errorString()), targetPath);
    pending.setAutoRemove(false);

    BackupRecord record;
    record.originalFilePath = projectFilePath;
    record.backupFilePath = targetPath;
    record.projectName = baseName;
    record.timestamp = QDateTime::currentDateTime();
    record.fileSizeBytes = QFileInfo(targetPath).size();

    if (outBackupPath) {
        *outBackupPath = targetPath;
    }

    HubLogger::instance().logInfo(
        QStringLiteral("BackupManager"),
        QStringLiteral("createBackup"),
        QStringLiteral("Backup de projeto criado com sucesso"),
        targetPath
    );

    ActivityManager::instance().addActivity(
        QStringLiteral("Backup Criado"),
        QStringLiteral("Cópia de segurança criada para '%1' (%2)").arg(baseName, targetFileName),
        QStringLiteral("backup")
    );

    emit backupCreated(record);
    return true;
}

std::vector<BackupRecord> BackupManager::listBackups() const {
    std::vector<BackupRecord> list;
    if (m_backupDirectory.isEmpty() || !QDir(m_backupDirectory).exists()) {
        return list;
    }

    QDir dir(m_backupDirectory);
    const QFileInfoList entries = dir.entryInfoList(QStringList() << QStringLiteral("*_backup_*"), QDir::Files, QDir::Time);

    for (const auto& info : entries) {
        BackupRecord record;
        record.backupFilePath = info.absoluteFilePath();
        const QString fileName = info.fileName();
        const int idx = fileName.indexOf(QStringLiteral("_backup_"));
        record.projectName = idx > 0 ? fileName.left(idx) : info.completeBaseName();
        record.timestamp = info.lastModified();
        record.fileSizeBytes = info.size();
        list.push_back(record);
    }

    return list;
}

std::vector<BackupRecord> BackupManager::backupsForProject(const QString& projectFilePath) const {
    const QString baseName = QFileInfo(projectFilePath).completeBaseName();
    std::vector<BackupRecord> all = listBackups();
    std::vector<BackupRecord> filtered;
    for (const auto& b : all) {
        if (b.projectName.compare(baseName, Qt::CaseInsensitive) == 0) {
            filtered.push_back(b);
        }
    }
    return filtered;
}

int BackupManager::totalBackupsCount() const {
    return static_cast<int>(listBackups().size());
}

qint64 BackupManager::totalBackupsSize() const {
    qint64 total = 0;
    const auto all = listBackups();
    for (const auto& b : all) {
        total += b.fileSizeBytes;
    }
    return total;
}

bool BackupManager::openBackupDirectory() const {
    if (!QDir(m_backupDirectory).exists()) {
        QDir().mkpath(m_backupDirectory);
    }
    return QDesktopServices::openUrl(QUrl::fromLocalFile(m_backupDirectory));
}

} // namespace creative_suite::hub
