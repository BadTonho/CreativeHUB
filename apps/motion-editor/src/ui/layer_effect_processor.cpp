#include "layer_effect_processor.h"
#include "layer_effect_worker_pool.h"

#include <creative_suite/effects/effects.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace motion::ui {
namespace {

template<typename Operation>
bool measureEffect(const EffectTimingRecorder& recorder,
                   LayerEffectKind effect,
                   Operation&& operation)
{
    const auto started = std::chrono::steady_clock::now();
    bool completed = false;
    try {
        completed = operation();
    } catch (...) {
        if (recorder) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started).count();
            if (elapsed >= 0) {
                try { recorder(effect, static_cast<std::uint64_t>(elapsed)); } catch (...) {}
            }
        }
        throw;
    }
    if (recorder) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        if (elapsed >= 0) {
            try { recorder(effect, static_cast<std::uint64_t>(elapsed)); } catch (...) {}
        }
    }
    return completed;
}

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

bool horizontalBoxBlurPass(const creative_suite::media::RgbaFrame& source,
                           creative_suite::media::RgbaFrame& destination,
                           int radius,
                           detail::LayerEffectWorkerPool& worker_pool,
                           const std::function<bool()>& should_cancel)
{
    const int window_size = radius * 2 + 1;
    return worker_pool.parallelFor(
        static_cast<std::size_t>(source.height), should_cancel,
        [&](std::size_t begin, std::size_t end,
            const detail::LayerEffectWorkerPool::CancellationPredicate& is_cancelled) {
        for (auto y = begin; y < end; ++y) {
            if (is_cancelled && is_cancelled()) return;
            std::array<std::uint32_t, 4> sums{};
            for (int offset = -radius; offset <= radius; ++offset) {
                const int sample_x = std::clamp(offset, 0, source.width - 1);
                const auto* pixel = source.rgba_pixels.data() +
                    y * static_cast<std::size_t>(source.stride) +
                    static_cast<std::size_t>(sample_x) * 4U;
                for (std::size_t channel = 0; channel < 4; ++channel)
                    sums[channel] += static_cast<std::uint32_t>(pixel[channel]);
            }

            for (int x = 0; x < source.width; ++x) {
                auto* output = destination.rgba_pixels.data() +
                    y * static_cast<std::size_t>(destination.stride) +
                    static_cast<std::size_t>(x) * 4U;
                for (std::size_t channel = 0; channel < 4; ++channel) {
                    output[channel] = static_cast<std::uint8_t>(
                        (sums[channel] + static_cast<std::uint32_t>(window_size / 2)) /
                        static_cast<std::uint32_t>(window_size));
                }
                if (x + 1 < source.width) {
                    const int leaving_x = std::clamp(x - radius, 0, source.width - 1);
                    const int entering_x = std::clamp(x + radius + 1, 0, source.width - 1);
                    const auto* leaving = source.rgba_pixels.data() +
                        y * static_cast<std::size_t>(source.stride) +
                        static_cast<std::size_t>(leaving_x) * 4U;
                    const auto* entering = source.rgba_pixels.data() +
                        y * static_cast<std::size_t>(source.stride) +
                        static_cast<std::size_t>(entering_x) * 4U;
                    for (std::size_t channel = 0; channel < 4; ++channel) {
                        sums[channel] -= static_cast<std::uint32_t>(leaving[channel]);
                        sums[channel] += static_cast<std::uint32_t>(entering[channel]);
                    }
                }
            }
        }
    });
}

bool verticalBoxBlurPassTiled(const creative_suite::media::RgbaFrame& source,
                              creative_suite::media::RgbaFrame& destination,
                              int radius,
                              detail::LayerEffectWorkerPool& worker_pool,
                              const std::function<bool()>& should_cancel)
{
    constexpr int tile_width = 32;
    const int window_size = radius * 2 + 1;
    const auto denominator = static_cast<std::uint32_t>(window_size);
    const auto rounding_bias = static_cast<std::uint32_t>(window_size / 2);
    const auto tile_count = static_cast<std::size_t>(
        (source.width + tile_width - 1) / tile_width);

    return worker_pool.parallelFor(
        tile_count, should_cancel,
        [&](std::size_t tile_begin, std::size_t tile_end,
            const detail::LayerEffectWorkerPool::CancellationPredicate& is_cancelled) {
        for (auto tile_index = tile_begin; tile_index < tile_end; ++tile_index) {
            const int tile_start = static_cast<int>(tile_index) * tile_width;
            const int columns = std::min(tile_width, source.width - tile_start);
            std::array<std::array<std::uint32_t, 4>, tile_width> sums{};
            for (int offset = -radius; offset <= radius; ++offset) {
                const int sample_y = std::clamp(offset, 0, source.height - 1);
                const auto* row = source.rgba_pixels.data() +
                    static_cast<std::size_t>(sample_y) * static_cast<std::size_t>(source.stride) +
                    static_cast<std::size_t>(tile_start) * 4U;
                for (int column = 0; column < columns; ++column) {
                    const auto* pixel = row + static_cast<std::size_t>(column) * 4U;
                    for (std::size_t channel = 0; channel < 4; ++channel)
                        sums[static_cast<std::size_t>(column)][channel] +=
                            static_cast<std::uint32_t>(pixel[channel]);
                }
            }

            for (int y = 0; y < source.height; ++y) {
                if (is_cancelled && is_cancelled()) return;
                auto* output_row = destination.rgba_pixels.data() +
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(destination.stride) +
                    static_cast<std::size_t>(tile_start) * 4U;
                for (int column = 0; column < columns; ++column) {
                    auto* output = output_row + static_cast<std::size_t>(column) * 4U;
                    for (std::size_t channel = 0; channel < 4; ++channel) {
                        output[channel] = static_cast<std::uint8_t>(
                            (sums[static_cast<std::size_t>(column)][channel] + rounding_bias) /
                            denominator);
                    }
                }

                if (y + 1 < source.height) {
                    const int leaving_y = std::clamp(y - radius, 0, source.height - 1);
                    const int entering_y = std::clamp(y + radius + 1, 0, source.height - 1);
                    const auto* leaving_row = source.rgba_pixels.data() +
                        static_cast<std::size_t>(leaving_y) * static_cast<std::size_t>(source.stride) +
                        static_cast<std::size_t>(tile_start) * 4U;
                    const auto* entering_row = source.rgba_pixels.data() +
                        static_cast<std::size_t>(entering_y) * static_cast<std::size_t>(source.stride) +
                        static_cast<std::size_t>(tile_start) * 4U;
                    for (int column = 0; column < columns; ++column) {
                        const auto index = static_cast<std::size_t>(column);
                        const auto* leaving = leaving_row + index * 4U;
                        const auto* entering = entering_row + index * 4U;
                        for (std::size_t channel = 0; channel < 4; ++channel) {
                            sums[index][channel] -= static_cast<std::uint32_t>(leaving[channel]);
                            sums[index][channel] += static_cast<std::uint32_t>(entering[channel]);
                        }
                    }
                }
            }
        }
    });
}

bool applyGaussianBlur(creative_suite::media::RgbaFrame& frame,
                       double sigma,
                       detail::LayerEffectWorkerPool& worker_pool,
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
    if (!worker_pool.parallelFor(
            static_cast<std::size_t>(frame.height), should_cancel,
            [&](std::size_t begin, std::size_t end,
                const detail::LayerEffectWorkerPool::CancellationPredicate& is_cancelled) {
        for (auto y = begin; y < end; ++y) {
            if (is_cancelled && is_cancelled()) return;
            auto* row = frame.rgba_pixels.data() +
                y * static_cast<std::size_t>(frame.stride);
            for (int x = 0; x < frame.width; ++x) {
                auto* pixel = row + static_cast<std::size_t>(x) * 4U;
                const auto alpha = static_cast<unsigned>(pixel[3]);
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    pixel[channel] = static_cast<std::uint8_t>(
                        (static_cast<unsigned>(pixel[channel]) * alpha + 127U) / 255U);
                }
            }
        }
    })) return false;
    for (int pass = 0; pass < 3; ++pass) {
        if (!horizontalBoxBlurPass(frame, scratch, radius, worker_pool, should_cancel) ||
            !verticalBoxBlurPassTiled(scratch, frame, radius, worker_pool, should_cancel))
            return false;
    }
    return worker_pool.parallelFor(
        static_cast<std::size_t>(frame.height), should_cancel,
        [&](std::size_t begin, std::size_t end,
            const detail::LayerEffectWorkerPool::CancellationPredicate& is_cancelled) {
        for (auto y = begin; y < end; ++y) {
            if (is_cancelled && is_cancelled()) return;
            auto* row = frame.rgba_pixels.data() +
                y * static_cast<std::size_t>(frame.stride);
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
    });
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
    const std::function<bool()>& should_cancel,
    const EffectTimingRecorder& record_effect_timing,
    detail::LayerEffectWorkerPool* requested_worker_pool)
{
    if (effects.empty()) return true;
    if (!model::validLayerEffects(effects)) {
        throw std::invalid_argument("The layer contains invalid effect parameters.");
    }
    if (!hasEnabledLayerEffects(effects)) return true;
    validateFrame(frame);
    auto& worker_pool = requested_worker_pool != nullptr
        ? *requested_worker_pool
        : detail::sharedLayerEffectWorkerPool();

    for (const auto& effect : effects) {
        if (should_cancel && should_cancel()) return false;
        const bool completed = std::visit([&](const auto& value) {
            using Effect = std::decay_t<decltype(value)>;
            if (!value.enabled) return true;
            if constexpr (std::is_same_v<Effect, model::GaussianBlurEffect>) {
                return measureEffect(record_effect_timing, LayerEffectKind::GaussianBlur,
                    [&] {
                        return applyGaussianBlur(
                            frame, value.radius_pixels, worker_pool, should_cancel);
                    });
            } else {
                return measureEffect(record_effect_timing, LayerEffectKind::ColorAdjustment,
                    [&] {
                        const auto result = creative_suite::effects::applyColorAdjustment(
                            frame,
                            {value.brightness, value.contrast_percent,
                             value.saturation_percent},
                            should_cancel);
                        if (result == creative_suite::effects::ProcessingResult::InvalidInput) {
                            throw std::invalid_argument(
                                "The shared Color Adjustment processor rejected a valid Motion frame.");
                        }
                        return result == creative_suite::effects::ProcessingResult::Completed;
                    });
            }
        }, effect);
        if (!completed) return false;
    }
    return true;
}

} // namespace motion::ui
