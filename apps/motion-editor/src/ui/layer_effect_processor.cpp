#include "layer_effect_processor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace motion::ui {
namespace {

void validateFrame(const creative_suite::media::RgbaFrame& frame)
{
    if (frame.width <= 0 || frame.height <= 0 ||
        frame.width > std::numeric_limits<int>::max() / 4 ||
        frame.stride < frame.width * 4 || frame.stride <= 0) {
        throw std::invalid_argument("The effect input is not a valid RGBA frame.");
    }
    const auto required = static_cast<std::uint64_t>(frame.stride) *
        static_cast<std::uint64_t>(frame.height);
    if (required > frame.rgba_pixels.size()) {
        throw std::invalid_argument("The effect input frame storage is incomplete.");
    }
}

std::uint8_t byteFromUnit(double value) noexcept
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
}

bool boxBlurPass(const creative_suite::media::RgbaFrame& source,
                 creative_suite::media::RgbaFrame& destination,
                 int radius,
                 bool horizontal,
                 const std::function<bool()>& should_cancel)
{
    const int outer_count = horizontal ? source.height : source.width;
    const int inner_count = horizontal ? source.width : source.height;
    const int source_stride = source.stride;
    const int destination_stride = destination.stride;
    const int window_size = radius * 2 + 1;
    for (int outer = 0; outer < outer_count; ++outer) {
        if (should_cancel && should_cancel()) return false;
        std::array<double, 4> sums{};
        for (int offset = -radius; offset <= radius; ++offset) {
            const int sample = std::clamp(offset, 0, inner_count - 1);
            const auto* pixel = source.rgba_pixels.data() +
                (horizontal
                    ? static_cast<std::size_t>(outer) * static_cast<std::size_t>(source_stride) +
                        static_cast<std::size_t>(sample) * 4U
                    : static_cast<std::size_t>(sample) * static_cast<std::size_t>(source_stride) +
                        static_cast<std::size_t>(outer) * 4U);
            for (std::size_t channel = 0; channel < 4; ++channel)
                sums[channel] += pixel[channel];
        }
        for (int inner = 0; inner < inner_count; ++inner) {
            auto* output = destination.rgba_pixels.data() +
                (horizontal
                    ? static_cast<std::size_t>(outer) * static_cast<std::size_t>(destination_stride) +
                        static_cast<std::size_t>(inner) * 4U
                    : static_cast<std::size_t>(inner) * static_cast<std::size_t>(destination_stride) +
                        static_cast<std::size_t>(outer) * 4U);
            for (std::size_t channel = 0; channel < 4; ++channel) {
                output[channel] = static_cast<std::uint8_t>(std::lround(
                    sums[channel] / static_cast<double>(window_size)));
            }
            if (inner + 1 < inner_count) {
                const int leaving = std::clamp(inner - radius, 0, inner_count - 1);
                const int entering = std::clamp(inner + radius + 1, 0, inner_count - 1);
                const auto* leaving_pixel = source.rgba_pixels.data() +
                    (horizontal
                        ? static_cast<std::size_t>(outer) * static_cast<std::size_t>(source_stride) +
                            static_cast<std::size_t>(leaving) * 4U
                        : static_cast<std::size_t>(leaving) * static_cast<std::size_t>(source_stride) +
                            static_cast<std::size_t>(outer) * 4U);
                const auto* entering_pixel = source.rgba_pixels.data() +
                    (horizontal
                        ? static_cast<std::size_t>(outer) * static_cast<std::size_t>(source_stride) +
                            static_cast<std::size_t>(entering) * 4U
                        : static_cast<std::size_t>(entering) * static_cast<std::size_t>(source_stride) +
                            static_cast<std::size_t>(outer) * 4U);
                for (std::size_t channel = 0; channel < 4; ++channel)
                    sums[channel] += static_cast<double>(entering_pixel[channel]) -
                        static_cast<double>(leaving_pixel[channel]);
            }
        }
    }
    return true;
}

bool applyGaussianBlur(creative_suite::media::RgbaFrame& frame,
                       double sigma,
                       const std::function<bool()>& should_cancel)
{
    const int radius = static_cast<int>(std::lround(sigma));
    if (radius <= 0) return true;

    // Three horizontal/vertical box passes approximate a Gaussian while
    // keeping work linear in frame size, even at the maximum blur radius.
    creative_suite::media::RgbaFrame scratch;
    scratch.width = frame.width;
    scratch.height = frame.height;
    scratch.stride = frame.stride;
    scratch.rgba_pixels.resize(frame.rgba_pixels.size());
    for (int y = 0; y < frame.height; ++y) {
        if (should_cancel && should_cancel()) return false;
        auto* row = frame.rgba_pixels.data() +
            static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride);
        for (int x = 0; x < frame.width; ++x) {
            auto* pixel = row + static_cast<std::size_t>(x) * 4U;
            const auto alpha = static_cast<unsigned>(pixel[3]);
            for (std::size_t channel = 0; channel < 3; ++channel) {
                pixel[channel] = static_cast<std::uint8_t>(
                    (static_cast<unsigned>(pixel[channel]) * alpha + 127U) / 255U);
            }
        }
    }
    for (int pass = 0; pass < 3; ++pass) {
        if (!boxBlurPass(frame, scratch, radius, true, should_cancel) ||
            !boxBlurPass(scratch, frame, radius, false, should_cancel)) return false;
    }
    for (int y = 0; y < frame.height; ++y) {
        if (should_cancel && should_cancel()) return false;
        auto* row = frame.rgba_pixels.data() +
            static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride);
        for (int x = 0; x < frame.width; ++x) {
            auto* pixel = row + static_cast<std::size_t>(x) * 4U;
            const auto alpha = static_cast<unsigned>(pixel[3]);
            if (alpha == 0) {
                pixel[0] = pixel[1] = pixel[2] = 0;
            } else {
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    pixel[channel] = static_cast<std::uint8_t>(std::min(
                        255U, (static_cast<unsigned>(pixel[channel]) * 255U + alpha / 2U) /
                            alpha));
                }
            }
        }
    }
    return true;
}

void applyColorAdjustment(creative_suite::media::RgbaFrame& frame,
                          const model::ColorAdjustmentEffect& effect,
                          const std::function<bool()>& should_cancel)
{
    const double brightness = effect.brightness / 100.0;
    const double contrast = effect.contrast_percent / 100.0;
    const double saturation = effect.saturation_percent / 100.0;
    for (int y = 0; y < frame.height; ++y) {
        if (should_cancel && should_cancel()) return;
        auto* row = frame.rgba_pixels.data() +
            static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride);
        for (int x = 0; x < frame.width; ++x) {
            auto* pixel = row + static_cast<std::size_t>(x) * 4U;
            double red = static_cast<double>(pixel[0]) / 255.0 + brightness;
            double green = static_cast<double>(pixel[1]) / 255.0 + brightness;
            double blue = static_cast<double>(pixel[2]) / 255.0 + brightness;
            red = (red - 0.5) * contrast + 0.5;
            green = (green - 0.5) * contrast + 0.5;
            blue = (blue - 0.5) * contrast + 0.5;
            const double luma = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
            red = luma + (red - luma) * saturation;
            green = luma + (green - luma) * saturation;
            blue = luma + (blue - luma) * saturation;
            pixel[0] = byteFromUnit(red);
            pixel[1] = byteFromUnit(green);
            pixel[2] = byteFromUnit(blue);
        }
    }
}

} // namespace

bool hasEnabledLayerEffects(const std::vector<model::LayerEffect>& effects) noexcept
{
    return std::any_of(effects.begin(), effects.end(), [](const auto& effect) {
        return std::visit([](const auto& value) { return value.enabled; }, effect);
    });
}

bool applyLayerEffects(
    creative_suite::media::RgbaFrame& frame,
    const std::vector<model::LayerEffect>& effects,
    const std::function<bool()>& should_cancel)
{
    if (effects.empty()) return true;
    if (!model::validLayerEffects(effects)) {
        throw std::invalid_argument("The layer contains invalid effect parameters.");
    }
    if (!hasEnabledLayerEffects(effects)) return true;
    validateFrame(frame);

    for (const auto& effect : effects) {
        if (should_cancel && should_cancel()) return false;
        const bool completed = std::visit([&](const auto& value) {
            using Effect = std::decay_t<decltype(value)>;
            if (!value.enabled) return true;
            if constexpr (std::is_same_v<Effect, model::GaussianBlurEffect>) {
                return applyGaussianBlur(frame, value.radius_pixels, should_cancel);
            } else {
                applyColorAdjustment(frame, value, should_cancel);
                return !(should_cancel && should_cancel());
            }
        }, effect);
        if (!completed) return false;
    }
    return true;
}

} // namespace motion::ui
