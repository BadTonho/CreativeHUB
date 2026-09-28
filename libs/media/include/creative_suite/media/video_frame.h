#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace creative_suite::media {

// Owned, tightly or explicitly-strided, 8-bit RGBA pixels. stride is the
// number of bytes per row; storage must contain at least stride * height
// bytes. The channels use straight alpha; this type does not impose a color
// space.
struct RgbaFrame {
    int width = 0;
    int height = 0;
    int stride = 0;
    std::vector<std::uint8_t> rgba_pixels;
};

using RgbaFramePtr = std::shared_ptr<const RgbaFrame>;
using VideoFrame = RgbaFrame;
using VideoFramePtr = RgbaFramePtr;

} // namespace creative_suite::media
