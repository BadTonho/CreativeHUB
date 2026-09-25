#pragma once

#include "project/project_document.h"

#include <QString>

#include <cstdint>

namespace ui {

enum class RenderQualityPreset {
    Low,
    Standard,
    High,
    Custom,
};

struct RenderJobSettings {
    QString output_path;
    QString container_name;
    QString video_encoder_name;
    QString audio_encoder_name;
    int width = 1920;
    int height = 1080;
    double frame_rate = 30.0;
    bool export_audio = true;
    double video_bitrate_mbps = 10.0;
    int audio_bitrate_kbps = 192;
    RenderQualityPreset quality_preset = RenderQualityPreset::Standard;
};

struct RenderJob {
    std::uint64_t id = 0;
    QString display_name;
    RenderJobSettings settings;
    project::ProjectDocument project_snapshot;
};

}  // namespace ui
