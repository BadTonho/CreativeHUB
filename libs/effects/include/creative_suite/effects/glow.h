#pragma once

#include <creative_suite/effects/gaussian_blur.h>

#include <array>
#include <cstdint>
#include <optional>

namespace creative_suite::effects {

enum class GlowMode : std::uint8_t { Outer, Inner, Both };

struct GlowParameters {
    std::array<std::uint8_t, 3> color{255, 255, 255};
    double opacity_percent = 50.0;
    double radius_pixels = 10.0;
    GlowMode mode = GlowMode::Outer;

    friend bool operator==(const GlowParameters&, const GlowParameters&) = default;
};

struct GlowBounds {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
};

[[nodiscard]] bool validGlow(const GlowParameters&) noexcept;
// Outer and Both include a three-pass Gaussian halo and one interpolation
// pixel. Inner keeps the original dimensions. Each returned image is bounded
// to 64 MiB and 16384 pixels per dimension.
[[nodiscard]] GlowBounds glowBounds(int width, int height, const GlowParameters&);
// Straight RGBA8 input/output. Outer glow is placed behind the source; inner
// glow tints the source within the alpha edge band without changing its alpha.
// Both applies
// both using the same color and evaluated scalar parameters. Cancellation
// returns nullopt; invalid dimensions or exhausted bounds throw before output
// allocation. The synchronous range executor is never retained.
[[nodiscard]] std::optional<media::RgbaFrame> applyGlow(
    const media::RgbaFrame& source,
    const GlowParameters& parameters,
    const CancellationPredicate& cancel = {},
    const RangeExecutor& executor = {});

} // namespace creative_suite::effects
