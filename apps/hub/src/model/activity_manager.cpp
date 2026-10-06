#include "activity_manager.h"
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

ActivityManager& ActivityManager::instance() {
    static ActivityManager s_instance;
    return s_instance;
}

ActivityManager::ActivityManager(QObject* parent)
    : QObject(parent)
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    m_storagePath = QDir(dataDir).filePath(QStringLiteral("activities.json"));
    load();
}

ActivityManager::ActivityManager(const QString& storagePath, QObject* parent)
    : QObject(parent)
    , m_storagePath(storagePath)
{
    load();
}

void ActivityManager::setStoragePath(const QString& path) {
    m_storagePath = path;
}

QString ActivityManager::storagePath() const {
    return m_storagePath;
}

bool ActivityManager::load() {
    m_activities.clear();

    if (m_storagePath.isEmpty() || !QFile::exists(m_storagePath)) {
        return false;
    }

    QFile file(m_storagePath);
    if (!file.open(QIODevice::ReadOnly)) {
        HubLogger::instance().logError(
            QStringLiteral("ActivityManager"),
            QStringLiteral("load"),
            QStringLiteral("Falha ao abrir arquivo de atividades"),
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

        ActivityItem item;
        item.id = obj.value(QStringLiteral("id")).toString();
        item.title = obj.value(QStringLiteral("title")).toString();
        item.description = obj.value(QStringLiteral("description")).toString();
        item.category = obj.value(QStringLiteral("category")).toString(QStringLiteral("system"));
        item.timestamp = QDateTime::fromString(obj.value(QStringLiteral("timestamp")).toString(), Qt::ISODate);
        item.isRead = obj.value(QStringLiteral("isRead")).toBool(false);

        if (item.isValid()) {
            m_activities.push_back(item);
        }
    }

    std::sort(m_activities.begin(), m_activities.end(), [](const ActivityItem& a, const ActivityItem& b) {
        return a.timestamp > b.timestamp;
    });

    emit activitiesChanged();
    return true;
}

bool ActivityManager::save() const {
    if (m_storagePath.isEmpty()) {
        return false;
    }

    const QFileInfo info(m_storagePath);
    QDir().mkpath(info.absolutePath());

    QFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        HubLogger::instance().logError(
            QStringLiteral("ActivityManager"),
            QStringLiteral("save"),
            QStringLiteral("Falha ao salvar atividades"),
            m_storagePath
        );
        return false;
    }

    QJsonArray array;
    for (const auto& item : m_activities) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), item.id);
        obj.insert(QStringLiteral("title"), item.title);
        obj.insert(QStringLiteral("description"), item.description);
        obj.insert(QStringLiteral("category"), item.category);
        obj.insert(QStringLiteral("timestamp"), item.timestamp.toString(Qt::ISODate));
        obj.insert(QStringLiteral("isRead"), item.isRead);
        array.append(obj);
    }

    const QJsonDocument doc(array);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    return true;
}

void ActivityManager::addActivity(const QString& title, const QString& description, const QString& category) {
    ActivityItem item;
    item.title = title;
    item.description = description;
    item.category = category;
    item.timestamp = QDateTime::currentDateTime();
    item.isRead = false;
    addActivity(item);
}

void ActivityManager::addActivity(const ActivityItem& item) {
    if (!item.isValid()) {
        return;
    }

    m_activities.insert(m_activities.begin(), item);

    // Keep up to 100 activities
    if (m_activities.size() > 100) {
        m_activities.resize(100);
    }

    save();
    emit activityAdded(item);
    emit activitiesChanged();
}

int ActivityManager::unreadCount() const noexcept {
    return static_cast<int>(std::count_if(m_activities.begin(), m_activities.end(), [](const ActivityItem& item) {
        return !item.isRead;
    }));
}

void ActivityManager::markAllAsRead() {
    bool changed = false;
    for (auto& item : m_activities) {
        if (!item.isRead) {
            item.isRead = true;
            changed = true;
        }
    }

    if (changed) {
        save();
        emit activitiesChanged();
    }
}

void ActivityManager::clear() {
    if (!m_activities.empty()) {
        m_activities.clear();
        save();
        emit activitiesChanged();
    }
}

} // namespace creative_suite::hub
