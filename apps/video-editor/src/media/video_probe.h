#pragma once

#include "video_metadata.h"

namespace media {

class VideoProbe final {
public:
    VideoMetadata probe(const std::filesystem::path& source_path) const;
};

} // namespace media
