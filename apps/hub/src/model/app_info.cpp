#include "app_info.h"

#include <utility>

namespace creative_suite::hub {

AppInfo::AppInfo(QString id,
                 QString name,
                 QString tagLine,
                 QString description,
                 QString installedVersion,
                 QString latestVersion,
                 QString executableName,
                 QString iconPath,
                 AppStatus status)
    : m_id(std::move(id))
    , m_name(std::move(name))
    , m_tagLine(std::move(tagLine))
    , m_description(std::move(description))
    , m_installedVersion(std::move(installedVersion))
    , m_latestVersion(std::move(latestVersion))
    , m_executableName(std::move(executableName))
    , m_iconPath(std::move(iconPath))
    , m_status(status)
{
}

} // namespace creative_suite::hub
