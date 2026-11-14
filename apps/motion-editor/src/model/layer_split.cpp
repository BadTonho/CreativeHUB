#include "layer_split.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace motion::model {
namespace {

using creative_suite::animation::CubicBezierEasing;
using creative_suite::animation::InterpolationMode;
using creative_suite::animation::Keyframe;
using creative_suite::animation::ScalarKeyframes;

constexpr double kEasingTolerance = 1e-9;

struct Point {
    double x, y;
};

Point lerp(Point a, Point b, double u) noexcept {
    return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u};
}

// Accepts a value that is within rounding noise of [0, 1].
bool clampUnit(double& value) noexcept {
    if (!std::isfinite(value) || value < -kEasingTolerance || value > 1.0 + kEasingTolerance) return false;
    value = std::clamp(value, 0.0, 1.0);
    return true;
}

// Shared split of an ordered key list. `Key` needs `frame`, `interpolation`, `easing`;
// `boundary(previous, split_frame)` builds the key that starts the later half inside a segment.
template <class Key, class Boundary>
bool splitKeys(const std::vector<Key>& source, std::int64_t split_frame, std::vector<Key>& first,
               std::vector<Key>& second, Boundary&& boundary) {
    std::vector<Key> head, tail;
    const auto next = std::lower_bound(source.begin(), source.end(), split_frame,
        [](const Key& key, std::int64_t frame) { return key.frame < frame; });
    head.assign(source.begin(), next == source.end() ? next : next + 1);
    if (next == source.end()) {
        if (!source.empty()) {
            // After the last key the value is constant; one key at frame 0 keeps it keyed.
            auto key = boundary(source.back(), split_frame, true);
            if (!key) return false;
            tail.push_back(std::move(*key));
        }
    } else {
        if (next->frame > split_frame && next != source.begin()) {
            auto key = boundary(*(next - 1), split_frame, false);
            if (!key) return false;
            tail.push_back(std::move(*key));
        }
        for (auto it = next; it != source.end(); ++it) {
            tail.push_back(*it);
            tail.back().frame -= split_frame;
        }
    }
    first = std::move(head);
    second = std::move(tail);
    return true;
}

std::size_t scalarCount(const ScalarKeyframes& keys) noexcept { return keys.size(); }

// `first` and `second` start as identical copies; their curves are replaced by the split halves.
bool splitCurve(ScalarKeyframes& first, ScalarKeyframes& second, std::int64_t split_frame) {
    const ScalarKeyframes source = first;
    return splitScalarKeyframes(source, split_frame, first, second);
}

} // namespace

std::optional<CubicBezierEasing> subdivideEasingRight(const CubicBezierEasing& easing,
                                                      double progress) noexcept {
    if (!(progress > 0.0 && progress < 1.0) || !creative_suite::animation::validCubicBezierEasing(easing)) {
        return std::nullopt;
    }
    const Point p0{0, 0}, p1{easing.x1, easing.y1}, p2{easing.x2, easing.y2}, p3{1, 1};
    // x(u) is monotone because 0 <= x1 <= x2 <= 1, so bisection finds the unique parameter.
    double low = 0.0, high = 1.0;
    for (int i = 0; i < 80; ++i) {
        const double u = (low + high) / 2;
        const double inv = 1 - u;
        const double x = 3 * inv * inv * u * p1.x + 3 * inv * u * u * p2.x + u * u * u;
        (x < progress ? low : high) = u;
    }
    const double u = (low + high) / 2;
    const Point a = lerp(p0, p1, u), b = lerp(p1, p2, u), c = lerp(p2, p3, u);
    const Point d = lerp(a, b, u), e = lerp(b, c, u), f = lerp(d, e, u);
    const double span_x = 1.0 - f.x, span_y = 1.0 - f.y;
    if (span_x < 1e-12) return std::nullopt;
    if (span_y < 1e-12) return CubicBezierEasing{};  // flat remainder: any easing yields a constant
    CubicBezierEasing result{
        (e.x - f.x) / span_x, (e.y - f.y) / span_y,
        (c.x - f.x) / span_x, (c.y - f.y) / span_y};
    if (!clampUnit(result.x1) || !clampUnit(result.y1) || !clampUnit(result.x2) || !clampUnit(result.y2)) {
        return std::nullopt;
    }
    result.x2 = std::max(result.x1, result.x2);
    if (!creative_suite::animation::validCubicBezierEasing(result)) return std::nullopt;
    return result;
}

bool splitScalarKeyframes(const ScalarKeyframes& source, std::int64_t split_frame, ScalarKeyframes& first,
                          ScalarKeyframes& second) {
    ScalarKeyframes head, tail;
    const bool ok = splitKeys(source, split_frame, head, tail,
        [&](const Keyframe& previous, std::int64_t frame, bool after_last) -> std::optional<Keyframe> {
            Keyframe key;
            key.frame = 0;
            key.value = creative_suite::animation::evaluateScalar(0.0, source, frame);
            if (after_last) return key;
            const auto upper = std::lower_bound(source.begin(), source.end(), frame,
                [](const Keyframe& k, std::int64_t f) { return k.frame < f; });
            const double progress = static_cast<double>(frame - previous.frame) /
                                    static_cast<double>(upper->frame - previous.frame);
            key.interpolation = previous.interpolation;
            if (previous.interpolation == InterpolationMode::CubicBezier) {
                const auto easing = subdivideEasingRight(previous.easing, progress);
                if (!easing) return std::nullopt;
                key.easing = *easing;
            }
            return key;
        });
    if (!ok) return false;
    first = std::move(head);
    second = std::move(tail);
    return true;
}

bool splitPathAnimation(const PathAnimation& source, std::int64_t split_frame, PathAnimation& first,
                        PathAnimation& second) {
    std::vector<PathKeyframe> head, tail;
    const bool ok = splitKeys(source.keys, split_frame, head, tail,
        [&](const PathKeyframe& previous, std::int64_t frame, bool after_last) -> std::optional<PathKeyframe> {
            PathKeyframe key;
            key.frame = 0;
            key.geometry = evaluatePath(source, frame);
            if (after_last) return key;
            const auto upper = std::lower_bound(source.keys.begin(), source.keys.end(), frame,
                [](const PathKeyframe& k, std::int64_t f) { return k.frame < f; });
            const double progress = static_cast<double>(frame - previous.frame) /
                                    static_cast<double>(upper->frame - previous.frame);
            key.interpolation = previous.interpolation;
            if (previous.interpolation == InterpolationMode::CubicBezier) {
                const auto easing = subdivideEasingRight(previous.easing, progress);
                if (!easing) return std::nullopt;
                key.easing = *easing;
            }
            return key;
        });
    if (!ok) return false;
    first = PathAnimation{source.base, std::move(head)};
    second = PathAnimation{source.base, std::move(tail)};
    return true;
}

static bool splitLayerCurves(CompositionLayer& first, CompositionLayer& second, std::int64_t split_frame) {
    const auto curve = [&](ScalarKeyframes& a, ScalarKeyframes& b) { return splitCurve(a, b, split_frame); };
    bool ok = curve(first.keyframes.position_x, second.keyframes.position_x) &&
              curve(first.keyframes.position_y, second.keyframes.position_y) &&
              curve(first.keyframes.scale, second.keyframes.scale) &&
              curve(first.keyframes.rotation, second.keyframes.rotation) &&
              curve(first.keyframes.opacity, second.keyframes.opacity);
    for (std::size_t i = 0; ok && i < first.anchor_keyframes.size(); ++i) {
        ok = curve(first.anchor_keyframes[i], second.anchor_keyframes[i]);
    }
    if (ok && first.scale_y && second.scale_y) ok = curve(first.scale_y->keyframes, second.scale_y->keyframes);
    for (std::size_t e = 0; ok && e < first.effects.size(); ++e) {
        for (int i = 0; ok && i < 10; ++i) {
            const auto property = static_cast<EffectParameterProperty>(i);
            auto* a = mutableEffectKeyframesFor(first.effects[e], property);
            auto* b = mutableEffectKeyframesFor(second.effects[e], property);
            if (a && b) ok = curve(*a, *b);
        }
    }
    if (!ok) return false;
    auto* first_shape = std::get_if<ShapeLayerContent>(&first.content);
    auto* second_shape = std::get_if<ShapeLayerContent>(&second.content);
    if (first_shape && second_shape) {
        if (!first_shape->trim_paths.isDefault()) {
            TrimPathAnimation a = first_shape->trim_paths.get(), b = a;
            for (std::size_t k = 0; ok && k < a.keys.size(); ++k) ok = curve(a.keys[k], b.keys[k]);
            if (!ok) return false;
            first_shape->trim_paths = TrimPathStorage(std::move(a));
            second_shape->trim_paths = TrimPathStorage(std::move(b));
        }
        if (first_shape->path && !first_shape->path->keys.empty()) {
            PathAnimation a, b;
            if (!splitPathAnimation(*first_shape->path, split_frame, a, b)) return false;
            first_shape->path = PathAnimationStorage(std::move(a));
            second_shape->path = PathAnimationStorage(std::move(b));
        }
    }
    return true;
}

std::size_t layerKeyframeCount(const CompositionLayer& layer) noexcept {
    std::size_t count = 0;
    count += scalarCount(layer.keyframes.position_x) + scalarCount(layer.keyframes.position_y) +
             scalarCount(layer.keyframes.scale) + scalarCount(layer.keyframes.rotation) +
             scalarCount(layer.keyframes.opacity);
    for (const auto& keys : layer.anchor_keyframes) count += keys.size();
    if (layer.scale_y) count += layer.scale_y->keyframes.size();
    for (const auto& effect : layer.effects) {
        for (int i = 0; i < 10; ++i) {
            if (const auto* keys = effectKeyframesFor(effect, static_cast<EffectParameterProperty>(i))) count += keys->size();
        }
    }
    if (const auto* shape = std::get_if<ShapeLayerContent>(&layer.content)) {
        if (shape->path) count += pathKeyframeCost(*shape->path);
        count += trimKeyframeCost(shape->trim_paths.get());
    }
    return count;
}

SplitLayerResult splitLayer(const CompositionLayer& layer, std::int64_t split_frame, LayerId second_id,
                            FrameRate timeline_rate, LayerSplit& result) {
    if (split_frame <= 0 || split_frame >= layer.duration_frames) return SplitLayerResult::InvalidFrame;

    CompositionLayer first = layer;
    CompositionLayer second = layer;
    second.id = second_id;
    second.timeline_start_frame = layer.timeline_start_frame + split_frame;
    second.duration_frames = layer.duration_frames - split_frame;
    first.duration_frames = split_frame;

    if (layer.kind == LayerKind::Video) {
        const auto source = sourceFrameForTimelineFrame(split_frame, layer.source_start_frame,
            layer.source_frame_rate, timeline_rate, layer.source_frame_count);
        if (!source || layer.maximum_timeline_duration_frames <= split_frame) return SplitLayerResult::Failed;
        second.source_start_frame = *source;
        second.maximum_timeline_duration_frames = layer.maximum_timeline_duration_frames - split_frame;
        if (second.duration_frames > second.maximum_timeline_duration_frames) {
            return SplitLayerResult::Failed;
        }
    }

    if (!splitLayerCurves(first, second, split_frame)) return SplitLayerResult::InexactEasing;
    result.first = std::move(first);
    result.second = std::move(second);
    return SplitLayerResult::Split;
}

} // namespace motion::model
