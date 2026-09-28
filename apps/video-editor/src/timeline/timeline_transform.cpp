#include "timeline_transform.h"

namespace timeline {

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
        for (const auto& keyframe : keyframesFor(keyframes, property)) {
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
        for (const auto& keyframe : keyframesFor(keyframes, property)) {
            if (keyframe.frame >= new_start_frame && keyframe.frame < new_end) {
                static_cast<void>(setKeyframe(
                    result, property, keyframe.frame - new_start_frame, keyframe.value));
            }
        }
    }
    return result;
}

} // namespace timeline
