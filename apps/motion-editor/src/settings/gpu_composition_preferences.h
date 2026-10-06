#pragma once

namespace motion::settings {

// The experimental Motion preview compositor is enabled only by an exact
// CREATIVE_SUITE_MOTION_GPU_COMPOSITION=1 process setting.
[[nodiscard]] bool resolveGpuCompositionEnabled(const char* value) noexcept;
[[nodiscard]] bool gpuCompositionEnabled() noexcept;

} // namespace motion::settings
