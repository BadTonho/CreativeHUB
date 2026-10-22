#include <creative_suite/effects/glow.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace creative_suite::effects {

bool validGlow(const GlowParameters& parameters) noexcept
{
    return std::isfinite(parameters.opacity_percent) &&
        parameters.opacity_percent >= 0.0 && parameters.opacity_percent <= 100.0 &&
        std::isfinite(parameters.radius_pixels) &&
        parameters.radius_pixels >= 0.0 && parameters.radius_pixels <= 100.0 &&
        (parameters.mode == GlowMode::Outer || parameters.mode == GlowMode::Inner ||
         parameters.mode == GlowMode::Both);
}

GlowBounds glowBounds(int width, int height, const GlowParameters& parameters)
{
    if (!validGlow(parameters) || width <= 0 || height <= 0 || width > 16384 || height > 16384)
        throw std::invalid_argument("Invalid Glow parameters or source dimensions.");

    GlowBounds bounds{0, 0, width, height};
    const bool has_outer = parameters.mode != GlowMode::Inner;
    if (has_outer && parameters.opacity_percent > 0.0 && std::lround(parameters.radius_pixels) > 0) {
        const int halo = 3 * static_cast<int>(std::lround(parameters.radius_pixels)) + 1;
        bounds.left = halo;
        bounds.top = halo;
        bounds.width += 2 * halo;
        bounds.height += 2 * halo;
    }
    if (bounds.width > 16384 || bounds.height > 16384 ||
        std::uint64_t(bounds.width) * bounds.height * 4 > 64ULL * 1024 * 1024)
        throw std::runtime_error("Glow expanded image exceeds the 64 MiB / 16384 pixel limit.");
    return bounds;
}

std::optional<media::RgbaFrame> applyGlow(const media::RgbaFrame& source,
                                         const GlowParameters& parameters,
                                         const CancellationPredicate& cancel,
                                         const RangeExecutor& executor)
{
    const auto bounds = glowBounds(source.width, source.height, parameters);
    if (source.width <= 0 || source.height <= 0 || source.width > std::numeric_limits<int>::max() / 4 ||
        source.stride < source.width * 4 ||
        std::uint64_t(source.stride) * source.height > source.rgba_pixels.size())
        throw std::invalid_argument("Incomplete Glow source storage.");
    if (cancel && cancel())
        return {};
    if (parameters.opacity_percent == 0.0 || std::lround(parameters.radius_pixels) == 0)
        return source;

    const bool inner_only = parameters.mode == GlowMode::Inner;
    const int inner_halo = inner_only ? 3 * static_cast<int>(std::lround(parameters.radius_pixels)) + 1 : 0;
    const int blur_width = bounds.width + 2 * inner_halo;
    const int blur_height = bounds.height + 2 * inner_halo;
    if (blur_width > 16384 || blur_height > 16384 ||
        std::uint64_t(blur_width) * blur_height * 4 > 64ULL * 1024 * 1024)
        throw std::runtime_error("Glow blur image exceeds the 64 MiB / 16384 pixel limit.");
    const int blur_left = bounds.left + inner_halo;
    const int blur_top = bounds.top + inner_halo;
    media::RgbaFrame alpha{blur_width, blur_height, blur_width * 4,
        std::vector<std::uint8_t>(std::size_t(blur_width) * blur_height * 4)};
    for (int y = 0; y < source.height; ++y) {
        if (cancel && cancel())
            return {};
        for (int x = 0; x < source.width; ++x) {
            auto* pixel = alpha.rgba_pixels.data() + std::size_t(y + blur_top) * alpha.stride +
                (x + blur_left) * 4;
            pixel[3] = source.rgba_pixels[std::size_t(y) * source.stride + x * 4 + 3];
        }
    }
    const auto blur_result = applyGaussianBlur(alpha, parameters.radius_pixels, cancel, executor);
    if (blur_result == ProcessingResult::Cancelled)
        return {};
    if (blur_result == ProcessingResult::InvalidInput)
        throw std::invalid_argument("The shared Gaussian Blur processor rejected the Glow alpha frame.");

    media::RgbaFrame output{bounds.width, bounds.height, bounds.width * 4,
        std::vector<std::uint8_t>(std::size_t(bounds.width) * bounds.height * 4)};
    const double strength = parameters.opacity_percent / 100.0;
    const bool outer = parameters.mode != GlowMode::Inner;
    const bool inner = parameters.mode != GlowMode::Outer;
    for (int y = 0; y < bounds.height; ++y) {
        if (cancel && cancel())
            return {};
        for (int x = 0; x < bounds.width; ++x) {
            const int sx = x - bounds.left;
            const int sy = y - bounds.top;
            const auto* source_pixel = sx >= 0 && sy >= 0 && sx < source.width && sy < source.height
                ? source.rgba_pixels.data() + std::size_t(sy) * source.stride + sx * 4
                : nullptr;
            const auto* blurred_pixel = alpha.rgba_pixels.data() +
                std::size_t(y + inner_halo) * alpha.stride + (x + inner_halo) * 4;
            const double source_alpha = source_pixel ? source_pixel[3] / 255.0 : 0.0;
            const double blurred_alpha = blurred_pixel[3] / 255.0;
            const double outer_alpha = outer ? blurred_alpha * strength : 0.0;
            const double inner_mix = inner ? std::max(0.0, 1.0 - blurred_alpha) * strength : 0.0;

            const double base_alpha = source_alpha + outer_alpha * (1.0 - source_alpha);
            // Inner Glow recolors a fraction of covered source pixels and keeps
            // the source alpha unchanged. The outer halo remains underneath.
            const double output_alpha = base_alpha;
            auto* output_pixel = output.rgba_pixels.data() + std::size_t(y) * output.stride + x * 4;
            for (int channel = 0; channel < 3; ++channel) {
                const double glow_channel = parameters.color[channel] / 255.0;
                const double source_channel = source_pixel ? source_pixel[channel] / 255.0 : 0.0;
                const double outer_premultiplied = glow_channel * outer_alpha * (1.0 - source_alpha);
                const double inner_premultiplied = source_alpha *
                    (source_channel * (1.0 - inner_mix) + glow_channel * inner_mix);
                const double premultiplied = inner_premultiplied + outer_premultiplied;
                output_pixel[channel] = output_alpha > 0.0
                    ? static_cast<std::uint8_t>(std::clamp(std::lround(premultiplied / output_alpha * 255.0), 0L, 255L))
                    : 0;
            }
            output_pixel[3] = static_cast<std::uint8_t>(std::clamp(std::lround(output_alpha * 255.0), 0L, 255L));
        }
    }
    return output;
}

} // namespace creative_suite::effects
