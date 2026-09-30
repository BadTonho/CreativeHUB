#pragma once

#include "../model/composition_document.h"

#include <creative_suite/media/video_frame.h>

#include <functional>
#include <vector>

namespace motion::ui {

[[nodiscard]] bool hasEnabledLayerEffects(
    const std::vector<model::LayerEffect>& effects) noexcept;

// Applies an ordered Motion Studio effect stack to an owned straight-alpha
// RGBA8 frame. Returns false only when cancellation is requested.
[[nodiscard]] bool applyLayerEffects(
    creative_suite::media::RgbaFrame& frame,
    const std::vector<model::LayerEffect>& effects,
    const std::function<bool()>& should_cancel = {});

} // namespace motion::ui
