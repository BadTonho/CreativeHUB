#pragma once

#include "media_library.h"

#include <filesystem>

namespace creative_suite::media {

class VideoDecoder final {
public:
    [[nodiscard]] VideoFrame decode_first_frame(
        const std::filesystem::path& source_path) const;
};

} // namespace creative_suite::media
