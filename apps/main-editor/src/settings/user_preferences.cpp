#include "user_preferences.h"

#include <QSettings>

namespace settings {

bool previewPerformanceMetricsEnabled() {
    QSettings settings;
    return settings.value(kPreviewMetricsEnabledKey, false).toBool();
}

void setPreviewPerformanceMetricsEnabled(bool enabled) {
    QSettings settings;
    settings.setValue(kPreviewMetricsEnabledKey, enabled);
}

} // namespace settings
