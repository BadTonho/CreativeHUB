#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace creative_suite::effects {
struct ColorizeParameters {
    int hue = 0;
    int saturation = 25;
    int lightness = 0;
    bool operator==(const ColorizeParameters&) const = default;
};
struct HueRangeParameters {
    int hue = 0, saturation = 0, lightness = 0;
    bool operator==(const HueRangeParameters&) const = default;
};
// Encoded-sRGB HSL controls. Global values apply before the six circularly
// interpolated ranges selected from the original chromatic hue. Hue is a signed
// rotation in degrees; saturation/lightness move toward zero or one by signed
// percent. Colorize stores an independent absolute HSL target and enable flag.
struct HueSaturationParameters {
    int hue = 0;
    int saturation = 0;
    int lightness = 0;
    bool colorize_enabled = false;
    ColorizeParameters colorize;
    // Red, yellow, green, cyan, blue and magenta, in hue-wheel order.
    std::array<HueRangeParameters, 6> ranges{};
    bool operator==(const HueSaturationParameters&) const = default;
};
[[nodiscard]] inline bool validHueSaturation(const HueSaturationParameters& p) noexcept {
    return std::all_of(p.ranges.begin(), p.ranges.end(), [](const auto& r) {
        return r.hue >= -180 && r.hue <= 180 && r.saturation >= -100 &&
            r.saturation <= 100 && r.lightness >= -100 && r.lightness <= 100;
    }) && p.hue >= -180 && p.hue <= 180 && p.saturation >= -100 &&
        p.saturation <= 100 && p.lightness >= -100 && p.lightness <= 100 &&
        p.colorize.hue >= 0 && p.colorize.hue <= 360 &&
        p.colorize.saturation >= 0 && p.colorize.saturation <= 100 &&
        p.colorize.lightness >= -100 && p.colorize.lightness <= 100;
}
// Borrowed finite straight RGB in [0,1], validated parameters; no allocation,
// alpha processing or retained state. Normal Saturation does not colorize gray
// pixels; the explicit Colorize mode applies its absolute target to every pixel.
[[nodiscard]] inline std::array<unsigned char,3> hueSaturationRgb(
    const HueSaturationParameters& p, std::array<double,3> rgb) noexcept {
    const double low = std::min({rgb[0], rgb[1], rgb[2]});
    const double high = std::max({rgb[0], rgb[1], rgb[2]});
    const double delta = high - low;
    const auto adjust = [](double value, double amount) {
        return amount < 0 ? value * (1. + amount / 100.) :
            value + (1. - value) * (amount / 100.);
    };
    const double original_lightness = (high + low) * .5;
    const bool colorize = p.colorize_enabled;
    double lightness = adjust(original_lightness, colorize ? p.colorize.lightness : p.lightness);
    double hue = 0, saturation = 0;
    if (colorize) {
        hue = (p.colorize.hue % 360) / 60.;
        saturation = p.colorize.saturation / 100.;
    } else if (delta > 0) {
        saturation = adjust(std::clamp(delta / (1. - std::abs(2. * original_lightness - 1.)), 0., 1.), p.saturation);
        hue = high == rgb[0] ? (rgb[1] - rgb[2]) / delta :
            high == rgb[1] ? (rgb[2] - rgb[0]) / delta + 2. : (rgb[0] - rgb[1]) / delta + 4.;
        const double original_hue = hue;
        if (hue < 0) hue += 6.;
        if (hue >= 6.) hue = 0.; // Floating-point rounding at the red boundary.
        const int first = static_cast<int>(hue);
        const double fraction = hue - first;
        const auto& a = p.ranges[first];
        const auto& b = p.ranges[(first + 1) % 6];
        const auto blend = [fraction](int x, int y) { return x * (1. - fraction) + y * fraction; };
        if (a == HueRangeParameters{} && b == HueRangeParameters{}) {
            hue = std::fmod(original_hue + p.hue / 60., 6.);
        } else {
            saturation = adjust(saturation, blend(a.saturation, b.saturation));
            lightness = adjust(lightness, blend(a.lightness, b.lightness));
            hue = std::fmod(hue + (p.hue + blend(a.hue, b.hue)) / 60., 6.);
        }
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
