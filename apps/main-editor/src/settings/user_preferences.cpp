#include "user_preferences.h"

#include <QSettings>

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

} // namespace settings
