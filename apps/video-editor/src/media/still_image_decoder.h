#pragma once

#include "video_frame.h"
#include "video_metadata.h"

#include <filesystem>

namespace media {

inline constexpr double kStillImageFrameRate = 30.0;
inline constexpr double kStillImageDurationSeconds = 5.0;
inline constexpr std::int64_t kStillImageFrameCount = 150;

class StillImageDecoder final {
public:
    [[nodiscard]] VideoMetadata probe(const std::filesystem::path& source_path) const;
    [[nodiscard]] VideoFrame decode_first_frame(
        const std::filesystem::path& source_path) const;

    [[nodiscard]] static bool supportsPath(
        const std::filesystem::path& source_path) noexcept;
};

} // namespace media
