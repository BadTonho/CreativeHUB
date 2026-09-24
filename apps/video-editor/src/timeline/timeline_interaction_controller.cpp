#include "timeline_interaction_controller.h"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

namespace timeline {
namespace {

double manhattanDistance(const QPointF& left, const QPointF& right) noexcept {
    return std::abs(left.x() - right.x()) + std::abs(left.y() - right.y());
}

} // namespace

void TimelineInteractionController::beginRulerSeek(
    std::int64_t frame,
    double content_x) noexcept {
    ruler_seek_pending_ = true;
    ruler_frame_ = frame;
    ruler_content_x_ = content_x;
}

void TimelineInteractionController::updateRulerSeek(
    std::int64_t frame,
    double content_x) noexcept {
    if (!ruler_seek_pending_) return;
    ruler_frame_ = frame;
    ruler_content_x_ = content_x;
}

std::optional<std::int64_t> TimelineInteractionController::finishRulerSeek() noexcept {
    const auto result = ruler_seek_pending_ ? ruler_frame_ : std::nullopt;
    ruler_seek_pending_ = false;
    ruler_frame_.reset();
    ruler_content_x_.reset();
    return result;
}

void TimelineInteractionController::clearTransientPreview() noexcept {
    seek_preview_local_frame_.reset();
    if (!ruler_seek_pending_) {
        ruler_frame_.reset();
        ruler_content_x_.reset();
    }
}

void TimelineInteractionController::beginMove(
    ClipId clip_id,
    TrackId source_track_id,
    std::int64_t source_start_frame,
    QPointF press_position) noexcept {
    move_pending_ = true;
    move_active_ = false;
    moving_clip_id_ = clip_id;
    source_track_id_ = source_track_id;
    move_press_position_ = press_position;
    move_target_ = TimelineMoveTarget{source_track_id, source_start_frame, std::nullopt};
}

bool TimelineInteractionController::updateMoveActivation(QPointF position) noexcept {
    if (!move_pending_ || move_active_ ||
        manhattanDistance(position, move_press_position_) <= dragThreshold()) {
        return false;
    }
    move_pending_ = false;
    move_active_ = true;
    return true;
}

void TimelineInteractionController::setMoveTarget(
    std::optional<TimelineMoveTarget> target) noexcept {
    if (move_active_) move_target_ = target;
}

std::optional<TimelineMoveRequest> TimelineInteractionController::finishMove() noexcept {
    std::optional<TimelineMoveRequest> result;
    if (move_active_ && move_target_.has_value()) {
        result = TimelineMoveRequest{
            moving_clip_id_, move_target_->track_id,
            move_target_->timeline_start_frame};
    }
    cancelMove();
    return result;
}

void TimelineInteractionController::cancelMove() noexcept {
    move_pending_ = false;
    move_active_ = false;
    moving_clip_id_ = 0;
    source_track_id_ = 0;
    move_press_position_ = {};
    move_target_.reset();
}

void TimelineInteractionController::beginSplit(
    ClipId clip_id,
    std::int64_t local_frame,
    QPointF press_position) noexcept {
    split_pending_ = true;
    split_moved_ = false;
    split_clip_id_ = clip_id;
    split_local_frame_ = local_frame;
    split_press_position_ = press_position;
}

void TimelineInteractionController::updateSplit(QPointF position) noexcept {
    if (!split_pending_) return;
    split_moved_ = manhattanDistance(position, split_press_position_) > dragThreshold();
}

std::optional<TimelineSplitRequest> TimelineInteractionController::finishSplit() noexcept {
    const auto result = split_pending_ && !split_moved_
        ? std::optional<TimelineSplitRequest>{
              TimelineSplitRequest{split_clip_id_, split_local_frame_}}
        : std::nullopt;
    cancelSplit();
    return result;
}

void TimelineInteractionController::cancelSplit() noexcept {
    split_pending_ = false;
    split_moved_ = false;
    split_clip_id_ = 0;
    split_local_frame_ = 0;
    split_press_position_ = {};
}

void TimelineInteractionController::beginSeek(
    ClipId clip_id,
    std::int64_t clip_start_frame,
    std::int64_t local_frame,
    QPointF press_position) noexcept {
    seek_pending_ = true;
    seek_dragging_ = false;
    seek_clip_id_ = clip_id;
    seek_clip_start_frame_ = clip_start_frame;
    seek_press_position_ = press_position;
    seek_preview_local_frame_ = local_frame;
}

bool TimelineInteractionController::updateSeek(
    QPointF position,
    std::optional<std::int64_t> local_frame) noexcept {
    if (!seek_pending_ && !seek_dragging_) return false;
    if (seek_pending_ && !seek_dragging_) {
        if (manhattanDistance(position, seek_press_position_) <= dragThreshold()) return false;
        seek_pending_ = false;
        seek_dragging_ = true;
        seek_preview_local_frame_ = local_frame;
        return true;
    }
    seek_preview_local_frame_ = local_frame;
    return false;
}

std::optional<std::int64_t> TimelineInteractionController::finishSeek() noexcept {
    std::optional<std::int64_t> result;
    if (seek_dragging_ && seek_clip_start_frame_ >= 0 &&
        seek_preview_local_frame_.has_value() &&
        *seek_preview_local_frame_ >= 0 &&
        *seek_preview_local_frame_ <=
            std::numeric_limits<std::int64_t>::max() - seek_clip_start_frame_) {
        result = seek_clip_start_frame_ + *seek_preview_local_frame_;
    }
    seek_pending_ = false;
    seek_dragging_ = false;
    seek_clip_id_ = 0;
    seek_clip_start_frame_ = 0;
    seek_press_position_ = {};
    seek_preview_local_frame_.reset();
    return result;
}

void TimelineInteractionController::setDropPreview(
    TimelineDropPreview preview) noexcept {
    drop_preview_ = std::move(preview);
}

void TimelineInteractionController::clearDropPreview() noexcept {
    drop_preview_ = {};
}

void TimelineInteractionController::clearDropGhost() noexcept {
    drop_preview_.media = false;
    drop_preview_.duration_frames = 1;
    drop_preview_.label.clear();
    drop_preview_.valid = false;
    drop_preview_.snap_guide_frame.reset();
}

void TimelineInteractionController::cancelAll() noexcept {
    ruler_seek_pending_ = false;
    ruler_frame_.reset();
    ruler_content_x_.reset();
    cancelMove();
    cancelSplit();
    seek_pending_ = false;
    seek_dragging_ = false;
    seek_clip_id_ = 0;
    seek_clip_start_frame_ = 0;
    seek_press_position_ = {};
    seek_preview_local_frame_.reset();
    trim_gesture_.cancel();
    clearDropPreview();
}

} // namespace timeline
