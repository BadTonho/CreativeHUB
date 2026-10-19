#include "bezier_path.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace motion::model {
namespace a = creative_suite::animation;
namespace {
PathPoint add(PathPoint p, PathPoint q) {
    return {p.x + q.x, p.y + q.y};
}
PathPoint sub(PathPoint p, PathPoint q) {
    return {p.x - q.x, p.y - q.y};
}
PathPoint mix(PathPoint p, PathPoint q, double t) {
    return {std::lerp(p.x, q.x, t), std::lerp(p.y, q.y, t)};
}
bool finite(PathPoint p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::abs(p.x) <= 1e9 && std::abs(p.y) <= 1e9;
}
std::array<PathPoint, 4> controls(const PathGeometry &p, std::size_t i) {
    if (i >= p.nodes.size() || (!p.closed && i + 1 >= p.nodes.size()))
        throw std::invalid_argument("Invalid path segment");
    const auto &l = p.nodes[i];
    const auto &r = p.nodes[(i + 1) % p.nodes.size()];
    return {l.point, add(l.point, l.outgoing), add(r.point, r.incoming), r.point};
}
} // namespace
bool validPathGeometry(const PathGeometry &p, bool mask) noexcept {
    if (p.nodes.size() < (p.closed ? 3 : 2) || p.nodes.size() > maximum_path_nodes || (mask && !p.closed))
        return false;
    return std::all_of(p.nodes.begin(), p.nodes.end(), [](const auto &n) {
        return finite(n.point) && finite(n.incoming) && finite(n.outgoing) &&
               finite(add(n.point, n.incoming)) && finite(add(n.point, n.outgoing));
    });
}
bool validPathRasterGeometry(const PathGeometry &path, double stroke, double margin) noexcept {
    if (!validPathGeometry(path) || !std::isfinite(stroke) || stroke < 0 || !std::isfinite(margin) ||
        margin < 0)
        return false;
    double left = 1e30, top = 1e30, right = -1e30, bottom = -1e30;
    const auto include = [&](PathPoint p) {
        left = std::min(left, p.x);
        right = std::max(right, p.x);
        top = std::min(top, p.y);
        bottom = std::max(bottom, p.y);
    };
    for (std::size_t i = 0; i < (path.closed ? path.nodes.size() : path.nodes.size() - 1); ++i) {
        const auto p = controls(path, i);
        include(p[0]);
        include(p[3]);
        for (int axis = 0; axis < 2; ++axis) {
            const auto value = [axis](PathPoint p) { return axis ? p.y : p.x; };
            const auto a = -value(p[0]) + 3 * value(p[1]) - 3 * value(p[2]) + value(p[3]);
            const auto b = 2 * (value(p[0]) - 2 * value(p[1]) + value(p[2]));
            const auto c = value(p[1]) - value(p[0]);
            const auto root = [&](double t) {
                if (t > 0 && t < 1)
                    include(pathSegmentPoint(path, i, t));
            };
            if (std::abs(a) < 1e-20) {
                if (std::abs(b) >= 1e-20)
                    root(-c / b);
            } else {
                const auto d = b * b - 4 * a * c;
                if (d >= 0) {
                    root((-b - std::sqrt(d)) / (2 * a));
                    root((-b + std::sqrt(d)) / (2 * a));
                }
            }
        }
    }
    const auto padding = stroke * .5 + margin + 2;
    const auto width = std::max(1., std::ceil(right + padding) - std::floor(left - padding));
    const auto height = std::max(1., std::ceil(bottom + padding) - std::floor(top - padding));
    return std::isfinite(width) && std::isfinite(height) && width <= 16384 && height <= 16384 &&
           width * height * 4 <= 64ULL * 1024 * 1024;
}
bool samePathTopology(const PathGeometry &p, const PathGeometry &q) noexcept {
    return p.closed == q.closed && p.nodes.size() == q.nodes.size();
}
std::size_t pathKeyframeCost(const PathAnimation &p) noexcept {
    return p.keys.size() * p.base.nodes.size() * 6;
}
bool validPathAnimation(const PathAnimation &p, bool mask) noexcept {
    if (!validPathGeometry(p.base, mask) || pathKeyframeCost(p) > 2'000'000)
        return false;
    std::int64_t previous = -1;
    for (const auto &k : p.keys) {
        if (k.frame <= previous || k.frame >= (std::int64_t(1) << 50) ||
            !samePathTopology(p.base, k.geometry) || !validPathGeometry(k.geometry, mask) ||
            !a::validKeyframeInterpolation(k.interpolation, k.easing))
            return false;
        previous = k.frame;
    }
    return true;
}
struct PathAnimationStorage::Data {
    PathAnimation animation;
    bool valid;
    explicit Data(PathAnimation value) : animation(std::move(value)), valid(validPathAnimation(animation)) {
    }
};
PathAnimationStorage::PathAnimationStorage(PathAnimation value)
    : data_(std::make_shared<const Data>(std::move(value))) {
}
const PathAnimation &PathAnimationStorage::operator*() const noexcept {
    return data_->animation;
}
const PathAnimation *PathAnimationStorage::operator->() const noexcept {
    return &data_->animation;
}
bool PathAnimationStorage::valid(bool mask) const noexcept {
    return data_ && data_->valid && (!mask || data_->animation.base.closed);
}
bool operator==(const PathAnimationStorage &left, const PathAnimationStorage &right) {
    return left.data_ == right.data_ ||
           (left.data_ && right.data_ && left.data_->animation == right.data_->animation);
}
PathGeometry evaluatePath(const PathAnimation &p, std::int64_t frame) {
    if (p.keys.empty())
        return p.base;
    const auto r = std::upper_bound(p.keys.begin(), p.keys.end(), frame,
                                    [](auto f, const auto &k) { return f < k.frame; });
    if (r == p.keys.begin())
        return r->geometry;
    if (r == p.keys.end())
        return p.keys.back().geometry;
    const auto &l = *(r - 1);
    a::ScalarKeyframes progress{{l.frame, 0, l.interpolation, l.easing}, {r->frame, 1}};
    const auto t = a::evaluateScalar(0, progress, frame);
    auto result = l.geometry;
    for (std::size_t i = 0; i < result.nodes.size(); ++i) {
        auto &n = result.nodes[i];
        const auto &q = r->geometry.nodes[i];
        n.point = mix(n.point, q.point, t);
        n.incoming = mix(n.incoming, q.incoming, t);
        n.outgoing = mix(n.outgoing, q.outgoing, t);
    }
    return result;
}
PathPoint pathSegmentPoint(const PathGeometry &p, std::size_t i, double t) {
    const auto c = controls(p, i);
    return mix(mix(mix(c[0], c[1], t), mix(c[1], c[2], t), t), mix(mix(c[1], c[2], t), mix(c[2], c[3], t), t),
               t);
}
bool subdividePath(PathGeometry &p, std::size_t i, double t) {
    if (!validPathGeometry(p) || p.nodes.size() >= maximum_path_nodes || !std::isfinite(t) || t <= 0 ||
        t >= 1 || i >= p.nodes.size() || (!p.closed && i + 1 == p.nodes.size()))
        return false;
    const auto c = controls(p, i);
    const auto u = mix(c[0], c[1], t), v = mix(c[1], c[2], t), w = mix(c[2], c[3], t);
    const auto x = mix(u, v, t), y = mix(v, w, t), z = mix(x, y, t);
    auto candidate = p;
    candidate.nodes[i].outgoing = sub(u, c[0]);
    candidate.nodes[(i + 1) % p.nodes.size()].incoming = sub(w, c[3]);
    candidate.nodes.insert(candidate.nodes.begin() + i + 1, {z, sub(x, z), sub(y, z)});
    if (!validPathGeometry(candidate))
        return false;
    p = std::move(candidate);
    return true;
}
} // namespace motion::model
