#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace creative_suite::effects {

struct LevelsChannelParameters {
    int input_black = 0;
    int input_white = 255;
    double gamma = 1.0;
    int output_black = 0;
    int output_white = 255;
    bool operator==(const LevelsChannelParameters&) const = default;
};

// Encoded-sRGB RGB-master and per-channel mapping. Alpha is untouched.
struct LevelsParameters {
    // Keep these five RGB master fields at their existing API locations.
    int input_black = 0;
    int input_white = 255;
    double gamma = 1.0;
    int output_black = 0;
    int output_white = 255;
    // Red, green, and blue. The master mapping is applied before these sets.
    std::array<LevelsChannelParameters, 3> channels{};
    bool operator==(const LevelsParameters&) const = default;
};

[[nodiscard]] inline bool validLevelsControls(const LevelsChannelParameters& p) noexcept {
    return p.input_black >= 0 && p.input_black < p.input_white && p.input_white <= 255 &&
        std::isfinite(p.gamma) && p.gamma >= .1 && p.gamma <= 9.99 &&
        p.output_black >= 0 && p.output_black <= p.output_white && p.output_white <= 255;
}

[[nodiscard]] inline LevelsChannelParameters levelsChannel(
    const LevelsParameters& p, int channel) noexcept {
    if (channel >= 1 && channel <= 3)
        return p.channels[static_cast<std::size_t>(channel - 1)];
    return {p.input_black, p.input_white, p.gamma, p.output_black, p.output_white};
}

inline void setLevelsChannel(LevelsParameters* p, int channel,
    const LevelsChannelParameters& value) noexcept {
    if (!p) return;
    if (channel >= 1 && channel <= 3) {
        p->channels[static_cast<std::size_t>(channel - 1)] = value;
    } else {
        p->input_black = value.input_black;
        p->input_white = value.input_white;
        p->gamma = value.gamma;
        p->output_black = value.output_black;
        p->output_white = value.output_white;
    }
}

[[nodiscard]] inline bool validLevels(const LevelsParameters& p) noexcept {
    if (!validLevelsControls(levelsChannel(p, 0))) return false;
    for (const auto& channel : p.channels)
        if (!validLevelsControls(channel)) return false;
    return true;
}

[[nodiscard]] inline bool identityLevels(const LevelsParameters& p) noexcept {
    return p == LevelsParameters{};
}

[[nodiscard]] inline unsigned char levelsByte(
    const LevelsChannelParameters& p, double value) noexcept {
    const double input = std::clamp(value, 0., 1.) * 255.;
    const double normalized = std::clamp(
        (input - p.input_black) / (p.input_white - p.input_black), 0., 1.);
    const double output = p.output_black +
        (p.output_white - p.output_black) * std::pow(normalized, 1. / p.gamma);
    return static_cast<unsigned char>(std::lround(std::clamp(output, 0., 255.)));
}

[[nodiscard]] inline std::array<unsigned char, 3> levelsRgb(
    const LevelsParameters& p, const std::array<double, 3>& rgb) noexcept {
    std::array<unsigned char, 3> result{};
    for (int i = 0; i < 3; ++i) {
        const auto master = levelsByte(levelsChannel(p, 0), rgb[i]);
        result[i] = levelsByte(levelsChannel(p, i + 1), master / 255.);
    }
    return result;
}

} // namespace creative_suite::effects
