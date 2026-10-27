#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace creative_suite::effects {
// Straight encoded-sRGB. Three tonal ranges, each in RGB-axis order.
struct ColorBalanceParameters {
    std::array<std::array<int, 3>, 3> ranges{};
    bool preserve_lightness = true;
    bool operator==(const ColorBalanceParameters&) const = default;
};
[[nodiscard]] inline bool validColorBalance(const ColorBalanceParameters& p) noexcept {
    for (const auto& range : p.ranges)
        for (int value : range) if (value < -100 || value > 100) return false;
    return true;
}
[[nodiscard]] inline bool identityColorBalance(const ColorBalanceParameters& p) noexcept {
    for (const auto& range : p.ranges) for (int value : range) if (value) return false;
    return true;
}
[[nodiscard]] inline std::array<unsigned char, 3> colorBalanceRgb(
    const ColorBalanceParameters& p, std::array<double, 3> rgb) noexcept {
    const double original = (std::min({rgb[0],rgb[1],rgb[2]}) + std::max({rgb[0],rgb[1],rgb[2]})) * .5;
    const double shadows = std::max(0., 1. - 2. * original);
    const double highlights = std::max(0., 2. * original - 1.);
    const double weights[]{shadows, 1. - shadows - highlights, highlights};
    for (int channel = 0; channel < 3; ++channel) {
        double change = 0;
        for (int range = 0; range < 3; ++range) change += weights[range] * p.ranges[range][channel];
        rgb[channel] = std::clamp(rgb[channel] + change / 200., 0., 1.);
    }
    if (p.preserve_lightness && !identityColorBalance(p)) {
        const double lo = std::min({rgb[0],rgb[1],rgb[2]}), hi = std::max({rgb[0],rgb[1],rgb[2]});
        const double delta = hi - lo, lightness = (hi + lo) * .5;
        const double saturation = delta > 0 ? delta / (1. - std::abs(2. * lightness - 1.)) : 0.;
        const double chroma = (1. - std::abs(2. * original - 1.)) * saturation;
        if (delta > 0) for (auto& channel : rgb) channel = (channel - lo) * chroma / delta + original - chroma * .5;
        else rgb = {original, original, original};
    }
    std::array<unsigned char, 3> out{};
    for (int i = 0; i < 3; ++i) out[i] = static_cast<unsigned char>(std::lround(std::clamp(rgb[i],0.,1.)*255.));
    return out;
}
}
