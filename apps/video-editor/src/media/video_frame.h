#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace media {

struct VideoFrame {
    int width = 0;
    int height = 0;
    int stride = 0;
    std::vector<std::uint8_t> rgba_pixels;
};

using VideoFramePtr = std::shared_ptr<const VideoFrame>;

} // namespace media
