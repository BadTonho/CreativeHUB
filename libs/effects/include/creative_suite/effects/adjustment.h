#pragma once
#include "gradient_map.h"
#include "effects.h"
#include <variant>
#include <type_traits>
namespace creative_suite::effects {
struct BrightnessContrastParameters {
    int brightness=0,contrast=0;
    bool operator==(const BrightnessContrastParameters&) const = default;
};
using AdjustmentParameters=std::variant<GradientMapParameters,BrightnessContrastParameters>;
[[nodiscard]] inline bool identityAdjustment(const AdjustmentParameters& p) noexcept {
    const auto* value=std::get_if<BrightnessContrastParameters>(&p);
    return value && value->brightness==0 && value->contrast==0;
}
[[nodiscard]] inline bool validBrightnessContrast(const BrightnessContrastParameters& p) noexcept {
    return p.brightness>=-100 && p.brightness<=100 && p.contrast>=-100 && p.contrast<=100;
}
[[nodiscard]] inline bool validAdjustment(const AdjustmentParameters& p) noexcept {
    return std::visit([](const auto& value){if constexpr(std::is_same_v<std::decay_t<decltype(value)>,GradientMapParameters>)return validGradientMap(value);else return validBrightnessContrast(value);},p);
}
[[nodiscard]] inline std::array<unsigned char,3> adjustmentRgb(const AdjustmentParameters& p,std::array<double,3> rgb) noexcept {
    return std::visit([&](const auto& value){if constexpr(std::is_same_v<std::decay_t<decltype(value)>,GradientMapParameters>)return gradientMapColor(value,.2126*rgb[0]+.7152*rgb[1]+.0722*rgb[2]);else return colorAdjustmentRgb(rgb,{double(value.brightness),double(100+value.contrast),100.});},p);
}
} // namespace creative_suite::effects
