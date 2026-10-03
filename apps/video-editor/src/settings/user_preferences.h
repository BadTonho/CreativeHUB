#pragma once

namespace settings {

enum class AudioWaveformDisplayMode : int {
    Mono = 0,
    Stereo = 1,
};

inline constexpr char kGpuCompositionEnabledKey[] = "performance/gpu_composition_enabled";
[[nodiscard]] bool gpuCompositionEnabled();
void setGpuCompositionEnabled(bool enabled);

inline constexpr char kPreviewMetricsEnabledKey[] =
    "performance/preview_metrics_enabled";
inline constexpr char kProjectAutosaveEnabledKey[] =
    "project/autosave_enabled";
inline constexpr char kProjectAutosaveIntervalSecondsKey[] =
    "project/autosave_interval_seconds";
inline constexpr char kProjectAutosaveRetentionKey[] =
    "project/autosave_retention";
inline constexpr char kMonitorVolumePercentKey[] =
    "playback/monitor_volume_percent";
inline constexpr char kAudioWaveformDisplayModeKey[] =
    "timeline/audio_waveform_display_mode";

inline constexpr int kDefaultProjectAutosaveIntervalSeconds = 30;
inline constexpr int kMinimumProjectAutosaveIntervalSeconds = 10;
inline constexpr int kMaximumProjectAutosaveIntervalSeconds = 300;
inline constexpr int kDefaultProjectAutosaveRetention = 5;
inline constexpr int kMinimumProjectAutosaveRetention = 5;
inline constexpr int kMaximumProjectAutosaveRetention = 20;
inline constexpr int kDefaultMonitorVolumePercent = 100;
inline constexpr int kMinimumMonitorVolumePercent = 0;
inline constexpr int kMaximumMonitorVolumePercent = 200;

[[nodiscard]] bool previewPerformanceMetricsEnabled();
void setPreviewPerformanceMetricsEnabled(bool enabled);

[[nodiscard]] bool projectAutosaveEnabled();
void setProjectAutosaveEnabled(bool enabled);
[[nodiscard]] int projectAutosaveIntervalSeconds();
void setProjectAutosaveIntervalSeconds(int seconds);
[[nodiscard]] int projectAutosaveRetention();
void setProjectAutosaveRetention(int count);
[[nodiscard]] int monitorVolumePercent();
void setMonitorVolumePercent(int percent);
[[nodiscard]] AudioWaveformDisplayMode audioWaveformDisplayMode();
void setAudioWaveformDisplayMode(AudioWaveformDisplayMode mode);

} // namespace settings
