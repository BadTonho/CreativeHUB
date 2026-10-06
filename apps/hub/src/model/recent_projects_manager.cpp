#include "recent_projects_manager.h"
#include "../diagnostics/hub_logger.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>

namespace creative_suite::hub {

RecentProjectsManager::RecentProjectsManager(QObject* parent)
    : QObject(parent)
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    m_storagePath = QDir(dataDir).filePath(QStringLiteral("recent_projects.json"));
    load();
}

RecentProjectsManager::RecentProjectsManager(const QString& storagePath, QObject* parent)
    : QObject(parent)
    , m_storagePath(storagePath)
{
    load();
}

void RecentProjectsManager::setStoragePath(const QString& path) {
    m_storagePath = path;
}

QString RecentProjectsManager::storagePath() const {
    return m_storagePath;
}

bool RecentProjectsManager::load() {
    m_projects.clear();

    if (m_storagePath.isEmpty() || !QFile::exists(m_storagePath)) {
        return false;
    }

    QFile file(m_storagePath);
    if (!file.open(QIODevice::ReadOnly)) {
        HubLogger::instance().logError(
            QStringLiteral("RecentProjectsManager"),
            QStringLiteral("load"),
            QStringLiteral("Falha ao abrir arquivo de projetos recentes"),
            m_storagePath
        );
        return false;
    }

    const QByteArray data = file.readAll();
    file.close();

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        return false;
    }

    const QJsonArray array = doc.array();
    for (const auto& val : array) {
        if (!val.isObject()) continue;
        const QJsonObject obj = val.toObject();

        RecentProject p;
        p.filePath = obj.value(QStringLiteral("filePath")).toString();
        p.name = obj.value(QStringLiteral("name")).toString();
        p.appId = obj.value(QStringLiteral("appId")).toString();
        p.lastOpened = QDateTime::fromString(obj.value(QStringLiteral("lastOpened")).toString(), Qt::ISODate);
        p.fileSizeBytes = obj.value(QStringLiteral("fileSizeBytes")).toVariant().toLongLong();

        if (p.name.isEmpty() && !p.filePath.isEmpty()) {
            p.name = QFileInfo(p.filePath).fileName();
        }
        if (p.appId.isEmpty() && !p.filePath.isEmpty()) {
            p.appId = detectAppForFile(p.filePath);
        }

        if (p.isValid()) {
            m_projects.push_back(p);
        }
    }

    std::sort(m_projects.begin(), m_projects.end(), [](const RecentProject& a, const RecentProject& b) {
        return a.lastOpened > b.lastOpened;
    });

    emit projectsChanged();
    return true;
}

bool RecentProjectsManager::save() const {
    if (m_storagePath.isEmpty()) {
        return false;
    }

    const QFileInfo info(m_storagePath);
    QDir().mkpath(info.absolutePath());

    QFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        HubLogger::instance().logError(
            QStringLiteral("RecentProjectsManager"),
            QStringLiteral("save"),
            QStringLiteral("Falha ao salvar projetos recentes"),
            m_storagePath
        );
        return false;
    }

    QJsonArray array;
    for (const auto& p : m_projects) {
        QJsonObject obj;
        obj.insert(QStringLiteral("filePath"), p.filePath);
        obj.insert(QStringLiteral("name"), p.name);
        obj.insert(QStringLiteral("appId"), p.appId);
        obj.insert(QStringLiteral("lastOpened"), p.lastOpened.toString(Qt::ISODate));
        obj.insert(QStringLiteral("fileSizeBytes"), p.fileSizeBytes);
        array.append(obj);
    }

    const QJsonDocument doc(array);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    return true;
}

void RecentProjectsManager::addOrUpdateProject(const RecentProject& project) {
    if (!project.isValid()) {
        return;
    }

    RecentProject toAdd = project;
    if (toAdd.name.isEmpty()) {
        const QString base = QFileInfo(toAdd.filePath).completeBaseName();
        toAdd.name = base.isEmpty() ? QFileInfo(toAdd.filePath).fileName() : base;
    }
    if (toAdd.appId.isEmpty()) {
        toAdd.appId = detectAppForFile(toAdd.filePath);
    }
    if (!toAdd.lastOpened.isValid()) {
        toAdd.lastOpened = QDateTime::currentDateTime();
    }
    if (toAdd.fileSizeBytes <= 0 && QFile::exists(toAdd.filePath)) {
        toAdd.fileSizeBytes = QFileInfo(toAdd.filePath).size();
    }

    auto it = std::find_if(m_projects.begin(), m_projects.end(), [&toAdd](const RecentProject& existing) {
        return existing.filePath == toAdd.filePath;
    });

    if (it != m_projects.end()) {
        m_projects.erase(it);
    }
    m_projects.insert(m_projects.begin(), toAdd);

    std::stable_sort(m_projects.begin(), m_projects.end(), [](const RecentProject& a, const RecentProject& b) {
        return a.lastOpened > b.lastOpened;
    });

    // Cap at 50 recent projects
    if (m_projects.size() > 50) {
        m_projects.resize(50);
    }

    save();
    emit projectsChanged();
}

void RecentProjectsManager::addOrUpdateProject(const QString& filePath, const QString& appId) {
    RecentProject project;
    project.filePath = filePath;
    const QString base = QFileInfo(filePath).completeBaseName();
    project.name = base.isEmpty() ? QFileInfo(filePath).fileName() : base;
    project.appId = appId.isEmpty() ? detectAppForFile(filePath) : appId;
    project.lastOpened = QDateTime::currentDateTime();
    if (QFile::exists(filePath)) {
        project.fileSizeBytes = QFileInfo(filePath).size();
    }
    addOrUpdateProject(project);
}

bool RecentProjectsManager::removeProject(const QString& filePath) {
    auto it = std::remove_if(m_projects.begin(), m_projects.end(), [&filePath](const RecentProject& p) {
        return p.filePath == filePath;
    });

    if (it != m_projects.end()) {
        m_projects.erase(it, m_projects.end());
        save();
        emit projectsChanged();
        return true;
    }

    return false;
}

void RecentProjectsManager::clear() {
    m_projects.clear();
    save();
    emit projectsChanged();
}

std::vector<RecentProject> RecentProjectsManager::findByApp(const QString& appId) const {
    if (appId.isEmpty()) {
        return m_projects;
    }

    std::vector<RecentProject> result;
    for (const auto& p : m_projects) {
        if (p.appId == appId) {
            result.push_back(p);
        }
    }
    return result;
}

QString RecentProjectsManager::detectAppForFile(const QString& filePath) {
    const QString lower = filePath.toLower();
    if (lower.endsWith(QStringLiteral(".csp")) || lower.endsWith(QStringLiteral(".csve"))) {
        return QStringLiteral("video-editor");
    }
    if (lower.endsWith(QStringLiteral(".cimg")) || lower.endsWith(QStringLiteral(".csie"))) {
        return QStringLiteral("image-editor");
    }
    if (lower.endsWith(QStringLiteral(".motion")) || lower.endsWith(QStringLiteral(".csme"))) {
        return QStringLiteral("motion-editor");
    }
    return QString();
}

} // namespace creative_suite::hub
