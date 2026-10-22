#include "layer_position.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace motion::model {
bool validLayerPosition(double x, double y) noexcept
{
    return std::isfinite(x) && std::isfinite(y) && std::abs(x) <= 1e6 && std::abs(y) <= 1e6;
}

std::array<double, 2> positionFromCanvasDelta(double x, double y, CanvasSize canvas,
    const AffineTransform& inverse, double dx, double dy)
{
    if (canvas.width <= 0 || canvas.height <= 0 || !validLayerPosition(x,y) ||
        !std::isfinite(dx) || !std::isfinite(dy) || inverse.tx!=0 || inverse.ty!=0 ||
        !creative_suite::composition::validAffine(inverse))
        throw std::invalid_argument("Position drag requires finite invertible geometry.");
    const auto delta = inverse.map(dx,dy);
    const std::array result{x + delta[0]/canvas.width, y + delta[1]/canvas.height};
    if (!std::isfinite(result[0]) || !std::isfinite(result[1]))
        throw std::runtime_error("Position drag exceeds finite geometry limits.");
    return result;
}

bool CompositionDocument::setLayerPositionAtFrame(LayerId id, double x, double y, std::int64_t frame)
{
    auto* layer = findLayer(id);
    if (!layer || !validLayerPosition(x,y) || frame < layer->timeline_start_frame ||
        frame-layer->timeline_start_frame >= layer->duration_frames) return false;
    namespace a = creative_suite::animation;
    const auto local = frame-layer->timeline_start_frame;
    const auto initial = a::evaluateTransform(layer->transform,layer->keyframes,local);
    if (initial.position_x == x && initial.position_y == y) return false;
    auto candidate = *layer;
    std::size_t inserted = 0;
    const auto set = [local,&inserted](auto& keys, double& base, double old, double value) {
        if (old == value) return;
        if (keys.empty()) base = value;
        else {
            const auto size = keys.size();
            if (!a::setKeyframe(keys,local,value)) throw std::runtime_error("Position key insertion failed.");
            inserted += keys.size()-size;
        }
    };
    set(candidate.keyframes.position_x,candidate.transform.position_x,initial.position_x,x);
    set(candidate.keyframes.position_y,candidate.transform.position_y,initial.position_y,y);
    if (!canInsertKeyframes(inserted)) return false;
    if (!a::validTransform(candidate.transform) || !a::validTransformKeyframes(candidate.keyframes)) return false;
    (void)localLayerGeometry(candidate,canvas_size_,local);
    (void)LayerTransformHierarchy(canvas_size_,layers_).evaluate(frame,LayerPositionOverride{id,x,y});
    *layer = std::move(candidate);
    return true;
}

bool CompositionDocument::canInsertKeyframes(std::size_t inserted) const noexcept
{

    if (inserted > maximum_document_keyframes) return false;
    std::size_t count = inserted;
    bool exceeds_budget = false;
    const auto add = [&count,&exceeds_budget](const auto& keys) {
        if (keys.size() > maximum_document_keyframes-count) {
            exceeds_budget = true;
            count = maximum_document_keyframes;
        } else count += keys.size();
    };
    const auto addPath=[&](const PathAnimation& path){
        const auto cost=pathKeyframeCost(path);
        if(cost>maximum_document_keyframes-count){exceeds_budget=true;count=maximum_document_keyframes;}else count+=cost;
    };
    for (const auto& mask : masks_) {for (const auto& keys : mask.keyframes) add(keys);if(mask.path)addPath(*mask.path);}

    for (const auto& item : layers_) {
        if(const auto* shape=std::get_if<ShapeLayerContent>(&item.content))for(const auto& keys:shape->trim_paths.get().keys)add(keys);
        if(const auto* shape=std::get_if<ShapeLayerContent>(&item.content);shape&&shape->path)addPath(*shape->path);
        add(item.keyframes.position_x);add(item.keyframes.position_y);add(item.keyframes.scale);
        add(item.keyframes.rotation);add(item.keyframes.opacity);
        for (const auto& keys : item.anchor_keyframes) add(keys);
        if (item.scale_y) add(item.scale_y->keyframes);
        for (const auto& effect : item.effects) std::visit([&](const auto& e) {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E,GaussianBlurEffect>) add(e.radius_keyframes);
            else if constexpr(std::is_same_v<E,DropShadowEffect>){for(const auto& keys:e.keyframes)add(keys);}
            else if constexpr(std::is_same_v<E,GlowEffect>){for(const auto& keys:e.keyframes)add(keys);}
            else {add(e.brightness_keyframes);add(e.contrast_keyframes);add(e.saturation_keyframes);}
        },effect);
    }
    return !exceeds_budget; // Expected editing limit, not a technical failure.
}
} // namespace motion::model
