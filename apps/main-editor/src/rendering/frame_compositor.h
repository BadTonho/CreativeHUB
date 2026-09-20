#pragma once

#include "../media/video_frame.h"
#include "../timeline/timeline_transform.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace rendering {

struct CompositionLayer {
    const media::VideoFrame* frame = nullptr;
    timeline::Transform2D transform;
};

class FrameCompositor final {
public:
    [[nodiscard]] static std::optional<media::VideoFrame> compose(
        int width,
        int height,
        const std::vector<CompositionLayer>& layers);
};

} // namespace rendering
