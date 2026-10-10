#pragma once

#include "timeline/timeline_row_height_mode.h"

namespace settings {

enum class AudioWaveformDisplayMode : int {
    Mono = 0,
    Stereo = 1,
};

enum class WorkspacePageTransitionStyle : int {
    WorkspaceContent = 0,
    EntireApplicationWindow = 1,
};

inline constexpr char kGpuCompositionEnabledKey[] = "performance/gpu_composition_enabled";
[[nodiscard]] bool gpuCompositionEnabled();
void setGpuCompositionEnabled(bool enabled);
inline constexpr char kHardwareDecodingEnabledKey[] = "performance/hardware_decoding_enabled";
[[nodiscard]] bool hardwareDecodingEnabled();
void setHardwareDecodingEnabled(bool enabled);

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
inline constexpr char kTimelineTrackGroupSplitRatioKey[] =
    "timeline/track_group_split_ratio";
inline constexpr char kTimelineTrackRowHeightAdjustmentModeKey[] =
    "timeline/track_row_height_adjustment_mode";
inline constexpr char kWorkspacePageTransitionsEnabledKey[] =
    "workspace/page_transitions_enabled";
inline constexpr char kWorkspacePageTransitionDurationMsKey[] =
    "workspace/page_transition_duration_ms";
inline constexpr char kWorkspacePageTransitionStyleKey[] =
    "workspace/page_transition_style";

inline constexpr int kDefaultProjectAutosaveIntervalSeconds = 30;
inline constexpr int kMinimumProjectAutosaveIntervalSeconds = 10;
inline constexpr int kMaximumProjectAutosaveIntervalSeconds = 300;
inline constexpr int kDefaultProjectAutosaveRetention = 5;
inline constexpr int kMinimumProjectAutosaveRetention = 5;
inline constexpr int kMaximumProjectAutosaveRetention = 20;
inline constexpr int kDefaultMonitorVolumePercent = 100;
inline constexpr int kMinimumMonitorVolumePercent = 0;
inline constexpr int kMaximumMonitorVolumePercent = 200;
inline constexpr double kDefaultTimelineTrackGroupSplitRatio = 0.5;
inline constexpr double kMinimumTimelineTrackGroupSplitRatio = 0.2;
inline constexpr double kMaximumTimelineTrackGroupSplitRatio = 0.8;
inline constexpr timeline::TrackRowHeightAdjustmentMode
    kDefaultTimelineTrackRowHeightAdjustmentMode =
        timeline::TrackRowHeightAdjustmentMode::Together;
inline constexpr bool kDefaultWorkspacePageTransitionsEnabled = true;
inline constexpr WorkspacePageTransitionStyle
    kDefaultWorkspacePageTransitionStyle =
        WorkspacePageTransitionStyle::WorkspaceContent;
inline constexpr int kDefaultWorkspacePageTransitionDurationMs = 250;
inline constexpr int kMinimumWorkspacePageTransitionDurationMs = 100;
inline constexpr int kMaximumWorkspacePageTransitionDurationMs = 600;
inline constexpr int kWorkspacePageTransitionDurationStepMs = 25;

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
[[nodiscard]] double timelineTrackGroupSplitRatio();
void setTimelineTrackGroupSplitRatio(double ratio);
[[nodiscard]] timeline::TrackRowHeightAdjustmentMode
timelineTrackRowHeightAdjustmentMode();
void setTimelineTrackRowHeightAdjustmentMode(
    timeline::TrackRowHeightAdjustmentMode mode);
[[nodiscard]] bool workspacePageTransitionsEnabled();
void setWorkspacePageTransitionsEnabled(bool enabled);
[[nodiscard]] WorkspacePageTransitionStyle workspacePageTransitionStyle();
void setWorkspacePageTransitionStyle(WorkspacePageTransitionStyle style);
[[nodiscard]] int workspacePageTransitionDurationMs();
void setWorkspacePageTransitionDurationMs(int duration_ms);

} // namespace settings
