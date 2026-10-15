#pragma once
#include <creative_suite/effects/effects.h>
#include <cstddef>

namespace creative_suite::effects {
using CancellationPredicate = std::function<bool()>;
using RangeOperation = std::function<void(std::size_t, std::size_t, const CancellationPredicate&)>;
// Synchronous executor: finish every range before returning. False means cancellation.
// Omit the executor for sequential processing. No executor or frame is retained.
using RangeExecutor = std::function<bool(std::size_t, const CancellationPredicate&, const RangeOperation&)>;
// Three box pairs approximate Gaussian Blur using Motion's existing byte rounding,
// clamped borders and premultiplied alpha. Radius is finite [0,100] receiver pixels.
// Invalid input is unchanged; cancellation can leave partial data to be discarded.
// Callback/allocation exceptions propagate to the caller's diagnostic boundary.
[[nodiscard]] ProcessingResult applyGaussianBlur(media::RgbaFrame& frame, double radius_pixels,
    const CancellationPredicate& should_cancel = {}, const RangeExecutor& executor = {});
} // namespace creative_suite::effects
