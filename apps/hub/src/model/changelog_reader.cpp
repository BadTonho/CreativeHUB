#include "changelog_reader.h"
#include "../diagnostics/hub_logger.h"
#include <creative_suite/updater/release_catalog.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

namespace creative_suite::hub {

ChangelogReader::ChangelogReader(QString customBasePath)
    : m_basePath(std::move(customBasePath))
{
    if (m_basePath.isEmpty()) {
        m_basePath = defaultChangelogPath();
    }
}

QString ChangelogReader::basePath() const {
    return m_basePath;
}

void ChangelogReader::setBasePath(const QString& path) {
    m_basePath = path;
}

QString ChangelogReader::changelogDirForApp(const QString& appId) const {
    if (m_basePath.isEmpty() || appId.isEmpty()) {
        return QString();
    }
    return QDir(m_basePath).filePath(appId);
}

QString ChangelogReader::defaultChangelogPath() {
    // 1. Current working directory
    const QString currentCheck = QDir(QDir::currentPath()).filePath(QStringLiteral("Changelog"));
    if (QDir(currentCheck).exists()) {
        return QDir::cleanPath(currentCheck);
    }

    // 2. Walk up directory tree from executable (development build / runtime)
    QString searchDir = QCoreApplication::applicationDirPath();
    if (!searchDir.isEmpty()) {
        QDir dir(searchDir);
        for (int i = 0; i < 6; ++i) {
            if (dir.exists(QStringLiteral("Changelog"))) {
                return QDir::cleanPath(dir.filePath(QStringLiteral("Changelog")));
            }
            if (!dir.cdUp()) {
                break;
            }
        }
    }

    // 3. Alongside application binary
    const QString alongsideCheck = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("Changelog"));
    if (QDir(alongsideCheck).exists()) {
        return QDir::cleanPath(alongsideCheck);
    }

    // 4. Shared data installation path (e.g. Linux / macOS bundle)
    const QString sharedCheck = QDir::cleanPath(
        QCoreApplication::applicationDirPath() + QStringLiteral("/../share/creative-suite/Changelog"));
    if (QDir(sharedCheck).exists()) {
        return sharedCheck;
    }

    // Fallback to current working directory convention
    return QDir::cleanPath(QDir::currentPath() + QStringLiteral("/Changelog"));
}

QStringList ChangelogReader::availableVersions(const QString& appId) const {
    const QString appDir = changelogDirForApp(appId);
    if (appDir.isEmpty()) {
        return {};
    }

    QDir dir(appDir);
    if (!dir.exists()) {
        return {};
    }

    const auto fileList = dir.entryInfoList(
        QStringList{QStringLiteral("*.md")},
        QDir::Files | QDir::NoDotAndDotDot,
        QDir::Name
    );

    QStringList versions;
    for (const auto& fileInfo : fileList) {
        const QString name = fileInfo.fileName();
        if (name.startsWith(QLatin1Char('.')) || name == QStringLiteral(".gitkeep")) {
            continue;
        }

        QString ver = fileInfo.completeBaseName();
        if (ver.startsWith(QLatin1Char('v')) || ver.startsWith(QLatin1Char('V'))) {
            ver = ver.mid(1);
        }

        if (!ver.isEmpty() && !versions.contains(ver)) {
            versions.append(ver);
        }
    }

    // Sort descending by semantic versioning
    std::sort(versions.begin(), versions.end(), [](const QString& a, const QString& b) {
        const auto cmp = creative_suite::updater::compareVersions(a, b);
        if (cmp.has_value()) {
            return *cmp > 0;
        }
        return a > b;
    });

    return versions;
}

QString ChangelogReader::loadChangelog(const QString& appId, const QString& version) const {
    const QString appDir = changelogDirForApp(appId);
    if (appDir.isEmpty()) {
        return QString();
    }

    QString targetVersion = version.trimmed();
    if (targetVersion.startsWith(QLatin1Char('v')) || targetVersion.startsWith(QLatin1Char('V'))) {
        targetVersion = targetVersion.mid(1);
    }

    if (targetVersion.isEmpty()) {
        const auto versions = availableVersions(appId);
        if (versions.isEmpty()) {
            return QString();
        }
        targetVersion = versions.first();
    }

    QDir dir(appDir);
    QString candidatePath = dir.filePath(targetVersion + QStringLiteral(".md"));
    if (!QFile::exists(candidatePath)) {
        candidatePath = dir.filePath(QStringLiteral("v") + targetVersion + QStringLiteral(".md"));
    }
    if (!QFile::exists(candidatePath)) {
        return QString();
    }

    QFile file(candidatePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        HubLogger::instance().logWarning(
            QStringLiteral("ChangelogReader"),
            QStringLiteral("loadChangelog"),
            QStringLiteral("Falha ao abrir arquivo de changelog para leitura"),
            candidatePath
        );
        return QString();
    }

    return QString::fromUtf8(file.readAll());
}

bool ChangelogReader::hasChangelogs(const QString& appId) const {
    return !availableVersions(appId).isEmpty();
}

} // namespace creative_suite::hub
