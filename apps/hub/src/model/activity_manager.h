#pragma once

#include "activity_item.h"

#include <QObject>
#include <QString>
#include <vector>

namespace creative_suite::hub {

class ActivityManager : public QObject {
    Q_OBJECT

public:
    static ActivityManager& instance();

    explicit ActivityManager(QObject* parent = nullptr);
    explicit ActivityManager(const QString& storagePath, QObject* parent = nullptr);

    void setStoragePath(const QString& path);
    [[nodiscard]] QString storagePath() const;

    bool load();
    bool save() const;

    void addActivity(const QString& title, const QString& description, const QString& category = QStringLiteral("system"));
    void addActivity(const ActivityItem& item);
    void markAllAsRead();
    void clear();

    [[nodiscard]] const std::vector<ActivityItem>& activities() const noexcept { return m_activities; }
    [[nodiscard]] int unreadCount() const noexcept;

signals:
    void activityAdded(const ActivityItem& item);
    void activitiesChanged();

private:
    QString m_storagePath;
    std::vector<ActivityItem> m_activities;
};

} // namespace creative_suite::hub
