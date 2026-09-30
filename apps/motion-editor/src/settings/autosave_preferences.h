#pragma once

namespace motion::settings {

inline constexpr int default_autosave_interval_seconds = 30;
inline constexpr int minimum_autosave_interval_seconds = 10;
inline constexpr int maximum_autosave_interval_seconds = 300;
inline constexpr int default_recovery_retention = 5;
inline constexpr int minimum_recovery_retention = 5;
inline constexpr int maximum_recovery_retention = 20;

[[nodiscard]] bool autosaveEnabled();
void setAutosaveEnabled(bool enabled);
[[nodiscard]] int autosaveIntervalSeconds();
void setAutosaveIntervalSeconds(int seconds);
[[nodiscard]] int recoveryRetention();
void setRecoveryRetention(int count);

[[nodiscard]] bool previewPerformanceMetricsEnabled();
void setPreviewPerformanceMetricsEnabled(bool enabled);

} // namespace motion::settings
