#include <creative_suite/effects/gaussian_blur.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
namespace creative_suite::effects {
namespace {
struct RangeRunner {
    const RangeExecutor& executor;
    bool parallelFor(std::size_t count, const CancellationPredicate& cancel, const RangeOperation& operation) const {
        if (cancel && cancel()) return false;
        if (executor) return executor(count, cancel, operation);
        operation(0, count, cancel);
        return !cancel || !cancel();
    }
};
bool horizontalBoxBlurPass(const creative_suite::media::RgbaFrame& source,
                           creative_suite::media::RgbaFrame& destination,
                           int radius,
                           const RangeRunner& worker_pool,
                           const std::function<bool()>& should_cancel)
{
    const int window_size = radius * 2 + 1;
    return worker_pool.parallelFor(
        static_cast<std::size_t>(source.height), should_cancel,
        [&](std::size_t begin, std::size_t end,
            const CancellationPredicate& is_cancelled) {
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
                              const RangeRunner& worker_pool,
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
            const CancellationPredicate& is_cancelled) {
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

bool applyGaussianBlurKernel(creative_suite::media::RgbaFrame& frame,
                       double sigma,
                       const RangeRunner& worker_pool,
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
                const CancellationPredicate& is_cancelled) {
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
            const CancellationPredicate& is_cancelled) {
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
ProcessingResult applyGaussianBlur(media::RgbaFrame& frame, double radius_pixels,
    const CancellationPredicate& cancel, const RangeExecutor& executor) {
    if (!std::isfinite(radius_pixels) || radius_pixels < 0 || radius_pixels > 100 ||
        frame.width <= 0 || frame.height <= 0 || frame.width > std::numeric_limits<int>::max()/4 ||
        frame.stride < frame.width*4 || frame.stride <= 0 ||
        std::uint64_t(frame.stride)*frame.height > frame.rgba_pixels.size()) return ProcessingResult::InvalidInput;
    if (cancel && cancel()) return ProcessingResult::Cancelled;
    return applyGaussianBlurKernel(frame, radius_pixels, RangeRunner{executor}, cancel)
        ? ProcessingResult::Completed : ProcessingResult::Cancelled;
}
} // namespace creative_suite::effects
