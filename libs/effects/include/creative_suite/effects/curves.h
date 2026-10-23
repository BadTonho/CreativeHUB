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

using CurvesPointList = std::vector<CurvesPoint>;

enum class CurvesChannel : std::size_t { RGB = 0, Red = 1, Green = 2, Blue = 3 };

[[nodiscard]] inline CurvesPointList identityCurvePoints() {
    return {{0, 0}, {255, 255}};
}

// Encoded-sRGB master and per-channel curves. Points are owned by the value;
// the shared evaluator has no Qt dependency. `points` remains the RGB master.
struct CurvesParameters {
    CurvesPointList points = identityCurvePoints();
    std::array<CurvesPointList, 3> channels{
        identityCurvePoints(), identityCurvePoints(), identityCurvePoints()};
    bool operator==(const CurvesParameters&) const = default;
};

[[nodiscard]] inline const CurvesPointList& curvesPoints(
    const CurvesParameters& p, CurvesChannel channel) noexcept {
    if (channel == CurvesChannel::RGB) return p.points;
    return p.channels[static_cast<std::size_t>(channel) - 1];
}

[[nodiscard]] inline bool validCurvePoints(const CurvesPointList& points) noexcept {
    if (points.size() < 2 || points.size() > 16 ||
        points.front().input != 0 || points.back().input != 255) return false;
    int previous = -1;
    for (const auto& point : points) {
        if (point.input <= previous || point.input > 255 ||
            point.output < 0 || point.output > 255) return false;
        previous = point.input;
    }
    return true;
}

[[nodiscard]] inline bool validCurves(const CurvesParameters& p) noexcept {
    return validCurvePoints(p.points) &&
        std::all_of(p.channels.begin(), p.channels.end(), validCurvePoints);
}

// PCHIP tangents preserve each segment's output bounds, including reversals.
// Call only with validated points. The 256-byte table is returned by value.
[[nodiscard]] inline std::array<std::uint8_t, 256> curvesLut(
    const CurvesPointList& points) noexcept {
    std::array<std::uint8_t, 256> table{};
    const auto n = points.size();
    std::array<double, 15> h{}, slope{};
    std::array<double, 16> tangent{};
    for (std::size_t i = 0; i + 1 < n; ++i) {
        h[i] = points[i + 1].input - points[i].input;
        slope[i] = (points[i + 1].output - points[i].output) / h[i];
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
        const auto& left = points[segment];
        const auto& right = points[segment + 1];
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

[[nodiscard]] inline std::array<std::uint8_t, 256> curvesLut(
    const CurvesParameters& p) noexcept {
    return curvesLut(p.points);
}

using CurvesLookupTables = std::array<std::array<std::uint8_t, 256>, 3>;

// Compose the master curve followed by each output channel's own curve.
[[nodiscard]] inline CurvesLookupTables curvesLuts(const CurvesParameters& p) noexcept {
    const auto master = curvesLut(p.points);
    CurvesLookupTables tables{};
    for (std::size_t channel = 0; channel < tables.size(); ++channel) {
        const auto individual = curvesLut(p.channels[channel]);
        for (std::size_t input = 0; input < master.size(); ++input)
            tables[channel][input] = individual[master[input]];
    }
    return tables;
}

[[nodiscard]] inline bool identityCurves(const CurvesParameters& p) noexcept {
    if (!validCurves(p)) return false;
    const auto tables = curvesLuts(p);
    for (const auto& table : tables)
        for (int i = 0; i < 256; ++i)
            if (table[i] != i) return false;
    return true;
}

[[nodiscard]] inline std::array<unsigned char, 3> curvesRgb(
    const CurvesParameters& p, const std::array<double, 3>& rgb) noexcept {
    const auto tables = curvesLuts(p);
    return {tables[0][std::lround(std::clamp(rgb[0], 0., 1.) * 255)],
        tables[1][std::lround(std::clamp(rgb[1], 0., 1.) * 255)],
        tables[2][std::lround(std::clamp(rgb[2], 0., 1.) * 255)]};
}

} // namespace creative_suite::effects
