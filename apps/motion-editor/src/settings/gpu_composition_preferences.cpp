#include "gpu_composition_preferences.h"

#include <QByteArray>
#include <QSettings>

#include <cstring>

namespace motion::settings {

namespace {

// Explicit names so the preference is readable before QApplication exists (the
// OpenGL surface format must be chosen first); they match the names main() sets.
QSettings preferenceStore()
{
    return QSettings(QStringLiteral("Creative Suite"), QStringLiteral("Motion Studio"));
}

} // namespace

bool resolveGpuCompositionEnabled(const char* environment_value, bool stored_preference) noexcept
{
    if (environment_value != nullptr) {
        if (std::strcmp(environment_value, "0") == 0) return false;
        if (std::strcmp(environment_value, "1") == 0) return true;
    }
    return stored_preference;
}

bool gpuCompositionPreference()
{
    return preferenceStore().value(QLatin1String(kGpuCompositionPreferenceKey), true).toBool();
}

void setGpuCompositionPreference(bool enabled)
{
    preferenceStore().setValue(QLatin1String(kGpuCompositionPreferenceKey), enabled);
}

bool gpuCompositionEnabled() noexcept
{
    static const bool enabled = [] {
        const auto value = qgetenv("CREATIVE_SUITE_MOTION_GPU_COMPOSITION");
        return resolveGpuCompositionEnabled(value.isNull() ? nullptr : value.constData(),
                                            gpuCompositionPreference());
    }();
    return enabled;
}

} // namespace motion::settings
