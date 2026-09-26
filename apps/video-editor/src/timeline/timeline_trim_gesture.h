#pragma once

#include "timeline_model.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace timeline {

struct TrimPointerPosition {
    double x = 0.0;
    double y = 0.0;

    friend bool operator==(const TrimPointerPosition&, const TrimPointerPosition&) = default;
};

struct TrimGestureMove {
    bool started = false;
    bool repaint = false;
};

struct TrimGestureLegacyRange {
    std::int64_t local_start_frame = 0;
    std::int64_t local_end_frame = 0;
};

struct TrimGestureFinish {
    enum class Kind { None, SelectTransition, RequestTrim };

    Kind kind = Kind::None;
    ClipLocation location;
    std::optional<std::pair<std::size_t, std::size_t>> transition_pair;
    ClipEdge edge = ClipEdge::Left;
    ClipEdgeEditMode mode = ClipEdgeEditMode::Individual;
    std::int64_t boundary_frame = 0;
    std::optional<TrimGestureLegacyRange> legacy_range;
};

// Holds only the decision and preview state of an edge-trim pointer gesture.
// TimelineWidget owns geometry, mouse capture, painting, and signal delivery.
class TimelineTrimGesture final {
public:
    enum class Phase { Inactive, PendingTransition, Active };

    void begin(
        const std::vector<TimelineTrack>& tracks,
        ClipLocation location,
        ClipEdge edge,
        ClipEdgeEditMode mode,
        std::int64_t original_boundary_frame,
        std::int64_t scale_duration,
        TrimPointerPosition position,
        std::optional<std::pair<std::size_t, std::size_t>> transition_pair = std::nullopt,
        FrameRate timeline_frame_rate = {});
    [[nodiscard]] TrimGestureMove move(
        const std::vector<TimelineTrack>& tracks,
        std::optional<std::int64_t> boundary_frame,
        TrimPointerPosition position);
    [[nodiscard]] TrimGestureFinish finish(
        const std::vector<TimelineTrack>& tracks,
        std::optional<std::int64_t> boundary_frame,
        TrimPointerPosition position);
    void cancel() noexcept;

    [[nodiscard]] Phase phase() const noexcept { return phase_; }
    [[nodiscard]] bool active() const noexcept { return phase_ != Phase::Inactive; }
    [[nodiscard]] ClipLocation location() const noexcept { return location_; }
    [[nodiscard]] ClipEdge edge() const noexcept { return edge_; }
    [[nodiscard]] std::int64_t scaleDuration() const noexcept { return scale_duration_; }
    [[nodiscard]] const std::optional<ClipEdgeEditPreview>& preview() const noexcept {
        return preview_;
    }

private:
    void refreshPreview(
        const std::vector<TimelineTrack>& tracks,
        std::optional<std::int64_t> boundary_frame);

    Phase phase_ = Phase::Inactive;
    ClipLocation location_{};
    ClipEdge edge_ = ClipEdge::Left;
    ClipEdgeEditMode mode_ = ClipEdgeEditMode::Individual;
    std::int64_t original_boundary_frame_ = 0;
    std::int64_t scale_duration_ = 0;
    TrimPointerPosition last_position_{};
    std::optional<std::pair<std::size_t, std::size_t>> transition_pair_;
    std::optional<ClipEdgeEditPreview> preview_;
    FrameRate timeline_frame_rate_;
};

} // namespace timeline
