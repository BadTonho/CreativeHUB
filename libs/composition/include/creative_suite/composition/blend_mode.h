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
} // namespace creative_suite::composition
