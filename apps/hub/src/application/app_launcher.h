#pragma once

#include "../model/app_info.h"
#include <QString>
#include <QStringList>
#include <optional>

namespace creative_suite::hub {

class AppLauncher {
public:
    AppLauncher();

    void addSearchPath(const QString& path);
    void setSearchPaths(const QStringList& paths);
    [[nodiscard]] const QStringList& searchPaths() const noexcept { return m_searchPaths; }

    [[nodiscard]] std::optional<QString> findExecutable(const QString& executableName) const;
    [[nodiscard]] bool isInstalled(const AppInfo& app) const;

    bool launch(const AppInfo& app, const QStringList& arguments = {});

private:
    QStringList m_searchPaths;
};

} // namespace creative_suite::hub
