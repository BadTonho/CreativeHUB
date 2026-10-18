#pragma once

#include "composition_document.h"

namespace motion::model {

// Evaluated local values only: no document, curves, media or history ownership.
// Throws on nonfinite/noninvertible geometry. The caller applies editing limits.
struct AnchorRepositionValues {
    AnchorPoint anchor;
    double position_x;
    double position_y;
};

[[nodiscard]] AnchorRepositionValues compensatedAnchorValues(
    const creative_suite::animation::Transform2D& evaluated, double scale_y,
    CanvasSize canvas, AnchorPoint previous, AnchorPoint requested);

} // namespace motion::model
