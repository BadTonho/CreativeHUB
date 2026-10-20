#pragma once
#include <creative_suite/effects/gaussian_blur.h>
#include <array>
#include <cstdint>

namespace creative_suite::effects {
struct DropShadowParameters {
    std::array<std::uint8_t, 3> color{0, 0, 0};
    double opacity_percent = 50, angle_degrees = 45, distance_pixels = 10, softness_pixels = 10;
    friend bool operator==(const DropShadowParameters &, const DropShadowParameters &) = default;
};
struct DropShadowBounds {
    int left = 0, top = 0, width = 0, height = 0;
    double dx = 0, dy = 0;
};
[[nodiscard]] bool validDropShadow(const DropShadowParameters &) noexcept;
// Expanded output: source pixel (0,0) is at (left,top). Coordinates are source
// pixels before any fit/transform. Each image is bounded to 64 MiB / 16384px.
// Invalid parameters or exhausted dimensions throw before allocation.
[[nodiscard]] DropShadowBounds dropShadowBounds(int width, int height, const DropShadowParameters &);
// Straight RGBA input/output; original source-over colored blurred alpha.
// Returns null on cancellation. Does not retain input, executor or callbacks.
[[nodiscard]] std::optional<media::RgbaFrame> applyDropShadow(const media::RgbaFrame &,
                                                              const DropShadowParameters &,
                                                              const CancellationPredicate & = {},
                                                              const RangeExecutor & = {});
} // namespace creative_suite::effects
