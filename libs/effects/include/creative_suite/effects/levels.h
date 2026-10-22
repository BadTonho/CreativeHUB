#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace creative_suite::effects {

// Global, encoded-sRGB channel mapping. Inputs are borrowed and alpha is untouched.
struct LevelsParameters {
    int input_black = 0;
    int input_white = 255;
    double gamma = 1.0;
    int output_black = 0;
    int output_white = 255;
    bool operator==(const LevelsParameters&) const = default;
};

[[nodiscard]] inline bool validLevels(const LevelsParameters& p) noexcept {
    return p.input_black >= 0 && p.input_black < p.input_white && p.input_white <= 255 &&
        std::isfinite(p.gamma) && p.gamma >= .1 && p.gamma <= 9.99 &&
        p.output_black >= 0 && p.output_black <= p.output_white && p.output_white <= 255;
}

[[nodiscard]] inline std::array<unsigned char, 3> levelsRgb(
    const LevelsParameters& p, const std::array<double, 3>& rgb) noexcept {
    std::array<unsigned char, 3> result{};
    for (int i = 0; i < 3; ++i) {
        const double input = std::clamp(rgb[i], 0., 1.) * 255.;
        const double normalized = std::clamp(
            (input - p.input_black) / (p.input_white - p.input_black), 0., 1.);
        const double output = p.output_black +
            (p.output_white - p.output_black) * std::pow(normalized, 1. / p.gamma);
        result[i] = static_cast<unsigned char>(std::lround(std::clamp(output, 0., 255.)));
    }
    return result;
}

} // namespace creative_suite::effects
