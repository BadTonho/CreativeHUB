#include "timeline_transform.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace timeline {
namespace {

std::vector<Keyframe>& mutableKeyframes(
    TransformKeyframes& keyframes,
    TransformProperty property) noexcept {
    switch (property) {
    case TransformProperty::PositionX: return keyframes.position_x;
    case TransformProperty::PositionY: return keyframes.position_y;
    case TransformProperty::Scale: return keyframes.scale;
    case TransformProperty::Rotation: return keyframes.rotation;
    case TransformProperty::Opacity: return keyframes.opacity;
    }
    return keyframes.position_x;
}

const std::vector<Keyframe>& keyframesForImpl(
    const TransformKeyframes& keyframes,
    TransformProperty property) noexcept {
    switch (property) {
    case TransformProperty::PositionX: return keyframes.position_x;
    case TransformProperty::PositionY: return keyframes.position_y;
    case TransformProperty::Scale: return keyframes.scale;
    case TransformProperty::Rotation: return keyframes.rotation;
    case TransformProperty::Opacity: return keyframes.opacity;
    }
    return keyframes.position_x;
}

double baseValue(const Transform2D& base, TransformProperty property) noexcept {
    switch (property) {
    case TransformProperty::PositionX: return base.position_x;
    case TransformProperty::PositionY: return base.position_y;
    case TransformProperty::Scale: return base.scale;
    case TransformProperty::Rotation: return base.rotation_degrees;
    case TransformProperty::Opacity: return base.opacity;
    }
    return 0.0;
}

void setBaseValue(
    Transform2D& base,
    TransformProperty property,
    double value) noexcept {
    switch (property) {
    case TransformProperty::PositionX: base.position_x = value; break;
    case TransformProperty::PositionY: base.position_y = value; break;
    case TransformProperty::Scale: base.scale = value; break;
    case TransformProperty::Rotation: base.rotation_degrees = value; break;
    case TransformProperty::Opacity: base.opacity = value; break;
    }
}

} // namespace

bool validTransform(const Transform2D& transform) noexcept {
    return std::isfinite(transform.position_x) &&
        std::isfinite(transform.position_y) &&
        std::isfinite(transform.scale) && transform.scale > 0.0 &&
        std::isfinite(transform.rotation_degrees) &&
        std::isfinite(transform.opacity) &&
        transform.opacity >= 0.0 && transform.opacity <= 1.0;
}

bool validKeyframeValue(TransformProperty property, double value) noexcept {
    if (!std::isfinite(value)) return false;
    if (property == TransformProperty::Scale) return value > 0.0;
    if (property == TransformProperty::Opacity) return value >= 0.0 && value <= 1.0;
    return true;
}

const std::vector<Keyframe>& keyframesFor(
    const TransformKeyframes& keyframes,
    TransformProperty property) noexcept {
    return keyframesForImpl(keyframes, property);
}

double evaluateProperty(
    const Transform2D& base,
    const TransformKeyframes& keyframes,
    TransformProperty property,
    std::int64_t local_frame) noexcept {
    const auto& values = keyframesForImpl(keyframes, property);
    if (values.empty()) return baseValue(base, property);
    if (local_frame <= values.front().frame) return values.front().value;
    if (local_frame >= values.back().frame) return values.back().value;
    const auto upper = std::upper_bound(values.begin(), values.end(), local_frame,
        [](std::int64_t frame, const Keyframe& keyframe) {
            return frame < keyframe.frame;
        });
    const auto& right = *upper;
    const auto& left = *(upper - 1);
    const auto distance = right.frame - left.frame;
    if (distance <= 0) return right.value;
    const double fraction = static_cast<double>(local_frame - left.frame) /
        static_cast<double>(distance);
    return left.value + (right.value - left.value) * fraction;
}

Transform2D evaluateTransform(
    const Transform2D& base,
    const TransformKeyframes& keyframes,
    std::int64_t local_frame) noexcept {
    return Transform2D{
        evaluateProperty(base, keyframes, TransformProperty::PositionX, local_frame),
        evaluateProperty(base, keyframes, TransformProperty::PositionY, local_frame),
        evaluateProperty(base, keyframes, TransformProperty::Scale, local_frame),
        evaluateProperty(base, keyframes, TransformProperty::Rotation, local_frame),
        evaluateProperty(base, keyframes, TransformProperty::Opacity, local_frame)};
}

bool setKeyframe(
    TransformKeyframes& keyframes,
    TransformProperty property,
    std::int64_t local_frame,
    double value) noexcept {
    if (local_frame < 0 || !validKeyframeValue(property, value)) return false;
    auto& values = mutableKeyframes(keyframes, property);
    const auto found = std::find_if(values.begin(), values.end(),
        [local_frame](const auto& keyframe) { return keyframe.frame == local_frame; });
    if (found != values.end()) {
        found->value = value;
        return true;
    }
    values.push_back({local_frame, value});
    std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
        return left.frame < right.frame;
    });
    return true;
}

bool removeKeyframe(
    TransformKeyframes& keyframes,
    TransformProperty property,
    std::int64_t local_frame) noexcept {
    auto& values = mutableKeyframes(keyframes, property);
    const auto found = std::find_if(values.begin(), values.end(),
        [local_frame](const auto& keyframe) { return keyframe.frame == local_frame; });
    if (found == values.end()) return false;
    values.erase(found);
    return true;
}

TransformKeyframes splitKeyframes(
    const Transform2D& base,
    const TransformKeyframes& keyframes,
    std::int64_t split_frame,
    Transform2D& right_base) noexcept {
    right_base = evaluateTransform(base, keyframes, split_frame);
    TransformKeyframes right;
    for (const auto property : {TransformProperty::PositionX,
                                TransformProperty::PositionY,
                                TransformProperty::Scale,
                                TransformProperty::Rotation,
                                TransformProperty::Opacity}) {
        for (const auto& keyframe : keyframesForImpl(keyframes, property)) {
            if (keyframe.frame >= split_frame) {
                static_cast<void>(setKeyframe(
                    right, property, keyframe.frame - split_frame, keyframe.value));
            }
        }
    }
    return right;
}

TransformKeyframes trimKeyframes(
    const Transform2D& base,
    const TransformKeyframes& keyframes,
    std::int64_t old_start_frame,
    std::int64_t new_start_frame,
    std::int64_t new_duration_frames,
    Transform2D& new_base) noexcept {
    const auto new_end = new_start_frame + new_duration_frames;
    new_base = new_start_frame == old_start_frame
        ? base
        : evaluateTransform(base, keyframes, new_start_frame - old_start_frame);
    TransformKeyframes result;
    for (const auto property : {TransformProperty::PositionX,
                                TransformProperty::PositionY,
                                TransformProperty::Scale,
                                TransformProperty::Rotation,
                                TransformProperty::Opacity}) {
        for (const auto& keyframe : keyframesForImpl(keyframes, property)) {
            if (keyframe.frame >= new_start_frame && keyframe.frame < new_end) {
                static_cast<void>(setKeyframe(
                    result, property, keyframe.frame - new_start_frame, keyframe.value));
            }
        }
    }
    return result;
}

} // namespace timeline
