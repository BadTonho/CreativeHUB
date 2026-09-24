#include "timeline_geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace timeline {
namespace {

const TimelineClip& displayedClip(
    const std::vector<TimelineTrack>& tracks,
    ClipLocation location,
    const std::optional<ClipEdgeEditPreview>& preview) noexcept {
    static const TimelineClip empty;
    if (preview.has_value()) {
        if (preview->clip_location == location) return preview->clip;
        if (preview->neighbor_location == location && preview->neighbor_clip.has_value()) {
            return *preview->neighbor_clip;
        }
    }
    if (location.track_index >= tracks.size() ||
        location.clip_index >= tracks[location.track_index].clips.size()) {
        return empty;
    }
    return tracks[location.track_index].clips[location.clip_index];
}

} // namespace

TimelineGeometry::TimelineGeometry(
    const std::vector<TimelineTrack>& tracks,
    QSizeF bounds,
    double row_height,
    double zoom_factor,
    std::optional<std::int64_t> fixed_duration) noexcept
    : tracks_(tracks),
      bounds_(bounds),
      row_height_(std::max(0.0, row_height)),
      zoom_factor_(std::isfinite(zoom_factor) && zoom_factor > 0.0 ? zoom_factor : 1.0),
      fixed_duration_(fixed_duration) {}

double TimelineGeometry::frameRate() const noexcept {
    for (const auto& track : tracks_) {
        for (const auto& clip : track.clips) {
            if (clip.frame_rate.has_value() && std::isfinite(*clip.frame_rate) &&
                *clip.frame_rate > 0.0) {
                return *clip.frame_rate;
            }
        }
    }
    return 30.0;
}

std::int64_t TimelineGeometry::totalDuration() const noexcept {
    auto duration = std::int64_t{0};
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    for (const auto& track : tracks_) {
        for (const auto& clip : track.clips) {
            if (clip.timeline_duration_frames <= 0 || clip.timeline_start_frame < 0 ||
                clip.timeline_start_frame > maximum - clip.timeline_duration_frames) {
                continue;
            }
            duration = std::max(
                duration, clip.timeline_start_frame + clip.timeline_duration_frames);
        }
    }
    return duration;
}

std::int64_t TimelineGeometry::standardDuration() const noexcept {
    const auto raw = std::ceil(static_cast<long double>(frameRate()) *
                               standard_duration_seconds);
    return raw >= static_cast<long double>(std::numeric_limits<std::int64_t>::max())
        ? std::numeric_limits<std::int64_t>::max()
        : std::max<std::int64_t>(1, static_cast<std::int64_t>(raw));
}

std::int64_t TimelineGeometry::displayDuration() const noexcept {
    if (fixed_duration_.has_value() && *fixed_duration_ > 0) return *fixed_duration_;
    const auto standard = standardDuration();
    const auto zoom_duration = std::max<long double>(
        1.0L,
        std::ceil(static_cast<long double>(standard) /
                  static_cast<long double>(zoom_factor_)));
    const auto max_duration = static_cast<long double>(
        std::numeric_limits<std::int64_t>::max());
    return std::max(
        totalDuration(),
        static_cast<std::int64_t>(std::min(zoom_duration, max_duration)));
}

QRectF TimelineGeometry::trackRect(std::size_t index) const noexcept {
    const double width = std::max(
        0.0, bounds_.width() - left_margin - right_margin);
    return QRectF(
        left_margin,
        top_margin + static_cast<double>(index) * (row_height_ + row_gap),
        width,
        row_height_);
}

QRectF TimelineGeometry::rulerRect() const noexcept {
    return QRectF(
        left_margin + track_header_width,
        12.0,
        std::max(0.0, bounds_.width() - left_margin - right_margin - track_header_width),
        25.0);
}

QRectF TimelineGeometry::trackContentRect(std::size_t index) const noexcept {
    return trackRect(index).adjusted(track_header_width, 0.0, -6.0, 0.0);
}

QRectF TimelineGeometry::clipRect(
    const TimelineClip& clip,
    std::size_t track_index) const noexcept {
    const auto duration = displayDuration();
    if (duration <= 0) return {};
    const auto content = trackContentRect(track_index);
    const double begin = static_cast<double>(clip.timeline_start_frame) /
        static_cast<double>(duration);
    const double end = static_cast<double>(clip.timeline_start_frame +
        clip.timeline_duration_frames) / static_cast<double>(duration);
    return QRectF(
        content.left() + content.width() * begin,
        content.top(),
        std::max(2.0, content.width() * (end - begin)),
        content.height());
}

double TimelineGeometry::pixelsPerFrame() const noexcept {
    const auto content = trackContentRect(0);
    const auto duration = displayDuration();
    if (content.width() <= 0.0 || duration <= 0) return 0.0;
    return content.width() / static_cast<double>(duration);
}

std::optional<std::int64_t> TimelineGeometry::frameAtContentX(double x) const noexcept {
    const auto duration = displayDuration();
    const auto content = trackContentRect(0);
    if (content.width() <= 0.0 || x < content.left() || x > content.right()) {
        return std::nullopt;
    }
    if (duration <= 0) return 0;
    const double fraction = std::clamp(
        (x - content.left()) / content.width(), 0.0, 1.0);
    return std::clamp<std::int64_t>(
        static_cast<std::int64_t>(std::llround(fraction * duration)), 0, duration);
}

std::optional<std::int64_t> TimelineGeometry::playheadFrameAtRulerX(double x) const noexcept {
    const auto frame = frameAtContentX(x);
    if (!frame.has_value()) return std::nullopt;
    const auto total = totalDuration();
    if (total <= 0) return std::nullopt;
    return std::clamp<std::int64_t>(*frame, 0, total - 1);
}

double TimelineGeometry::contentXForFrame(std::int64_t frame) const noexcept {
    const auto content = trackContentRect(0);
    const auto duration = displayDuration();
    if (content.width() <= 0.0 || duration <= 0) return content.left();
    const auto bounded = std::clamp<std::int64_t>(frame, 0, duration);
    return content.left() + content.width() * static_cast<double>(bounded) /
        static_cast<double>(duration);
}

std::optional<std::size_t> TimelineHitTester::trackAt(
    const TimelineGeometry& geometry,
    std::size_t track_count,
    double y) noexcept {
    for (std::size_t index = 0; index < track_count; ++index) {
        if (geometry.trackRect(index).contains(QPointF(TimelineGeometry::left_margin, y))) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<ClipLocation> TimelineHitTester::clipAt(
    const std::vector<TimelineTrack>& tracks,
    const TimelineGeometry& geometry,
    double x,
    double y,
    const std::optional<ClipEdgeEditPreview>& preview) noexcept {
    const auto track_index = trackAt(geometry, tracks.size(), y);
    if (!track_index.has_value() || geometry.displayDuration() <= 0) return std::nullopt;
    const QPointF position(x, y);
    std::optional<ClipLocation> media_match;
    for (std::size_t clip_index = 0;
         clip_index < tracks[*track_index].clips.size(); ++clip_index) {
        const ClipLocation location{*track_index, clip_index};
        const auto& clip = displayedClip(tracks, location, preview);
        if (!geometry.clipRect(clip, *track_index).contains(position)) continue;
        if (clip.kind == ClipKind::Text) return location;
        media_match = location;
    }
    return media_match;
}

std::optional<std::pair<std::size_t, std::size_t>> TimelineHitTester::transitionPairAt(
    const std::vector<TimelineTrack>& tracks,
    const TimelineGeometry& geometry,
    double x,
    double y,
    const std::optional<ClipEdgeEditPreview>& preview) noexcept {
    const auto track_index = trackAt(geometry, tracks.size(), y);
    if (!track_index.has_value()) return std::nullopt;
    const auto& track = tracks[*track_index];
    const auto duration = geometry.displayDuration();
    if (duration <= 0) return std::nullopt;
    const auto content = geometry.trackContentRect(*track_index);
    for (std::size_t from_index = 0; from_index + 1 < track.clips.size(); ++from_index) {
        const auto& from = displayedClip(tracks, {*track_index, from_index}, preview);
        const auto& to = displayedClip(tracks, {*track_index, from_index + 1}, preview);
        if (from.timeline_start_frame > std::numeric_limits<std::int64_t>::max() -
                from.timeline_duration_frames ||
            from.timeline_start_frame + from.timeline_duration_frames != to.timeline_start_frame) {
            continue;
        }
        const double boundary = content.left() + content.width() *
            static_cast<double>(to.timeline_start_frame) / static_cast<double>(duration);
        const auto transition = std::find_if(
            track.transitions.begin(), track.transitions.end(), [&from, &to](const auto& value) {
                return value.from_clip_id == from.clip_id && value.to_clip_id == to.clip_id;
            });
        const double tolerance = transition == track.transitions.end()
            ? 8.0
            : std::max(8.0, content.width() *
                static_cast<double>(transition->duration_frames) / static_cast<double>(duration));
        if (std::abs(x - boundary) <= tolerance) {
            return std::make_pair(from_index, from_index + 1);
        }
    }
    return std::nullopt;
}

} // namespace timeline
