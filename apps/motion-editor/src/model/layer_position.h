#pragma once
#include "layer_transform_hierarchy.h"

namespace motion::model {
[[nodiscard]] bool validLayerPosition(double x, double y) noexcept;
// inverse_linear is inverse(linear(ParentWorld * Binding)), excluding the
// selected layer's own rotation/scale/Anchor. Deltas are composition pixels.
[[nodiscard]] std::array<double, 2> positionFromCanvasDelta(
    double initial_x, double initial_y, CanvasSize,
    const AffineTransform& inverse_linear, double dx, double dy);
} // namespace motion::model
