#include "composition_document.h"
namespace motion::model {
double shadowBaseValue(const DropShadowEffect &e, EffectParameterProperty p) noexcept {
    switch (p) {
    case EffectParameterProperty::ShadowOpacityPercent:
        return e.parameters.opacity_percent;
    case EffectParameterProperty::ShadowAngleDegrees:
        return e.parameters.angle_degrees;
    case EffectParameterProperty::ShadowDistancePixels:
        return e.parameters.distance_pixels;
    case EffectParameterProperty::ShadowSoftnessPixels:
        return e.parameters.softness_pixels;
    default:
        return 0;
    }
}
void setShadowBaseValue(DropShadowEffect &e, EffectParameterProperty p, double v) noexcept {
    switch (p) {
    case EffectParameterProperty::ShadowOpacityPercent:
        e.parameters.opacity_percent = v;
        break;
    case EffectParameterProperty::ShadowAngleDegrees:
        e.parameters.angle_degrees = v;
        break;
    case EffectParameterProperty::ShadowDistancePixels:
        e.parameters.distance_pixels = v;
        break;
    case EffectParameterProperty::ShadowSoftnessPixels:
        e.parameters.softness_pixels = v;
        break;
    default:
        break;
    }
}
DropShadowEffect evaluateDropShadowEffect(const DropShadowEffect &e, std::int64_t frame) {
    DropShadowEffect result;
    result.enabled = e.enabled;
    result.parameters = e.parameters;
    result.mask = e.mask;
    for (std::size_t i = 0; i < 4; ++i) {
        setShadowBaseValue(result, shadow_properties[i],
                           creative_suite::animation::evaluateScalar(shadowBaseValue(e, shadow_properties[i]),
                                                                     e.keyframes[i], frame));
        result.keyframes[i].clear();
    }
    return result;
}
} // namespace motion::model
