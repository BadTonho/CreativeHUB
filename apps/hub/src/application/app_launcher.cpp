#include "app_launcher.h"
#include "../diagnostics/hub_logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace creative_suite::hub {

AppLauncher::AppLauncher() {
    const QString appDir = QCoreApplication::applicationDirPath();
    m_searchPaths.append(appDir);

    // Common sibling build directories
    m_searchPaths.append(QDir(appDir).filePath(QStringLiteral("../video-editor/Release")));
    m_searchPaths.append(QDir(appDir).filePath(QStringLiteral("../image-editor/Release")));
    m_searchPaths.append(QDir(appDir).filePath(QStringLiteral("../motion-editor/Release")));
    m_searchPaths.append(QDir(appDir).filePath(QStringLiteral("../video-editor/Debug")));
    m_searchPaths.append(QDir(appDir).filePath(QStringLiteral("../image-editor/Debug")));
    m_searchPaths.append(QDir(appDir).filePath(QStringLiteral("../motion-editor/Debug")));

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

    for (const auto& searchDir : m_searchPaths) {
        QDir dir(searchDir);
        if (!dir.exists()) {
            continue;
        }

        const QString fullPath = dir.filePath(executableName);
        QFileInfo fileInfo(fullPath);
        if (fileInfo.exists() && fileInfo.isFile() && fileInfo.isExecutable()) {
            return fullPath;
        }
    }

    return std::nullopt;
}

bool AppLauncher::isInstalled(const AppInfo& app) const {
    return findExecutable(app.executableName()).has_value();
}

bool AppLauncher::launch(const AppInfo& app) {
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

    const bool success = QProcess::startDetached(path, QStringList(), workingDir);
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
