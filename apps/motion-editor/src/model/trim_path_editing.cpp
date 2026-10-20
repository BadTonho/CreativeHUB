#include "composition_document.h"
#include <algorithm>

namespace motion::model {
namespace a = creative_suite::animation;
const TrimPathStorage *CompositionDocument::layerTrimPaths(LayerId id) const noexcept {
    const auto *layer = findLayer(id);
    const auto *shape = layer ? std::get_if<ShapeLayerContent>(&layer->content) : nullptr;
    return shape && shape->shape == ShapeKind::BezierPath && shape->path ? &shape->trim_paths : nullptr;
}
bool CompositionDocument::replaceLayerTrimPaths(LayerId id, TrimPathAnimation value) {
    const auto *old = layerTrimPaths(id);
    if (!old || !validTrimAnimation(value) || old->get() == value)
        return false;
    const auto before = trimKeyframeCost(old->get()), after = trimKeyframeCost(value);
    if (!canInsertKeyframes(after > before ? after - before : 0))
        return false;
    std::get<ShapeLayerContent>(findLayer(id)->content).trim_paths = std::move(value);
    return true;
}
bool CompositionDocument::setLayerTrimPathsEnabled(LayerId id, bool enabled) {
    const auto *old = layerTrimPaths(id);
    if (!old || old->get().base.enabled == enabled)
        return false;
    auto value = old->get();
    value.base.enabled = enabled;
    return replaceLayerTrimPaths(id, std::move(value));
}
bool CompositionDocument::setLayerTrimValueAtFrame(LayerId id, TrimPathProperty property, double value,
                                                   std::int64_t frame) {
    const auto *old = layerTrimPaths(id);
    const auto *layer = findLayer(id);
    if (!old || !validTrimValue(property, value) || frame < layer->timeline_start_frame ||
        frame - layer->timeline_start_frame >= layer->duration_frames)
        return false;
    frame -= layer->timeline_start_frame;
    auto data = old->get();
    auto &keys = data.keys[static_cast<std::size_t>(property)];
    if (keys.empty())
        setTrimValue(data.base, property, value);
    else {
        const auto at = std::lower_bound(keys.begin(), keys.end(), frame,
                                         [](const auto &k, auto f) { return k.frame < f; });
        if (at == keys.end() || at->frame != frame)
            return false;
        at->value = value;
    }
    return replaceLayerTrimPaths(id, std::move(data));
}
bool CompositionDocument::setLayerTrimKeyframe(LayerId id, TrimPathProperty property, std::int64_t frame) {
    const auto *old = layerTrimPaths(id);
    const auto *layer = findLayer(id);
    if (!old || !validTrimProperty(property) || frame < layer->timeline_start_frame ||
        frame - layer->timeline_start_frame >= layer->duration_frames)
        return false;
    frame -= layer->timeline_start_frame;
    auto data = old->get();
    auto &keys = data.keys[static_cast<std::size_t>(property)];
    const auto at =
        std::lower_bound(keys.begin(), keys.end(), frame, [](const auto &k, auto f) { return k.frame < f; });
    if (at != keys.end() && at->frame == frame)
        return false;
    a::Keyframe key{frame, a::evaluateScalar(trimValue(data.base, property), keys, frame)};
    if (at != keys.begin()) {
        key.interpolation = (at - 1)->interpolation;
        key.easing = (at - 1)->easing;
    }
    keys.insert(at, key);
    return replaceLayerTrimPaths(id, std::move(data));
}
bool CompositionDocument::removeLayerTrimKeyframe(LayerId id, TrimPathProperty property, std::int64_t frame) {
    const auto *old = layerTrimPaths(id);
    if (!old || !validTrimProperty(property))
        return false;
    auto data = old->get();
    auto &keys = data.keys[static_cast<std::size_t>(property)];
    if (!std::erase_if(keys, [frame](const auto &k) { return k.frame == frame; }))
        return false;
    return replaceLayerTrimPaths(id, std::move(data));
}
bool CompositionDocument::moveLayerTrimKeyframe(LayerId id, TrimPathProperty property, std::int64_t from,
                                                std::int64_t to) {
    const auto *old = layerTrimPaths(id);
    const auto *layer = findLayer(id);
    if (!old || !validTrimProperty(property) || from == to || to < 0 || to >= layer->duration_frames)
        return false;
    auto data = old->get();
    auto &keys = data.keys[static_cast<std::size_t>(property)];
    if (std::any_of(keys.begin(), keys.end(), [to](const auto &k) { return k.frame == to; }))
        return false;
    auto at = std::find_if(keys.begin(), keys.end(), [from](const auto &k) { return k.frame == from; });
    if (at == keys.end())
        return false;
    at->frame = to;
    std::sort(keys.begin(), keys.end(), [](const auto &l, const auto &r) { return l.frame < r.frame; });
    return replaceLayerTrimPaths(id, std::move(data));
}
bool CompositionDocument::setLayerTrimKeyframeInterpolation(LayerId id, TrimPathProperty property,
                                                            std::int64_t frame, a::InterpolationMode mode,
                                                            const a::CubicBezierEasing &easing) {
    const auto *old = layerTrimPaths(id);
    if (!old || !validTrimProperty(property) || !a::validKeyframeInterpolation(mode, easing))
        return false;
    auto data = old->get();
    for (auto &key : data.keys[static_cast<std::size_t>(property)])
        if (key.frame == frame) {
            key.interpolation = mode;
            key.easing = easing;
            return replaceLayerTrimPaths(id, std::move(data));
        }
    return false;
}
} // namespace motion::model
