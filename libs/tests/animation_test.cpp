#include <creative_suite/animation/animation.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace creative_suite::animation;

    Transform2D base;
    TransformKeyframes keyframes;
    if (!setKeyframe(keyframes, TransformProperty::PositionX, 0, 0.0) ||
        !setKeyframe(keyframes, TransformProperty::PositionX, 10, 1.0)) {
        std::cerr << "Could not create animation keyframes.\n";
        return 1;
    }

    const auto evaluated = evaluateTransform(base, keyframes, 5);
    if (std::abs(evaluated.position_x - 0.5) > 1e-12 ||
        evaluateProperty(base, keyframes, TransformProperty::PositionX, -1) != 0.0 ||
        evaluateProperty(base, keyframes, TransformProperty::PositionX, 11) != 1.0) {
        std::cerr << "Shared linear keyframe evaluation changed.\n";
        return 1;
    }

    if (setKeyframe(keyframes, TransformProperty::Opacity, 2, 1.1) ||
        setKeyframe(keyframes, TransformProperty::Scale, -1, 1.0) ||
        !validTransform(base)) {
        std::cerr << "Shared transform validation changed.\n";
        return 1;
    }

    return 0;
}
