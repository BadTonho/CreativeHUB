#pragma once

#include "../model/composition_document.h"

#include <creative_suite/media/video_frame.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace motion::ui {

namespace detail {
class LayerEffectWorkerPool;
}

enum class LayerEffectKind {
    GaussianBlur,
    ColorAdjustment,
};

using EffectTimingRecorder =
    std::function<void(LayerEffectKind effect, std::uint64_t duration_nanoseconds)>;

[[nodiscard]] bool hasEnabledLayerEffects(
    const std::vector<model::LayerEffect>& effects) noexcept;

// Applies an ordered Motion Studio effect stack to an owned straight-alpha
// RGBA8 frame. Blur timings measure wall time, including shared-pool queue and
// synchronization time. Returns false only when cancellation is requested.
[[nodiscard]] bool applyLayerEffects(
    creative_suite::media::RgbaFrame& frame,
    const std::vector<model::LayerEffect>& effects,
    const std::function<bool()>& should_cancel = {},
    const EffectTimingRecorder& record_effect_timing = {},
    detail::LayerEffectWorkerPool* worker_pool = nullptr);

} // namespace motion::ui
