#include <creative_suite/animation/animation.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
    using namespace creative_suite::animation;

    try {
        Transform2D base;
        base.position_x = 0.25;
        base.opacity = 0.75;

        TransformKeyframes keyframes;
        require(evaluateProperty(
                    base, keyframes, TransformProperty::PositionX, 4) == 0.25,
                "An unkeyed property did not use its base value.");

        require(setKeyframe(keyframes, TransformProperty::PositionX, 10, 1.0) &&
                    setKeyframe(keyframes, TransformProperty::PositionX, 0, 0.0),
                "Keyframes could not be inserted out of order.");
        const auto& position_keys = keyframesFor(
            keyframes, TransformProperty::PositionX);
        require(position_keys.size() == 2 &&
                    position_keys[0].frame == 0 && position_keys[1].frame == 10,
                "Inserted keyframes were not sorted by frame.");

        require(evaluateProperty(
                    base, keyframes, TransformProperty::PositionX, -1) == 0.0 &&
                    evaluateProperty(
                        base, keyframes, TransformProperty::PositionX, 0) == 0.0 &&
                    std::abs(evaluateProperty(
                        base, keyframes, TransformProperty::PositionX, 5) - 0.5) < 1e-12 &&
                    evaluateProperty(
                        base, keyframes, TransformProperty::PositionX, 10) == 1.0 &&
                    evaluateProperty(
                        base, keyframes, TransformProperty::PositionX, 11) == 1.0,
                "Keyframe evaluation did not interpolate linearly and clamp endpoints.");

        require(setKeyframe(keyframes, TransformProperty::PositionX, 10, 0.8) &&
                    keyframesFor(keyframes, TransformProperty::PositionX).size() == 2 &&
                    evaluateProperty(
                        base, keyframes, TransformProperty::PositionX, 11) == 0.8,
                "Setting a keyframe at an existing frame did not replace it.");

        require(setKeyframe(keyframes, TransformProperty::Rotation, 2, 720.0) &&
                    setKeyframe(keyframes, TransformProperty::Scale, 2, 0.5) &&
                    setKeyframe(keyframes, TransformProperty::Opacity, 2, 0.0),
                "Valid rotation, scale, or opacity keyframes were rejected.");
        const auto evaluated = evaluateTransform(base, keyframes, 2);
        require(evaluated.rotation_degrees == 720.0 && evaluated.scale == 0.5 &&
                    evaluated.opacity == 0.0,
                "Transform properties were not evaluated independently.");

        require(!setKeyframe(keyframes, TransformProperty::PositionY, -1, 0.0) &&
                    !setKeyframe(keyframes, TransformProperty::Opacity, 0, 1.1) &&
                    !setKeyframe(keyframes, TransformProperty::Scale, 0, 0.0) &&
                    !setKeyframe(
                        keyframes,
                        TransformProperty::Rotation,
                        0,
                        std::numeric_limits<double>::infinity()),
                "Invalid frame numbers or keyframe values were accepted.");

        Transform2D off_canvas;
        off_canvas.position_x = 1.25;
        off_canvas.position_y = -0.25;
        require(validTransform(off_canvas),
                "Finite off-canvas normalized positions were rejected.");
        off_canvas.opacity = 1.01;
        require(!validTransform(off_canvas),
                "Opacity outside [0, 1] was accepted for a transform.");

        require(removeKeyframe(keyframes, TransformProperty::PositionX, 0) &&
                    !removeKeyframe(keyframes, TransformProperty::PositionX, 0),
                "Keyframe removal did not report whether it removed a key.");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
