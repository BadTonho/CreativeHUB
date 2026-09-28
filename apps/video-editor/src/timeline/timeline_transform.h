#pragma once

#include <creative_suite/animation/animation.h>

namespace timeline {

using creative_suite::animation::Keyframe;
using creative_suite::animation::Transform2D;
using creative_suite::animation::TransformKeyframes;
using creative_suite::animation::TransformProperty;
using creative_suite::animation::evaluateProperty;
using creative_suite::animation::evaluateTransform;
using creative_suite::animation::keyframesFor;
using creative_suite::animation::removeKeyframe;
using creative_suite::animation::setKeyframe;
using creative_suite::animation::validKeyframeValue;
using creative_suite::animation::validTransform;

// Clip splitting and trimming are Video Editor timeline operations.
[[nodiscard]] TransformKeyframes splitKeyframes(
    const Transform2D& base,
    const TransformKeyframes& keyframes,
    std::int64_t split_frame,
    Transform2D& right_base) noexcept;

[[nodiscard]] TransformKeyframes trimKeyframes(
    const Transform2D& base,
    const TransformKeyframes& keyframes,
    std::int64_t old_start_frame,
    std::int64_t new_start_frame,
    std::int64_t new_duration_frames,
    Transform2D& new_base) noexcept;

} // namespace timeline
