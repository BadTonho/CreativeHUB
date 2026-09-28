#pragma once

#include "video_metadata.h"

namespace creative_suite::media {

class VideoProbe final {
public:
    [[nodiscard]] VideoMetadata probe(const std::filesystem::path& source_path) const;
};

} // namespace creative_suite::media
