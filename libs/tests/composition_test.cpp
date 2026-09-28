#include <creative_suite/composition/frame_compositor.h>

#include <iostream>
#include <vector>

int main() {
    using namespace creative_suite;

    media::RgbaFrame source;
    source.width = 2;
    source.height = 2;
    source.stride = 8;
    source.rgba_pixels = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255};

    composition::CompositionLayer layer;
    layer.frame = &source;
    const auto composed = composition::FrameCompositor::compose(2, 2, {layer});
    if (!composed.has_value() || composed->rgba_pixels != source.rgba_pixels) {
        std::cerr << "Shared identity composition changed the RGBA pixels.\n";
        return 1;
    }

    return 0;
}
