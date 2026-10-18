#include "layer_rotation.h"
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace motion::model {
bool validLayerRotation(double degrees) noexcept
{
    return std::isfinite(degrees) && std::abs(degrees) <= 1e9;
}

std::array<double, 2> rotationGuideDirection(const AffineTransform& outer, double degrees)
{
    if (!creative_suite::composition::validAffine(outer) || !validLayerRotation(degrees))
        throw std::invalid_argument("Rotation guide requires finite invertible geometry.");
    const auto radians = std::remainder(degrees, 360.) * std::numbers::pi / 180.;
    const auto x = std::sin(radians), y = -std::cos(radians);
    const std::array result{outer.a*x + outer.c*y, outer.b*x + outer.d*y};
    const auto length = std::hypot(result[0], result[1]);
    if (!std::isfinite(length) || length == 0)
        throw std::runtime_error("Rotation guide exceeds finite geometry limits.");
    return {result[0]/length, result[1]/length};
}

RotationDragCalculation::RotationDragCalculation(double initial, const AffineTransform& inverse,
    double px, double py, double x, double y, double dead_radius)
    : inverse_(inverse), initial_(initial), pivot_x_(px), pivot_y_(py), dead_radius_(dead_radius)
{
    if (!validLayerRotation(initial) || inverse.tx != 0 || inverse.ty != 0 ||
        !creative_suite::composition::validAffine(inverse) || !std::isfinite(px) || !std::isfinite(py) ||
        !std::isfinite(dead_radius) || dead_radius < 0)
        throw std::invalid_argument("Rotation drag requires finite invertible geometry.");
    previous_ = angle(x,y);
}

std::optional<double> RotationDragCalculation::angle(double x, double y) const
{
    if (!std::isfinite(x) || !std::isfinite(y)) throw std::invalid_argument("Non-finite rotation pointer.");
    x -= pivot_x_; y -= pivot_y_;
    if (!std::isfinite(x) || !std::isfinite(y)) throw std::runtime_error("Rotation pointer exceeds finite limits.");
    if (std::hypot(x,y) <= dead_radius_) return {};
    const auto local = inverse_.map(x,y);
    if (!std::isfinite(local[0]) || !std::isfinite(local[1]) || (local[0]==0 && local[1]==0))
        throw std::runtime_error("Rotation pointer exceeds finite geometry limits.");
    return std::atan2(local[1],local[0]) * 180. / std::numbers::pi;
}

double RotationDragCalculation::update(double x, double y)
{
    const auto current = angle(x,y);
    if (current && previous_) {
        auto delta = std::remainder(*current - *previous_,360.);
        if (delta == -180.) delta = 180.;
        total_ += delta;
    }
    // Entering the neutral zone discards the reference; exiting rebases it.
    previous_ = current;
    const auto result = std::abs(total_) <= 1e-9 ? initial_ : initial_ + total_;
    if (!std::isfinite(result)) throw std::runtime_error("Rotation exceeds finite limits.");
    return result;
}

bool CompositionDocument::setLayerRotationAtFrame(LayerId id, double degrees, std::int64_t frame)
{
    auto* layer = findLayer(id);
    if (!layer || !validLayerRotation(degrees) || frame < layer->timeline_start_frame ||
        frame-layer->timeline_start_frame >= layer->duration_frames) return false;
    namespace a = creative_suite::animation;
    const auto local = frame-layer->timeline_start_frame;
    if (a::evaluateTransform(layer->transform,layer->keyframes,local).rotation_degrees == degrees) return false;
    auto candidate = *layer;
    if (candidate.keyframes.rotation.empty()) candidate.transform.rotation_degrees = degrees;
    else {
        const auto count = candidate.keyframes.rotation.size();
        if (!a::setKeyframe(candidate.keyframes.rotation,local,degrees))
            throw std::runtime_error("Rotation key insertion failed.");
        if (!canInsertKeyframes(candidate.keyframes.rotation.size()-count)) return false;
    }
    if (!a::validTransform(candidate.transform) || !a::validTransformKeyframes(candidate.keyframes)) return false;
    (void)localLayerGeometry(candidate,canvas_size_,local);
    (void)LayerTransformHierarchy(canvas_size_,layers_).evaluate(frame,{},LayerRotationOverride{id,degrees});
    *layer = std::move(candidate);
    return true;
}
} // namespace motion::model
