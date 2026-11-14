#pragma once

#include <QString>

namespace creative_suite::hub {

enum class AppStatus {
    NotInstalled,
    Installed,
    UpdateAvailable,
    Downloading,
    Installing
};

inline QString appStatusToString(AppStatus status) {
    switch (status) {
        case AppStatus::NotInstalled:
            return QStringLiteral("Not installed");
        case AppStatus::Installed:
            return QStringLiteral("Installed");
        case AppStatus::UpdateAvailable:
            return QStringLiteral("Update available");
        case AppStatus::Downloading:
            return QStringLiteral("Downloading...");
        case AppStatus::Installing:
            return QStringLiteral("Installing...");
    }
    return QStringLiteral("Unknown");
}

} // namespace creative_suite::hub
