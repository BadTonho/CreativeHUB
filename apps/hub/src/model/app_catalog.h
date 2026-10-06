#pragma once

#include "app_info.h"
#include <QObject>
#include <vector>
#include <optional>

namespace creative_suite::hub {

class AppCatalog : public QObject {
    Q_OBJECT

public:
    explicit AppCatalog(QObject* parent = nullptr);

    [[nodiscard]] const std::vector<AppInfo>& apps() const noexcept { return m_apps; }
    [[nodiscard]] std::optional<AppInfo> findApp(const QString& id) const;

    void updateAppStatus(const QString& id, AppStatus status);
    void updateAppVersion(const QString& id, const QString& installedVersion);
    void updateLatestVersion(const QString& id, const QString& latestVersion);

    void populateDefaultApps();

signals:
    void appUpdated(const QString& id);
    void catalogReloaded();

private:
    std::vector<AppInfo> m_apps;
};

} // namespace creative_suite::hub
