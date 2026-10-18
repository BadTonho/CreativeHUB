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
                                    CanvasSize canvas, AnchorPoint anchor)
{
    ParentTransformOffset result{value.position_x * canvas.width,
        value.position_y * canvas.height, value.scale, value.rotation_degrees};
    if (!validParentOffset(result)) throw std::runtime_error("Invalid layer hierarchy geometry.");
    if (!validAnchorPoint(anchor)) throw std::runtime_error("Invalid layer anchor point.");
    if (anchor != AnchorPoint{}) result = composeOffsets(result, {-anchor.x_pixels, -anchor.y_pixels, 1, 0});
    return result;
}

std::array<double, 2> resolvedAnchorPosition(const creative_suite::animation::Transform2D& resolved,
                                           AnchorPoint anchor, CanvasSize canvas)
{
    if (anchor == AnchorPoint{}) return {resolved.position_x,resolved.position_y};
    const auto pivot = composeOffsets(geometryOffset(resolved, canvas), {anchor.x_pixels, anchor.y_pixels, 1, 0});
    return {pivot.translation_x_pixels / canvas.width, pivot.translation_y_pixels / canvas.height};
}

AffineTransform affineOffset(const ParentTransformOffset& offset)
{
    if(!validParentOffset(offset))throw std::runtime_error("Invalid legacy geometry.");
    const auto angle=std::remainder(offset.rotation_degrees,360.)*std::numbers::pi/180.;
    const auto c=std::cos(angle),s=std::sin(angle);
    AffineTransform result{c*offset.scale,s*offset.scale,-s*offset.scale,c*offset.scale,
        offset.translation_x_pixels,offset.translation_y_pixels};
    if(!creative_suite::composition::validAffine(result))throw std::runtime_error("Legacy geometry has no finite affine inverse.");
    return result;
}
AffineTransform localLayerGeometry(const CompositionLayer& layer,CanvasSize canvas,std::int64_t frame)
{
    const auto t=creative_suite::animation::evaluateTransform(layer.transform,layer.keyframes,frame);
    const auto scale=evaluateLayerScale(layer,frame);const auto anchor=evaluateLayerAnchor(layer,frame);
    return localGeometry(t,scale[1],canvas,anchor);
}
AffineTransform localGeometry(const creative_suite::animation::Transform2D& t,double sy,CanvasSize canvas,AnchorPoint anchor)
{
    if(t.scale==sy)return affineOffset(geometryOffset(t,canvas,anchor));
    const auto angle=std::remainder(t.rotation_degrees,360.)*std::numbers::pi/180.;
    const auto c=std::cos(angle),s=std::sin(angle);
    AffineTransform result{c*t.scale,s*t.scale,-s*sy,c*sy,t.position_x*canvas.width,t.position_y*canvas.height};
    result.tx-=result.a*anchor.x_pixels+result.c*anchor.y_pixels;
    result.ty-=result.b*anchor.x_pixels+result.d*anchor.y_pixels;
    if(!validParentAffine(result))throw std::runtime_error("Local scale geometry exceeds finite invertible limits.");
    return result;
}
AffineTransform sourcePixelGeometry(const AffineTransform& world,int width,int height,double fit)
{
    return creative_suite::composition::composeAffine(world,{fit,0,0,fit,-width*.5*fit,-height*.5*fit});
}
std::array<double,2> resolvedAnchorPosition(const ResolvedLayerTransform& value,AnchorPoint anchor,CanvasSize canvas)
{
    if(!value.affine)return resolvedAnchorPosition(static_cast<const creative_suite::animation::Transform2D&>(value),anchor,canvas);
    const auto p=value.world.map(anchor.x_pixels,anchor.y_pixels);
    return {p[0]/canvas.width,p[1]/canvas.height};
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
            !creative_suite::animation::validTransformKeyframes(layer.keyframes) ||
            !validAnchorPoint(layer.anchor_point) ||
            (layer.scale_y && !validScaleChannel(*layer.scale_y)) ||
            (layer.parent_affine && (!validParentAffine(*layer.parent_affine) || layer.parent_offset!=ParentTransformOffset{})) ||
            !creative_suite::animation::validScalarKeyframes(layer.anchor_keyframes[0]) ||
            !creative_suite::animation::validScalarKeyframes(layer.anchor_keyframes[1]))
            throw std::invalid_argument("Invalid or duplicate layer hierarchy node.");
        nodes_.push_back({layer.id, layer.parent_id, layer.parent_offset,
            layer.timeline_start_frame, layer.transform, layer.keyframes, layer.anchor_point, layer.anchor_keyframes,layer.scale_y,layer.parent_affine});
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
            n.start != l.timeline_start_frame || n.transform != l.transform || n.keyframes != l.keyframes ||
            n.anchor != l.anchor_point || n.anchor_keys != l.anchor_keyframes || n.scale_y!=l.scale_y || n.parent_affine!=l.parent_affine)
            return false;
    }
    return true;
}

std::unordered_map<LayerId, ResolvedLayerTransform>
LayerTransformHierarchy::evaluate(std::int64_t frame, std::optional<LayerPositionOverride> position) const
{
    if(frame<0)throw std::invalid_argument("Negative hierarchy composition frame.");
    if (position && (!indices_.contains(position->layer_id) || !std::isfinite(position->x) || !std::isfinite(position->y)))
        throw std::invalid_argument("Invalid hierarchy Position override.");
    std::vector<ParentTransformOffset> legacy(nodes_.size());
    std::vector<AffineTransform> global(nodes_.size());
    std::vector<bool> uses_affine(nodes_.size());
    std::unordered_map<LayerId,ResolvedLayerTransform> result;result.reserve(nodes_.size());
    namespace c=creative_suite::composition;
    for(const auto i:order_) {
        const auto& n=nodes_[i];
        try {
        const auto local_frame=frame<n.start?0:frame-n.start;
        auto value=creative_suite::animation::evaluateTransform(n.transform,n.keyframes,local_frame);
        if (position && position->layer_id == n.id) {
            value.position_x = position->x;
            value.position_y = position->y;
        }
        const auto sy=n.scale_y?creative_suite::animation::evaluateScalar(n.scale_y->value,n.scale_y->keyframes,local_frame):value.scale;
        const AnchorPoint anchor{creative_suite::animation::evaluateScalar(n.anchor.x_pixels,n.anchor_keys[0],local_frame),
            creative_suite::animation::evaluateScalar(n.anchor.y_pixels,n.anchor_keys[1],local_frame)};
        const auto parent=n.parent_id?indices_.at(n.parent_id):i;
        uses_affine[i]=sy!=value.scale || n.parent_affine || (n.parent_id && uses_affine[parent]);
        if(!uses_affine[i]) {
            const auto local=geometryOffset(value,canvas_,anchor);
            legacy[i]=n.offset==ParentTransformOffset{}?local:composeOffsets(n.offset,local);
            if(n.parent_id)legacy[i]=composeOffsets(legacy[parent],legacy[i]);
            global[i]=affineOffset(legacy[i]);
            if(n.parent_id || n.offset!=ParentTransformOffset{} || anchor!=AnchorPoint{}) {
                value.position_x=legacy[i].translation_x_pixels/canvas_.width;value.position_y=legacy[i].translation_y_pixels/canvas_.height;
                value.scale=legacy[i].scale;value.rotation_degrees=legacy[i].rotation_degrees;
            }
        } else {
            const auto geometry=localGeometry(value,sy,canvas_,anchor);
            global[i]=c::composeAffine(n.parent_affine.value_or(affineOffset(n.offset)),geometry);
            if(n.parent_id)global[i]=c::composeAffine(global[parent],global[i]);
            value.position_x=global[i].tx/canvas_.width;value.position_y=global[i].ty/canvas_.height;
            value.scale=1;value.rotation_degrees=0;
        }
        if(!creative_suite::animation::validTransform(value))throw std::runtime_error("Invalid resolved geometry for layer "+std::to_string(n.id));
        ResolvedLayerTransform resolved;static_cast<creative_suite::animation::Transform2D&>(resolved)=value;
        resolved.world=global[i];resolved.affine=uses_affine[i];result.emplace(n.id,resolved);
        }catch(const std::exception& error){throw std::runtime_error("Hierarchy geometry for layer "+std::to_string(n.id)+": "+error.what());}
    }
    return result;
}
} // namespace motion::model
