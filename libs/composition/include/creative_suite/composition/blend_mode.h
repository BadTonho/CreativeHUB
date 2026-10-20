#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace creative_suite::composition {
enum class BlendMode : std::uint8_t { Normal, Multiply, Screen, Overlay, Add, Difference };
[[nodiscard]] constexpr bool validBlendMode(BlendMode mode) noexcept {
    return static_cast<unsigned>(mode) <= static_cast<unsigned>(BlendMode::Difference);
}
[[nodiscard]] constexpr std::string_view blendModeIdentifier(BlendMode mode) noexcept {
    switch (mode) {
    case BlendMode::Normal: return "normal";
    case BlendMode::Multiply: return "multiply";
    case BlendMode::Screen: return "screen";
    case BlendMode::Overlay: return "overlay";
    case BlendMode::Add: return "add";
    case BlendMode::Difference: return "difference";
    }
    return {};
}
[[nodiscard]] constexpr std::optional<BlendMode> parseBlendMode(std::string_view name) noexcept {
    for (unsigned i=0; i<=static_cast<unsigned>(BlendMode::Difference); ++i) {
        const auto mode=static_cast<BlendMode>(i);
        if (blendModeIdentifier(mode)==name) return mode;
    }
    return {};
}
// Straight normalized channels in the caller's existing working space.
// Callers validate the mode before composition; no color conversion occurs.
[[nodiscard]] inline double blendChannel(BlendMode mode,double backdrop,double source) noexcept {
    switch (mode) {
    case BlendMode::Normal: return source;
    case BlendMode::Multiply: return backdrop*source;
    case BlendMode::Screen: return 1-(1-backdrop)*(1-source);
    case BlendMode::Overlay: return backdrop<=.5 ? 2*backdrop*source : 1-2*(1-backdrop)*(1-source);
    case BlendMode::Add: return std::min(1.,backdrop+source);
    case BlendMode::Difference: return std::abs(backdrop-source);
    }
    return 0;
}
// Premultiplied backdrop channel, straight source channel and effective alphas.
// SourceAtop preserves the backdrop alpha; source-over retains transparency.
// No quantization, color conversion or opaque finalization occurs here.
[[nodiscard]] inline double composeBlendChannel(BlendMode mode, double pb, double ab,
    double cs, double as, bool source_atop = false) noexcept {
    if (mode == BlendMode::Normal && !source_atop) return as*cs+(1-as)*pb;
    const double cb = ab > 0 ? std::clamp(pb/ab, 0., 1.) : 0.;
    return (1-as)*pb + (source_atop ? 0. : as*(1-ab)*cs) + as*ab*blendChannel(mode,cb,cs);
}
[[nodiscard]] inline double composeBlendAlpha(double ab, double as, bool source_atop = false) noexcept {
    return source_atop ? ab : as+ab*(1-as);
}
} // namespace creative_suite::composition
