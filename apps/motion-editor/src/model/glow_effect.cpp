#include "composition_document.h"

namespace motion::model {

double glowBaseValue(const GlowEffect& effect, EffectParameterProperty property) noexcept
{
    switch (property) {
    case EffectParameterProperty::GlowOpacityPercent:
        return effect.parameters.opacity_percent;
    case EffectParameterProperty::GlowRadiusPixels:
        return effect.parameters.radius_pixels;
    default:
        return 0.0;
    }
}

void setGlowBaseValue(GlowEffect& effect, EffectParameterProperty property, double value) noexcept
{
    switch (property) {
    case EffectParameterProperty::GlowOpacityPercent:
        effect.parameters.opacity_percent = value;
        break;
    case EffectParameterProperty::GlowRadiusPixels:
        effect.parameters.radius_pixels = value;
        break;
    default:
        break;
    }
}

GlowEffect evaluateGlowEffect(const GlowEffect& effect, std::int64_t frame)
{
    GlowEffect result = effect;
    for (std::size_t index = 0; index < glow_properties.size(); ++index) {
        const auto property = glow_properties[index];
        setGlowBaseValue(result, property, creative_suite::animation::evaluateScalar(
            glowBaseValue(effect, property), effect.keyframes[index], frame));
        result.keyframes[index].clear();
    }
    return result;
}

} // namespace motion::model
