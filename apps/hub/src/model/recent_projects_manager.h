#pragma once

#include "recent_project.h"

#include <QObject>
#include <QString>
#include <vector>

namespace creative_suite::hub {

class RecentProjectsManager : public QObject {
    Q_OBJECT

public:
    explicit RecentProjectsManager(QObject* parent = nullptr);
    explicit RecentProjectsManager(const QString& storagePath, QObject* parent = nullptr);

    void setStoragePath(const QString& path);
    [[nodiscard]] QString storagePath() const;

    bool load();
    bool save() const;

    void addOrUpdateProject(const RecentProject& project);
    void addOrUpdateProject(const QString& filePath, const QString& appId = QString());
    bool removeProject(const QString& filePath);
    void clear();

    [[nodiscard]] const std::vector<RecentProject>& projects() const noexcept { return m_projects; }
    [[nodiscard]] std::vector<RecentProject> findByApp(const QString& appId) const;

    [[nodiscard]] static QString detectAppForFile(const QString& filePath);

signals:
    void projectsChanged();

private:
    QString m_storagePath;
    std::vector<RecentProject> m_projects;
};

} // namespace creative_suite::hub
