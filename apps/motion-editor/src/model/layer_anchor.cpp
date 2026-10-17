#include "composition_document.h"
#include "layer_transform_hierarchy.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace motion::model {
namespace a = creative_suite::animation;
namespace {
bool validProperty(AnchorProperty property) noexcept
{
    return property == AnchorProperty::X || property == AnchorProperty::Y;
}
void validateGeometry(const CompositionLayer& layer, CanvasSize canvas, std::int64_t local_frame)
{
    const auto transform = a::evaluateTransform(layer.transform, layer.keyframes, local_frame);
    (void)geometryOffset(transform, canvas, evaluateLayerAnchor(layer, local_frame));
}
}

bool validAnchorPoint(const AnchorPoint& point) noexcept
{
    return std::isfinite(point.x_pixels) && std::isfinite(point.y_pixels);
}

AnchorPoint evaluateLayerAnchor(const CompositionLayer& layer, std::int64_t frame) noexcept
{
    return {a::evaluateScalar(layer.anchor_point.x_pixels, layer.anchor_keyframes[0], frame),
        a::evaluateScalar(layer.anchor_point.y_pixels, layer.anchor_keyframes[1], frame)};
}

bool CompositionDocument::setLayerAnchorPoint(LayerId id, AnchorPoint point)
{
    auto* layer = findLayer(id);
    if (!layer || !validAnchorPoint(point) || point == layer->anchor_point) return false;
    // Validate the base geometry as well as the stored finite values.
    (void)geometryOffset(layer->transform, canvas_size_, point);
    layer->anchor_point = point;
    return true;
}

bool CompositionDocument::replaceLayerAnchorKeyframes(LayerId id, AnchorProperty property, a::ScalarKeyframes keys)
{
    auto* layer = findLayer(id);
    if (!layer || !validProperty(property) || !a::validScalarKeyframes(keys)) return false;
    auto& target = layer->anchor_keyframes[static_cast<std::size_t>(property)];
    if (target == keys) return false;
    auto candidate = *layer;
    candidate.anchor_keyframes[static_cast<std::size_t>(property)] = std::move(keys);
    (void)geometryOffset(candidate.transform, canvas_size_, candidate.anchor_point);
    for (const auto& key : candidate.anchor_keyframes[static_cast<std::size_t>(property)])
        validateGeometry(candidate, canvas_size_, key.frame);
    // Removing the final key exposes the base; removing an interior key changes
    // the interpolation at its old frame. Check both before publishing.
    for (const auto& key : target) validateGeometry(candidate, canvas_size_, key.frame);
    target = std::move(candidate.anchor_keyframes[static_cast<std::size_t>(property)]);
    return true;
}

bool CompositionDocument::setLayerAnchorKeyframe(LayerId id, AnchorProperty property, std::int64_t frame, double value)
{
    auto* layer = findLayer(id);
    if (!layer || !validProperty(property) || frame < 0 || !std::isfinite(value)) return false;
    auto candidate = *layer;
    auto& keys = candidate.anchor_keyframes[static_cast<std::size_t>(property)];
    if (!a::setKeyframe(keys, frame, value) || keys == layer->anchor_keyframes[static_cast<std::size_t>(property)]) return false;
    validateGeometry(candidate, canvas_size_, frame);
    layer->anchor_keyframes = std::move(candidate.anchor_keyframes);
    return true;
}

bool CompositionDocument::removeLayerAnchorKeyframe(LayerId id, AnchorProperty property, std::int64_t frame)
{
    auto* layer = findLayer(id);
    if (!layer || !validProperty(property)) return false;
    auto keys = layer->anchor_keyframes[static_cast<std::size_t>(property)];
    if (!a::removeKeyframe(keys, frame)) return false;
    return replaceLayerAnchorKeyframes(id, property, std::move(keys));
}

bool CompositionDocument::moveLayerAnchorKeyframe(LayerId id, AnchorProperty property, std::int64_t from, std::int64_t to)
{
    auto* layer = findLayer(id);
    if (!layer || !validProperty(property) || from < 0 || to < 0 || from == to || to >= layer->duration_frames) return false;
    auto keys = layer->anchor_keyframes[static_cast<std::size_t>(property)];
    auto key = std::find_if(keys.begin(), keys.end(), [from](const auto& k) { return k.frame == from; });
    if (key == keys.end() || std::any_of(keys.begin(), keys.end(), [to](const auto& k) { return k.frame == to; })) return false;
    key->frame = to;
    std::sort(keys.begin(), keys.end(), [](const auto& left, const auto& right) { return left.frame < right.frame; });
    return replaceLayerAnchorKeyframes(id, property, std::move(keys));
}

bool CompositionDocument::setLayerAnchorKeyframeInterpolation(LayerId id, AnchorProperty property, std::int64_t frame,
    a::InterpolationMode mode, const a::CubicBezierEasing& easing)
{
    auto* layer = findLayer(id);
    if (!layer || !validProperty(property) || !a::validKeyframeInterpolation(mode, easing)) return false;
    auto keys = layer->anchor_keyframes[static_cast<std::size_t>(property)];
    auto key = std::find_if(keys.begin(), keys.end(), [frame](const auto& k) { return k.frame == frame; });
    if (key == keys.end() || (key->interpolation == mode && key->easing == easing)) return false;
    key->interpolation = mode; key->easing = easing;
    return replaceLayerAnchorKeyframes(id, property, std::move(keys));
}

bool CompositionDocument::repositionLayerAnchorKeepingPose(LayerId id, AnchorPoint point, std::int64_t frame)
{
    auto* layer = findLayer(id);
    if (!layer || !validAnchorPoint(point) || frame < layer->timeline_start_frame ||
        frame - layer->timeline_start_frame >= layer->duration_frames) return false;
    const auto local = frame - layer->timeline_start_frame;
    const auto previous = evaluateLayerAnchor(*layer, local);
    if (point == previous) return false;
    const auto transform = a::evaluateTransform(layer->transform, layer->keyframes, local);
    const auto radians = std::remainder(transform.rotation_degrees, 360.0) * std::numbers::pi / 180.0;
    const auto dx = point.x_pixels - previous.x_pixels, dy = point.y_pixels - previous.y_pixels;
    const auto x = transform.position_x + transform.scale * (std::cos(radians)*dx - std::sin(radians)*dy) / canvas_size_.width;
    const auto y = transform.position_y + transform.scale * (std::sin(radians)*dx + std::cos(radians)*dy) / canvas_size_.height;
    if (!std::isfinite(x) || !std::isfinite(y))
        throw std::runtime_error("Anchor repositioning exceeds finite position limits for layer " + std::to_string(id));
    auto candidate = *layer;
    const auto set = [local](auto& keys, double& base, double old_value, double new_value) {
        if (old_value == new_value) return;
        if (keys.empty()) base = new_value;
        else (void)a::setKeyframe(keys, local, new_value);
    };
    set(candidate.anchor_keyframes[0],candidate.anchor_point.x_pixels,previous.x_pixels,point.x_pixels);
    set(candidate.anchor_keyframes[1],candidate.anchor_point.y_pixels,previous.y_pixels,point.y_pixels);
    set(candidate.keyframes.position_x,candidate.transform.position_x,transform.position_x,x);
    set(candidate.keyframes.position_y,candidate.transform.position_y,transform.position_y,y);
    if (!a::validTransform(candidate.transform) || !a::validTransformKeyframes(candidate.keyframes) ||
        !validAnchorPoint(candidate.anchor_point)) return false;
    validateGeometry(candidate, canvas_size_, local);
    // Validate the complete affected graph at the operation frame before publishing.
    auto staged = layers_;
    *std::find_if(staged.begin(), staged.end(), [id](const auto& l) { return l.id == id; }) = candidate;
    (void)LayerTransformHierarchy(canvas_size_, staged).evaluate(frame);
    *layer = std::move(candidate);
    return true;
}
} // namespace motion::model
