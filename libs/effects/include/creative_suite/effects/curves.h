#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace creative_suite::effects {

struct CurvesPoint {
    int input = 0;
    int output = 0;
    bool operator==(const CurvesPoint&) const = default;
};

// Global encoded-sRGB curve. Points are owned by the value; no Qt dependency.
struct CurvesParameters {
    std::vector<CurvesPoint> points{{0, 0}, {255, 255}};
    bool operator==(const CurvesParameters&) const = default;
};

[[nodiscard]] inline bool validCurves(const CurvesParameters& p) noexcept {
    if (p.points.size() < 2 || p.points.size() > 16 ||
        p.points.front().input != 0 || p.points.back().input != 255) return false;
    int previous = -1;
    for (const auto& point : p.points) {
        if (point.input <= previous || point.input > 255 ||
            point.output < 0 || point.output > 255) return false;
        previous = point.input;
    }
    return true;
}

// PCHIP tangents preserve each segment's output bounds, including reversals.
// Call only with validated points. The 256-byte table is returned by value.
[[nodiscard]] inline std::array<std::uint8_t, 256> curvesLut(
    const CurvesParameters& p) noexcept {
    std::array<std::uint8_t, 256> table{};
    const auto n = p.points.size();
    std::array<double, 15> h{}, slope{};
    std::array<double, 16> tangent{};
    for (std::size_t i = 0; i + 1 < n; ++i) {
        h[i] = p.points[i + 1].input - p.points[i].input;
        slope[i] = (p.points[i + 1].output - p.points[i].output) / h[i];
    }
    if (n == 2) {
        tangent[0] = tangent[1] = slope[0];
    } else {
        const auto endpoint = [](double h0, double h1, double d0, double d1) {
            double value = ((2 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
            if (value * d0 <= 0) return 0.0;
            if (d0 * d1 < 0 && std::abs(value) > 3 * std::abs(d0)) value = 3 * d0;
            return value;
        };
        tangent[0] = endpoint(h[0], h[1], slope[0], slope[1]);
        tangent[n - 1] = endpoint(h[n - 2], h[n - 3], slope[n - 2], slope[n - 3]);
        for (std::size_t i = 1; i + 1 < n; ++i) {
            if (slope[i - 1] * slope[i] <= 0) continue;
            const double w1 = 2 * h[i] + h[i - 1];
            const double w2 = h[i] + 2 * h[i - 1];
            tangent[i] = (w1 + w2) / (w1 / slope[i - 1] + w2 / slope[i]);
        }
    }
    for (std::size_t segment = 0; segment + 1 < n; ++segment) {
        const auto& left = p.points[segment];
        const auto& right = p.points[segment + 1];
        for (int x = left.input; x <= right.input; ++x) {
            const double t = (x - left.input) / h[segment];
            const double t2 = t * t, t3 = t2 * t;
            const double value = (2 * t3 - 3 * t2 + 1) * left.output +
                (t3 - 2 * t2 + t) * h[segment] * tangent[segment] +
                (-2 * t3 + 3 * t2) * right.output +
                (t3 - t2) * h[segment] * tangent[segment + 1];
            table[x] = static_cast<std::uint8_t>(std::lround(std::clamp(
                value, double(std::min(left.output, right.output)),
                double(std::max(left.output, right.output)))));
        }
    }
    return table;
}

[[nodiscard]] inline bool identityCurves(const CurvesParameters& p) noexcept {
    if (!validCurves(p)) return false;
    if (p.points == CurvesParameters{}.points) return true;
    const auto table = curvesLut(p);
    for (int i = 0; i < 256; ++i) if (table[i] != i) return false;
    return true;
}

[[nodiscard]] inline std::array<unsigned char, 3> curvesRgb(
    const CurvesParameters& p, const std::array<double, 3>& rgb) noexcept {
    const auto table = curvesLut(p);
    return {table[std::lround(std::clamp(rgb[0], 0., 1.) * 255)],
        table[std::lround(std::clamp(rgb[1], 0., 1.) * 255)],
        table[std::lround(std::clamp(rgb[2], 0., 1.) * 255)]};
}

} // namespace creative_suite::effects
