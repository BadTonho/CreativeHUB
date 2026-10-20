#include "path_rasterizer.h"
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPainterPathStroker>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numeric>
#include <stdexcept>
namespace motion::ui {
namespace c = creative_suite::composition;
namespace m = creative_suite::media;
namespace {
constexpr std::size_t maximum_bytes = 64ULL * 1024 * 1024;
QColor color(const model::ColorRgba &v) {
    return {v[0], v[1], v[2], v[3]};
}
struct Edge {
    QPointF a, b;
    QRectF box;
};
// Balanced spatial index bounds nearest-edge and parity queries without a
// per-pixel scan of every segment. Adaptive subdivision has a fixed budget.
class EdgeIndex {
    struct Node {
        QRectF box;
        std::size_t begin, end;
        int left = -1, right = -1;
    };
    std::vector<Edge> edges_;
    std::vector<Node> nodes_;
    bool cancelled_ = false;
    int build(std::size_t begin, std::size_t end) {
        QRectF box = edges_[begin].box;
        for (auto i = begin + 1; i < end; ++i)
            box = box.united(edges_[i].box);
        const auto id = int(nodes_.size());
        nodes_.push_back({box, begin, end});
        if (end - begin > 8) {
            const auto mid = (begin + end) / 2;
            const bool x = box.width() >= box.height();
            std::nth_element(edges_.begin() + begin, edges_.begin() + mid, edges_.begin() + end,
                             [x](const auto &l, const auto &r) {
                                 return x ? l.box.center().x() < r.box.center().x()
                                          : l.box.center().y() < r.box.center().y();
                             });
            const auto l = build(begin, mid), r = build(mid, end);
            nodes_[id].left = l;
            nodes_[id].right = r;
        }
        return id;
    }
    static double boxDistance(QPointF p, QRectF b) {
        const auto x = std::max({b.left() - p.x(), 0., p.x() - b.right()}),
                   y = std::max({b.top() - p.y(), 0., p.y() - b.bottom()});
        return x * x + y * y;
    }

  public:
    EdgeIndex(const model::PathGeometry &path, double sx, double sy, const std::function<bool()> &cancel,
              std::size_t budget) {
        constexpr std::size_t stack_budget = 64 * 1024;
        if (budget <= stack_budget)
            throw std::runtime_error("Bezier mask scratch exceeds the coverage budget");
        budget -= stack_budget;
        struct Curve {
            std::array<QPointF, 4> p;
            int depth = 0;
        };
        const auto point = [=](model::PathPoint p) { return QPointF(p.x * sx, p.y * sy); };
        std::vector<Curve> pending;
        pending.reserve(32);
        for (std::size_t i = 0; i < path.nodes.size(); ++i) {
            const auto &l = path.nodes[i];
            const auto &r = path.nodes[(i + 1) % path.nodes.size()];
            pending.push_back(
                {{point(l.point), point({l.point.x + l.outgoing.x, l.point.y + l.outgoing.y}),
                  point({r.point.x + r.incoming.x, r.point.y + r.incoming.y}), point(r.point)}});
            while (!pending.empty()) {
                if (cancel && cancel()) {
                    cancelled_ = true;
                    return;
                }
                const auto curve = pending.back();
                pending.pop_back();
                const auto &p = curve.p;
                const auto v = p[3] - p[0];
                const auto length = std::hypot(v.x(), v.y());
                const auto distance = [&](QPointF q) {
                    return length ? std::abs(v.x() * (q.y() - p[0].y()) - v.y() * (q.x() - p[0].x())) / length
                                  : std::hypot(q.x() - p[0].x(), q.y() - p[0].y());
                };
                const auto chord = QPointF::dotProduct(v, v);
                const bool monotone = chord > 0 && QPointF::dotProduct(p[1] - p[0], v) >= 0 &&
                                      QPointF::dotProduct(p[1] - p[0], v) <= chord &&
                                      QPointF::dotProduct(p[2] - p[0], v) >= 0 &&
                                      QPointF::dotProduct(p[2] - p[0], v) <= chord;
                const bool flat =
                    std::max(distance(p[1]), distance(p[2])) <= .03125 && (monotone || chord == 0);
                if (!flat && curve.depth >= 24)
                    throw std::runtime_error("Bezier mask subdivision precision exceeds its bounded depth");
                if (flat) {
                    if (p[0] != p[3] && edges_.size() == edges_.capacity()) {
                        const auto next = std::max<std::size_t>(16, edges_.capacity() * 2);
                        if ((next + edges_.capacity()) * sizeof(Edge) > budget)
                            throw std::runtime_error("Bezier mask edge storage exceeds the coverage budget");
                        edges_.reserve(next);
                    }
                    if (p[0] != p[3])
                        edges_.push_back(
                            {p[0], p[3],
                             QRectF(p[0], p[3]).normalized().adjusted(-1e-10, -1e-10, 1e-10, 1e-10)});
                    if (edges_.size() > 65536)
                        throw std::runtime_error("Bezier mask flattening exceeds the segment budget");
                    continue;
                }
                const auto a = (p[0] + p[1]) * .5, b = (p[1] + p[2]) * .5, c = (p[2] + p[3]) * .5,
                           d = (a + b) * .5, e = (b + c) * .5, f = (d + e) * .5;
                pending.push_back({{f, e, c, p[3]}, curve.depth + 1});
                pending.push_back({{p[0], a, d, f}, curve.depth + 1});
            }
        }
        if (!edges_.empty()) {
            if (edges_.capacity() * sizeof(Edge) + edges_.size() * 2 * sizeof(Node) > budget)
                throw std::runtime_error("Bezier mask spatial index exceeds the coverage budget");
            nodes_.reserve(edges_.size() * 2);
            build(0, edges_.size());
        }
    }
    bool cancelled() const noexcept { return cancelled_; }
    double signedDistance(QPointF p) const {
        if (nodes_.empty())
            return 1e30;
        double best = 1e60;
        bool inside = false;
        std::array<int, 64> stack{};
        int count = 1;
        stack[0] = 0;
        while (count) {
            const auto &n = nodes_[stack[--count]];
            const bool ray = p.y() >= n.box.top() && p.y() <= n.box.bottom() && p.x() < n.box.right();
            if (!ray && boxDistance(p, n.box) > best)
                continue;
            if (n.left >= 0) {
                stack[count++] = n.left;
                stack[count++] = n.right;
                continue;
            }
            for (auto i = n.begin; i < n.end; ++i) {
                const auto &e = edges_[i];
                const auto v = e.b - e.a, w = p - e.a;
                const auto denominator = QPointF::dotProduct(v, v);
                const auto t = denominator ? std::clamp(QPointF::dotProduct(w, v) / denominator, 0., 1.) : 0.;
                const auto d = p - (e.a + t * v);
                best = std::min(best, QPointF::dotProduct(d, d));
                if ((e.a.y() > p.y()) != (e.b.y() > p.y()) &&
                    p.x() < (e.b.x() - e.a.x()) * (p.y() - e.a.y()) / (e.b.y() - e.a.y()) + e.a.x())
                    inside = !inside;
            }
        }
        return (inside ? -1 : 1) * std::sqrt(best);
    }
};
} // namespace
QPainterPath painterPath(const model::PathGeometry &p, double sx, double sy) {
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    if (p.nodes.empty())
        return path;
    const auto point = [=](model::PathPoint q) { return QPointF(q.x * sx, q.y * sy); };
    path.moveTo(point(p.nodes.front().point));
    const auto segments = p.closed ? p.nodes.size() : p.nodes.size() - 1;
    for (std::size_t i = 0; i < segments; ++i) {
        const auto &l = p.nodes[i];
        const auto &r = p.nodes[(i + 1) % p.nodes.size()];
        path.cubicTo(point({l.point.x + l.outgoing.x, l.point.y + l.outgoing.y}),
                     point({r.point.x + r.incoming.x, r.point.y + r.incoming.y}), point(r.point));
    }
    if (p.closed)
        path.closeSubpath();
    return path;
}
std::optional<PathRaster> rasterizePath(const model::ShapeLayerContent &content,
                                        const model::PathGeometry &geometry, double margin,
                                        const std::function<bool()> &cancel, model::TrimPathRange trim,
                                        const model::PathLengthTable* measurement) {
    if (content.width <= 0 || content.height <= 0 || content.stroke_width_pixels < 0 ||
        !std::isfinite(trim.start) || !std::isfinite(trim.span) || trim.start<0 || trim.start>=1 || trim.span<0 || trim.span>1 ||
        content.stroke_width_pixels > 4096 || !model::validPathGeometry(geometry) ||
        !model::validPathRasterGeometry(geometry, content.stroke_width_pixels, margin) ||
        !std::isfinite(margin) || margin < 0 || (cancel && cancel()))
        return {};
    const auto path = painterPath(geometry);
    auto bounds = path.boundingRect();
    if (content.stroke_width_pixels > 0) {
        QPainterPathStroker stroke;
        stroke.setWidth(content.stroke_width_pixels);
        stroke.setCapStyle(Qt::RoundCap);
        stroke.setJoinStyle(Qt::RoundJoin);
        bounds = bounds.united(stroke.createStroke(path).boundingRect());
    }
    bounds = bounds.adjusted(-margin - 2, -margin - 2, margin + 2, margin + 2);
    const auto x = std::floor(bounds.left()), y = std::floor(bounds.top());
    const auto w = std::max(1., std::ceil(bounds.right()) - x),
               h = std::max(1., std::ceil(bounds.bottom()) - y);
    if (!std::isfinite(w) || !std::isfinite(h) || w > 16384 || h > 16384 || w * h * 4 > maximum_bytes)
        return {};
    auto frame = std::make_shared<m::RgbaFrame>(m::RgbaFrame{
        int(w), int(h), int(w) * 4, std::vector<std::uint8_t>(std::size_t(w) * std::size_t(h) * 4)});
    QImage image(frame->rgba_pixels.data(), int(w), int(h), frame->stride, QImage::Format_RGBA8888);
    if (image.isNull())
        return {};
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(-x, -y);
    painter.setBrush(geometry.closed ? QBrush(color(content.fill_color)) : Qt::NoBrush);
    painter.setPen(content.stroke_width_pixels > 0
                       ? QPen(color(content.stroke_color), content.stroke_width_pixels, Qt::SolidLine,
                              Qt::RoundCap, Qt::RoundJoin)
                       : QPen(Qt::NoPen));
    if(trim.span==1)painter.drawPath(path);
    else {
        painter.setPen(Qt::NoPen);painter.drawPath(path);
        if(trim.span>0 && content.stroke_width_pixels>0) {
            std::shared_ptr<const model::PathLengthTable> owned;
            if(!measurement || !model::measuredPathMatches(*measurement,geometry)) {
                owned=model::measureTrimPath(geometry,cancel);measurement=owned.get();
            }
            if(!measurement)return {};
            const auto pieces=model::extractTrimPath(*measurement,trim,cancel);if(!pieces)return {};
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(color(content.stroke_color),content.stroke_width_pixels,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            for(const auto& piece:*pieces)painter.drawPath(painterPath(piece));
        }
    }
    painter.end();
    if (cancel && cancel())
        return {};
    return PathRaster{std::move(frame),
                      x,
                      y,
                      {content.width, content.height, x + content.width * .5, y + content.height * .5}};
}
m::RgbaFramePtr MotionMaskRasterCache::coverage(const model::Mask &mask, std::int64_t frame, int width,
                                                int height, const std::function<bool()> &cancel,
                                                c::MaskRasterSpace space,
                                                const model::PathGeometry *override_path) {
    if (cancel && cancel())
        return {};
    if (!model::validMotionMask(mask) || space.logical_width < 0 || space.logical_height < 0 ||
        !std::isfinite(space.origin_x) || !std::isfinite(space.origin_y) || width <= 0 || height <= 0 ||
        width > 16384 || height > 16384 || std::size_t(width) * height * 4 > maximum_bytes)
        return {};
    auto parameters = c::evaluateShapeMask(mask.parameters, mask.keyframes, frame);
    std::optional<model::PathGeometry> geometry;
    if (mask.path)
        geometry = override_path ? *override_path : model::evaluatePath(*mask.path, frame);
    for (const auto &e : entries_)
        if (e.kind == mask.kind && e.parameters == parameters && e.path == geometry &&
            e.width == width && e.height == height && e.space == space)
            return e.frame;
    m::RgbaFramePtr result;
    if (!geometry) {
        c::ShapeMaskRasterCache cache;
        result = cache.coverage(mask.kind, parameters, width, height, cancel, space);
    } else {
        if (!model::validPathGeometry(*geometry, true))
            return {};
        const auto lw = space.logical_width ? space.logical_width : width,
                   lh = space.logical_height ? space.logical_height : height;
        const auto path = painterPath(*geometry, lw, lh);
        const auto soft = parameters.soft_edge * std::min(lw, lh);
        auto output = std::make_shared<m::RgbaFrame>(m::RgbaFrame{
            width, height, width * 4, std::vector<std::uint8_t>(std::size_t(width) * height * 4)});
        if (soft == 0) {
            QImage image(output->rgba_pixels.data(), width, height, output->stride, QImage::Format_RGBA8888);
            if (image.isNull())
                return {};
            image.fill(Qt::black);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.translate(-space.origin_x, -space.origin_y);
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::white);
            painter.drawPath(path);
            painter.end();
            for (int y = 0; y < height; ++y) {
                if (cancel && cancel())
                    return {};
                for (int x = 0; x < width; ++x) {
                    const auto v = image.constScanLine(y)[x * 4];
                    auto *pixel = output->rgba_pixels.data() + std::size_t(y) * output->stride + x * 4;
                    pixel[0] = pixel[1] = pixel[2] = parameters.invert ? 255 - v : v;
                    pixel[3] = 255;
                }
            }
        } else {
            EdgeIndex index(*geometry, lw, lh, cancel, maximum_bytes - output->rgba_pixels.size());
            if (index.cancelled())
                return {};
            for (int y = 0; y < height; ++y) {
                if (cancel && cancel())
                    return {};
                for (int x = 0; x < width; ++x) {
                    double value = 0;
                    for (double oy : {.25, .75})
                        for (double ox : {.25, .75}) {
                            const auto distance =
                                index.signedDistance({x + ox + space.origin_x, y + oy + space.origin_y});
                            const auto t = std::clamp(.5 - distance / soft, 0., 1.);
                            value += t * t * (3 - 2 * t) * .25;
                        }
                    if (parameters.invert)
                        value = 1 - value;
                    const auto v = std::uint8_t(std::lround(value * 255));
                    auto *pixel = output->rgba_pixels.data() + std::size_t(y) * output->stride + x * 4;
                    pixel[0] = pixel[1] = pixel[2] = v;
                    pixel[3] = 255;
                }
            }
        }
        result = std::move(output);
    }
    if (!result)
        return {};
    const auto size = result->rgba_pixels.size();
    while (!entries_.empty() && (entries_.size() >= 4 || bytes_ + size > maximum_bytes)) {
        bytes_ -= entries_.front().frame->rgba_pixels.size();
        entries_.erase(entries_.begin());
    }
    bytes_ += size;
    entries_.push_back({mask.id, mask.kind, parameters, std::move(geometry), width, height, space, result});
    return result;
}
} // namespace motion::ui
