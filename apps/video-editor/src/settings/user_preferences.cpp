#include "user_preferences.h"

#include <QSettings>
#include <QString>

#include <algorithm>
#include <cmath>

namespace settings {

bool gpuCompositionEnabled() {
    return QSettings().value(kGpuCompositionEnabledKey, false).toBool();
}

void setGpuCompositionEnabled(bool enabled) {
    QSettings().setValue(kGpuCompositionEnabledKey, enabled);
}

bool previewPerformanceMetricsEnabled() {
    constexpr bool default_enabled = true;
    QSettings settings;
    return settings.value(kPreviewMetricsEnabledKey, default_enabled).toBool();
}

void setPreviewPerformanceMetricsEnabled(bool enabled) {
    QSettings settings;
    settings.setValue(kPreviewMetricsEnabledKey, enabled);
}

bool projectAutosaveEnabled() {
    return QSettings().value(kProjectAutosaveEnabledKey, true).toBool();
}

void setProjectAutosaveEnabled(bool enabled) {
    QSettings settings;
    settings.setValue(kProjectAutosaveEnabledKey, enabled);
}

int projectAutosaveIntervalSeconds() {
    const auto value = QSettings().value(
        kProjectAutosaveIntervalSecondsKey,
        kDefaultProjectAutosaveIntervalSeconds).toInt();
    return std::clamp(
        value,
        kMinimumProjectAutosaveIntervalSeconds,
        kMaximumProjectAutosaveIntervalSeconds);
}

void setProjectAutosaveIntervalSeconds(int seconds) {
    QSettings settings;
    settings.setValue(
        kProjectAutosaveIntervalSecondsKey,
        std::clamp(
            seconds,
            kMinimumProjectAutosaveIntervalSeconds,
            kMaximumProjectAutosaveIntervalSeconds));
}

int projectAutosaveRetention() {
    const auto value = QSettings().value(
        kProjectAutosaveRetentionKey,
        kDefaultProjectAutosaveRetention).toInt();
    return std::clamp(
        value,
        kMinimumProjectAutosaveRetention,
        kMaximumProjectAutosaveRetention);
}

void setProjectAutosaveRetention(int count) {
    QSettings settings;
    settings.setValue(
        kProjectAutosaveRetentionKey,
        std::clamp(
            count,
            kMinimumProjectAutosaveRetention,
            kMaximumProjectAutosaveRetention));
}

int monitorVolumePercent() {
    const auto stored = QSettings().value(kMonitorVolumePercentKey);
    bool ok = false;
    const auto value = stored.toInt(&ok);
    if (!ok) return kDefaultMonitorVolumePercent;
    return std::clamp(
        value,
        kMinimumMonitorVolumePercent,
        kMaximumMonitorVolumePercent);
}

void setMonitorVolumePercent(int percent) {
    QSettings settings;
    settings.setValue(
        kMonitorVolumePercentKey,
        std::clamp(
            percent,
            kMinimumMonitorVolumePercent,
            kMaximumMonitorVolumePercent));
}

AudioWaveformDisplayMode audioWaveformDisplayMode() {
    const auto stored = QSettings().value(kAudioWaveformDisplayModeKey);
    bool ok = false;
    const auto value = stored.toInt(&ok);
    if (!ok || value != static_cast<int>(AudioWaveformDisplayMode::Stereo)) {
        return AudioWaveformDisplayMode::Mono;
    }
    return AudioWaveformDisplayMode::Stereo;
}

void setAudioWaveformDisplayMode(AudioWaveformDisplayMode mode) {
    if (mode != AudioWaveformDisplayMode::Mono &&
        mode != AudioWaveformDisplayMode::Stereo) {
        mode = AudioWaveformDisplayMode::Mono;
    }
    QSettings().setValue(kAudioWaveformDisplayModeKey, static_cast<int>(mode));
}

double timelineTrackGroupSplitRatio() {
    const auto stored = QSettings().value(
        kTimelineTrackGroupSplitRatioKey,
        kDefaultTimelineTrackGroupSplitRatio);
    bool ok = false;
    const auto value = stored.toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
        return kDefaultTimelineTrackGroupSplitRatio;
    }
    return std::clamp(value,
        kMinimumTimelineTrackGroupSplitRatio,
        kMaximumTimelineTrackGroupSplitRatio);
}

void setTimelineTrackGroupSplitRatio(double ratio) {
    if (!std::isfinite(ratio)) return;
    QSettings().setValue(
        kTimelineTrackGroupSplitRatioKey,
        std::clamp(ratio,
            kMinimumTimelineTrackGroupSplitRatio,
            kMaximumTimelineTrackGroupSplitRatio));
}

bool workspacePageTransitionsEnabled() {
    const auto stored = QSettings().value(
        kWorkspacePageTransitionsEnabledKey);
    if (!stored.isValid()) return kDefaultWorkspacePageTransitionsEnabled;

    const auto value = stored.toString().trimmed().toLower();
    if (value == QStringLiteral("true") || value == QStringLiteral("1")) {
        return true;
    }
    if (value == QStringLiteral("false") || value == QStringLiteral("0")) {
        return false;
    }
    return kDefaultWorkspacePageTransitionsEnabled;
}

void setWorkspacePageTransitionsEnabled(bool enabled) {
    QSettings().setValue(kWorkspacePageTransitionsEnabledKey, enabled);
}

WorkspacePageTransitionStyle workspacePageTransitionStyle() {
    const auto stored = QSettings().value(kWorkspacePageTransitionStyleKey);
    bool ok = false;
    const auto value = stored.toInt(&ok);
    if (!ok) return kDefaultWorkspacePageTransitionStyle;
    switch (static_cast<WorkspacePageTransitionStyle>(value)) {
    case WorkspacePageTransitionStyle::WorkspaceContent:
    case WorkspacePageTransitionStyle::EntireApplicationWindow:
        return static_cast<WorkspacePageTransitionStyle>(value);
    }
    return kDefaultWorkspacePageTransitionStyle;
}

void setWorkspacePageTransitionStyle(WorkspacePageTransitionStyle style) {
    switch (style) {
    case WorkspacePageTransitionStyle::WorkspaceContent:
    case WorkspacePageTransitionStyle::EntireApplicationWindow:
        QSettings().setValue(
            kWorkspacePageTransitionStyleKey,
            static_cast<int>(style));
        break;
    }
}

int workspacePageTransitionDurationMs() {
    const auto stored = QSettings().value(
        kWorkspacePageTransitionDurationMsKey);
    bool ok = false;
    const auto value = stored.toInt(&ok);
    if (!ok) return kDefaultWorkspacePageTransitionDurationMs;

    const auto clamped = std::clamp(
        value,
        kMinimumWorkspacePageTransitionDurationMs,
        kMaximumWorkspacePageTransitionDurationMs);
    const auto steps = (clamped - kMinimumWorkspacePageTransitionDurationMs +
                        kWorkspacePageTransitionDurationStepMs / 2) /
        kWorkspacePageTransitionDurationStepMs;
    return kMinimumWorkspacePageTransitionDurationMs +
        steps * kWorkspacePageTransitionDurationStepMs;
}

void setWorkspacePageTransitionDurationMs(int duration_ms) {
    const auto clamped = std::clamp(
        duration_ms,
        kMinimumWorkspacePageTransitionDurationMs,
        kMaximumWorkspacePageTransitionDurationMs);
    const auto steps = (clamped - kMinimumWorkspacePageTransitionDurationMs +
                        kWorkspacePageTransitionDurationStepMs / 2) /
        kWorkspacePageTransitionDurationStepMs;
    QSettings().setValue(
        kWorkspacePageTransitionDurationMsKey,
        kMinimumWorkspacePageTransitionDurationMs +
            steps * kWorkspacePageTransitionDurationStepMs);
}

} // namespace settings
