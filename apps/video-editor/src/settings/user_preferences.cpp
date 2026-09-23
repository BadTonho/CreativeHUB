#include "user_preferences.h"

#include <QSettings>

#include <algorithm>

namespace settings {

bool previewPerformanceMetricsEnabled() {
    constexpr bool default_enabled = true;
    QSettings settings;
    return settings.value(kPreviewMetricsEnabledKey, default_enabled).toBool();
}

void setPreviewPerformanceMetricsEnabled(bool enabled) {
    QSettings settings;
    settings.setValue(kPreviewMetricsEnabledKey, enabled);
}

bool projectAutosaveEnabled() {
    return QSettings().value(kProjectAutosaveEnabledKey, true).toBool();
}

void setProjectAutosaveEnabled(bool enabled) {
    QSettings settings;
    settings.setValue(kProjectAutosaveEnabledKey, enabled);
}

int projectAutosaveIntervalSeconds() {
    const auto value = QSettings().value(
        kProjectAutosaveIntervalSecondsKey,
        kDefaultProjectAutosaveIntervalSeconds).toInt();
    return std::clamp(
        value,
        kMinimumProjectAutosaveIntervalSeconds,
        kMaximumProjectAutosaveIntervalSeconds);
}

void setProjectAutosaveIntervalSeconds(int seconds) {
    QSettings settings;
    settings.setValue(
        kProjectAutosaveIntervalSecondsKey,
        std::clamp(
            seconds,
            kMinimumProjectAutosaveIntervalSeconds,
            kMaximumProjectAutosaveIntervalSeconds));
}

int projectAutosaveRetention() {
    const auto value = QSettings().value(
        kProjectAutosaveRetentionKey,
        kDefaultProjectAutosaveRetention).toInt();
    return std::clamp(
        value,
        kMinimumProjectAutosaveRetention,
        kMaximumProjectAutosaveRetention);
}

void setProjectAutosaveRetention(int count) {
    QSettings settings;
    settings.setValue(
        kProjectAutosaveRetentionKey,
        std::clamp(
            count,
            kMinimumProjectAutosaveRetention,
            kMaximumProjectAutosaveRetention));
}

int monitorVolumePercent() {
    const auto stored = QSettings().value(kMonitorVolumePercentKey);
    bool ok = false;
    const auto value = stored.toInt(&ok);
    if (!ok) return kDefaultMonitorVolumePercent;
    return std::clamp(
        value,
        kMinimumMonitorVolumePercent,
        kMaximumMonitorVolumePercent);
}

void setMonitorVolumePercent(int percent) {
    QSettings settings;
    settings.setValue(
        kMonitorVolumePercentKey,
        std::clamp(
            percent,
            kMinimumMonitorVolumePercent,
            kMaximumMonitorVolumePercent));
}

} // namespace settings
