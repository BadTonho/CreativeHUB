#pragma once
#include <creative_suite/animation/animation.h>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>

namespace motion::model {
struct PathPoint {
    double x = 0, y = 0;
    friend bool operator==(const PathPoint &, const PathPoint &) = default;
};
struct PathNode {
    PathPoint point, incoming, outgoing;
    friend bool operator==(const PathNode &, const PathNode &) = default;
};
struct PathGeometry {
    std::vector<PathNode> nodes;
    bool closed = false;
    friend bool operator==(const PathGeometry &, const PathGeometry &) = default;
};
struct PathKeyframe {
    std::int64_t frame = 0;
    PathGeometry geometry;
    creative_suite::animation::InterpolationMode interpolation =
        creative_suite::animation::InterpolationMode::Linear;
    creative_suite::animation::CubicBezierEasing easing;
    friend bool operator==(const PathKeyframe &, const PathKeyframe &) = default;
};
struct PathAnimation {
    PathGeometry base;
    std::vector<PathKeyframe> keys;
    friend bool operator==(const PathAnimation &, const PathAnimation &) = default;
};
struct PathTarget {
    std::uint64_t id = 0;
    bool mask = false;
    friend bool operator==(const PathTarget &, const PathTarget &) = default;
};
struct PathOverride {
    PathTarget target;
    std::shared_ptr<const PathGeometry> geometry;
};
inline constexpr std::size_t maximum_path_nodes = 4096;
[[nodiscard]] bool validPathGeometry(const PathGeometry &, bool mask = false) noexcept;
[[nodiscard]] bool validPathRasterGeometry(const PathGeometry &, double stroke = 0,
                                           double margin = 0) noexcept;
[[nodiscard]] bool samePathTopology(const PathGeometry &, const PathGeometry &) noexcept;
[[nodiscard]] bool validPathAnimation(const PathAnimation &, bool mask = false) noexcept;
[[nodiscard]] std::size_t pathKeyframeCost(const PathAnimation &) noexcept;
// Immutable value storage: document/history/scene copies share curves. Equality
// remains semantic for native round trips, with constant-time shared identity.
class PathAnimationStorage {
    struct Data;
    std::shared_ptr<const Data> data_;

  public:
    PathAnimationStorage() = default;
    PathAnimationStorage(PathAnimation);
    explicit operator bool() const noexcept {
        return bool(data_);
    }
    [[nodiscard]] const PathAnimation &operator*() const noexcept;
    [[nodiscard]] const PathAnimation *operator->() const noexcept;
    [[nodiscard]] bool valid(bool mask = false) const noexcept;
    friend bool operator==(const PathAnimationStorage &, const PathAnimationStorage &);
};
[[nodiscard]] PathGeometry evaluatePath(const PathAnimation &, std::int64_t frame);
[[nodiscard]] bool subdividePath(PathGeometry &, std::size_t segment, double t);
[[nodiscard]] PathPoint pathSegmentPoint(const PathGeometry &, std::size_t segment, double t);
} // namespace motion::model
