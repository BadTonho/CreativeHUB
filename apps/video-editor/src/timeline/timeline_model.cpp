#include "timeline_model.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace timeline {
namespace {

std::optional<std::int64_t> durationInFrames(const media::VideoMetadata& metadata) {
    if (metadata.frame_count.has_value() && *metadata.frame_count > 0) {
        return *metadata.frame_count;
    }
    if (!metadata.duration_seconds.has_value() ||
        !metadata.frame_rate.has_value() ||
        !std::isfinite(*metadata.duration_seconds) ||
        !std::isfinite(*metadata.frame_rate) ||
        *metadata.duration_seconds <= 0.0 ||
        *metadata.frame_rate <= 0.0) {
        return std::nullopt;
    }
    const double estimated = *metadata.duration_seconds * *metadata.frame_rate;
    if (!std::isfinite(estimated) ||
        estimated > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(estimated)));
}

std::optional<std::int64_t> sourceFrameLimit(const TimelineClip& clip) noexcept {
    if (clip.kind != ClipKind::Video) return std::nullopt;
    if (clip.frame_count.has_value() && *clip.frame_count > 0) {
        return clip.frame_count;
    }
    if (!clip.duration_seconds.has_value() || !clip.frame_rate.has_value() ||
        !std::isfinite(*clip.duration_seconds) || !std::isfinite(*clip.frame_rate) ||
        *clip.duration_seconds <= 0.0 || *clip.frame_rate <= 0.0) {
        return std::nullopt;
    }
    const long double estimated =
        static_cast<long double>(*clip.duration_seconds) *
        static_cast<long double>(*clip.frame_rate);
    const auto exclusive_max = std::ldexp(1.0L, 63);
    if (!std::isfinite(estimated) || estimated >= exclusive_max) {
        return std::nullopt;
    }
    const auto rounded = std::ceil(estimated);
    if (rounded >= exclusive_max) return std::nullopt;
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(rounded));
}

std::optional<std::int64_t> clipTimelineEnd(const TimelineClip& clip) noexcept {
    if (clip.timeline_start_frame < 0 || clip.timeline_duration_frames <= 0 ||
        clip.timeline_start_frame > std::numeric_limits<std::int64_t>::max() -
            clip.timeline_duration_frames) {
        return std::nullopt;
    }
    return clip.timeline_start_frame + clip.timeline_duration_frames;
}

std::optional<std::int64_t> shiftedSourceStart(
    const TimelineClip& clip,
    std::int64_t new_timeline_start_frame) noexcept {
    if (clip.source_start_frame < 0 || new_timeline_start_frame < 0) {
        return std::nullopt;
    }
    if (clip.kind != ClipKind::Video) return clip.source_start_frame;
    const auto delta = new_timeline_start_frame - clip.timeline_start_frame;
    if (delta >= 0) {
        if (delta > std::numeric_limits<std::int64_t>::max() -
                clip.source_start_frame) {
            return std::nullopt;
        }
        return clip.source_start_frame + delta;
    }
    const auto removed = -delta;
    if (removed <= clip.source_start_frame) {
        return clip.source_start_frame - removed;
    }
    return std::nullopt;
}

TransformKeyframes reframeKeyframes(
    const TimelineClip& old_clip,
    std::int64_t new_timeline_start_frame,
    std::int64_t new_duration_frames,
    Transform2D& new_transform) noexcept {
    const auto start_delta = new_timeline_start_frame - old_clip.timeline_start_frame;
    const auto old_anchor_frame = std::max<std::int64_t>(0, start_delta);
    new_transform = evaluateTransform(
        old_clip.transform, old_clip.keyframes, old_anchor_frame);

    TransformKeyframes result;
    if (start_delta == 0) {
        for (const auto property : {TransformProperty::PositionX,
                                    TransformProperty::PositionY,
                                    TransformProperty::Scale,
                                    TransformProperty::Rotation,
                                    TransformProperty::Opacity}) {
            for (const auto& keyframe : keyframesFor(old_clip.keyframes, property)) {
                if (keyframe.frame < new_duration_frames) {
                    static_cast<void>(setKeyframe(
                        result, property, keyframe.frame, keyframe.value));
                }
            }
        }
        return result;
    }
    for (const auto property : {TransformProperty::PositionX,
                                TransformProperty::PositionY,
                                TransformProperty::Scale,
                                TransformProperty::Rotation,
                                TransformProperty::Opacity}) {
        const auto& old_keys = keyframesFor(old_clip.keyframes, property);
        if (old_keys.empty()) continue;

        const auto anchor_new_frame = start_delta < 0 ? -start_delta : 0;
        const auto anchor_value = evaluateProperty(
            old_clip.transform,
            old_clip.keyframes,
            property,
            old_anchor_frame);
        static_cast<void>(setKeyframe(
            result, property, anchor_new_frame, anchor_value));

        for (const auto& keyframe : old_keys) {
            if (start_delta > 0 && keyframe.frame < start_delta) continue;
            if (start_delta < 0 &&
                keyframe.frame > std::numeric_limits<std::int64_t>::max() +
                    start_delta) {
                continue;
            }
            const auto shifted_frame = keyframe.frame - start_delta;
            if (shifted_frame >= 0 && shifted_frame < new_duration_frames) {
                static_cast<void>(setKeyframe(
                    result, property, shifted_frame, keyframe.value));
            }
        }
    }
    return result;
}

bool sameOverlapClass(const TimelineClip& left, const TimelineClip& right) noexcept {
    return left.kind == right.kind ||
        (isMediaClipKind(left.kind) && isMediaClipKind(right.kind));
}

} // namespace

TimelineModel::TimelineModel() {
    tracks_.push_back({next_track_id_++, "Video 1", 1.0, false, {}});
    assertIdentityInvariants();
}

bool TimelineModel::validName(const std::string& name) noexcept {
    return !name.empty() && name.find_first_not_of(" \t\r\n") != std::string::npos;
}

bool TimelineModel::validAudioGain(double gain) noexcept {
    return std::isfinite(gain) && gain >= 0.0 && gain <= 2.0;
}

bool TimelineModel::validTextStyle(const TextStyle& text) noexcept {
    if (text.font_family.empty() || !std::isfinite(text.font_size_pixels) ||
        text.font_size_pixels <= 0.0 || text.font_size_pixels > 512.0) {
        return false;
    }
    switch (text.alignment) {
    case TextAlignment::Left:
    case TextAlignment::Center:
    case TextAlignment::Right:
        return true;
    }
    return false;
}

bool TimelineModel::validTransitionKind(TransitionKind kind) noexcept {
    switch (kind) {
    case TransitionKind::CrossDissolve:
    case TransitionKind::FadeToBlack:
        return true;
    }
    return false;
}

std::filesystem::path TimelineModel::canonicalPath(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (!error) return canonical;
    const auto absolute = std::filesystem::absolute(path, error);
    if (!error) return absolute.lexically_normal();
    return path.lexically_normal();
}

std::optional<std::int64_t> TimelineModel::durationInFrames(
    const media::VideoMetadata& metadata) {
    if (metadata.frame_count.has_value() && *metadata.frame_count > 0) {
        return *metadata.frame_count;
    }
    if (!metadata.duration_seconds.has_value() ||
        !metadata.frame_rate.has_value() ||
        !std::isfinite(*metadata.duration_seconds) ||
        !std::isfinite(*metadata.frame_rate) ||
        *metadata.duration_seconds <= 0.0 ||
        *metadata.frame_rate <= 0.0) {
        return std::nullopt;
    }
    const double estimated = *metadata.duration_seconds * *metadata.frame_rate;
    if (!std::isfinite(estimated) ||
        estimated > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(estimated)));
}

bool TimelineModel::overlaps(
    const TimelineClip& left,
    std::int64_t start_frame,
    std::int64_t duration_frames) noexcept {
    if (duration_frames <= 0 || left.timeline_duration_frames <= 0) return true;
    if (start_frame > std::numeric_limits<std::int64_t>::max() - duration_frames ||
        left.timeline_start_frame > std::numeric_limits<std::int64_t>::max() -
            left.timeline_duration_frames) {
        return true;
    }
    const auto end_frame = start_frame + duration_frames;
    const auto left_end = left.timeline_start_frame + left.timeline_duration_frames;
    return start_frame < left_end && left.timeline_start_frame < end_frame;
}

bool TimelineModel::overlapsSameKind(
    const TimelineClip& left,
    ClipKind kind,
    std::int64_t start_frame,
    std::int64_t duration_frames) noexcept {
    const bool same_visual_media_kind = isMediaClipKind(left.kind) &&
        isMediaClipKind(kind);
    return (left.kind == kind || same_visual_media_kind) &&
        overlaps(left, start_frame, duration_frames);
}

std::optional<ClipEdgeEditPreview> previewClipEdgeEdit(
    const std::vector<TimelineTrack>& tracks,
    ClipLocation location,
    ClipEdge edge,
    std::int64_t requested_boundary_frame,
    ClipEdgeEditMode mode) {
    if ((edge != ClipEdge::Left && edge != ClipEdge::Right) ||
        (mode != ClipEdgeEditMode::Rolling &&
         mode != ClipEdgeEditMode::Individual)) {
        return std::nullopt;
    }
    if (location.track_index >= tracks.size()) return std::nullopt;
    const auto& track = tracks[location.track_index];
    if (location.clip_index >= track.clips.size()) return std::nullopt;
    const auto& original = track.clips[location.clip_index];
    const auto original_end = clipTimelineEnd(original);
    if (!original_end.has_value() || original.source_start_frame < 0 ||
        original.source_start_frame > std::numeric_limits<std::int64_t>::max() -
            original.timeline_duration_frames) {
        return std::nullopt;
    }

    std::optional<std::size_t> shared_neighbor_index;
    if (edge == ClipEdge::Left && location.clip_index > 0) {
        const auto previous_index = location.clip_index - 1;
        const auto previous_end = clipTimelineEnd(track.clips[previous_index]);
        if (previous_end.has_value() && *previous_end == original.timeline_start_frame) {
            shared_neighbor_index = previous_index;
        }
    } else if (edge == ClipEdge::Right &&
               location.clip_index + 1 < track.clips.size()) {
        const auto& next = track.clips[location.clip_index + 1];
        if (next.timeline_start_frame == *original_end) {
            shared_neighbor_index = location.clip_index + 1;
        }
    }
    const auto neighbor_index = mode == ClipEdgeEditMode::Rolling
        ? shared_neighbor_index
        : std::nullopt;

    std::int64_t minimum_boundary = edge == ClipEdge::Left
        ? 0
        : original.timeline_start_frame + 1;
    std::int64_t maximum_boundary = edge == ClipEdge::Left
        ? *original_end - 1
        : std::numeric_limits<std::int64_t>::max();

    if (neighbor_index.has_value()) {
        const auto& neighbor = track.clips[*neighbor_index];
        const auto neighbor_end = clipTimelineEnd(neighbor);
        if (!neighbor_end.has_value() || neighbor.source_start_frame < 0 ||
            neighbor.source_start_frame > std::numeric_limits<std::int64_t>::max() -
                neighbor.timeline_duration_frames) {
            return std::nullopt;
        }
        if (edge == ClipEdge::Left) {
            minimum_boundary = neighbor.timeline_start_frame + 1;
        } else {
            maximum_boundary = *neighbor_end - 1;
        }
    } else if (edge == ClipEdge::Left) {
        for (std::size_t index = 0; index < track.clips.size(); ++index) {
            if (index == location.clip_index) continue;
            const auto& other = track.clips[index];
            const bool overlaps_adjacent_media =
                mode == ClipEdgeEditMode::Individual &&
                shared_neighbor_index == index &&
                isMediaClipKind(original.kind) && isMediaClipKind(other.kind);
            if (overlaps_adjacent_media) continue;
            const auto other_end = clipTimelineEnd(other);
            if (other_end.has_value() && *other_end <= original.timeline_start_frame &&
                sameOverlapClass(other, original)) {
                minimum_boundary = std::max(minimum_boundary, *other_end);
            }
        }
    } else {
        for (std::size_t index = 0; index < track.clips.size(); ++index) {
            if (index == location.clip_index) continue;
            const auto& other = track.clips[index];
            const bool overlaps_adjacent_media =
                mode == ClipEdgeEditMode::Individual &&
                shared_neighbor_index == index &&
                isMediaClipKind(original.kind) && isMediaClipKind(other.kind);
            if (overlaps_adjacent_media) continue;
            if (other.timeline_start_frame >= *original_end &&
                sameOverlapClass(other, original)) {
                maximum_boundary = std::min(
                    maximum_boundary, other.timeline_start_frame);
            }
        }
    }

    if (original.kind == ClipKind::Video) {
        if (edge == ClipEdge::Left) {
            minimum_boundary = std::max(
                minimum_boundary,
                original.timeline_start_frame - original.source_start_frame);
        } else {
            const auto current_source_end = original.source_start_frame +
                original.timeline_duration_frames;
            const auto limit = sourceFrameLimit(original).value_or(current_source_end);
            if (limit < original.source_start_frame) return std::nullopt;
            const auto available = limit - original.source_start_frame;
            const auto max_boundary = available >
                    std::numeric_limits<std::int64_t>::max() -
                        original.timeline_start_frame
                ? std::numeric_limits<std::int64_t>::max()
                : original.timeline_start_frame + available;
            maximum_boundary = std::min(maximum_boundary, max_boundary);
        }
    }

    if (neighbor_index.has_value()) {
        const auto& neighbor = track.clips[*neighbor_index];
        if (edge == ClipEdge::Left && neighbor.kind == ClipKind::Video) {
            const auto current_source_end = neighbor.source_start_frame +
                neighbor.timeline_duration_frames;
            const auto limit = sourceFrameLimit(neighbor).value_or(current_source_end);
            if (limit < neighbor.source_start_frame) return std::nullopt;
            const auto available = limit - neighbor.source_start_frame;
            const auto max_boundary = available >
                    std::numeric_limits<std::int64_t>::max() -
                        neighbor.timeline_start_frame
                ? std::numeric_limits<std::int64_t>::max()
                : neighbor.timeline_start_frame + available;
            maximum_boundary = std::min(maximum_boundary, max_boundary);
        } else if (edge == ClipEdge::Right && neighbor.kind == ClipKind::Video) {
            minimum_boundary = std::max(
                minimum_boundary,
                neighbor.timeline_start_frame - neighbor.source_start_frame);
        }
    }

    if (minimum_boundary > maximum_boundary) return std::nullopt;
    const auto boundary = std::clamp(
        requested_boundary_frame, minimum_boundary, maximum_boundary);

    ClipEdgeEditPreview preview;
    preview.clip_location = location;
    preview.clip = original;
    preview.boundary_frame = boundary;
    if (edge == ClipEdge::Left) {
        preview.clip.timeline_start_frame = boundary;
        preview.clip.timeline_duration_frames = *original_end - boundary;
        const auto source_start = shiftedSourceStart(original, boundary);
        if (!source_start.has_value()) return std::nullopt;
        preview.clip.source_start_frame = *source_start;
    } else {
        preview.clip.timeline_duration_frames = boundary - original.timeline_start_frame;
    }
    preview.clip.keyframes = reframeKeyframes(
        original,
        preview.clip.timeline_start_frame,
        preview.clip.timeline_duration_frames,
        preview.clip.transform);

    if (neighbor_index.has_value()) {
        const auto& original_neighbor = track.clips[*neighbor_index];
        const auto neighbor_end = clipTimelineEnd(original_neighbor);
        if (!neighbor_end.has_value()) return std::nullopt;
        auto neighbor = original_neighbor;
        if (edge == ClipEdge::Left) {
            neighbor.timeline_duration_frames =
                boundary - neighbor.timeline_start_frame;
        } else {
            neighbor.timeline_start_frame = boundary;
            neighbor.timeline_duration_frames = *neighbor_end - boundary;
            const auto source_start = shiftedSourceStart(
                original_neighbor, neighbor.timeline_start_frame);
            if (!source_start.has_value()) return std::nullopt;
            neighbor.source_start_frame = *source_start;
        }
        neighbor.keyframes = reframeKeyframes(
            original_neighbor,
            neighbor.timeline_start_frame,
            neighbor.timeline_duration_frames,
            neighbor.transform);
        preview.neighbor_location = ClipLocation{
            location.track_index, *neighbor_index};
        preview.neighbor_clip = std::move(neighbor);
    }

    return preview;
}

std::int64_t TimelineModel::trackEnd(const TimelineTrack& track) noexcept {
    std::int64_t end = 0;
    for (const auto& clip : track.clips) {
        if (clip.timeline_start_frame <= std::numeric_limits<std::int64_t>::max() -
                clip.timeline_duration_frames) {
            end = std::max(end, clip.timeline_start_frame + clip.timeline_duration_frames);
        }
    }
    return end;
}

TimelineTrack* TimelineModel::trackAt(std::size_t track_index) noexcept {
    return track_index < tracks_.size() ? &tracks_[track_index] : nullptr;
}

const TimelineTrack* TimelineModel::trackAt(std::size_t track_index) const noexcept {
    return track_index < tracks_.size() ? &tracks_[track_index] : nullptr;
}

AddTrackResult TimelineModel::addTrack(std::string name) {
    if (!validName(name)) return AddTrackResult::InvalidName;
    tracks_.insert(
        tracks_.begin(),
        TimelineTrack{next_track_id_++, std::move(name), 1.0, false, {}});
    assertIdentityInvariants();
    return AddTrackResult::Added;
}

TrackMutationResult TimelineModel::renameTrack(
    std::size_t track_index,
    std::string name) {
    auto* track = trackAt(track_index);
    if (track == nullptr) return TrackMutationResult::InvalidIndex;
    if (!validName(name)) return TrackMutationResult::InvalidName;
    if (track->name == name) return TrackMutationResult::NoChange;
    track->name = std::move(name);
    return TrackMutationResult::Changed;
}

TrackMutationResult TimelineModel::moveTrack(
    std::size_t from_index,
    std::size_t to_index) {
    if (from_index >= tracks_.size() || to_index >= tracks_.size()) {
        return TrackMutationResult::InvalidIndex;
    }
    if (from_index == to_index) return TrackMutationResult::NoChange;
    auto moved = std::move(tracks_[from_index]);
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(from_index));
    tracks_.insert(
        tracks_.begin() + static_cast<std::ptrdiff_t>(to_index),
        std::move(moved));
    for (auto& track : tracks_) {
        for (auto& clip : track.clips) clip.track_id = track.track_id;
    }
    assertIdentityInvariants();
    return TrackMutationResult::Changed;
}

TrackMutationResult TimelineModel::removeTrack(std::size_t track_index) {
    if (track_index >= tracks_.size()) return TrackMutationResult::InvalidIndex;
    if (!tracks_[track_index].clips.empty()) return TrackMutationResult::NotEmpty;
    if (tracks_.size() == 1) return TrackMutationResult::NoChange;
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(track_index));
    assertIdentityInvariants();
    return TrackMutationResult::Changed;
}

AddClipResult TimelineModel::addClip(
    std::size_t track_index,
    const media::VideoMetadata& metadata,
    std::int64_t timeline_start_frame) {
    auto* track = trackAt(track_index);
    if (track == nullptr) return AddClipResult::InvalidTrack;
    const auto duration_frames = durationInFrames(metadata);
    if (!duration_frames.has_value()) return AddClipResult::InvalidTimingMetadata;
    if (timeline_start_frame < 0 ||
        *duration_frames > std::numeric_limits<std::int64_t>::max() - timeline_start_frame) {
        return AddClipResult::InvalidPosition;
    }
    for (const auto& existing : track->clips) {
        if (overlapsSameKind(
                existing, ClipKind::Video, timeline_start_frame, *duration_frames)) {
            return AddClipResult::Overlap;
        }
    }
    TimelineClip clip{
        timeline_start_frame,
        0,
        *duration_frames,
        canonicalPath(metadata.source_path),
        metadata.display_name,
        metadata.duration_seconds,
        metadata.frame_rate,
        metadata.frame_count,
        1.0,
        false,
        next_clip_id_++,
        track->track_id,
        {},
        {},
        metadata.kind == media::MediaKind::Image ? ClipKind::Image : ClipKind::Video,
        {}};
    track->clips.push_back(std::move(clip));
    std::stable_sort(track->clips.begin(), track->clips.end(),
              [](const auto& left, const auto& right) {
                  return left.timeline_start_frame < right.timeline_start_frame;
              });
    assertIdentityInvariants();
    return AddClipResult::Added;
}

AddClipResult TimelineModel::addTextClip(
    std::size_t track_index,
    std::int64_t timeline_start_frame,
    std::int64_t duration_frames,
    double frame_rate) {
    auto* track = trackAt(track_index);
    if (track == nullptr) return AddClipResult::InvalidTrack;
    if (timeline_start_frame < 0 || duration_frames <= 0 ||
        duration_frames > std::numeric_limits<std::int64_t>::max() - timeline_start_frame ||
        !std::isfinite(frame_rate) || frame_rate <= 0.0) {
        return AddClipResult::InvalidPosition;
    }
    for (const auto& existing : track->clips) {
        if (overlapsSameKind(
                existing, ClipKind::Text, timeline_start_frame, duration_frames)) {
            return AddClipResult::Overlap;
        }
    }

    TimelineClip clip;
    clip.timeline_start_frame = timeline_start_frame;
    clip.timeline_duration_frames = duration_frames;
    clip.display_name = "Text";
    clip.duration_seconds = static_cast<double>(duration_frames) / frame_rate;
    clip.frame_rate = frame_rate;
    clip.frame_count = duration_frames;
    clip.clip_id = next_clip_id_++;
    clip.track_id = track->track_id;
    clip.kind = ClipKind::Text;
    track->clips.push_back(std::move(clip));
    std::stable_sort(track->clips.begin(), track->clips.end(),
              [](const auto& left, const auto& right) {
                  return left.timeline_start_frame < right.timeline_start_frame;
              });
    assertIdentityInvariants();
    return AddClipResult::Added;
}

MoveClipResult TimelineModel::moveClip(
    ClipLocation from,
    ClipLocation to,
    std::int64_t timeline_start_frame) {
    auto* source_track = trackAt(from.track_index);
    auto* target_track = trackAt(to.track_index);
    if (source_track == nullptr || from.clip_index >= source_track->clips.size()) {
        return MoveClipResult::InvalidIndex;
    }
    if (target_track == nullptr) return MoveClipResult::InvalidTrack;
    const auto clip = source_track->clips[from.clip_index];
    if (timeline_start_frame < 0 || clip.timeline_duration_frames <= 0 ||
        timeline_start_frame > std::numeric_limits<std::int64_t>::max() -
            clip.timeline_duration_frames) {
        return MoveClipResult::InvalidPosition;
    }
    if (from.track_index == to.track_index && from.clip_index == to.clip_index &&
        clip.timeline_start_frame == timeline_start_frame) {
        return MoveClipResult::NoChange;
    }
    for (std::size_t index = 0; index < target_track->clips.size(); ++index) {
        if (target_track == source_track && index == from.clip_index) continue;
        if (overlapsSameKind(target_track->clips[index], clip.kind,
                             timeline_start_frame, clip.timeline_duration_frames)) {
            return MoveClipResult::Overlap;
        }
    }
    source_track->clips.erase(
        source_track->clips.begin() + static_cast<std::ptrdiff_t>(from.clip_index));
    auto moved = clip;
    moved.timeline_start_frame = timeline_start_frame;
    moved.track_id = target_track->track_id;
    target_track->clips.push_back(std::move(moved));
    std::stable_sort(target_track->clips.begin(), target_track->clips.end(),
              [](const auto& left, const auto& right) {
                  return left.timeline_start_frame < right.timeline_start_frame;
              });
    removeInvalidTransitions(*source_track);
    if (target_track != source_track) removeInvalidTransitions(*target_track);
    assertIdentityInvariants();
    return MoveClipResult::Moved;
}

SplitClipResult TimelineModel::splitClip(
    std::size_t track_index,
    std::size_t clip_index,
    std::int64_t local_frame) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return SplitClipResult::InvalidIndex;
    }
    auto& clip = track->clips[clip_index];
    if (local_frame <= 0 || local_frame >= clip.timeline_duration_frames ||
        clip.source_start_frame < 0 ||
        local_frame > std::numeric_limits<std::int64_t>::max() - clip.source_start_frame) {
        return SplitClipResult::InvalidBoundary;
    }
    const auto original_transform = clip.transform;
    TimelineClip right = clip;
    right.clip_id = next_clip_id_++;
    right.source_start_frame += local_frame;
    right.timeline_start_frame += local_frame;
    right.timeline_duration_frames -= local_frame;
    right.keyframes = splitKeyframes(
        clip.transform,
        clip.keyframes,
        local_frame,
        right.transform);
    clip.keyframes = trimKeyframes(
        clip.transform,
        clip.keyframes,
        0,
        0,
        local_frame,
        clip.transform);
    clip.transform = original_transform;
    clip.timeline_duration_frames = local_frame;
    track->clips.insert(
        track->clips.begin() + static_cast<std::ptrdiff_t>(clip_index + 1),
        std::move(right));
    removeInvalidTransitions(*track);
    assertIdentityInvariants();
    return SplitClipResult::Split;
}

RemoveClipResult TimelineModel::removeClip(
    std::size_t track_index,
    std::size_t clip_index) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return RemoveClipResult::InvalidIndex;
    }
    track->clips.erase(track->clips.begin() + static_cast<std::ptrdiff_t>(clip_index));
    removeInvalidTransitions(*track);
    assertIdentityInvariants();
    return RemoveClipResult::Removed;
}

TrimClipResult TimelineModel::trimClip(
    std::size_t track_index,
    std::size_t clip_index,
    std::int64_t new_source_start_frame,
    std::int64_t new_duration_frames) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return TrimClipResult::InvalidIndex;
    }
    auto& clip = track->clips[clip_index];
    if (clip.source_start_frame < 0 || clip.timeline_duration_frames <= 0 ||
        clip.source_start_frame > std::numeric_limits<std::int64_t>::max() -
            clip.timeline_duration_frames ||
        new_source_start_frame < clip.source_start_frame ||
        new_source_start_frame < 0 || new_duration_frames <= 0 ||
        new_source_start_frame > std::numeric_limits<std::int64_t>::max() -
            new_duration_frames) {
        return TrimClipResult::InvalidRange;
    }
    const auto old_end = clip.source_start_frame + clip.timeline_duration_frames;
    const auto new_end = new_source_start_frame + new_duration_frames;
    if (clip.source_start_frame > old_end || new_source_start_frame >= old_end ||
        new_end > old_end) {
        return TrimClipResult::InvalidRange;
    }
    for (std::size_t index = 0; index < track->clips.size(); ++index) {
        if (index != clip_index && overlapsSameKind(track->clips[index], clip.kind,
                                                    clip.timeline_start_frame,
                                                    new_duration_frames)) {
            return TrimClipResult::InvalidRange;
        }
    }
    const auto local_start = new_source_start_frame - clip.source_start_frame;
    Transform2D trimmed_transform;
    const auto trimmed_keyframes = trimKeyframes(
        clip.transform,
        clip.keyframes,
        0,
        local_start,
        new_duration_frames,
        trimmed_transform);
    clip.source_start_frame = new_source_start_frame;
    clip.timeline_duration_frames = new_duration_frames;
    clip.transform = trimmed_transform;
    clip.keyframes = trimmed_keyframes;
    removeInvalidTransitions(*track);
    return TrimClipResult::Trimmed;
}

TrimClipResult TimelineModel::trimClipEdge(
    std::size_t track_index,
    std::size_t clip_index,
    ClipEdge edge,
    std::int64_t boundary_frame,
    ClipEdgeEditMode mode) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return TrimClipResult::InvalidIndex;
    }
    const auto preview = previewClipEdgeEdit(
        tracks_, ClipLocation{track_index, clip_index}, edge, boundary_frame, mode);
    if (!preview.has_value()) return TrimClipResult::InvalidRange;

    const bool clip_changed = preview->clip != track->clips[clip_index];
    const bool neighbor_changed = preview->neighbor_location.has_value() &&
        preview->neighbor_clip.has_value() &&
        *preview->neighbor_clip != track->clips[
            preview->neighbor_location->clip_index];
    if (!clip_changed && !neighbor_changed) return TrimClipResult::NoChange;

    track->clips[clip_index] = preview->clip;
    if (preview->neighbor_location.has_value() && preview->neighbor_clip.has_value()) {
        track->clips[preview->neighbor_location->clip_index] = *preview->neighbor_clip;
    }
    std::stable_sort(track->clips.begin(), track->clips.end(),
              [](const auto& left, const auto& right) {
                  return left.timeline_start_frame < right.timeline_start_frame;
              });
    removeInvalidTransitions(*track);
    return TrimClipResult::Trimmed;
}

AddClipResult TimelineModel::addClip(const media::VideoMetadata& metadata) {
    const auto* track = trackAt(0);
    return addClip(0, metadata, track == nullptr ? 0 : trackEnd(*track));
}

MoveClipResult TimelineModel::moveClip(std::size_t from_index, std::size_t to_index) {
    if (from_index >= clipCount() || to_index >= clipCount()) {
        return MoveClipResult::InvalidIndex;
    }
    auto* track = trackAt(0);
    if (from_index == to_index) return MoveClipResult::NoChange;
    if (from_index < to_index) {
        std::rotate(track->clips.begin() + static_cast<std::ptrdiff_t>(from_index),
                    track->clips.begin() + static_cast<std::ptrdiff_t>(from_index + 1),
                    track->clips.begin() + static_cast<std::ptrdiff_t>(to_index + 1));
    } else {
        std::rotate(track->clips.begin() + static_cast<std::ptrdiff_t>(to_index),
                    track->clips.begin() + static_cast<std::ptrdiff_t>(from_index),
                    track->clips.begin() + static_cast<std::ptrdiff_t>(from_index + 1));
    }
    std::int64_t start = 0;
    for (auto& clip : track->clips) {
        clip.timeline_start_frame = start;
        start += clip.timeline_duration_frames;
    }
    return MoveClipResult::Moved;
}

SplitClipResult TimelineModel::splitClip(std::size_t clip_index, std::int64_t local_frame) {
    return splitClip(0, clip_index, local_frame);
}

RemoveClipResult TimelineModel::removeClip(std::size_t clip_index) {
    return removeClip(0, clip_index);
}

TrimClipResult TimelineModel::trimClip(
    std::size_t clip_index,
    std::int64_t new_source_start_frame,
    std::int64_t new_duration_frames) {
    return trimClip(0, clip_index, new_source_start_frame, new_duration_frames);
}

void TimelineModel::clear() noexcept {
    for (auto& track : tracks_) {
        track.clips.clear();
        track.transitions.clear();
    }
}

bool TimelineModel::hasClip() const noexcept {
    return std::any_of(tracks_.begin(), tracks_.end(),
                       [](const auto& track) { return !track.clips.empty(); });
}

std::size_t TimelineModel::clipCount() const noexcept {
    std::size_t count = 0;
    for (const auto& track : tracks_) count += track.clips.size();
    return count;
}

std::size_t TimelineModel::clipCount(std::size_t track_index) const noexcept {
    const auto* track = trackAt(track_index);
    return track == nullptr ? 0 : track->clips.size();
}

std::size_t TimelineModel::trackCount() const noexcept {
    return tracks_.size();
}

std::int64_t TimelineModel::totalDurationFrames() const noexcept {
    std::int64_t end = 0;
    for (const auto& track : tracks_) end = std::max(end, trackEnd(track));
    return end;
}

const std::vector<TimelineTrack>& TimelineModel::tracks() const noexcept {
    return tracks_;
}

std::optional<std::size_t> TimelineModel::firstClipIndexForSource(
    const std::filesystem::path& source_path) const {
    if (tracks_.empty()) return std::nullopt;
    const auto canonical_source = canonicalPath(source_path);
    for (std::size_t index = 0; index < tracks_.front().clips.size(); ++index) {
        if (canonicalPath(tracks_.front().clips[index].source_path) == canonical_source) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<ClipLocation> TimelineModel::clipAt(
    std::size_t track_index,
    std::int64_t timeline_frame) const {
    const auto* track = trackAt(track_index);
    if (track == nullptr || timeline_frame < 0) return std::nullopt;
    std::optional<ClipLocation> video_match;
    for (std::size_t index = 0; index < track->clips.size(); ++index) {
        const auto& clip = track->clips[index];
        if (timeline_frame >= clip.timeline_start_frame &&
            timeline_frame < clip.timeline_start_frame + clip.timeline_duration_frames) {
            if (clip.kind == ClipKind::Text) return ClipLocation{track_index, index};
            video_match = ClipLocation{track_index, index};
        }
    }
    return video_match;
}

std::optional<ClipLocation> TimelineModel::topClipAt(std::int64_t timeline_frame) const {
    for (std::size_t track = 0; track < tracks_.size(); ++track) {
        if (const auto location = clipAt(track, timeline_frame); location.has_value()) {
            return location;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> TimelineModel::locateTrack(TrackId track_id) const {
    if (track_id == 0) return std::nullopt;
    for (std::size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
        if (tracks_[track_index].track_id == track_id) return track_index;
    }
    return std::nullopt;
}

std::optional<ClipLocation> TimelineModel::locateClip(ClipId clip_id) const {
    for (std::size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
        for (std::size_t clip_index = 0; clip_index < tracks_[track_index].clips.size(); ++clip_index) {
            if (tracks_[track_index].clips[clip_index].clip_id == clip_id) {
                return ClipLocation{track_index, clip_index};
            }
        }
    }
    return std::nullopt;
}

const TimelineTransition* TimelineModel::transitionBetween(
    std::size_t track_index,
    std::size_t from_clip_index,
    std::size_t to_clip_index) const noexcept {
    const auto* track = trackAt(track_index);
    if (track == nullptr || from_clip_index >= track->clips.size() ||
        to_clip_index >= track->clips.size()) {
        return nullptr;
    }
    const auto from_id = track->clips[from_clip_index].clip_id;
    const auto to_id = track->clips[to_clip_index].clip_id;
    const auto found = std::find_if(
        track->transitions.begin(),
        track->transitions.end(),
        [from_id, to_id](const TimelineTransition& transition) {
            return transition.from_clip_id == from_id &&
                transition.to_clip_id == to_id;
        });
    return found == track->transitions.end() ? nullptr : &*found;
}

TimelineModel::Snapshot TimelineModel::snapshot() const {
    assertIdentityInvariants();
    Snapshot result;
    result.tracks = tracks_;
    result.next_track_id = next_track_id_;
    result.next_clip_id = next_clip_id_;
    return result;
}

void TimelineModel::ensureIdentifiers() {
    TrackId max_track = 0;
    ClipId max_clip = 0;
    for (auto& track : tracks_) {
        if (track.track_id == 0) track.track_id = next_track_id_++;
        max_track = std::max(max_track, track.track_id);
        for (auto& clip : track.clips) {
            clip.track_id = track.track_id;
            if (clip.clip_id == 0) clip.clip_id = next_clip_id_++;
            max_clip = std::max(max_clip, clip.clip_id);
        }
    }
    next_track_id_ = std::max(next_track_id_, max_track + 1);
    next_clip_id_ = std::max(next_clip_id_, max_clip + 1);
}

void TimelineModel::restore(Snapshot snapshot) {
    tracks_ = std::move(snapshot.tracks);
    next_track_id_ = snapshot.next_track_id;
    next_clip_id_ = snapshot.next_clip_id;
    ensureIdentifiers();
    if (tracks_.empty()) {
        tracks_.push_back({next_track_id_++, "Video 1", 1.0, false, {}});
    }
    for (auto& track : tracks_) removeInvalidTransitions(track);
    assertIdentityInvariants();
}

void TimelineModel::assertIdentityInvariants() const {
#ifndef NDEBUG
    std::unordered_set<TrackId> track_ids;
    std::unordered_set<ClipId> clip_ids;
    for (const auto& track : tracks_) {
        assert(track.track_id != 0);
        assert(track_ids.insert(track.track_id).second);
        for (const auto& clip : track.clips) {
            assert(clip.clip_id != 0);
            assert(clip.track_id == track.track_id);
            assert(clip_ids.insert(clip.clip_id).second);
        }
    }
#endif
}

std::optional<std::pair<std::size_t, std::size_t>>
TimelineModel::transitionClipIndexes(
    const TimelineTrack& track,
    const TimelineTransition& transition) noexcept {
    std::optional<std::size_t> from_index;
    std::optional<std::size_t> to_index;
    for (std::size_t index = 0; index < track.clips.size(); ++index) {
        if (track.clips[index].clip_id == transition.from_clip_id) {
            from_index = index;
        }
        if (track.clips[index].clip_id == transition.to_clip_id) {
            to_index = index;
        }
    }
    if (!from_index.has_value() || !to_index.has_value()) return std::nullopt;
    return std::make_pair(*from_index, *to_index);
}

void TimelineModel::removeInvalidTransitions(TimelineTrack& track) noexcept {
    track.transitions.erase(
        std::remove_if(
            track.transitions.begin(),
            track.transitions.end(),
            [&track](const TimelineTransition& transition) {
                if (!validTransitionKind(transition.kind) ||
                    transition.duration_frames <= 0) {
                    return true;
                }
                const auto indexes = transitionClipIndexes(track, transition);
                if (!indexes.has_value() || indexes->second != indexes->first + 1) {
                    return true;
                }
                const auto& from = track.clips[indexes->first];
                const auto& to = track.clips[indexes->second];
                if (from.timeline_start_frame >
                        std::numeric_limits<std::int64_t>::max() -
                            from.timeline_duration_frames ||
                    from.timeline_start_frame + from.timeline_duration_frames !=
                        to.timeline_start_frame) {
                    return true;
                }
                const auto maximum = std::min(
                    from.timeline_duration_frames,
                    to.timeline_duration_frames);
                return maximum <= 0 || transition.duration_frames > maximum;
            }),
        track.transitions.end());
}

void TimelineModel::updateDisplayNameForSource(
    const std::filesystem::path& source_path,
    const std::string& display_name) {
    const auto canonical_source = canonicalPath(source_path);
    for (auto& track : tracks_) {
        for (auto& clip : track.clips) {
            if (isMediaClipKind(clip.kind) &&
                canonicalPath(clip.source_path) == canonical_source) {
                clip.display_name = display_name;
            }
        }
    }
}

AudioParameterResult TimelineModel::setClipAudio(
    std::size_t track_index,
    std::size_t clip_index,
    double gain,
    bool muted) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return AudioParameterResult::InvalidIndex;
    }
    if (!validAudioGain(gain)) return AudioParameterResult::InvalidValue;
    auto& clip = track->clips[clip_index];
    if (clip.audio_gain == gain && clip.audio_muted == muted) {
        return AudioParameterResult::NoChange;
    }
    clip.audio_gain = gain;
    clip.audio_muted = muted;
    return AudioParameterResult::Changed;
}

AudioParameterResult TimelineModel::setTrackAudio(
    std::size_t track_index,
    double gain,
    bool muted) {
    auto* track = trackAt(track_index);
    if (track == nullptr) return AudioParameterResult::InvalidIndex;
    if (!validAudioGain(gain)) return AudioParameterResult::InvalidValue;
    if (track->audio_gain == gain && track->audio_muted == muted) {
        return AudioParameterResult::NoChange;
    }
    track->audio_gain = gain;
    track->audio_muted = muted;
    return AudioParameterResult::Changed;
}

TransformParameterResult TimelineModel::setClipTransform(
    std::size_t track_index,
    std::size_t clip_index,
    const Transform2D& transform) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return TransformParameterResult::InvalidIndex;
    }
    if (!validTransform(transform)) return TransformParameterResult::InvalidValue;
    auto& clip = track->clips[clip_index];
    if (clip.transform == transform) return TransformParameterResult::NoChange;
    clip.transform = transform;
    return TransformParameterResult::Changed;
}

TransformParameterResult TimelineModel::setClipKeyframe(
    std::size_t track_index,
    std::size_t clip_index,
    TransformProperty property,
    std::int64_t local_frame,
    double value) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return TransformParameterResult::InvalidIndex;
    }
    const auto& clip = track->clips[clip_index];
    if (local_frame < 0 || local_frame >= clip.timeline_duration_frames ||
        !validKeyframeValue(property, value)) {
        return TransformParameterResult::InvalidValue;
    }
    auto& mutable_clip = track->clips[clip_index];
    const auto before = mutable_clip.keyframes;
    if (!setKeyframe(mutable_clip.keyframes, property, local_frame, value)) {
        return TransformParameterResult::InvalidValue;
    }
    return before == mutable_clip.keyframes
        ? TransformParameterResult::NoChange
        : TransformParameterResult::Changed;
}

TransformParameterResult TimelineModel::removeClipKeyframe(
    std::size_t track_index,
    std::size_t clip_index,
    TransformProperty property,
    std::int64_t local_frame) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return TransformParameterResult::InvalidIndex;
    }
    if (!removeKeyframe(track->clips[clip_index].keyframes, property, local_frame)) {
        return TransformParameterResult::NoChange;
    }
    return TransformParameterResult::Changed;
}

TextParameterResult TimelineModel::setClipText(
    std::size_t track_index,
    std::size_t clip_index,
    const TextStyle& text) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return TextParameterResult::InvalidIndex;
    }
    if (track->clips[clip_index].kind != ClipKind::Text || !validTextStyle(text)) {
        return TextParameterResult::InvalidValue;
    }
    auto& clip = track->clips[clip_index];
    if (clip.text == text) return TextParameterResult::NoChange;
    clip.text = text;
    clip.display_name = text.content.empty() ? "Text" : text.content;
    return TextParameterResult::Changed;
}

TransitionMutationResult TimelineModel::addTransition(
    std::size_t track_index,
    std::size_t from_clip_index,
    std::size_t to_clip_index,
    TransitionKind kind,
    std::int64_t duration_frames) {
    auto* track = trackAt(track_index);
    if (track == nullptr || from_clip_index >= track->clips.size() ||
        to_clip_index >= track->clips.size()) {
        return TransitionMutationResult::InvalidIndex;
    }
    if (!validTransitionKind(kind) || from_clip_index + 1 != to_clip_index) {
        return TransitionMutationResult::InvalidBoundary;
    }
    const auto& from = track->clips[from_clip_index];
    const auto& to = track->clips[to_clip_index];
    if (from.timeline_start_frame >
            std::numeric_limits<std::int64_t>::max() - from.timeline_duration_frames ||
        from.timeline_start_frame + from.timeline_duration_frames !=
            to.timeline_start_frame) {
        return TransitionMutationResult::InvalidBoundary;
    }
    const auto maximum = std::min(from.timeline_duration_frames,
                                  to.timeline_duration_frames);
    if (duration_frames <= 0 || maximum <= 0 || duration_frames > maximum) {
        return TransitionMutationResult::InvalidRange;
    }
    if (const auto* existing = transitionBetween(
            track_index, from_clip_index, to_clip_index);
        existing != nullptr) {
        return TransitionMutationResult::NoChange;
    }
    track->transitions.push_back(TimelineTransition{
        from.clip_id,
        to.clip_id,
        kind,
        duration_frames});
    return TransitionMutationResult::Added;
}

TransitionMutationResult TimelineModel::updateTransition(
    std::size_t track_index,
    std::size_t from_clip_index,
    std::size_t to_clip_index,
    TransitionKind kind,
    std::int64_t duration_frames) {
    auto* track = trackAt(track_index);
    if (track == nullptr || from_clip_index >= track->clips.size() ||
        to_clip_index >= track->clips.size()) {
        return TransitionMutationResult::InvalidIndex;
    }
    if (!validTransitionKind(kind) || from_clip_index + 1 != to_clip_index) {
        return TransitionMutationResult::InvalidBoundary;
    }
    const auto& from = track->clips[from_clip_index];
    const auto& to = track->clips[to_clip_index];
    if (from.timeline_start_frame >
            std::numeric_limits<std::int64_t>::max() - from.timeline_duration_frames ||
        from.timeline_start_frame + from.timeline_duration_frames !=
            to.timeline_start_frame) {
        return TransitionMutationResult::InvalidBoundary;
    }
    const auto maximum = std::min(from.timeline_duration_frames,
                                  to.timeline_duration_frames);
    if (duration_frames <= 0 || maximum <= 0 || duration_frames > maximum) {
        return TransitionMutationResult::InvalidRange;
    }
    const auto from_id = from.clip_id;
    const auto to_id = to.clip_id;
    const auto found = std::find_if(
        track->transitions.begin(),
        track->transitions.end(),
        [from_id, to_id](const TimelineTransition& transition) {
            return transition.from_clip_id == from_id &&
                transition.to_clip_id == to_id;
        });
    if (found == track->transitions.end()) return TransitionMutationResult::NotFound;
    if (found->kind == kind && found->duration_frames == duration_frames) {
        return TransitionMutationResult::NoChange;
    }
    found->kind = kind;
    found->duration_frames = duration_frames;
    return TransitionMutationResult::Updated;
}

TransitionMutationResult TimelineModel::removeTransition(
    std::size_t track_index,
    std::size_t from_clip_index,
    std::size_t to_clip_index) {
    auto* track = trackAt(track_index);
    if (track == nullptr || from_clip_index >= track->clips.size() ||
        to_clip_index >= track->clips.size()) {
        return TransitionMutationResult::InvalidIndex;
    }
    const auto from_id = track->clips[from_clip_index].clip_id;
    const auto to_id = track->clips[to_clip_index].clip_id;
    const auto found = std::find_if(
        track->transitions.begin(),
        track->transitions.end(),
        [from_id, to_id](const TimelineTransition& transition) {
            return transition.from_clip_id == from_id &&
                transition.to_clip_id == to_id;
        });
    if (found == track->transitions.end()) return TransitionMutationResult::NotFound;
    track->transitions.erase(found);
    return TransitionMutationResult::Removed;
}

} // namespace timeline
