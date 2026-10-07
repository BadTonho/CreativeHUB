#pragma once

namespace image_editor {

class MagicWandConfig final {
public:
    static constexpr int kDefaultTolerance = 0;
    static constexpr int kMinimumTolerance = 0;
    static constexpr int kMaximumTolerance = 255;
    static constexpr int kConnectivity = 4;
    static constexpr int kVerticalNeighborCount = 2;
};

} // namespace image_editor
