#include "gpu_composition_preferences.h"

#include <QByteArray>

#include <cstring>

namespace motion::settings {

bool resolveGpuCompositionEnabled(const char* value) noexcept
{
    return value != nullptr && std::strcmp(value, "1") == 0;
}

bool gpuCompositionEnabled() noexcept
{
    static const bool enabled = [] {
        const auto value = qgetenv("CREATIVE_SUITE_MOTION_GPU_COMPOSITION");
        return resolveGpuCompositionEnabled(value.isNull() ? nullptr : value.constData());
    }();
    return enabled;
}

} // namespace motion::settings
