#pragma once
#include "layer_transform_hierarchy.h"

namespace motion::model {
[[nodiscard]] bool validLayerRotation(double degrees) noexcept;
// Returns the projected local -Y direction in canvas pixels. Translation and
// own scale/Anchor do not affect the direction; no affine decomposition occurs.
[[nodiscard]] std::array<double, 2> rotationGuideDirection(const AffineTransform& outer, double degrees);

// Lightweight, media/Qt-free angular state. Coordinates and dead radius are in
// canvas pixels; inverse_linear is inverse(linear(ParentWorld * Binding)).
class RotationDragCalculation {
public:
    RotationDragCalculation(double initial, const AffineTransform& inverse_linear,
        double pivot_x, double pivot_y, double press_x, double press_y, double dead_radius);
    [[nodiscard]] double update(double pointer_x, double pointer_y);
private:
    [[nodiscard]] std::optional<double> angle(double x, double y) const;
    AffineTransform inverse_;
    double initial_, pivot_x_, pivot_y_, dead_radius_, total_ = 0;
    std::optional<double> previous_;
};
} // namespace motion::model
