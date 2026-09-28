#pragma once

#include "media_error.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace creative_suite::media {

enum class MediaKind {
    Video,
    Image,
};

struct AudioMetadata {
    std::string codec;
    int sample_rate = 0;
    int channel_count = 0;
    std::optional<double> duration_seconds;

    friend bool operator==(const AudioMetadata&, const AudioMetadata&) = default;
};

struct VideoMetadata {
    MediaKind kind = MediaKind::Video;
    std::filesystem::path source_path;
    std::string display_name;
    std::string container_format;
    std::string video_codec;
    int width = 0;
    int height = 0;
    std::optional<double> frame_rate;
    std::optional<double> duration_seconds;
    std::optional<std::int64_t> frame_count;
    std::optional<AudioMetadata> audio;
};

} // namespace creative_suite::media
