#pragma once

#include "model/composition_document.h"

#include <creative_suite/media/video_frame.h>

#include <optional>

namespace motion::ui {

// Rasterizes Motion Studio text and vector shapes into transparent RGBA8 frames.
// Rendering remains application-owned and provisional while the preview uses
// the shared CPU compositor.
// Text and primitive shapes only. Bezier paths require rasterizePath and its
// explicit local raster origin; discarding that origin would move the layer.
[[nodiscard]] std::optional<creative_suite::media::RgbaFrame> rasterizeLayerContent(
    const model::LayerContent& content);

} // namespace motion::ui
