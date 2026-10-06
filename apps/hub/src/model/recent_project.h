#pragma once

#include <QString>
#include <QDateTime>

namespace creative_suite::hub {

struct RecentProject {
    QString filePath;
    QString name;
    QString appId; // "video-editor", "image-editor", "motion-editor"
    QDateTime lastOpened;
    qint64 fileSizeBytes{0};

    [[nodiscard]] bool isValid() const noexcept {
        return !filePath.isEmpty();
    }
};

} // namespace creative_suite::hub
