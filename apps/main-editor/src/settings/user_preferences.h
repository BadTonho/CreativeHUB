#pragma once

namespace settings {

inline constexpr char kPreviewMetricsEnabledKey[] =
    "performance/preview_metrics_enabled";
inline constexpr char kProjectAutosaveEnabledKey[] =
    "project/autosave_enabled";
inline constexpr char kProjectAutosaveIntervalSecondsKey[] =
    "project/autosave_interval_seconds";
inline constexpr char kProjectAutosaveRetentionKey[] =
    "project/autosave_retention";

inline constexpr int kDefaultProjectAutosaveIntervalSeconds = 30;
inline constexpr int kMinimumProjectAutosaveIntervalSeconds = 10;
inline constexpr int kMaximumProjectAutosaveIntervalSeconds = 300;
inline constexpr int kDefaultProjectAutosaveRetention = 5;
inline constexpr int kMinimumProjectAutosaveRetention = 5;
inline constexpr int kMaximumProjectAutosaveRetention = 20;

[[nodiscard]] bool previewPerformanceMetricsEnabled();
void setPreviewPerformanceMetricsEnabled(bool enabled);

[[nodiscard]] bool projectAutosaveEnabled();
void setProjectAutosaveEnabled(bool enabled);
[[nodiscard]] int projectAutosaveIntervalSeconds();
void setProjectAutosaveIntervalSeconds(int seconds);
[[nodiscard]] int projectAutosaveRetention();
void setProjectAutosaveRetention(int count);

} // namespace settings
