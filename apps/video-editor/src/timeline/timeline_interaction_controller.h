#pragma once

#include "timeline_trim_gesture.h"

#include <QPointF>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace timeline {

struct TimelineMoveTarget {
    TrackId track_id = 0;
    std::int64_t timeline_start_frame = 0;
    std::optional<std::int64_t> snap_guide_frame;
};

struct TimelineMoveRequest {
    ClipId clip_id = 0;
    TrackId target_track_id = 0;
    std::int64_t timeline_start_frame = 0;
};

struct TimelineSplitRequest {
    ClipId clip_id = 0;
    std::int64_t local_frame = 0;
};

struct TimelineDropPreview {
    bool hovering = false;
    bool media = false;
    std::optional<std::size_t> target_track_index;
    std::optional<std::int64_t> target_frame;
    QPointF pointer_position;
    std::int64_t duration_frames = 1;
    QString label;
    bool valid = false;
    std::optional<std::int64_t> snap_guide_frame;
};

// Owns pointer-gesture state and turns completed gestures into stable-ID
// requests. TimelineWidget supplies hit-tested targets and paints the previews.
class TimelineInteractionController final {
public:
    [[nodiscard]] static constexpr double dragThreshold() noexcept { return 4.0; }

    void beginRulerSeek(std::int64_t frame, double content_x) noexcept;
    void updateRulerSeek(std::int64_t frame, double content_x) noexcept;
    [[nodiscard]] bool rulerSeekPending() const noexcept { return ruler_seek_pending_; }
    [[nodiscard]] std::optional<std::int64_t> rulerPreviewFrame() const noexcept {
        return ruler_frame_;
    }
    [[nodiscard]] std::optional<double> rulerContentX() const noexcept {
        return ruler_content_x_;
    }
    [[nodiscard]] std::optional<std::int64_t> finishRulerSeek() noexcept;
    void clearTransientPreview() noexcept;

    void beginMove(
        ClipId clip_id,
        TrackId source_track_id,
        std::int64_t source_start_frame,
        QPointF press_position) noexcept;
    [[nodiscard]] bool updateMoveActivation(QPointF position) noexcept;
    void setMoveTarget(std::optional<TimelineMoveTarget> target) noexcept;
    [[nodiscard]] bool movePending() const noexcept { return move_pending_; }
    [[nodiscard]] bool moveActive() const noexcept { return move_active_; }
    [[nodiscard]] ClipId movingClipId() const noexcept { return moving_clip_id_; }
    [[nodiscard]] TrackId sourceTrackId() const noexcept { return source_track_id_; }
    [[nodiscard]] std::optional<TimelineMoveTarget> moveTarget() const noexcept {
        return move_target_;
    }
    [[nodiscard]] std::optional<TimelineMoveRequest> finishMove() noexcept;
    void cancelMove() noexcept;

    void beginSplit(ClipId clip_id, std::int64_t local_frame, QPointF press_position) noexcept;
    void updateSplit(QPointF position) noexcept;
    [[nodiscard]] bool splitPending() const noexcept { return split_pending_; }
    [[nodiscard]] std::optional<TimelineSplitRequest> finishSplit() noexcept;
    void cancelSplit() noexcept;

    void beginSeek(
        ClipId clip_id,
        std::int64_t clip_start_frame,
        std::int64_t local_frame,
        QPointF press_position) noexcept;
    [[nodiscard]] bool updateSeek(
        QPointF position,
        std::optional<std::int64_t> local_frame) noexcept;
    [[nodiscard]] bool seekPending() const noexcept { return seek_pending_; }
    [[nodiscard]] bool seekDragging() const noexcept { return seek_dragging_; }
    [[nodiscard]] ClipId seekClipId() const noexcept { return seek_clip_id_; }
    [[nodiscard]] std::optional<std::int64_t> seekPreviewLocalFrame() const noexcept {
        return seek_preview_local_frame_;
    }
    [[nodiscard]] std::optional<std::int64_t> finishSeek() noexcept;

    [[nodiscard]] TimelineTrimGesture& trimGesture() noexcept { return trim_gesture_; }
    [[nodiscard]] const TimelineTrimGesture& trimGesture() const noexcept {
        return trim_gesture_;
    }

    void setDropPreview(TimelineDropPreview preview) noexcept;
    void clearDropPreview() noexcept;
    void clearDropGhost() noexcept;
    [[nodiscard]] const TimelineDropPreview& dropPreview() const noexcept {
        return drop_preview_;
    }

    void cancelAll() noexcept;

private:
    bool ruler_seek_pending_ = false;
    std::optional<std::int64_t> ruler_frame_;
    std::optional<double> ruler_content_x_;

    bool move_pending_ = false;
    bool move_active_ = false;
    ClipId moving_clip_id_ = 0;
    TrackId source_track_id_ = 0;
    QPointF move_press_position_;
    std::optional<TimelineMoveTarget> move_target_;

    bool split_pending_ = false;
    bool split_moved_ = false;
    ClipId split_clip_id_ = 0;
    std::int64_t split_local_frame_ = 0;
    QPointF split_press_position_;

    bool seek_pending_ = false;
    bool seek_dragging_ = false;
    ClipId seek_clip_id_ = 0;
    std::int64_t seek_clip_start_frame_ = 0;
    QPointF seek_press_position_;
    std::optional<std::int64_t> seek_preview_local_frame_;

    TimelineTrimGesture trim_gesture_;
    TimelineDropPreview drop_preview_;
};

} // namespace timeline
