#include "backup_manager.h"
#include "activity_manager.h"
#include "project_backup_paths.h"
#include "../diagnostics/hub_logger.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QDesktopServices>
#include <QUrl>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QRegularExpression>
#include <optional>
#include <algorithm>

namespace creative_suite::hub {
namespace {

QString projectIdentity(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(QDir::fromNativeSeparators(
        canonical.isEmpty() ? info.absoluteFilePath() : canonical));
}

bool sameProject(const QString& storedPath, const QString& identity) {
#ifdef Q_OS_WIN
    return storedPath.compare(identity, Qt::CaseInsensitive) == 0;
#else
    return storedPath == identity;
#endif
}

std::optional<BackupRecord> readBackupRecord(const QFileInfo& directory) {
    const QString metadataPath = QDir(directory.absoluteFilePath()).filePath(QStringLiteral("record.json"));
    const QFileInfo metadataInfo(metadataPath);
    // No committed record means an unfinished snapshot, not a published backup.
    if (!metadataInfo.exists()) return std::nullopt;
    const auto fail = [&metadataPath](const QString& cause) -> std::optional<BackupRecord> {
        HubLogger::instance().logError(QStringLiteral("BackupManager"),
            QStringLiteral("listBackups"), cause, metadataPath);
        return std::nullopt;
    };
    if (!metadataInfo.isFile() || metadataInfo.isSymLink() || metadataInfo.size() > 256 * 1024)
        return fail(QStringLiteral("Metadados de backup inválidos: arquivo irregular ou tamanho excessivo."));
    QFile metadata(metadataPath);
    if (!metadata.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Não foi possível ler os metadados (código %1): %2")
            .arg(static_cast<int>(metadata.error())).arg(metadata.errorString()));
    const QByteArray bytes = metadata.readAll();
    if (metadata.error() != QFileDevice::NoError)
        return fail(QStringLiteral("Falha na leitura dos metadados (código %1): %2")
            .arg(static_cast<int>(metadata.error())).arg(metadata.errorString()));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(QStringLiteral("Metadados de backup não são um objeto JSON válido (código %1): %2")
            .arg(static_cast<int>(parseError.error)).arg(parseError.errorString()));
    const auto object = document.object();
    if (object.value(QStringLiteral("format")).toString() != QStringLiteral("creative-suite.hub-backup")
        || object.value(QStringLiteral("version")).toDouble(-1) != 1)
        return fail(QStringLiteral("Formato ou versão dos metadados de backup não suportados."));
    BackupRecord record;
    record.originalFilePath = object.value(QStringLiteral("original_path")).toString();
    record.projectName = object.value(QStringLiteral("project_name")).toString();
    record.timestamp = QDateTime::fromString(object.value(QStringLiteral("created_at")).toString(),
                                             Qt::ISODateWithMs).toLocalTime();
    const QString fileName = object.value(QStringLiteral("snapshot_file")).toString();
    if (!QDir::isAbsolutePath(record.originalFilePath) || record.projectName.isEmpty()
        || !record.timestamp.isValid() || fileName.isEmpty()
        || fileName.compare(QStringLiteral("record.json"), Qt::CaseInsensitive) == 0
        || fileName.contains('/') || fileName.contains('\\') || fileName == QStringLiteral(".")
        || fileName == QStringLiteral("..") || QDir::isAbsolutePath(fileName))
        return fail(QStringLiteral("Origem, nome, data ou referência do snapshot são inválidos."));
    const QFileInfo snapshot(QDir(directory.absoluteFilePath()).filePath(fileName));
    if (!snapshot.isFile() || snapshot.isSymLink())
        return fail(QStringLiteral("O snapshot registrado está ausente ou não é um arquivo regular: %1")
            .arg(snapshot.absoluteFilePath()));
    record.backupFilePath = snapshot.absoluteFilePath();
    record.fileSizeBytes = snapshot.size();
    return record;
}

} // namespace

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
    if (!file.open(QIODevice::ReadOnly)) {
        HubLogger::instance().logError(QStringLiteral("BackupManager"), QStringLiteral("loadSettings"),
            QStringLiteral("Could not open backup settings: %1").arg(file.errorString()), settingsFile);
        return;
    }
    const auto settings_bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        HubLogger::instance().logError(QStringLiteral("BackupManager"), QStringLiteral("loadSettings"),
            QStringLiteral("Could not read backup settings: %1").arg(file.errorString()), settingsFile);
        return;
    }
    QJsonParseError parse_error;
    const QJsonDocument doc = QJsonDocument::fromJson(settings_bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
        HubLogger::instance().logError(QStringLiteral("BackupManager"), QStringLiteral("loadSettings"),
            QStringLiteral("Backup settings are invalid JSON: %1").arg(parse_error.errorString()), settingsFile);
        return;
    }
    const QString savedDir = doc.object().value(QStringLiteral("backupDirectory")).toString();
    if (!savedDir.isEmpty()) m_backupDirectory = savedDir;
}

void BackupManager::saveSettings() const {
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataDir);
    const QString settingsFile = QDir(dataDir).filePath(QStringLiteral("backup_settings.json"));

    QSaveFile file(settingsFile);
    QJsonObject obj;
    obj.insert(QStringLiteral("backupDirectory"), m_backupDirectory);
    const auto settings_bytes = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(settings_bytes) != settings_bytes.size() || !file.commit()) {
        HubLogger::instance().logError(QStringLiteral("BackupManager"), QStringLiteral("saveSettings"),
            QStringLiteral("Could not save backup settings: %1").arg(file.errorString()), settingsFile);
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
    const QString baseName = srcInfo.completeBaseName().isEmpty() ? srcInfo.fileName() : srcInfo.completeBaseName();
    const QString suffix = srcInfo.suffix();
    const QDateTime timestamp = QDateTime::currentDateTime();
    const QString timestampStr = timestamp.toString(QStringLiteral("yyyyMMdd_HHmmss"));

    const QString targetFileName = suffix.isEmpty()
        ? QStringLiteral("%1_backup_%2").arg(baseName, timestampStr)
        : QStringLiteral("%1_backup_%2.%3").arg(baseName, timestampStr, suffix);

    // An exclusively created directory gives every snapshot its own namespace.
    // Committing record.json is the publication point; readers ignore it before then.
    QTemporaryDir pending(QDir(m_backupDirectory).filePath(QStringLiteral("backup-XXXXXX")));
    if (!pending.isValid())
        return fail(QStringLiteral("Não foi possível reservar a pasta do backup: %1")
            .arg(pending.errorString()), m_backupDirectory);
    const QString targetPath = QDir(pending.path()).filePath(targetFileName);
    QSaveFile snapshot(targetPath);
    if (!snapshot.open(QIODevice::WriteOnly)
        || snapshot.write(prepared) != prepared.size() || !snapshot.commit())
        return fail(QStringLiteral("Falha ao gravar o backup (código %1): %2")
            .arg(static_cast<int>(snapshot.error())).arg(snapshot.errorString()), targetPath);
    BackupRecord record;
    record.originalFilePath = projectIdentity(projectFilePath);
    record.backupFilePath = targetPath;
    record.projectName = baseName;
    record.timestamp = timestamp;
    record.fileSizeBytes = QFileInfo(targetPath).size();

    const QJsonObject metadata{
        {QStringLiteral("format"), QStringLiteral("creative-suite.hub-backup")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("original_path"), record.originalFilePath},
        {QStringLiteral("project_name"), record.projectName},
        {QStringLiteral("created_at"), timestamp.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("snapshot_file"), targetFileName}};
    const QByteArray metadataBytes = QJsonDocument(metadata).toJson(QJsonDocument::Indented);
    const QString metadataPath = QDir(pending.path()).filePath(QStringLiteral("record.json"));
    QSaveFile metadataFile(metadataPath);
    if (!metadataFile.open(QIODevice::WriteOnly)
        || metadataFile.write(metadataBytes) != metadataBytes.size() || !metadataFile.commit())
        return fail(QStringLiteral("Falha ao publicar os metadados (código %1): %2")
            .arg(static_cast<int>(metadataFile.error())).arg(metadataFile.errorString()), metadataPath);
    pending.setAutoRemove(false);

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
    const auto directories = dir.entryInfoList({QStringLiteral("backup-*")},
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const auto& directory : directories) {
        if (const auto record = readBackupRecord(directory)) list.push_back(*record);
    }

    // Older flat snapshots have no recoverable source identity. Keep them visible
    // in the vault, but never guess which same-named original they belong to.
    const QFileInfoList entries = dir.entryInfoList({QStringLiteral("*_backup_*")},
        QDir::Files | QDir::NoSymLinks, QDir::Time);
    const QRegularExpression legacyName(QStringLiteral("^(.*)_backup_[0-9]{8}_[0-9]{6}$"));

    for (const auto& info : entries) {
        BackupRecord record;
        record.backupFilePath = info.absoluteFilePath();
        const auto match = legacyName.match(info.completeBaseName());
        if (!match.hasMatch()) continue;
        record.projectName = match.captured(1);
        record.timestamp = info.lastModified();
        record.fileSizeBytes = info.size();
        list.push_back(record);
    }

    std::sort(list.begin(), list.end(), [](const BackupRecord& left, const BackupRecord& right) {
        if (left.timestamp != right.timestamp) return left.timestamp > right.timestamp;
        return left.backupFilePath < right.backupFilePath;
    });
    return list;
}

std::vector<BackupRecord> BackupManager::backupsForProject(const QString& projectFilePath) const {
    if (projectFilePath.isEmpty()) return {};
    const QString identity = projectIdentity(projectFilePath);
    std::vector<BackupRecord> all = listBackups();
    std::vector<BackupRecord> filtered;
    for (const auto& b : all) {
        if (!b.originalFilePath.isEmpty() && sameProject(b.originalFilePath, identity)) {
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
