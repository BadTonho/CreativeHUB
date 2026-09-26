#include "timeline_trim_gesture.h"

namespace timeline {

void TimelineTrimGesture::begin(
    const std::vector<TimelineTrack>& tracks,
    ClipLocation location,
    ClipEdge edge,
    ClipEdgeEditMode mode,
    std::int64_t original_boundary_frame,
    std::int64_t scale_duration,
    TrimPointerPosition position,
    std::optional<std::pair<std::size_t, std::size_t>> transition_pair,
    FrameRate timeline_frame_rate) {
    phase_ = transition_pair.has_value() ? Phase::PendingTransition : Phase::Active;
    location_ = location;
    edge_ = edge;
    mode_ = mode;
    original_boundary_frame_ = original_boundary_frame;
    scale_duration_ = scale_duration;
    last_position_ = position;
    transition_pair_ = transition_pair;
    timeline_frame_rate_ = validFrameRate(timeline_frame_rate)
        ? reducedFrameRate(timeline_frame_rate)
        : FrameRate{};
    preview_ = previewClipEdgeEdit(
        tracks, location, edge, original_boundary_frame, mode,
        timeline_frame_rate_);
}

TrimGestureMove TimelineTrimGesture::move(
    const std::vector<TimelineTrack>& tracks,
    std::optional<std::int64_t> boundary_frame,
    TrimPointerPosition position) {
    TrimGestureMove result;
    if (phase_ == Phase::Inactive) return result;

    if (phase_ == Phase::PendingTransition) {
        const auto candidate = boundary_frame.has_value()
            ? previewClipEdgeEdit(tracks, location_, edge_, *boundary_frame, mode_,
                                  timeline_frame_rate_)
            : std::nullopt;
        if (!candidate.has_value() ||
            candidate->boundary_frame == original_boundary_frame_) {
            last_position_ = position;
            return result;
        }
        phase_ = Phase::Active;
        transition_pair_.reset();
        result.started = true;
    }

    if (position != last_position_) {
        refreshPreview(tracks, boundary_frame);
        last_position_ = position;
        result.repaint = boundary_frame.has_value();
    }
    return result;
}

TrimGestureFinish TimelineTrimGesture::finish(
    const std::vector<TimelineTrack>& tracks,
    std::optional<std::int64_t> boundary_frame,
    TrimPointerPosition position) {
    TrimGestureFinish result;
    if (phase_ == Phase::Inactive) return result;

    result.location = location_;
    if (phase_ == Phase::PendingTransition) {
        if (transition_pair_.has_value()) {
            result.kind = TrimGestureFinish::Kind::SelectTransition;
            result.transition_pair = transition_pair_;
        }
        cancel();
        return result;
    }

    if (position != last_position_) {
        refreshPreview(tracks, boundary_frame);
    }
    if (location_.track_index < tracks.size() &&
        location_.clip_index < tracks[location_.track_index].clips.size() &&
        preview_.has_value()) {
        const auto& original = tracks[location_.track_index].clips[location_.clip_index];
        const bool changed = preview_->clip != original ||
            (preview_->neighbor_location.has_value() &&
             preview_->neighbor_clip.has_value() &&
             *preview_->neighbor_clip != tracks[location_.track_index].clips[
                 preview_->neighbor_location->clip_index]);
        if (changed) {
            result.kind = TrimGestureFinish::Kind::RequestTrim;
            result.edge = edge_;
            result.mode = mode_;
            result.boundary_frame = preview_->boundary_frame;
            if (location_.track_index == 0 &&
                preview_->clip.timeline_start_frame >= original.timeline_start_frame) {
                const auto original_end = original.timeline_start_frame +
                    original.timeline_duration_frames;
                const auto preview_end = preview_->clip.timeline_start_frame +
                    preview_->clip.timeline_duration_frames;
                if (preview_end <= original_end) {
                    result.legacy_range = TrimGestureLegacyRange{
                        preview_->clip.timeline_start_frame - original.timeline_start_frame,
                        preview_end - original.timeline_start_frame};
                }
            }
        }
    }
    cancel();
    return result;
}

void TimelineTrimGesture::cancel() noexcept {
    phase_ = Phase::Inactive;
    transition_pair_.reset();
    preview_.reset();
    scale_duration_ = 0;
}

void TimelineTrimGesture::refreshPreview(
    const std::vector<TimelineTrack>& tracks,
    std::optional<std::int64_t> boundary_frame) {
    if (!boundary_frame.has_value()) return;
    const auto candidate = previewClipEdgeEdit(
        tracks, location_, edge_, *boundary_frame, mode_, timeline_frame_rate_);
    if (candidate.has_value()) preview_ = *candidate;
}

} // namespace timeline
