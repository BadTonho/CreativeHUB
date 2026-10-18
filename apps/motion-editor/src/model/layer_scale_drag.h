#pragma once
#include "layer_transform_hierarchy.h"

namespace motion::model {
enum class ScaleDragControl : std::uint8_t { X, Y, Diagonal };
inline constexpr double minimum_drag_scale = 1e-6, maximum_drag_scale = 1e6;
[[nodiscard]] bool validDragScale(double) noexcept;
// Axes = linear(ParentWorld * Binding) * R(local rotation), without own scale.
[[nodiscard]] std::array<std::array<double,2>,3> scaleDragDirections(const AffineTransform& axes);

class ScaleDragCalculation {
public:
    ScaleDragCalculation(ScaleDragControl, bool linked, std::array<double,2> initial,
        AffineTransform axes, std::array<double,2> initial_lever);
    [[nodiscard]] std::array<double,2> evaluate(double dx, double dy) const;
    [[nodiscard]] std::array<double,2> indicator(std::array<double,2> values) const;
private:
    ScaleDragControl control_;
    bool linked_;
    std::array<double,2> initial_, lever_, local_lever_;
    AffineTransform axes_, inverse_;
};
} // namespace motion::model
