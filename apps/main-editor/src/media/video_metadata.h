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
    explicit MediaError(std::string message, std::optional<int> error_code = std::nullopt)
        : std::runtime_error(std::move(message)), error_code_(error_code) {}

    [[nodiscard]] std::optional<int> error_code() const noexcept {
        return error_code_;
    }

private:
    std::optional<int> error_code_;
};

} // namespace media
