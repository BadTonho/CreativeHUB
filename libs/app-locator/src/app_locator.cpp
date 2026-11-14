#include "creative_suite/app_locator/app_locator.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace creative_suite::app_locator {
namespace {

const QString kExecutableSuffix = QStringLiteral(".exe");

QString withoutExecutableSuffix(const QString& name) {
    return name.endsWith(kExecutableSuffix, Qt::CaseInsensitive)
        ? name.left(name.size() - kExecutableSuffix.size()) : name;
}

bool isRunnableFile(const QFileInfo& info) {
    return info.isFile() && info.isExecutable();
}

} // namespace

QStringList executableCandidateNames(const QString& executableName) {
    if (executableName.isEmpty()) return {};
    const QString bare = withoutExecutableSuffix(executableName);
    QStringList names{executableName};
    const auto add = [&names](const QString& name) {
        if (!names.contains(name)) names.append(name);
    };
    add(bare);
    add(bare + kExecutableSuffix);
    add(bare + QStringLiteral(".app/Contents/MacOS/") + bare);
    return names;
}

QString applicationBaseName(const QString& applicationId) {
    return QStringLiteral("creative-suite-") + applicationId;
}

QString applicationSettingsKey(const QString& applicationId) {
    QString id = applicationId;
    id.replace(QLatin1Char('-'), QLatin1Char('_'));
    return QStringLiteral("applications/") + id + QStringLiteral("_executable");
}

QStringList siblingSearchDirectories(const QString& applicationId, const QString& applicationDirectory) {
    const QDir app_dir(applicationDirectory);
    QStringList directories{app_dir.absolutePath()};
    for (const auto& up : {QStringLiteral(".."), QStringLiteral("../..")}) {
        for (const auto& configuration : {QStringLiteral("Release"), QStringLiteral("Debug")}) {
            directories.append(app_dir.filePath(up + QLatin1Char('/') + applicationId +
                                                QLatin1Char('/') + configuration));
        }
    }
    for (auto& directory : directories) directory = QDir::cleanPath(directory);
    directories.removeDuplicates();
    return directories;
}

QString findApplicationExecutable(const QString& applicationId, const QString& applicationDirectory) {
    const QString base = applicationBaseName(applicationId);

    QSettings settings;
    const QString configured = settings.value(applicationSettingsKey(applicationId)).toString();
    if (!configured.isEmpty() && QFileInfo(configured).isFile()) return configured;

    for (const auto& directory : siblingSearchDirectories(applicationId, applicationDirectory)) {
        const QDir dir(directory);
#if defined(Q_OS_WIN)
        const QStringList names{base + kExecutableSuffix};
#else
        // Without the Windows suffix; the bundle layout is only meaningful for the sibling apps.
        const QStringList names{base, base + QStringLiteral(".app/Contents/MacOS/") + base};
#endif
        for (const auto& name : names) {
            const QFileInfo info(dir.filePath(name));
            if (isRunnableFile(info)) return info.absoluteFilePath();
        }
    }
    return QStandardPaths::findExecutable(base);
}

} // namespace creative_suite::app_locator
