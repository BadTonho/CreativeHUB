#pragma once
#include "bezier_path.h"
#include <array>
#include <functional>
#include <memory>
#include <optional>

namespace motion::model {
enum class TrimPathProperty : std::uint8_t { Start, End, Offset };
struct TrimPathValues {
    bool enabled = false;
    double start_percent = 0, end_percent = 100, offset_degrees = 0;
    friend bool operator==(const TrimPathValues &, const TrimPathValues &) = default;
};
struct TrimPathAnimation {
    TrimPathValues base;
    std::array<creative_suite::animation::ScalarKeyframes, 3> keys;
    friend bool operator==(const TrimPathAnimation &, const TrimPathAnimation &) = default;
};
[[nodiscard]] bool validTrimProperty(TrimPathProperty) noexcept;
[[nodiscard]] bool validTrimValue(TrimPathProperty, double) noexcept;
[[nodiscard]] double trimValue(const TrimPathValues &, TrimPathProperty) noexcept;
void setTrimValue(TrimPathValues &, TrimPathProperty, double) noexcept;
[[nodiscard]] bool validTrimAnimation(const TrimPathAnimation &) noexcept;
[[nodiscard]] std::size_t trimKeyframeCost(const TrimPathAnimation &) noexcept;
// Null storage is the disabled default; immutable curves are shared by history,
// scene snapshots and timeline rows. No animation is copied during a gesture.
class TrimPathStorage {
    struct Data;
    std::shared_ptr<const Data> data_;

  public:
    TrimPathStorage() = default;
    TrimPathStorage(TrimPathAnimation);
    [[nodiscard]] const TrimPathAnimation &get() const noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool isDefault() const noexcept;
    friend bool operator==(const TrimPathStorage &, const TrimPathStorage &);
};
[[nodiscard]] TrimPathValues evaluateTrimPaths(const TrimPathStorage &, std::int64_t local_frame);
struct TrimPathRange {
    double start = 0, span = 1;
    friend bool operator==(const TrimPathRange &, const TrimPathRange &) = default;
};
// Canonical visual identity: disabled/full and equivalent complete turns share
// the same range, without normalizing the saved values or their curves.
[[nodiscard]] TrimPathRange trimPathRange(const TrimPathValues &);

class PathLengthTable;
// Qt-free, evaluation-owned immutable arc-length index. At most 16 MiB of
// scratch/index storage and two million subdivision steps per operation.
// Null/empty optional means cancellation; invalid data/resource limits throw.
[[nodiscard]] std::shared_ptr<const PathLengthTable>
measureTrimPath(const PathGeometry &, const std::function<bool()> &cancel = {});
[[nodiscard]] bool measuredPathMatches(const PathLengthTable &, const PathGeometry &) noexcept;
[[nodiscard]] double measuredPathLength(const PathLengthTable &) noexcept;
[[nodiscard]] std::optional<std::vector<PathGeometry>>
extractTrimPath(const PathLengthTable &, TrimPathRange, const std::function<bool()> &cancel = {});
} // namespace motion::model
