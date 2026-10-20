#include <creative_suite/effects/drop_shadow.h>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace creative_suite::effects {
bool validDropShadow(const DropShadowParameters &p) noexcept {
    return std::isfinite(p.opacity_percent) && p.opacity_percent >= 0 && p.opacity_percent <= 100 &&
           std::isfinite(p.angle_degrees) && std::abs(p.angle_degrees) <= 1e9 &&
           std::isfinite(p.distance_pixels) && p.distance_pixels >= 0 && p.distance_pixels <= 16384 &&
           std::isfinite(p.softness_pixels) && p.softness_pixels >= 0 && p.softness_pixels <= 100;
}
DropShadowBounds dropShadowBounds(int width, int height, const DropShadowParameters &p) {
    if (!validDropShadow(p) || width <= 0 || height <= 0 || width > 16384 || height > 16384)
        throw std::invalid_argument("Invalid Drop Shadow parameters or source dimensions");
    DropShadowBounds b{0, 0, width, height};
    if (p.opacity_percent > 0) {
        const auto degrees = std::remainder(p.angle_degrees, 360.);
        const auto angle = degrees * std::numbers::pi / 180.;
        b.dx = p.distance_pixels * std::cos(angle);
        b.dy = p.distance_pixels * std::sin(angle);
        // Snap cardinal floating residues, never the saved angle or distance.
        if (std::abs(degrees) == 90)
            b.dx = 0;
        if (degrees == 0 || std::abs(degrees) == 180)
            b.dy = 0;
        const auto halo = 3 * std::lround(p.softness_pixels) + 1;
        b.left = int(std::max(0., std::ceil(halo - b.dx)));
        b.top = int(std::max(0., std::ceil(halo - b.dy)));
        b.width += b.left + int(std::max(0., std::ceil(halo + b.dx)));
        b.height += b.top + int(std::max(0., std::ceil(halo + b.dy)));
    }
    if (b.width > 16384 || b.height > 16384 || std::uint64_t(b.width) * b.height * 4 > 64ULL * 1024 * 1024)
        throw std::runtime_error("Drop Shadow expanded image exceeds the 64 MiB / 16384 pixel limit");
    return b;
}
std::optional<media::RgbaFrame> applyDropShadow(const media::RgbaFrame &source, const DropShadowParameters &p,
                                                const CancellationPredicate &cancel,
                                                const RangeExecutor &executor) {
    const auto b = dropShadowBounds(source.width, source.height, p);
    if (source.stride < source.width * 4 ||
        std::uint64_t(source.stride) * source.height > source.rgba_pixels.size())
        throw std::invalid_argument("Incomplete Drop Shadow source storage");
    if (cancel && cancel())
        return {};
    if (p.opacity_percent == 0)
        return source;
    media::RgbaFrame alpha{b.width, b.height, b.width * 4,
                           std::vector<std::uint8_t>(std::size_t(b.width) * b.height * 4)};
    for (int y = 0; y < source.height; ++y) {
        if (cancel && cancel())
            return {};
        for (int x = 0; x < source.width; ++x) {
            auto *dst = alpha.rgba_pixels.data() + std::size_t(y + b.top) * alpha.stride + (x + b.left) * 4;
            dst[0] = dst[1] = dst[2] = 255;
            dst[3] = source.rgba_pixels[std::size_t(y) * source.stride + x * 4 + 3];
        }
    }
    if (applyGaussianBlur(alpha, p.softness_pixels, cancel, executor) == ProcessingResult::Cancelled)
        return {};
    media::RgbaFrame output{b.width, b.height, b.width * 4,
                            std::vector<std::uint8_t>(std::size_t(b.width) * b.height * 4)};
    const auto sample = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < b.width && y < b.height
                   ? alpha.rgba_pixels[std::size_t(y) * alpha.stride + x * 4 + 3] / 255.
                   : 0.;
    };
    for (int y = 0; y < b.height; ++y) {
        if (cancel && cancel())
            return {};
        for (int x = 0; x < b.width; ++x) {
            const auto px = x - b.dx, py = y - b.dy;
            const int ix = int(std::floor(px)), iy = int(std::floor(py));
            const auto fx = px - ix, fy = py - iy;
            const auto top = (1 - fx) * sample(ix, iy) + fx * sample(ix + 1, iy);
            const auto bottom = (1 - fx) * sample(ix, iy + 1) + fx * sample(ix + 1, iy + 1);
            const auto shadow = ((1 - fy) * top + fy * bottom) * p.opacity_percent / 100.;
            const int sx = x - b.left, sy = y - b.top;
            const auto *src = sx >= 0 && sy >= 0 && sx < source.width && sy < source.height
                                  ? source.rgba_pixels.data() + std::size_t(sy) * source.stride + sx * 4
                                  : nullptr;
            const auto a = src ? src[3] / 255. : 0., ao = a + shadow * (1 - a);
            auto *dst = output.rgba_pixels.data() + std::size_t(y) * output.stride + x * 4;
            for (int c = 0; c < 3; ++c)
                dst[c] = ao > 0
                             ? std::uint8_t(std::clamp(
                                   std::lround(((src ? src[c] * a : 0) + p.color[c] * shadow * (1 - a)) / ao),
                                   0L, 255L))
                             : 0;
            dst[3] = std::uint8_t(std::clamp(std::lround(ao * 255), 0L, 255L));
        }
    }
    return output;
}
} // namespace creative_suite::effects
