#include "directory_restore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace creative_suite::updater::detail {
namespace {

bool copyDirectoryContents(const QString& source_path, const QString& destination_path) {
    QDir source(source_path);
    if (!source.exists() || !QDir().mkpath(destination_path)) return false;
    const auto entries = source.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name);
    for (const auto& entry : entries) {
        if (entry.isSymLink()) return false;
        const auto destination = QDir(destination_path).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyDirectoryContents(entry.absoluteFilePath(), destination)) return false;
        } else if (!QFile::copy(entry.absoluteFilePath(), destination)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool restoreDirectorySnapshot(const QString& snapshot_path, const QString& install_path) {
    const QFileInfo snapshot_info(snapshot_path);
    const QFileInfo install_info(install_path);
    if (!snapshot_info.isDir() || snapshot_info.isSymLink() ||
        !install_info.isDir() || install_info.isSymLink()) {
        return false;
    }

    const auto install_parent = install_info.absolutePath();
    QTemporaryDir staging(QDir(install_parent).filePath(QStringLiteral(".restore-XXXXXX")));
    if (!staging.isValid()) return false;

    const auto staged_install = QDir(staging.path()).filePath(QStringLiteral("application"));
    const auto previous_install = QDir(staging.path()).filePath(QStringLiteral("previous"));
    if (!copyDirectoryContents(snapshot_info.absoluteFilePath(), staged_install)) return false;

    const auto normalized_install = install_info.absoluteFilePath();
    if (!QDir().rename(normalized_install, previous_install)) return false;
    if (!QDir().rename(staged_install, normalized_install)) {
        if (!QDir().rename(previous_install, normalized_install))
            staging.setAutoRemove(false);
        return false;
    }
    return true;
}

} // namespace creative_suite::updater::detail
