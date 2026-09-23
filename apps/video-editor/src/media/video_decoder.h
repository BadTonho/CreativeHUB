#pragma once

#include "video_frame.h"

#include <filesystem>

namespace media {

class VideoDecoder final {
public:
    VideoFrame decode_first_frame(const std::filesystem::path& source_path) const;
};

} // namespace media
