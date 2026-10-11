#include "app_launcher.h"
#include "../diagnostics/hub_logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace creative_suite::hub {

AppLauncher::AppLauncher()
    : AppLauncher(QCoreApplication::applicationDirPath()) {}

AppLauncher::AppLauncher(const QString& appDir) {
    m_searchPaths.append(appDir);

    // CMake places app bundles inside the same app/configuration directories.
    QDir hubDirectory(QDir::cleanPath(appDir));
    if (hubDirectory.dirName() == QStringLiteral("MacOS")
        && QDir(QDir::cleanPath(hubDirectory.filePath(QStringLiteral("..")))).dirName() == QStringLiteral("Contents")
        && QDir(QDir::cleanPath(hubDirectory.filePath(QStringLiteral("../..")))).dirName().endsWith(QStringLiteral(".app"))) {
        hubDirectory = QDir(QDir::cleanPath(hubDirectory.filePath(QStringLiteral("../../.."))));
    }

    QStringList configurations{QStringLiteral("Release"), QStringLiteral("Debug"),
                               QStringLiteral("RelWithDebInfo"), QStringLiteral("MinSizeRel")};
    const QDir parentDirectory(QDir::cleanPath(hubDirectory.filePath(QStringLiteral(".."))));
    if (configurations.contains(hubDirectory.dirName())
        || parentDirectory.dirName() == QStringLiteral("hub")) {
        const QString configuration = hubDirectory.dirName();
        configurations.removeAll(configuration);
        configurations.prepend(configuration);
        hubDirectory = parentDirectory;
    } else {
        configurations.prepend(QString()); // Single-configuration build.
    }

    // Reach build/apps from hub, rather than searching inside hub/Release.
    const QDir appsDirectory(QDir::cleanPath(hubDirectory.filePath(QStringLiteral(".."))));
    const QStringList editorIds{QStringLiteral("video-editor"), QStringLiteral("image-editor"),
                               QStringLiteral("motion-editor")};
    for (const auto& configuration : configurations) {
        for (const auto& editorId : editorIds) {
            const QString relative = configuration.isEmpty() ? editorId : editorId + '/' + configuration;
            m_searchPaths.append(QDir::cleanPath(appsDirectory.filePath(relative)));
        }
    }
    // Also discover single-configuration sibling outputs from a configured Hub.
    if (!configurations.contains(QString())) {
        for (const auto& editorId : editorIds) {
            m_searchPaths.append(QDir::cleanPath(appsDirectory.filePath(editorId)));
        }
    }

    // Standard user apps directory
    const QString localAppData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    m_searchPaths.append(QDir(localAppData).filePath(QStringLiteral("apps")));
}

void AppLauncher::addSearchPath(const QString& path) {
    if (!path.isEmpty() && !m_searchPaths.contains(path)) {
        m_searchPaths.prepend(path);
    }
}

void AppLauncher::setSearchPaths(const QStringList& paths) {
    m_searchPaths = paths;
}

std::optional<QString> AppLauncher::findExecutable(const QString& executableName) const {
    if (executableName.isEmpty()) {
        return std::nullopt;
    }

    QStringList candidates = {executableName};
    if (executableName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        candidates.append(executableName.left(executableName.size() - 4));
    } else {
        candidates.append(executableName + QStringLiteral(".exe"));
    }
    const QString bundleExecutable = executableName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
        ? executableName.left(executableName.size() - 4) : executableName;
    candidates.append(bundleExecutable + QStringLiteral(".app/Contents/MacOS/") + bundleExecutable);

    for (const auto& searchDir : m_searchPaths) {
        QDir dir(searchDir);
        if (!dir.exists()) {
            continue;
        }

        for (const auto& candidate : candidates) {
            const QString fullPath = dir.filePath(candidate);
            QFileInfo fileInfo(fullPath);
            if (fileInfo.exists() && fileInfo.isFile() && fileInfo.isExecutable()) {
                return fullPath;
            }
        }
    }

    return std::nullopt;
}

bool AppLauncher::isInstalled(const AppInfo& app) const {
    return findExecutable(app.executableName()).has_value();
}

bool AppLauncher::launch(const AppInfo& app, const QStringList& arguments) {
    const auto execPath = findExecutable(app.executableName());
    if (!execPath.has_value()) {
        HubLogger::instance().logError(
            QStringLiteral("AppLauncher"),
            QStringLiteral("launch"),
            QStringLiteral("Executável não encontrado"),
            QStringLiteral("app: %1, exe: %2").arg(app.id(), app.executableName())
        );
        return false;
    }

    const QString path = *execPath;
    const QString workingDir = QFileInfo(path).absolutePath();

    HubLogger::instance().logInfo(
        QStringLiteral("AppLauncher"),
        QStringLiteral("launch"),
        QStringLiteral("Iniciando aplicativo"),
        path
    );

    const bool success = QProcess::startDetached(path, arguments, workingDir);
    if (!success) {
        HubLogger::instance().logError(
            QStringLiteral("AppLauncher"),
            QStringLiteral("launch"),
            QStringLiteral("Falha ao iniciar processo"),
            path
        );
        return false;
    }

    return true;
}

} // namespace creative_suite::hub
