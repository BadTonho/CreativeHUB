#include "autosave_preferences.h"

#include <QSettings>

#include <algorithm>

namespace motion::settings {
namespace {

constexpr auto kEnabledKey = "MotionStudio/Autosave/enabled";
constexpr auto kIntervalKey = "MotionStudio/Autosave/interval_seconds";
constexpr auto kRetentionKey = "MotionStudio/Autosave/retention";
constexpr auto kPreviewMetricsKey = "MotionStudio/Performance/preview_metrics_enabled";

} // namespace

bool autosaveEnabled()
{
    return QSettings().value(QLatin1String(kEnabledKey), true).toBool();
}

void setAutosaveEnabled(bool enabled)
{
    QSettings().setValue(QLatin1String(kEnabledKey), enabled);
}

int autosaveIntervalSeconds()
{
    return std::clamp(
        QSettings().value(QLatin1String(kIntervalKey),
                          default_autosave_interval_seconds).toInt(),
        minimum_autosave_interval_seconds,
        maximum_autosave_interval_seconds);
}

void setAutosaveIntervalSeconds(int seconds)
{
    QSettings().setValue(QLatin1String(kIntervalKey),
        std::clamp(seconds, minimum_autosave_interval_seconds,
                   maximum_autosave_interval_seconds));
}

int recoveryRetention()
{
    return std::clamp(
        QSettings().value(QLatin1String(kRetentionKey),
                          default_recovery_retention).toInt(),
        minimum_recovery_retention,
        maximum_recovery_retention);
}

void setRecoveryRetention(int count)
{
    QSettings().setValue(QLatin1String(kRetentionKey),
        std::clamp(count, minimum_recovery_retention, maximum_recovery_retention));
}

bool previewPerformanceMetricsEnabled()
{
    return QSettings().value(QLatin1String(kPreviewMetricsKey), true).toBool();
}

void setPreviewPerformanceMetricsEnabled(bool enabled)
{
    QSettings().setValue(QLatin1String(kPreviewMetricsKey), enabled);
}

} // namespace motion::settings
