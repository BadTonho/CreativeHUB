#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace creative_suite::effects {
// Global encoded-sRGB HSL controls. Hue is a signed rotation in degrees;
// saturation/lightness move toward zero or one by signed percent.
struct HueSaturationParameters {
    int hue = 0;
    int saturation = 0;
    int lightness = 0;
    bool operator==(const HueSaturationParameters&) const = default;
};
[[nodiscard]] inline bool validHueSaturation(const HueSaturationParameters& p) noexcept {
    return p.hue >= -180 && p.hue <= 180 && p.saturation >= -100 &&
        p.saturation <= 100 && p.lightness >= -100 && p.lightness <= 100;
}
// Borrowed finite straight RGB in [0,1], validated parameters; no allocation,
// alpha processing or retained state. Saturation does not colorize gray pixels.
[[nodiscard]] inline std::array<unsigned char,3> hueSaturationRgb(
    const HueSaturationParameters& p, std::array<double,3> rgb) noexcept {
    const double low = std::min({rgb[0], rgb[1], rgb[2]});
    const double high = std::max({rgb[0], rgb[1], rgb[2]});
    const double delta = high - low;
    const auto adjust = [](double value, int amount) {
        return amount < 0 ? value * (1. + amount / 100.) :
            value + (1. - value) * (amount / 100.);
    };
    const double original_lightness = (high + low) * .5;
    const double lightness = adjust(original_lightness, p.lightness);
    double hue = 0, saturation = 0;
    if (delta > 0) {
        saturation = adjust(std::clamp(delta / (1. - std::abs(2. * original_lightness - 1.)), 0., 1.), p.saturation);
        hue = high == rgb[0] ? (rgb[1] - rgb[2]) / delta :
            high == rgb[1] ? (rgb[2] - rgb[0]) / delta + 2. : (rgb[0] - rgb[1]) / delta + 4.;
        hue = std::fmod(hue + p.hue / 60., 6.);
        if (hue < 0) hue += 6.;
    }
    const double chroma = (1. - std::abs(2. * lightness - 1.)) * saturation;
    const double x = chroma * (1. - std::abs(std::fmod(hue, 2.) - 1.));
    const double m = lightness - chroma * .5;
    const int sector = static_cast<int>(hue);
    const std::array<double,3> color = sector == 0 ? std::array{chroma,x,0.} :
        sector == 1 ? std::array{x,chroma,0.} : sector == 2 ? std::array{0.,chroma,x} :
        sector == 3 ? std::array{0.,x,chroma} : sector == 4 ? std::array{x,0.,chroma} : std::array{chroma,0.,x};
    std::array<unsigned char,3> result{};
    for (int i = 0; i < 3; ++i)
        result[i] = static_cast<unsigned char>(std::lround(std::clamp(color[i] + m, 0., 1.) * 255.));
    return result;
}
} // namespace creative_suite::effects
