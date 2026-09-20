#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace media {

struct VideoMetadata {
    std::filesystem::path source_path;
    std::string display_name;
    std::string container_format;
    std::string video_codec;
    int width = 0;
    int height = 0;
    std::optional<double> frame_rate;
    std::optional<double> duration_seconds;
    std::optional<std::int64_t> frame_count;
};

class MediaError final : public std::runtime_error {
public:
    explicit MediaError(std::string message)
        : std::runtime_error(std::move(message)) {}
};

} // namespace media
