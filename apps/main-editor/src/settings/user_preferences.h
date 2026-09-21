#pragma once

namespace settings {

inline constexpr char kPreviewMetricsEnabledKey[] =
    "performance/preview_metrics_enabled";

[[nodiscard]] bool previewPerformanceMetricsEnabled();
void setPreviewPerformanceMetricsEnabled(bool enabled);

} // namespace settings
