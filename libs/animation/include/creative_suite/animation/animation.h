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

// Position is normalized to the composition canvas, scale must be positive,
// rotation is in degrees, and opacity is in [0, 1].
struct Transform2D {
    double position_x = 0.5;
    double position_y = 0.5;
    double scale = 1.0;
    double rotation_degrees = 0.0;
    double opacity = 1.0;

    friend bool operator==(const Transform2D&, const Transform2D&) = default;
};

struct Keyframe {
    // Frame numbers are local to the animated layer/property.
    std::int64_t frame = 0;
    double value = 0.0;

    friend bool operator==(const Keyframe&, const Keyframe&) = default;
};

struct TransformKeyframes {
    // Keep each property list sorted by frame. setKeyframe maintains this
    // order; evaluateProperty uses the first and last entries to clamp time.
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

// Evaluation clamps to the endpoint values outside the keyframe range and
// linearly interpolates between adjacent keyframes.
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
