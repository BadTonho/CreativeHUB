#include "recovery_store.h"

#include "image_document_session.h"
#include "image_document_store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace image_editor {

RecoveryStore::RecoveryStore(QString application_data_directory)
    : application_data_directory_(std::move(application_data_directory)) {
    if (application_data_directory_.isEmpty()) {
        application_data_directory_ = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
    }
}

QString RecoveryStore::recoveryDirectory() const {
    return QDir(application_data_directory_).filePath(QStringLiteral("recovery"));
}

QString RecoveryStore::pathFor(const ImageDocumentSession& session) const {
    QString identity = session.documentPath();
    if (identity.isEmpty()) identity = session.sourcePath();
    if (identity.isEmpty()) identity = session.recoverySessionId();
    if (identity.isEmpty()) return {};
    const auto bytes = identity.toUtf8();
    const auto digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    return QDir(recoveryDirectory()).filePath(
        QString::fromLatin1(digest) + QStringLiteral(".cimg-recovery"));
}

bool RecoveryStore::save(const ImageDocumentSession& session, QString* error) const {
    if (!session.hasSource() || !session.isDirty()) return true;
    if (!QDir().mkpath(recoveryDirectory())) {
        if (error != nullptr) *error = QStringLiteral("The recovery directory could not be created.");
        return false;
    }
    RecoveryDocumentData recovery;
    recovery.document = session.data();
    recovery.target_document_path = session.recoveryTargetPath();
    recovery.session_id = session.recoverySessionId();
    return ImageDocumentStore::saveRecovery(pathFor(session), recovery, error);
}

QStringList RecoveryStore::snapshots() const {
    QDir directory(recoveryDirectory());
    const auto files = directory.entryInfoList(
        {QStringLiteral("*.cimg-recovery")}, QDir::Files, QDir::Time);
    QStringList result;
    result.reserve(files.size());
    for (const auto& file : files) result.append(file.absoluteFilePath());
    return result;
}

bool RecoveryStore::remove(const QString& recovery_path) const {
    if (recovery_path.isEmpty()) return false;
    return QFileInfo::exists(recovery_path) && QFile::remove(recovery_path);
}

} // namespace image_editor
