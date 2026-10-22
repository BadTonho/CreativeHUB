#pragma once
#include "gradient_map.h"
#include "effects.h"
#include "hue_saturation.h"
#include "levels.h"
#include "curves.h"
#include <cmath>
#include <variant>
#include <type_traits>

namespace creative_suite::effects {
struct BrightnessContrastParameters {
    int brightness=0,contrast=0;
    bool operator==(const BrightnessContrastParameters&) const = default;
};
// Straight sRGB inputs; exposure is measured in stops. No allocation or ownership.
struct ExposureOffsetGammaParameters {
    double exposure = 0;
    double offset = 0;
    double gamma = 1;
    bool operator==(const ExposureOffsetGammaParameters&) const = default;
};
using AdjustmentParameters = std::variant<GradientMapParameters,
    BrightnessContrastParameters, ExposureOffsetGammaParameters, HueSaturationParameters,
    LevelsParameters, CurvesParameters>;
[[nodiscard]] inline bool identityAdjustment(const AdjustmentParameters& p) noexcept {
    if (const auto* value = std::get_if<BrightnessContrastParameters>(&p))
        return *value == BrightnessContrastParameters{};
    if (const auto* value = std::get_if<ExposureOffsetGammaParameters>(&p))
        return *value == ExposureOffsetGammaParameters{};
    if (const auto* value = std::get_if<HueSaturationParameters>(&p))
        return *value == HueSaturationParameters{};
    if (const auto* value = std::get_if<LevelsParameters>(&p))
        return *value == LevelsParameters{};
    if (const auto* value = std::get_if<CurvesParameters>(&p))
        return identityCurves(*value);
    return false;
}
[[nodiscard]] inline bool validBrightnessContrast(const BrightnessContrastParameters& p) noexcept {
    return p.brightness>=-100 && p.brightness<=100 && p.contrast>=-100 && p.contrast<=100;
}
[[nodiscard]] inline bool validExposureOffsetGamma(const ExposureOffsetGammaParameters& p) noexcept {
    return std::isfinite(p.exposure) && p.exposure >= -20 && p.exposure <= 20 &&
        std::isfinite(p.offset) && p.offset >= -.5 && p.offset <= .5 &&
        std::isfinite(p.gamma) && p.gamma >= .01 && p.gamma <= 9.99;
}
[[nodiscard]] inline bool validAdjustment(const AdjustmentParameters& p) noexcept {
    return std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, GradientMapParameters>) return validGradientMap(value);
        else if constexpr (std::is_same_v<T, BrightnessContrastParameters>) return validBrightnessContrast(value);
        else if constexpr (std::is_same_v<T, ExposureOffsetGammaParameters>) return validExposureOffsetGamma(value);
        else if constexpr (std::is_same_v<T, HueSaturationParameters>) return validHueSaturation(value);
        else if constexpr (std::is_same_v<T, LevelsParameters>) return validLevels(value);
        else return validCurves(value);
    }, p);
}
// Callers validate parameters and pass finite channels in [0,1]. Alpha is untouched.
[[nodiscard]] inline std::array<unsigned char,3> exposureOffsetGammaRgb(
    const ExposureOffsetGammaParameters& p, std::array<double,3> rgb) noexcept {
    std::array<unsigned char,3> result{};
    const double gain = std::exp2(p.exposure);
    for (int i = 0; i < 3; ++i) {
        const double v = std::clamp(rgb[i], 0., 1.);
        const double linear = v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
        // Clamping above one before pow is equivalent to clamping its result,
        // and avoids overflow at the maximum exposure/minimum gamma combination.
        const double corrected = std::pow(std::clamp(linear * gain + p.offset, 0., 1.), 1. / p.gamma);
        const double encoded = corrected <= .0031308 ? corrected * 12.92 : 1.055 * std::pow(corrected, 1. / 2.4) - .055;
        result[i] = static_cast<unsigned char>(std::lround(std::clamp(encoded, 0., 1.) * 255));
    }
    return result;
}
[[nodiscard]] inline std::array<unsigned char,3> adjustmentRgb(const AdjustmentParameters& p,std::array<double,3> rgb) noexcept {
    return std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, GradientMapParameters>)
            return gradientMapColor(value,.2126*rgb[0]+.7152*rgb[1]+.0722*rgb[2]);
        else if constexpr (std::is_same_v<T, BrightnessContrastParameters>)
            return colorAdjustmentRgb(rgb,{double(value.brightness),double(100+value.contrast),100.});
        else if constexpr (std::is_same_v<T, ExposureOffsetGammaParameters>) return exposureOffsetGammaRgb(value, rgb);
        else if constexpr (std::is_same_v<T, HueSaturationParameters>) return hueSaturationRgb(value, rgb);
        else if constexpr (std::is_same_v<T, LevelsParameters>) return levelsRgb(value, rgb);
        else return curvesRgb(value, rgb);
    }, p);
}
} // namespace creative_suite::effects
