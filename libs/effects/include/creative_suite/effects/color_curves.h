#pragma once

#include "curves.h"

#include <array>

namespace creative_suite::effects {

struct TonalQualifier {
    bool enabled = false;
    double hue_min = 0.0;
    double hue_max = 360.0;
    double saturation_min = 0.0;
    double saturation_max = 100.0;
    double luminance_min = 0.0;
    double luminance_max = 100.0;
    double hue_softness = 0.0;
    double saturation_softness = 0.0;
    double luminance_softness = 0.0;

    friend bool operator==(const TonalQualifier&, const TonalQualifier&) = default;
};

struct VideoCurvesParameters {
    CurvesParameters rgb;
    // Hue vs Hue, Hue vs Saturation, Hue vs Luminance,
    // Luminance vs Saturation, Saturation vs Saturation.
    std::array<CurvesPointList, 5> targeted{
        CurvesPointList{{0, 128}, {255, 128}},
        CurvesPointList{{0, 128}, {255, 128}},
        CurvesPointList{{0, 128}, {255, 128}},
        CurvesPointList{{0, 128}, {255, 128}},
        CurvesPointList{{0, 128}, {255, 128}}};
    TonalQualifier qualifier;

    friend bool operator==(const VideoCurvesParameters&, const VideoCurvesParameters&) = default;
};

inline constexpr std::size_t kVideoCurveCount = 9;
using VideoCurvesLuts = std::array<std::array<std::uint8_t, 256>, kVideoCurveCount>;

[[nodiscard]] bool validTonalQualifier(const TonalQualifier&) noexcept;
[[nodiscard]] bool validVideoCurves(const VideoCurvesParameters&) noexcept;
[[nodiscard]] VideoCurvesLuts videoCurvesLuts(const VideoCurvesParameters&) noexcept;
[[nodiscard]] double tonalQualifierMatte(
    const TonalQualifier&, const std::array<double, 3>& rgb) noexcept;
[[nodiscard]] std::array<std::uint8_t, 3> evaluateVideoCurves(
    const VideoCurvesParameters&, const VideoCurvesLuts&,
    const std::array<double, 3>& rgb, double* matte = nullptr) noexcept;
[[nodiscard]] bool identityVideoCurves(const VideoCurvesParameters&) noexcept;

}  // namespace creative_suite::effects
