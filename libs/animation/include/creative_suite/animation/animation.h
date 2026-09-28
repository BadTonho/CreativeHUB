#pragma once

#include <cstdint>
#include <vector>

namespace creative_suite::animation {

enum class TransformProperty {
    PositionX,
    PositionY,
    Scale,
    Rotation,
    Opacity,
};

// Position uses normalized composition-canvas coordinates and may lie outside
// [0, 1] to place a layer partly or fully off-canvas. Scale must be positive,
// rotation is in degrees, and opacity is in [0, 1]. All transform values must
// be finite.
struct Transform2D {
    double position_x = 0.5;
    double position_y = 0.5;
    double scale = 1.0;
    double rotation_degrees = 0.0;
    double opacity = 1.0;

    friend bool operator==(const Transform2D&, const Transform2D&) = default;
};

struct Keyframe {
    // Frame numbers are local to the animated layer/property and are
    // non-negative when inserted through setKeyframe().
    std::int64_t frame = 0;
    double value = 0.0;

    friend bool operator==(const Keyframe&, const Keyframe&) = default;
};

struct TransformKeyframes {
    // Each property list must have unique, ascending frame numbers.
    // setKeyframe() inserts/replaces and sorts; direct edits must preserve this
    // invariant. Evaluation clamps to the first and last keyframe.
    std::vector<Keyframe> position_x;
    std::vector<Keyframe> position_y;
    std::vector<Keyframe> scale;
    std::vector<Keyframe> rotation;
    std::vector<Keyframe> opacity;

    friend bool operator==(const TransformKeyframes&, const TransformKeyframes&) = default;
};

[[nodiscard]] bool validTransform(const Transform2D& transform) noexcept;
[[nodiscard]] bool validKeyframeValue(
    TransformProperty property,
    double value) noexcept;

[[nodiscard]] double evaluateProperty(
    const Transform2D& base,
    const TransformKeyframes& keyframes,
    TransformProperty property,
    std::int64_t local_frame) noexcept;

// With no keyframes, evaluation returns the base value. Otherwise it clamps
// to endpoint values and linearly interpolates between adjacent keyframes;
// there is no easing or angular wraparound.
[[nodiscard]] Transform2D evaluateTransform(
    const Transform2D& base,
    const TransformKeyframes& keyframes,
    std::int64_t local_frame) noexcept;

[[nodiscard]] bool setKeyframe(
    TransformKeyframes& keyframes,
    TransformProperty property,
    std::int64_t local_frame,
    double value) noexcept;

[[nodiscard]] bool removeKeyframe(
    TransformKeyframes& keyframes,
    TransformProperty property,
    std::int64_t local_frame) noexcept;

[[nodiscard]] const std::vector<Keyframe>& keyframesFor(
    const TransformKeyframes& keyframes,
    TransformProperty property) noexcept;

} // namespace creative_suite::animation
