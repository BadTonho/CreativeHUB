#pragma once

#include "../media/video_frame.h"
#include "../timeline/timeline_transform.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace rendering {

struct AlphaSpan {
    int begin = 0;
    int end = 0;
};

struct AlphaCoverage {
    int width = 0;
    int height = 0;
    std::vector<std::vector<AlphaSpan>> rows;
};

using AlphaCoveragePtr = std::shared_ptr<const AlphaCoverage>;

struct CompositionLayer {
    const media::VideoFrame* frame = nullptr;
    timeline::Transform2D transform;
    AlphaCoveragePtr alpha_coverage;
};

class FrameCompositor final {
public:
    [[nodiscard]] static AlphaCoveragePtr buildAlphaCoverage(
        const media::VideoFrame& frame);

    [[nodiscard]] static bool canUseAlphaCoverageFastPath(
        const CompositionLayer& layer) noexcept;

    [[nodiscard]] static std::optional<media::VideoFrame> compose(
        int width,
        int height,
        const std::vector<CompositionLayer>& layers);
};

} // namespace rendering
