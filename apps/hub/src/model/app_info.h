#pragma once

#include "app_status.h"
#include <QString>
#include <QStringList>
#include <QIcon>

namespace creative_suite::hub {

class AppInfo {
public:
    AppInfo() = default;
    AppInfo(QString id,
            QString name,
            QString tagLine,
            QString description,
            QString installedVersion,
            QString latestVersion,
            QString executableName,
            QString iconPath,
            AppStatus status = AppStatus::NotInstalled,
            QStringList features = {},
            QString projectFormat = {});

    [[nodiscard]] const QString& id() const noexcept { return m_id; }
    [[nodiscard]] const QString& name() const noexcept { return m_name; }
    [[nodiscard]] const QString& tagLine() const noexcept { return m_tagLine; }
    [[nodiscard]] const QString& description() const noexcept { return m_description; }
    [[nodiscard]] const QString& installedVersion() const noexcept { return m_installedVersion; }
    [[nodiscard]] const QString& latestVersion() const noexcept { return m_latestVersion; }
    [[nodiscard]] const QString& executableName() const noexcept { return m_executableName; }
    [[nodiscard]] const QString& iconPath() const noexcept { return m_iconPath; }
    [[nodiscard]] AppStatus status() const noexcept { return m_status; }
    [[nodiscard]] const QStringList& features() const noexcept { return m_features; }
    [[nodiscard]] const QString& projectFormat() const noexcept { return m_projectFormat; }

    void setStatus(AppStatus status) noexcept { m_status = status; }
    void setInstalledVersion(const QString& version) { m_installedVersion = version; }
    void setLatestVersion(const QString& version) { m_latestVersion = version; }
    void setFeatures(QStringList features) { m_features = std::move(features); }
    void setProjectFormat(QString format) { m_projectFormat = std::move(format); }

    [[nodiscard]] bool isInstalled() const noexcept {
        return m_status == AppStatus::Installed || m_status == AppStatus::UpdateAvailable;
    }

    [[nodiscard]] bool hasUpdate() const noexcept {
        return m_status == AppStatus::UpdateAvailable;
    }

private:
    QString m_id;
    QString m_name;
    QString m_tagLine;
    QString m_description;
    QString m_installedVersion;
    QString m_latestVersion;
    QString m_executableName;
    QString m_iconPath;
    AppStatus m_status{AppStatus::NotInstalled};
    QStringList m_features;
    QString m_projectFormat;
};

} // namespace creative_suite::hub
