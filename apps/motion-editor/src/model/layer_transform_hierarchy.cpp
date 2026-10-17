#include "layer_transform_hierarchy.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace motion::model {

bool validParentOffset(const ParentTransformOffset& value) noexcept
{
    return std::isfinite(value.translation_x_pixels) &&
        std::isfinite(value.translation_y_pixels) &&
        std::isfinite(value.rotation_degrees) && std::isfinite(value.scale) && value.scale > 0;
}

ParentTransformOffset composeOffsets(const ParentTransformOffset& left,
                                    const ParentTransformOffset& right)
{
    const auto radians = std::remainder(left.rotation_degrees, 360.0) * std::numbers::pi / 180.0;
    const auto cosine = std::cos(radians), sine = std::sin(radians);
    ParentTransformOffset result{
        left.translation_x_pixels + left.scale *
            (cosine * right.translation_x_pixels - sine * right.translation_y_pixels),
        left.translation_y_pixels + left.scale *
            (sine * right.translation_x_pixels + cosine * right.translation_y_pixels),
        left.scale * right.scale, left.rotation_degrees + right.rotation_degrees};
    if (!validParentOffset(left) || !validParentOffset(right) || !validParentOffset(result))
        throw std::runtime_error("Layer hierarchy transform exceeds finite geometry limits.");
    return result;
}

ParentTransformOffset inverseOffset(const ParentTransformOffset& value)
{
    if (!validParentOffset(value)) throw std::runtime_error("Invalid parent compensation.");
    ParentTransformOffset inverse{0, 0, 1.0 / value.scale, -value.rotation_degrees};
    return composeOffsets(inverse, {-value.translation_x_pixels, -value.translation_y_pixels, 1, 0});
}

ParentTransformOffset geometryOffset(const creative_suite::animation::Transform2D& value,
                                    CanvasSize canvas)
{
    ParentTransformOffset result{value.position_x * canvas.width,
        value.position_y * canvas.height, value.scale, value.rotation_degrees};
    if (!validParentOffset(result)) throw std::runtime_error("Invalid layer hierarchy geometry.");
    return result;
}

LayerTransformHierarchy::LayerTransformHierarchy(CanvasSize canvas,
                                               const std::vector<CompositionLayer>& layers)
    : canvas_(canvas)
{
    if (canvas.width <= 0 || canvas.height <= 0) throw std::invalid_argument("Invalid hierarchy canvas.");
    nodes_.reserve(layers.size());
    indices_.reserve(layers.size());
    for (const auto& layer : layers) {
        if (!layer.id || !indices_.emplace(layer.id, nodes_.size()).second ||
            layer.timeline_start_frame < 0 || !validParentOffset(layer.parent_offset) ||
            !creative_suite::animation::validTransform(layer.transform) ||
            !creative_suite::animation::validTransformKeyframes(layer.keyframes))
            throw std::invalid_argument("Invalid or duplicate layer hierarchy node.");
        nodes_.push_back({layer.id, layer.parent_id, layer.parent_offset,
            layer.timeline_start_frame, layer.transform, layer.keyframes});
    }
    std::vector<std::vector<std::size_t>> children(nodes_.size());
    order_.reserve(nodes_.size());
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        if (!nodes_[i].parent_id) order_.push_back(i);
        else {
            const auto parent = indices_.find(nodes_[i].parent_id);
            if (parent == indices_.end()) throw std::invalid_argument("Unknown parent layer reference.");
            children[parent->second].push_back(i);
        }
    }
    for (std::size_t i = 0; i < order_.size(); ++i)
        for (const auto child : children[order_[i]]) order_.push_back(child);
    if (order_.size() != nodes_.size()) throw std::invalid_argument("Layer parenting contains a cycle.");
}

bool LayerTransformHierarchy::matches(CanvasSize canvas, const std::vector<CompositionLayer>& layers) const noexcept
{
    if (canvas != canvas_ || layers.size() != nodes_.size()) return false;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const auto& n = nodes_[i]; const auto& l = layers[i];
        if (n.id != l.id || n.parent_id != l.parent_id || n.offset != l.parent_offset ||
            n.start != l.timeline_start_frame || n.transform != l.transform || n.keyframes != l.keyframes)
            return false;
    }
    return true;
}

std::unordered_map<LayerId, creative_suite::animation::Transform2D>
LayerTransformHierarchy::evaluate(std::int64_t frame) const
{
    if (frame < 0) throw std::invalid_argument("Negative hierarchy composition frame.");
    std::vector<ParentTransformOffset> global(nodes_.size());
    std::unordered_map<LayerId, creative_suite::animation::Transform2D> result;
    result.reserve(nodes_.size());
    for (const auto i : order_) {
        const auto& n = nodes_[i];
        auto value = creative_suite::animation::evaluateTransform(n.transform, n.keyframes,
            frame < n.start ? 0 : frame - n.start);
        const auto local = geometryOffset(value, canvas_);
        // Keep the old unparented path exact, including rotations beyond 360 degrees.
        global[i] = n.offset == ParentTransformOffset{} ? local : composeOffsets(n.offset, local);
        if (n.parent_id) global[i] = composeOffsets(global[indices_.at(n.parent_id)], global[i]);
        if (n.parent_id || n.offset != ParentTransformOffset{}) {
            value.position_x = global[i].translation_x_pixels / canvas_.width;
            value.position_y = global[i].translation_y_pixels / canvas_.height;
            value.scale = global[i].scale; value.rotation_degrees = global[i].rotation_degrees;
        }
        // Opacity is always local, never inherited.
        if (!creative_suite::animation::validTransform(value))
            throw std::runtime_error("Layer hierarchy evaluation produced invalid geometry for layer " + std::to_string(n.id));
        result.emplace(n.id, value);
    }
    return result;
}

} // namespace motion::model
