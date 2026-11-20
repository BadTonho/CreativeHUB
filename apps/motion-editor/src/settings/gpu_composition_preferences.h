#pragma once

namespace motion::settings {

// GPU preview/export composition is enabled by default (2026-11-20). It follows
// the stored preference (Settings > General, applied on the next launch) unless
// the CREATIVE_SUITE_MOTION_GPU_COMPOSITION process setting overrides it with the
// exact value "0" (force CPU) or "1" (force GPU). Any other value is ignored.
// Unsupported contexts and GPU failures still fall back to CPU.
inline constexpr char kGpuCompositionPreferenceKey[] =
    "MotionStudio/Performance/gpu_composition_enabled";

[[nodiscard]] bool resolveGpuCompositionEnabled(const char* environment_value,
                                                bool stored_preference) noexcept;
// The stored preference alone, default true. Safe to call before QApplication.
[[nodiscard]] bool gpuCompositionPreference();
void setGpuCompositionPreference(bool enabled);
// The effective setting for this process, resolved once at first use.
[[nodiscard]] bool gpuCompositionEnabled() noexcept;

} // namespace motion::settings
