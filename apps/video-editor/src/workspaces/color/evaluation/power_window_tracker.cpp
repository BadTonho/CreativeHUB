#include "power_window_tracker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace color {
namespace {

struct Point { double x = 0.0; double y = 0.0; };
struct GrayImage {
    int width = 0;
    int height = 0;
    std::vector<float> pixels;
};
using Pyramid = std::vector<GrayImage>;

bool canceled(const std::function<bool()>& predicate) {
    return predicate && predicate();
}

std::optional<GrayImage> makeGray(const media::VideoFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0 || frame.width > 16384 ||
        frame.height > 16384 || frame.stride < frame.width * 4 ||
        frame.rgba_pixels.size() < static_cast<std::size_t>(frame.stride) * frame.height)
        return std::nullopt;
    const double factor = std::min({1.0, 320.0 / frame.width, 180.0 / frame.height});
    GrayImage result;
    result.width = std::max(1, static_cast<int>(std::lround(frame.width * factor)));
    result.height = std::max(1, static_cast<int>(std::lround(frame.height * factor)));
    result.pixels.resize(static_cast<std::size_t>(result.width) * result.height);
    for (int y = 0; y < result.height; ++y) {
        const int source_y = std::min(frame.height - 1,
            static_cast<int>((static_cast<std::int64_t>(y) * frame.height) / result.height));
        const auto* row = frame.rgba_pixels.data() +
            static_cast<std::size_t>(source_y) * frame.stride;
        for (int x = 0; x < result.width; ++x) {
            const int source_x = std::min(frame.width - 1,
                static_cast<int>((static_cast<std::int64_t>(x) * frame.width) / result.width));
            const auto offset = static_cast<std::size_t>(source_x) * 4;
            result.pixels[static_cast<std::size_t>(y) * result.width + x] =
                static_cast<float>((54.0 * row[offset] + 183.0 * row[offset + 1] +
                    19.0 * row[offset + 2]) / 256.0);
        }
    }
    return result;
}

Pyramid makePyramid(GrayImage base) {
    Pyramid result;
    result.push_back(std::move(base));
    while (result.size() < 4 && result.back().width >= 48 &&
           result.back().height >= 32) {
        const auto& previous = result.back();
        GrayImage next;
        next.width = (previous.width + 1) / 2;
        next.height = (previous.height + 1) / 2;
        next.pixels.resize(static_cast<std::size_t>(next.width) * next.height);
        for (int y = 0; y < next.height; ++y) {
            for (int x = 0; x < next.width; ++x) {
                float sum = 0.0F;
                int count = 0;
                for (int oy = 0; oy < 2; ++oy) for (int ox = 0; ox < 2; ++ox) {
                    const int sx = x * 2 + ox;
                    const int sy = y * 2 + oy;
                    if (sx < previous.width && sy < previous.height) {
                        sum += previous.pixels[static_cast<std::size_t>(sy) *
                            previous.width + sx];
                        ++count;
                    }
                }
                next.pixels[static_cast<std::size_t>(y) * next.width + x] = sum / count;
            }
        }
        result.push_back(std::move(next));
    }
    return result;
}

float sample(const GrayImage& image, double x, double y) {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    if (x0 < 0 || y0 < 0 || x0 + 1 >= image.width || y0 + 1 >= image.height)
        return std::numeric_limits<float>::quiet_NaN();
    const double fx = x - x0;
    const double fy = y - y0;
    const auto at = [&](int sx, int sy) {
        return image.pixels[static_cast<std::size_t>(sy) * image.width + sx];
    };
    return static_cast<float>((1.0 - fy) * ((1.0 - fx) * at(x0, y0) + fx * at(x0 + 1, y0)) +
        fy * ((1.0 - fx) * at(x0, y0 + 1) + fx * at(x0 + 1, y0 + 1)));
}

bool insideWindow(Point point, const GrayImage& image,
                  creative_suite::composition::ShapeMaskKind kind,
                  const creative_suite::composition::ShapeMaskParameters& geometry) {
    const double dx = (point.x / image.width - geometry.center_x) / geometry.width;
    const double dy = (point.y / image.height - geometry.center_y) / geometry.height;
    const double angle = geometry.rotation * std::numbers::pi / 180.0;
    const double x = std::cos(angle) * dx + std::sin(angle) * dy;
    const double y = -std::sin(angle) * dx + std::cos(angle) * dy;
    return kind == creative_suite::composition::ShapeMaskKind::Ellipse
        ? x * x + y * y < 0.81
        : std::abs(x) < 0.45 && std::abs(y) < 0.45;
}

std::vector<Point> findFeatures(const GrayImage& image,
    creative_suite::composition::ShapeMaskKind kind,
    const creative_suite::composition::ShapeMaskParameters& geometry) {
    struct Candidate { Point point; double score; };
    std::vector<Candidate> candidates;
    for (int y = 5; y < image.height - 5; y += 2) {
        for (int x = 5; x < image.width - 5; x += 2) {
            const Point point{double(x), double(y)};
            if (!insideWindow(point, image, kind, geometry)) continue;
            double xx = 0.0, xy = 0.0, yy = 0.0;
            for (int oy = -2; oy <= 2; ++oy) for (int ox = -2; ox <= 2; ++ox) {
                const auto at = [&](int sx, int sy) {
                    return image.pixels[static_cast<std::size_t>(sy) * image.width + sx];
                };
                const double gx = (at(x + ox + 1, y + oy) - at(x + ox - 1, y + oy)) * .5;
                const double gy = (at(x + ox, y + oy + 1) - at(x + ox, y + oy - 1)) * .5;
                xx += gx * gx; xy += gx * gy; yy += gy * gy;
            }
            const double discriminant = std::sqrt(std::max(0.0,
                (xx - yy) * (xx - yy) + 4.0 * xy * xy));
            const double score = (xx + yy - discriminant) * .5;
            if (score > 300.0) candidates.push_back({point, score});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.score > b.score;
    });
    std::vector<Point> result;
    result.reserve(96);
    for (const auto& candidate : candidates) {
        const bool too_close = std::any_of(result.begin(), result.end(), [&](Point point) {
            const double dx = candidate.point.x - point.x;
            const double dy = candidate.point.y - point.y;
            return dx * dx + dy * dy < 64.0;
        });
        if (!too_close) result.push_back(candidate.point);
        if (result.size() >= 96) break;
    }
    return result;
}

struct TrackedPoint { Point from; Point to; double error = 0.0; };

std::optional<TrackedPoint> trackPoint(const Pyramid& previous, const Pyramid& next,
                                       Point point) {
    Point estimate = point;
    double final_error = 0.0;
    for (std::size_t reverse = previous.size(); reverse > 0; --reverse) {
        const auto level = reverse - 1;
        const double scale = std::ldexp(1.0, static_cast<int>(level));
        const auto& a = previous[level];
        const auto& b = next[level];
        const Point source{point.x / scale, point.y / scale};
        if (level + 1 != previous.size()) estimate = {estimate.x * 2.0, estimate.y * 2.0};
        else estimate = source;
        if (source.x < 5 || source.y < 5 || source.x >= a.width - 5 ||
            source.y >= a.height - 5) return std::nullopt;
        for (int iteration = 0; iteration < 12; ++iteration) {
            if (estimate.x < 5 || estimate.y < 5 || estimate.x >= b.width - 5 ||
                estimate.y >= b.height - 5) return std::nullopt;
            double xx = 0.0, xy = 0.0, yy = 0.0, xt = 0.0, yt = 0.0;
            double absolute_error = 0.0;
            int count = 0;
            for (int oy = -3; oy <= 3; ++oy) for (int ox = -3; ox <= 3; ++ox) {
                const double px = source.x + ox, py = source.y + oy;
                const double qx = estimate.x + ox, qy = estimate.y + oy;
                const double reference = sample(a, px, py);
                const double current = sample(b, qx, qy);
                const double gx = (sample(b, qx + 1, qy) - sample(b, qx - 1, qy)) * .5;
                const double gy = (sample(b, qx, qy + 1) - sample(b, qx, qy - 1)) * .5;
                if (!std::isfinite(reference) || !std::isfinite(current) ||
                    !std::isfinite(gx) || !std::isfinite(gy)) return std::nullopt;
                const double temporal = reference - current;
                xx += gx * gx; xy += gx * gy; yy += gy * gy;
                xt += gx * temporal; yt += gy * temporal;
                absolute_error += std::abs(temporal);
                ++count;
            }
            const double determinant = xx * yy - xy * xy;
            if (determinant < 1e-5) return std::nullopt;
            const double dx = (yy * xt - xy * yt) / determinant;
            const double dy = (xx * yt - xy * xt) / determinant;
            if (!std::isfinite(dx) || !std::isfinite(dy) ||
                std::abs(dx) > 8.0 || std::abs(dy) > 8.0) return std::nullopt;
            estimate.x += dx;
            estimate.y += dy;
            final_error = absolute_error / count;
            if (dx * dx + dy * dy < .0004) break;
        }
    }
    if (final_error > 32.0) return std::nullopt;
    return TrackedPoint{point, estimate, final_error};
}

struct Similarity { double a = 1.0, b = 0.0, tx = 0.0, ty = 0.0; };

Similarity fromPair(const TrackedPoint& first, const TrackedPoint& second) {
    const double sx = second.from.x - first.from.x;
    const double sy = second.from.y - first.from.y;
    const double denominator = sx * sx + sy * sy;
    if (denominator < 16.0) return {std::numeric_limits<double>::quiet_NaN()};
    const double dx = second.to.x - first.to.x;
    const double dy = second.to.y - first.to.y;
    const double a = (sx * dx + sy * dy) / denominator;
    const double b = (sx * dy - sy * dx) / denominator;
    return {a, b, first.to.x - (a * first.from.x - b * first.from.y),
        first.to.y - (b * first.from.x + a * first.from.y)};
}

Point apply(const Similarity& transform, Point point) {
    return {transform.a * point.x - transform.b * point.y + transform.tx,
        transform.b * point.x + transform.a * point.y + transform.ty};
}

std::optional<Similarity> fitSimilarity(const std::vector<TrackedPoint>& points,
                                        double& confidence) {
    if (points.size() < 6) return std::nullopt;
    std::vector<std::size_t> best;
    double best_error = std::numeric_limits<double>::max();
    const std::size_t pair_budget = std::min<std::size_t>(192,
        points.size() * (points.size() - 1) / 2);
    std::size_t tested = 0;
    for (std::size_t left = 0; left < points.size() && tested < pair_budget; ++left) {
        for (std::size_t right = left + 1; right < points.size() && tested < pair_budget; ++right) {
            ++tested;
            const auto candidate = fromPair(points[left], points[right]);
            if (!std::isfinite(candidate.a)) continue;
            std::vector<std::size_t> inliers;
            double error = 0.0;
            for (std::size_t index = 0; index < points.size(); ++index) {
                const auto estimate = apply(candidate, points[index].from);
                const double dx = estimate.x - points[index].to.x;
                const double dy = estimate.y - points[index].to.y;
                const double residual = std::hypot(dx, dy);
                if (residual <= 2.8) {
                    inliers.push_back(index);
                    error += residual;
                }
            }
            if (inliers.size() > best.size() ||
                (inliers.size() == best.size() && error < best_error)) {
                best = std::move(inliers);
                best_error = error;
            }
        }
    }
    if (best.size() < 6 || best.size() * 100 < points.size() * 35)
        return std::nullopt;
    Point source_mean{}, target_mean{};
    for (const auto index : best) {
        source_mean.x += points[index].from.x; source_mean.y += points[index].from.y;
        target_mean.x += points[index].to.x; target_mean.y += points[index].to.y;
    }
    const double count = static_cast<double>(best.size());
    source_mean.x /= count; source_mean.y /= count;
    target_mean.x /= count; target_mean.y /= count;
    double denominator = 0.0, real = 0.0, imaginary = 0.0;
    for (const auto index : best) {
        const auto& point = points[index];
        const double sx = point.from.x - source_mean.x;
        const double sy = point.from.y - source_mean.y;
        const double tx = point.to.x - target_mean.x;
        const double ty = point.to.y - target_mean.y;
        denominator += sx * sx + sy * sy;
        real += sx * tx + sy * ty;
        imaginary += sx * ty - sy * tx;
    }
    if (denominator < 1e-6) return std::nullopt;
    Similarity result;
    result.a = real / denominator;
    result.b = imaginary / denominator;
    result.tx = target_mean.x - (result.a * source_mean.x - result.b * source_mean.y);
    result.ty = target_mean.y - (result.b * source_mean.x + result.a * source_mean.y);
    const double scale = std::hypot(result.a, result.b);
    if (!std::isfinite(scale) || scale < .65 || scale > 1.55) return std::nullopt;
    confidence = static_cast<double>(best.size()) / points.size();
    if (confidence < .35) return std::nullopt;
    return result;
}

void appendGeometry(PowerWindowTrackResult& result,
                    const creative_suite::composition::ShapeMaskParameters& geometry,
                    std::int64_t frame) {
    const std::array<double, 5> values{geometry.center_x, geometry.center_y,
        geometry.width, geometry.height, geometry.rotation};
    for (std::size_t index = 0; index < values.size(); ++index)
        result.keyframes[index].push_back({frame, values[index]});
}

}  // namespace

struct PowerWindowTracker::State {
    creative_suite::composition::ShapeMaskKind kind =
        creative_suite::composition::ShapeMaskKind::Rectangle;
    creative_suite::composition::ShapeMaskParameters geometry;
    Pyramid previous;
    std::vector<Point> points;
    PowerWindowTrackResult result;
    int width = 0;
    int height = 0;
    int direction = 0;
    bool running = false;
};

PowerWindowTracker::PowerWindowTracker() = default;
PowerWindowTracker::~PowerWindowTracker() = default;
PowerWindowTracker::PowerWindowTracker(PowerWindowTracker&&) noexcept = default;
PowerWindowTracker& PowerWindowTracker::operator=(PowerWindowTracker&&) noexcept = default;

bool PowerWindowTracker::begin(const media::VideoFrame& first_frame,
    std::int64_t frame_number, creative_suite::composition::ShapeMaskKind kind,
    const creative_suite::composition::ShapeMaskParameters& initial_geometry) {
    state_.reset();
    if (frame_number < 0 ||
        (kind != creative_suite::composition::ShapeMaskKind::Rectangle &&
         kind != creative_suite::composition::ShapeMaskKind::Ellipse) ||
        !creative_suite::composition::validShapeMask(initial_geometry)) return false;
    auto gray = makeGray(first_frame);
    if (!gray) return false;
    auto state = std::make_unique<State>();
    state->width = first_frame.width;
    state->height = first_frame.height;
    state->kind = kind;
    state->geometry = initial_geometry;
    state->previous = makePyramid(std::move(*gray));
    state->points = findFeatures(state->previous.front(), kind, initial_geometry);
    if (state->points.size() < 6) return false;
    appendGeometry(state->result, initial_geometry, frame_number);
    state->result.last_valid_frame = frame_number;
    state->result.tracked_frame_count = 1;
    state->result.confidence = 1.0;
    state->running = true;
    state_ = std::move(state);
    return true;
}

bool PowerWindowTracker::advance(const media::VideoFrame& frame,
    std::int64_t frame_number, const std::function<bool()>& should_cancel) {
    if (!state_ || !state_->running || canceled(should_cancel)) return false;
    const auto step = frame_number - state_->result.last_valid_frame;
    if (step != 1 && step != -1) {
        state_->running = false;
        return false;
    }
    if (state_->direction == 0) state_->direction = static_cast<int>(step);
    else if (state_->direction != step) {
        state_->running = false;
        return false;
    }
    if (frame.width != state_->width || frame.height != state_->height) {
        state_->running = false;
        return false;
    }
    auto gray = makeGray(frame);
    if (!gray) {
        state_->running = false;
        return false;
    }
    auto next = makePyramid(std::move(*gray));
    std::vector<TrackedPoint> tracked;
    tracked.reserve(state_->points.size());
    for (const auto point : state_->points) {
        if (canceled(should_cancel)) return false;
        if (const auto value = trackPoint(state_->previous, next, point))
            tracked.push_back(*value);
    }
    double confidence = 0.0;
    const auto transform = fitSimilarity(tracked, confidence);
    if (!transform) {
        state_->running = false;
        return false;
    }
    auto geometry = state_->geometry;
    const double scale = std::hypot(transform->a, transform->b);
    const double angle = std::atan2(transform->b, transform->a) * 180.0 /
        std::numbers::pi;
    geometry.center_x += transform->tx / next.front().width;
    geometry.center_y += transform->ty / next.front().height;
    geometry.width *= scale;
    geometry.height *= scale;
    geometry.rotation += angle;
    if (!creative_suite::composition::validShapeMask(geometry)) {
        state_->running = false;
        return false;
    }
    appendGeometry(state_->result, geometry, frame_number);
    state_->geometry = geometry;
    state_->result.last_valid_frame = frame_number;
    state_->result.confidence = confidence;
    ++state_->result.tracked_frame_count;
    state_->points.clear();
    state_->points.reserve(tracked.size());
    for (const auto& point : tracked) {
        const auto predicted = apply(*transform, point.from);
        if (std::hypot(predicted.x - point.to.x, predicted.y - point.to.y) <= 2.8)
            state_->points.push_back(point.to);
    }
    if (state_->points.size() < 6) state_->running = false;
    state_->previous = std::move(next);
    return true;
}

PowerWindowTrackResult PowerWindowTracker::result(bool reached_end) const {
    if (!state_) return {};
    auto result = state_->result;
    result.reached_end = reached_end;
    for (auto& curve : result.keyframes)
        std::sort(curve.begin(), curve.end(), [](const auto& left, const auto& right) {
            return left.frame < right.frame;
        });
    return result;
}

bool PowerWindowTracker::active() const noexcept {
    return state_ && state_->running;
}

std::optional<PowerWindowTrackResult> trackPowerWindow(
    std::span<const media::VideoFrame> frames,
    std::span<const std::int64_t> frame_numbers,
    creative_suite::composition::ShapeMaskKind kind,
    const creative_suite::composition::ShapeMaskParameters& initial_geometry,
    const std::function<bool()>& should_cancel) {
    if (frames.size() < 2 || frames.size() != frame_numbers.size() ||
        (kind != creative_suite::composition::ShapeMaskKind::Rectangle &&
         kind != creative_suite::composition::ShapeMaskKind::Ellipse) ||
        !creative_suite::composition::validShapeMask(initial_geometry)) return std::nullopt;
    const auto step = frame_numbers[1] - frame_numbers[0];
    if (step != 1 && step != -1) return std::nullopt;
    for (std::size_t index = 1; index < frame_numbers.size(); ++index)
        if (frame_numbers[index] - frame_numbers[index - 1] != step) return std::nullopt;

    PowerWindowTracker tracker;
    if (!tracker.begin(frames.front(), frame_numbers.front(), kind, initial_geometry))
        return std::nullopt;
    for (std::size_t frame_index = 1; frame_index < frames.size(); ++frame_index) {
        if (canceled(should_cancel)) return std::nullopt;
        if (!tracker.advance(frames[frame_index], frame_numbers[frame_index],
                should_cancel)) {
            if (canceled(should_cancel)) return std::nullopt;
            break;
        }
    }
    return tracker.result(tracker.result().tracked_frame_count == frames.size());
}

}  // namespace color
