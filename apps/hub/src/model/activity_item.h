#pragma once

#include <QString>
#include <QDateTime>
#include <QUuid>

namespace creative_suite::hub {

struct ActivityItem {
    QString id{QUuid::createUuid().toString(QUuid::WithoutBraces)};
    QString title;
    QString description;
    QString category{QStringLiteral("system")}; // "project", "backup", "cache", "update", "system"
    QDateTime timestamp{QDateTime::currentDateTime()};
    bool isRead{false};

    [[nodiscard]] bool isValid() const noexcept {
        return !id.isEmpty() && !title.isEmpty();
    }
};

} // namespace creative_suite::hub
