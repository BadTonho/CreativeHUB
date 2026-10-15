#include <array>
#include <stdexcept>
#include <creative_suite/animation/animation.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace creative_suite::animation {
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

double cubicCoordinate(double t, double first_control, double second_control) noexcept {
    const double inverse = 1.0 - t;
    return 3.0 * inverse * inverse * t * first_control +
        3.0 * inverse * t * t * second_control + t * t * t;
}

double applyEasing(double progress, const CubicBezierEasing& easing) noexcept {
    // Bisection is deterministic and robust for the monotonic x curve. Frame
    // evaluation does not need sub-frame precision beyond double accuracy.
    double low = 0.0;
    double high = 1.0;
    for (int iteration = 0; iteration < 48; ++iteration) {
        const double middle = (low + high) * 0.5;
        if (cubicCoordinate(middle, easing.x1, easing.x2) < progress) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return cubicCoordinate((low + high) * 0.5, easing.y1, easing.y2);
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

bool validCubicBezierEasing(const CubicBezierEasing& easing) noexcept {
    return std::isfinite(easing.x1) && std::isfinite(easing.y1) &&
        std::isfinite(easing.x2) && std::isfinite(easing.y2) &&
        easing.x1 >= 0.0 && easing.x1 <= easing.x2 && easing.x2 <= 1.0 &&
        easing.y1 >= 0.0 && easing.y1 <= 1.0 &&
        easing.y2 >= 0.0 && easing.y2 <= 1.0;
}

bool validKeyframeInterpolation(
    InterpolationMode interpolation,
    const CubicBezierEasing& easing) noexcept {
    return (interpolation == InterpolationMode::Linear ||
            interpolation == InterpolationMode::CubicBezier) &&
        validCubicBezierEasing(easing);
}

bool validTransformKeyframes(const TransformKeyframes& keyframes) noexcept {
    const auto valid_property = [](const std::vector<Keyframe>& values,
                                   TransformProperty property) {
        std::int64_t previous = -1;
        for (const auto& keyframe : values) {
            if (keyframe.frame < 0 || keyframe.frame <= previous ||
                !validKeyframeValue(property, keyframe.value) ||
                !validKeyframeInterpolation(keyframe.interpolation, keyframe.easing)) {
                return false;
            }
            previous = keyframe.frame;
        }
        return true;
    };
    return valid_property(keyframes.position_x, TransformProperty::PositionX) &&
        valid_property(keyframes.position_y, TransformProperty::PositionY) &&
        valid_property(keyframes.scale, TransformProperty::Scale) &&
        valid_property(keyframes.rotation, TransformProperty::Rotation) &&
        valid_property(keyframes.opacity, TransformProperty::Opacity);
}

bool validScalarKeyframes(const ScalarKeyframes& keyframes) noexcept {
    std::int64_t previous = -1;
    for (const auto& keyframe : keyframes) {
        if (keyframe.frame < 0 || keyframe.frame <= previous ||
            !std::isfinite(keyframe.value) ||
            !validKeyframeInterpolation(keyframe.interpolation, keyframe.easing)) {
            return false;
        }
        previous = keyframe.frame;
    }
    return true;
}

double evaluateEasing(
    double progress,
    InterpolationMode interpolation,
    const CubicBezierEasing& easing) noexcept {
    if (!std::isfinite(progress)) return 0.0;
    progress = std::clamp(progress, 0.0, 1.0);
    if (interpolation != InterpolationMode::CubicBezier ||
        !validKeyframeInterpolation(interpolation, easing)) {
        return progress;
    }
    return applyEasing(progress, easing);
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
    double fraction = static_cast<double>(local_frame - left.frame) /
        static_cast<double>(distance);
    fraction = evaluateEasing(fraction, left.interpolation, left.easing);
    return left.value + (right.value - left.value) * fraction;
}

double evaluateScalar(
    double base_value,
    const ScalarKeyframes& keyframes,
    std::int64_t local_frame) noexcept {
    if (keyframes.empty()) return base_value;
    if (local_frame <= keyframes.front().frame) return keyframes.front().value;
    if (local_frame >= keyframes.back().frame) return keyframes.back().value;
    const auto upper = std::upper_bound(keyframes.begin(), keyframes.end(), local_frame,
        [](std::int64_t frame, const Keyframe& keyframe) {
            return frame < keyframe.frame;
        });
    const auto& right = *upper;
    const auto& left = *(upper - 1);
    const auto distance = right.frame - left.frame;
    if (distance <= 0) return right.value;
    double fraction = static_cast<double>(local_frame - left.frame) /
        static_cast<double>(distance);
    fraction = evaluateEasing(fraction, left.interpolation, left.easing);
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

bool setKeyframe(
    ScalarKeyframes& keyframes,
    std::int64_t local_frame,
    double value) noexcept {
    if (local_frame < 0 || !std::isfinite(value)) return false;
    const auto found = std::find_if(keyframes.begin(), keyframes.end(),
        [local_frame](const auto& keyframe) { return keyframe.frame == local_frame; });
    if (found != keyframes.end()) {
        found->value = value;
        return true;
    }
    keyframes.push_back({local_frame, value});
    std::sort(keyframes.begin(), keyframes.end(), [](const auto& left, const auto& right) {
        return left.frame < right.frame;
    });
    return true;
}

bool removeKeyframe(
    ScalarKeyframes& keyframes,
    std::int64_t local_frame) noexcept {
    const auto found = std::find_if(keyframes.begin(), keyframes.end(),
        [local_frame](const auto& keyframe) { return keyframe.frame == local_frame; });
    if (found == keyframes.end()) return false;
    keyframes.erase(found);
    return true;
}

bool setKeyframeInterpolation(
    TransformKeyframes& keyframes,
    TransformProperty property,
    std::int64_t local_frame,
    InterpolationMode interpolation,
    const CubicBezierEasing& easing) noexcept {
    if (local_frame < 0 || !validKeyframeInterpolation(interpolation, easing)) return false;
    auto& values = mutableKeyframes(keyframes, property);
    const auto found = std::find_if(values.begin(), values.end(),
        [local_frame](const auto& keyframe) { return keyframe.frame == local_frame; });
    if (found == values.end()) return false;
    if (found->interpolation == interpolation && found->easing == easing) return false;
    found->interpolation = interpolation;
    found->easing = easing;
    return true;
}

namespace {
struct CurvePoint { double x, y; };
CurvePoint mixCurvePoint(CurvePoint a, CurvePoint b, double t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}
using CurveSegment = std::array<CurvePoint, 4>;
std::pair<CurveSegment, CurveSegment> subdivideCurve(const CurveSegment& curve, double t) {
    const auto a = mixCurvePoint(curve[0], curve[1], t);
    const auto b = mixCurvePoint(curve[1], curve[2], t);
    const auto c = mixCurvePoint(curve[2], curve[3], t);
    const auto d = mixCurvePoint(a, b, t), e = mixCurvePoint(b, c, t);
    const auto f = mixCurvePoint(d, e, t);
    return {CurveSegment{curve[0], a, d, f}, CurveSegment{f, e, c, curve[3]}};
}
double curveParameterAtX(const CurveSegment& curve, double x) {
    double low = 0, high = 1;
    for (int iteration = 0; iteration < 60; ++iteration) {
        const auto middle = (low + high) * 0.5;
        const auto point = subdivideCurve(curve, middle).first.back();
        if (point.x < x) low = middle; else high = middle;
    }
    return (low + high) * 0.5;
}
}
creative_suite::animation::ScalarKeyframes sliceScalarCurve(
    const creative_suite::animation::ScalarKeyframes& source,
    std::int64_t delta, std::int64_t duration, double base) {
    if (duration <= 0 || delta > std::numeric_limits<std::int64_t>::max() - (duration - 1))
        throw std::invalid_argument("Invalid scalar curve slice range.");
    const auto valueAt = [&](std::int64_t frame) { return evaluateScalar(base,source,frame); };
    if (source.empty()) return {};
    creative_suite::animation::ScalarKeyframes output;
        output.push_back({0, valueAt(delta)});
        for (const auto& key : source) {
            // Comparison in source coordinates avoids overflowing a shifted key.
            if (key.frame > delta && key.frame - std::max<std::int64_t>(0, delta) < duration &&
                (delta >= 0 || key.frame <= std::numeric_limits<std::int64_t>::max() + delta)) {
                const auto shifted = key.frame - delta;
                if (shifted < duration) { auto copy = key; copy.frame = shifted; output.push_back(copy); }
            }
        }
        if (output.back().frame != duration - 1)
            output.push_back({duration - 1, valueAt(delta + duration - 1)});
        for (std::size_t index = 0; index + 1 < output.size(); ++index) {
            auto& left = output[index];
            const auto begin = left.frame + delta, end = output[index + 1].frame + delta;
            const auto upper = std::upper_bound(source.begin(), source.end(), begin,
                [](std::int64_t frame, const auto& key) { return frame < key.frame; });
            left.interpolation = creative_suite::animation::InterpolationMode::Linear;
            if (upper == source.begin() || upper == source.end()) continue;
            const auto& original_left = *(upper - 1);
            const auto& original_right = *upper;
            if (original_left.interpolation != creative_suite::animation::InterpolationMode::CubicBezier) continue;
            const double interval = static_cast<double>(original_right.frame - original_left.frame);
            const auto x0 = static_cast<double>(begin - original_left.frame) / interval;
            const auto x1 = static_cast<double>(end - original_left.frame) / interval;
            CurveSegment curve{CurvePoint{0, 0}, CurvePoint{original_left.easing.x1, original_left.easing.y1},
                CurvePoint{original_left.easing.x2, original_left.easing.y2}, CurvePoint{1, 1}};
            const auto t0 = curveParameterAtX(curve, x0), t1 = curveParameterAtX(curve, x1);
            if (t0 > 0) curve = subdivideCurve(curve, t0).second;
            if (t1 < 1) curve = subdivideCurve(curve, (t1 - t0) / (1 - t0)).first;
            const auto dx = curve[3].x - curve[0].x, dy = curve[3].y - curve[0].y;
            if (dx <= 0 || dy <= 0) continue;
            left.interpolation = creative_suite::animation::InterpolationMode::CubicBezier;
            left.easing = {std::clamp((curve[1].x - curve[0].x) / dx, 0.0, 1.0),
                std::clamp((curve[1].y - curve[0].y) / dy, 0.0, 1.0),
                std::clamp((curve[2].x - curve[0].x) / dx, 0.0, 1.0),
                std::clamp((curve[2].y - curve[0].y) / dy, 0.0, 1.0)};
        }
    return output;
}
} // namespace creative_suite::animation
