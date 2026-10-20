#include "trim_paths.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace motion::model {
namespace {
using Cubic = std::array<PathPoint, 4>;
struct Cancelled {};
struct Work {
    const std::function<bool()> &cancel;
    std::size_t steps = 0;
    void tick() {
        if (cancel && cancel())
            throw Cancelled{};
        if (++steps > 2'000'000)
            throw std::runtime_error("Trim Paths subdivision budget exceeded");
    }
};
PathPoint mix(PathPoint p, PathPoint q, double t) {
    return {std::lerp(p.x, q.x, t), std::lerp(p.y, q.y, t)};
}
PathPoint sub(PathPoint p, PathPoint q) {
    return {p.x - q.x, p.y - q.y};
}
double distance(PathPoint p, PathPoint q) {
    return std::hypot(p.x - q.x, p.y - q.y);
}
std::pair<Cubic, Cubic> split(const Cubic &p, double t) {
    const auto a = mix(p[0], p[1], t), b = mix(p[1], p[2], t), c = mix(p[2], p[3], t);
    const auto d = mix(a, b, t), e = mix(b, c, t), f = mix(d, e, t);
    return {{p[0], a, d, f}, {f, e, c, p[3]}};
}
Cubic slice(Cubic p, double from, double to) {
    if (to < 1)
        p = split(p, to).first;
    if (from > 0)
        p = split(p, from / to).second;
    return p;
}
std::pair<double, double> bounds(const Cubic &p) {
    const auto chord = distance(p[0], p[3]);
    return {chord, distance(p[0], p[1]) + distance(p[1], p[2]) + distance(p[2], p[3])};
}
struct Pending {
    Cubic p;
    double t0 = 0, t1 = 1;
    int depth = 0;
};
template <class Emit> void subdivide(Cubic p, double tolerance, Work &work, Emit emit) {
    std::vector<Pending> stack;
    stack.reserve(40);
    stack.push_back({p});
    while (!stack.empty()) {
        work.tick();
        auto v = stack.back();
        stack.pop_back();
        const auto [chord, polygon] = bounds(v.p);
        if (polygon - chord <= tolerance * (v.t1 - v.t0)) {
            emit(v, (chord + polygon) * .5);
            continue;
        }
        if (v.depth >= 32)
            throw std::runtime_error("Trim Paths length precision did not converge");
        const auto [l, r] = split(v.p, .5);
        const auto mid = (v.t0 + v.t1) * .5;
        stack.push_back({r, mid, v.t1, v.depth + 1});
        stack.push_back({l, v.t0, mid, v.depth + 1});
    }
}
double length(Cubic p, Work &work) {
    double sum = 0;
    subdivide(p, .0001, work, [&](const auto &, double n) { sum += n; });
    return sum;
}
} // namespace
class PathLengthTable {
  public:
    struct Leaf {
        std::size_t segment;
        double t0, t1, length, end;
    };
    PathGeometry geometry;
    std::vector<Cubic> segments;
    std::vector<Leaf> leaves;
    std::vector<std::size_t> first_leaf;
    std::vector<double> ends;
    double total = 0;
};
std::shared_ptr<const PathLengthTable> measureTrimPath(const PathGeometry &path,
                                                       const std::function<bool()> &cancel) {
    if (!validPathGeometry(path))
        throw std::invalid_argument("Invalid Trim Paths geometry");
    try {
        Work work{cancel};
        work.tick();
        auto table = std::make_shared<PathLengthTable>();
        table->geometry = path;
        const auto count = path.closed ? path.nodes.size() : path.nodes.size() - 1;
        table->segments.reserve(count);
        table->ends.reserve(count);
        table->first_leaf.reserve(count + 1);
        table->leaves.reserve(64);
        for (std::size_t i = 0; i < count; ++i) {
            const auto &a = path.nodes[i];
            const auto &b = path.nodes[(i + 1) % path.nodes.size()];
            Cubic cubic{a.point,
                        {a.point.x + a.outgoing.x, a.point.y + a.outgoing.y},
                        {b.point.x + b.incoming.x, b.point.y + b.incoming.y},
                        b.point};
            table->segments.push_back(cubic);
            table->first_leaf.push_back(table->leaves.size());
            subdivide(cubic, .005 / count, work, [&](const auto &v, double n) {
                if (table->leaves.size() >= 131072)
                    throw std::runtime_error("Trim Paths length index exceeds its 16 MiB budget");
                table->total += n;
                table->leaves.push_back({i, v.t0, v.t1, n, table->total});
            });
            table->ends.push_back(table->total);
        }
        table->first_leaf.push_back(table->leaves.size());
        return table;
    } catch (const Cancelled &) {
        return {};
    }
}
bool measuredPathMatches(const PathLengthTable &table, const PathGeometry &geometry) noexcept {
    return table.geometry == geometry;
}
double measuredPathLength(const PathLengthTable &table) noexcept {
    return table.total;
}
namespace {
double parameter(const PathLengthTable &table, std::size_t segment, double local_distance, Work &work) {
    const auto start = segment ? table.ends[segment - 1] : 0.;
    const auto size = table.ends[segment] - start;
    if (local_distance <= 0)
        return 0;
    if (local_distance >= size)
        return 1;
    const auto wanted = start + local_distance;
    const auto first = table.leaves.begin() + table.first_leaf[segment],
               last = table.leaves.begin() + table.first_leaf[segment + 1];
    const auto leaf =
        std::lower_bound(first, last, wanted, [](const auto &l, double d) { return l.end < d; });
    if (leaf == last)
        return 1;
    const auto target = wanted - (leaf->end - leaf->length);
    if (target <= 0)
        return leaf->t0;
    if (target >= leaf->length)
        return leaf->t1;
    const auto cubic = slice(table.segments[segment], leaf->t0, leaf->t1);
    double lo = 0, hi = 1;
    for (int i = 0; i < 64; ++i) {
        work.tick();
        const auto mid = (lo + hi) * .5;
        const auto n = length(split(cubic, mid).first, work);
        if (std::abs(n - target) <= .0005)
            return std::lerp(leaf->t0, leaf->t1, mid);
        if (n < target)
            lo = mid;
        else
            hi = mid;
    }
    throw std::runtime_error("Trim Paths cut precision did not converge");
}
PathGeometry extract(const PathLengthTable &table, double begin, double end, Work &work) {
    PathGeometry output;
    for (std::size_t i = 0; i < table.segments.size(); ++i) {
        work.tick();
        const auto start = i ? table.ends[i - 1] : 0.;
        const auto stop = table.ends[i];
        if (stop <= begin || start >= end || stop <= start)
            continue;
        const auto from = parameter(table, i, std::max(0., begin - start), work);
        const auto to = parameter(table, i, std::min(stop - start, end - start), work);
        const auto p = slice(table.segments[i], from, to);
        if (output.nodes.empty())
            output.nodes.push_back({p[0], {}, {}});
        output.nodes.back().outgoing = sub(p[1], p[0]);
        output.nodes.push_back({p[3], sub(p[2], p[3]), {}});
    }
    return output;
}
} // namespace
std::optional<std::vector<PathGeometry>> extractTrimPath(const PathLengthTable &table, TrimPathRange range,
                                                         const std::function<bool()> &cancel) {
    if (!std::isfinite(range.start) || !std::isfinite(range.span) || range.start < 0 || range.start >= 1 ||
        range.span < 0 || range.span > 1)
        throw std::invalid_argument("Invalid Trim Paths range");
    try {
        Work work{cancel};
        work.tick();
        std::vector<PathGeometry> output;
        if (range.span == 1) {
            output.push_back(table.geometry);
            return output;
        }
        if (range.span == 0 || table.total == 0)
            return output;
        const auto end = range.start + range.span;
        auto first = extract(table, range.start * table.total, std::min(1., end) * table.total, work);
        if (!first.nodes.empty())
            output.push_back(std::move(first));
        if (end > 1) {
            auto second = extract(table, 0, (end - 1) * table.total, work);
            if (!second.nodes.empty()) {
                if (table.geometry.closed && !output.empty()) {
                    auto &joined = output.back();
                    joined.nodes.back().outgoing = second.nodes.front().outgoing;
                    joined.nodes.insert(joined.nodes.end(), second.nodes.begin() + 1, second.nodes.end());
                } else
                    output.push_back(std::move(second));
            }
        }
        return output;
    } catch (const Cancelled &) {
        return std::nullopt;
    }
}
} // namespace motion::model
