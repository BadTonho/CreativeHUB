#include "composition_document.h"
#include <algorithm>
#include <limits>

namespace motion::model {
namespace a = creative_suite::animation;
bool validMotionMask(const Mask &m) noexcept {
    if (!creative_suite::composition_exchange::validMask(m))
        return false;
    if (!m.path)
        return true;
    auto parameters = m.parameters;
    parameters.soft_edge = 0;
    parameters.invert = false;
    return m.kind == creative_suite::composition::ShapeMaskKind::Rectangle &&
           parameters == creative_suite::composition::ShapeMaskParameters{} && m.path.valid(true) &&
           std::all_of(m.keyframes.begin(), m.keyframes.end(), [](const auto &keys) { return keys.empty(); });
}
const PathAnimation *CompositionDocument::pathFor(PathTarget target) const noexcept {
    if (target.mask) {
        const auto *m = findMask(target.id);
        return m && m->path ? &*m->path : nullptr;
    }
    const auto *layer = findLayer(target.id);
    const auto *shape = layer ? std::get_if<ShapeLayerContent>(&layer->content) : nullptr;
    return shape && shape->path ? &*shape->path : nullptr;
}
bool CompositionDocument::replacePathAnimation(PathTarget target, PathAnimation value) {
    const auto *old = pathFor(target);
    if (!old || *old == value || !validPathAnimation(value, target.mask) ||
        (!old->keys.empty() && !samePathTopology(old->base, value.base)))
        return false;
    if (!target.mask) {
        const auto *l = findLayer(target.id);
        const auto &shape = std::get<ShapeLayerContent>(l->content);
        if (!validPathRasterGeometry(value.base, shape.stroke_width_pixels))
            return false;
        for (const auto &k : value.keys)
            if (!validPathRasterGeometry(k.geometry, shape.stroke_width_pixels))
                return false;
    }
    const auto before = pathKeyframeCost(*old), after = pathKeyframeCost(value);
    if (!canInsertKeyframes(after > before ? after - before : 0))
        return false;
    if (target.mask) {
        for (auto &m : masks_)
            if (m.id == target.id) {
                m.path = std::move(value);
                return true;
            }
    } else {
        std::get<ShapeLayerContent>(findLayer(target.id)->content).path = std::move(value);
        return true;
    }
    return false;
}
namespace {
bool insert(PathAnimation &path, std::int64_t frame, const PathGeometry &geometry) {
    auto at = std::lower_bound(path.keys.begin(), path.keys.end(), frame,
                               [](const auto &k, auto f) { return k.frame < f; });
    if (at != path.keys.end() && at->frame == frame) {
        if (at->geometry == geometry)
            return false;
        at->geometry = geometry;
        return true;
    }
    PathKeyframe key{frame, geometry};
    if (at != path.keys.begin()) {
        key.interpolation = (at - 1)->interpolation;
        key.easing = (at - 1)->easing;
    }
    path.keys.insert(at, std::move(key));
    return true;
}
} // namespace
bool CompositionDocument::setPathAtFrame(PathTarget target, const PathGeometry &geometry,
                                         std::int64_t composition_frame) {
    const auto *old = pathFor(target);
    if (!old || composition_frame < 0 || !validPathGeometry(geometry, target.mask))
        return false;
    auto frame = composition_frame;
    if (!target.mask) {
        const auto *l = findLayer(target.id);
        frame -= l->timeline_start_frame;
        if (frame < 0 || frame >= l->duration_frames)
            return false;
    }
    if (evaluatePath(*old, frame) == geometry)
        return false;
    auto value = *old;
    if (value.keys.empty())
        value.base = geometry;
    else {
        if (!samePathTopology(value.base, geometry))
            return false;
        (void)insert(value, frame, geometry);
    }
    return replacePathAnimation(target, std::move(value));
}
bool CompositionDocument::setPathKeyframe(PathTarget target, std::int64_t frame) {
    const auto *old = pathFor(target);
    if (!old || frame < 0)
        return false;
    if (!target.mask) {
        const auto *l = findLayer(target.id);
        frame -= l->timeline_start_frame;
        if (frame < 0 || frame >= l->duration_frames)
            return false;
    }
    auto value = *old;
    if (!insert(value, frame, evaluatePath(*old, frame)))
        return false;
    return replacePathAnimation(target, std::move(value));
}
bool CompositionDocument::removePathKeyframe(PathTarget target, std::int64_t frame) {
    const auto *old = pathFor(target);
    if (!old)
        return false;
    auto value = *old;
    if (!std::erase_if(value.keys, [frame](const auto &k) { return k.frame == frame; }))
        return false;
    return replacePathAnimation(target, std::move(value));
}
bool CompositionDocument::movePathKeyframe(PathTarget target, std::int64_t from, std::int64_t to) {
    const auto *old = pathFor(target);
    if (!old || from == to || to < 0 || (!target.mask && to >= findLayer(target.id)->duration_frames))
        return false;
    if (std::any_of(old->keys.begin(), old->keys.end(), [to](const auto &k) { return k.frame == to; }))
        return false;
    auto value = *old;
    auto at =
        std::find_if(value.keys.begin(), value.keys.end(), [from](const auto &k) { return k.frame == from; });
    if (at == value.keys.end())
        return false;
    at->frame = to;
    std::sort(value.keys.begin(), value.keys.end(),
              [](const auto &l, const auto &r) { return l.frame < r.frame; });
    return replacePathAnimation(target, std::move(value));
}
bool CompositionDocument::setPathKeyframeInterpolation(PathTarget target, std::int64_t frame,
                                                       a::InterpolationMode mode,
                                                       const a::CubicBezierEasing &easing) {
    const auto *old = pathFor(target);
    if (!old || !a::validKeyframeInterpolation(mode, easing))
        return false;
    auto value = *old;
    for (auto &k : value.keys)
        if (k.frame == frame) {
            k.interpolation = mode;
            k.easing = easing;
            return replacePathAnimation(target, std::move(value));
        }
    return false;
}
bool CompositionDocument::clearPathAnimationAtFrame(PathTarget target, std::int64_t frame) {
    const auto *old = pathFor(target);
    if (!old || old->keys.empty() || frame < 0)
        return false;
    if (!target.mask) {
        const auto *l = findLayer(target.id);
        frame -= l->timeline_start_frame;
        if (frame < 0 || frame >= l->duration_frames)
            return false;
    }
    return replacePathAnimation(target, {evaluatePath(*old, frame), {}});
}
bool CompositionDocument::createPathLayer(const PathGeometry &geometry, std::int64_t frame, LayerId *added,
                                          std::string name) {
    if (!validPathGeometry(geometry) || !validPathRasterGeometry(geometry, 2) || frame < 0)
        return false;
    auto staged = *this;
    LayerId id = 0;
    if (!staged.addContentLayer(LayerKind::Shape, std::move(name), frame, &id))
        return false;
    auto &shape = std::get<ShapeLayerContent>(staged.findLayer(id)->content);
    shape.shape = ShapeKind::BezierPath;
    shape.width = canvas_size_.width;
    shape.height = canvas_size_.height;
    shape.stroke_width_pixels = 2;
    shape.path = PathAnimation{geometry, {}};
    *this = std::move(staged);
    if (added)
        *added = id;
    return true;
}
MaskId CompositionDocument::createPathMask(LayerId layer, int target, const PathGeometry &geometry) {
    if (!validPathGeometry(geometry, true))
        return 0;
    auto staged = *this;
    const auto id = staged.createMask(layer, target, creative_suite::composition::ShapeMaskKind::Rectangle);
    if (!id)
        return 0;
    for (auto &m : staged.masks_)
        if (m.id == id)
            m.path = PathAnimation{geometry, {}};
    *this = std::move(staged);
    return id;
}
} // namespace motion::model
