#pragma once

#include <QString>
#include <cstdint>

namespace creative_suite::hub {

enum class DownloadState {
    Idle,
    Queued,
    Downloading,
    Paused,
    Completed,
    Failed,
    Cancelled
};

struct DownloadItem {
    QString appId;
    QString appName;
    QString version;
    DownloadState state{DownloadState::Idle};
    int64_t bytesReceived{0};
    int64_t totalBytes{0};
    double speedBytesPerSec{0.0};
    QString errorMessage;

    [[nodiscard]] double progressPercentage() const noexcept {
        if (totalBytes <= 0) return 0.0;
        return (static_cast<double>(bytesReceived) / static_cast<double>(totalBytes)) * 100.0;
    }
};

} // namespace creative_suite::hub
