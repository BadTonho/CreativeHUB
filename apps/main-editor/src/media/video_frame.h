#pragma once

#include <cstdint>
#include <vector>

namespace media {

struct VideoFrame {
    int width = 0;
    int height = 0;
    int stride = 0;
    std::vector<std::uint8_t> rgba_pixels;
};

} // namespace media
