#pragma once
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace creative_suite::composition {
// Straight RGB in the current working space; no additional color conversion.
enum class TrackMatteMode : std::uint8_t { Alpha, AlphaInverted, Luma, LumaInverted };
[[nodiscard]] constexpr bool validTrackMatteMode(TrackMatteMode mode) noexcept {
    return mode==TrackMatteMode::Alpha || mode==TrackMatteMode::AlphaInverted ||
        mode==TrackMatteMode::Luma || mode==TrackMatteMode::LumaInverted;
}
[[nodiscard]] constexpr std::string_view trackMatteModeIdentifier(TrackMatteMode mode) noexcept {
    switch(mode) {
    case TrackMatteMode::Alpha:return "alpha";
    case TrackMatteMode::AlphaInverted:return "alpha_inverted";
    case TrackMatteMode::Luma:return "luma";
    case TrackMatteMode::LumaInverted:return "luma_inverted";
    }
    return {};
}
[[nodiscard]] constexpr std::optional<TrackMatteMode> parseTrackMatteMode(std::string_view text) noexcept {
    for(auto mode:{TrackMatteMode::Alpha,TrackMatteMode::AlphaInverted,TrackMatteMode::Luma,TrackMatteMode::LumaInverted})
        if(text==trackMatteModeIdentifier(mode))return mode;
    return {};
}
[[nodiscard]] constexpr double trackMatteCoverage(TrackMatteMode mode,double r,double g,double b,double a) noexcept {
    const double coverage=(mode==TrackMatteMode::Luma || mode==TrackMatteMode::LumaInverted)
        ? a*(0.2126*r+0.7152*g+0.0722*b):a;
    return mode==TrackMatteMode::AlphaInverted || mode==TrackMatteMode::LumaInverted?1-coverage:coverage;
}
}
