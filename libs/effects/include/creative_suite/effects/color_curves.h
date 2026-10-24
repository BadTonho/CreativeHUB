#pragma once

#include "curves.h"

#include <array>
#include <functional>
#include <span>
#include <vector>

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
    double denoise_percent = 0.0;
    double clean_black_percent = 0.0;
    double clean_white_percent = 100.0;
    double in_out_percent = 0.0;
    double blur_radius_pixels = 0.0;

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
[[nodiscard]] bool tonalQualifierRefinementIsNeutral(const TonalQualifier&) noexcept;
// Returns a width*height R8 matte. Refinement order is denoise, clean levels,
// coverage shift, then a separable Gaussian blur. Invalid geometry returns empty.
[[nodiscard]] std::vector<std::uint8_t> refinedTonalQualifierMatte(
    const TonalQualifier&, std::span<const std::uint8_t> rgba,
    int width, int height, int stride,
    const std::function<bool()>& should_cancel = {});
[[nodiscard]] std::array<std::uint8_t, 3> evaluateVideoCurves(
    const VideoCurvesParameters&, const VideoCurvesLuts&,
    const std::array<double, 3>& rgb, double* matte = nullptr) noexcept;
[[nodiscard]] bool identityVideoCurves(const VideoCurvesParameters&) noexcept;

}  // namespace creative_suite::effects
