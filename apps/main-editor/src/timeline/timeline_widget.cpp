#include "timeline_widget.h"

#include "../ui/media_drag_mime.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QMimeData>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace timeline {
namespace {

constexpr double left_margin = 12.0;
constexpr double right_margin = 12.0;
constexpr double top_margin = 48.0;
constexpr double minimum_row_height = 72.0;
constexpr double maximum_row_height = 180.0;
constexpr double row_gap = 10.0;
constexpr double track_header_width = 142.0;
constexpr double edge_width = 8.0;
constexpr double standard_timeline_duration_seconds = 60.0 * 60.0;

QString text(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

QString clipDuration(const TimelineClip& clip) {
    if (clip.frame_rate.has_value() && std::isfinite(*clip.frame_rate) &&
        *clip.frame_rate > 0.0 && clip.timeline_duration_frames > 0) {
        return QString::number(
            static_cast<double>(clip.timeline_duration_frames) / *clip.frame_rate,
            'f',
            3) + " s";
    }
    return "Unknown duration";
}

} // namespace

TimelineWidget::TimelineWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAcceptDrops(true);
    setMouseTracking(true);
    setContextMenuPolicy(Qt::DefaultContextMenu);
}

void TimelineWidget::setTracks(const std::vector<TimelineTrack>& tracks) {
    tracks_ = tracks;
    if (tracks_.empty()) tracks_.push_back(TimelineTrack{1, "Video 1", 1.0, false, {}});
    if (active_clip_.has_value() &&
        (active_clip_->track_index >= tracks_.size() ||
         active_clip_->clip_index >= tracks_[active_clip_->track_index].clips.size())) {
        active_clip_.reset();
        playhead_frame_ = 0;
    }
    if (selected_transition_.has_value()) {
        const auto& selection = *selected_transition_;
        bool exists = false;
        if (selection.track_index < tracks_.size() &&
            selection.from_clip_index < tracks_[selection.track_index].clips.size() &&
            selection.to_clip_index < tracks_[selection.track_index].clips.size()) {
            const auto from_id = tracks_[selection.track_index]
                .clips[selection.from_clip_index].clip_id;
            const auto to_id = tracks_[selection.track_index]
                .clips[selection.to_clip_index].clip_id;
            exists = std::any_of(
                tracks_[selection.track_index].transitions.begin(),
                tracks_[selection.track_index].transitions.end(),
                [from_id, to_id](const TimelineTransition& transition) {
                    return transition.from_clip_id == from_id &&
                        transition.to_clip_id == to_id;
                });
        }
        if (!exists) selected_transition_.reset();
    }
    setMinimumHeight(static_cast<int>(top_margin +
        tracks_.size() * minimum_row_height +
        (tracks_.size() > 0 ? tracks_.size() - 1 : 0) * row_gap + 12.0));
    updateHorizontalExtent();
    moving_active_ = false;
    move_pending_ = false;
    trimming_ = false;
    dragging_ = false;
    seek_pending_ = false;
    drag_frame_.reset();
    drag_hovering_ = false;
    drop_hover_track_.reset();
    drop_hover_frame_.reset();
    update();
}

void TimelineWidget::setClips(const std::vector<TimelineClip>& clips) {
    TimelineTrack track{1, "Video 1", 1.0, false, clips};
    setTracks({track});
}

void TimelineWidget::clearClips() {
    tracks_.clear();
    tracks_.push_back(TimelineTrack{1, "Video 1", 1.0, false, {}});
    setMinimumHeight(static_cast<int>(top_margin + minimum_row_height + 12.0));
    updateHorizontalExtent();
    active_clip_.reset();
    playhead_frame_ = 0;
    drag_frame_.reset();
    moving_active_ = false;
    move_pending_ = false;
    trimming_ = false;
    dragging_ = false;
    seek_pending_ = false;
    drag_hovering_ = false;
    drop_hover_track_.reset();
    drop_hover_frame_.reset();
    selected_transition_.reset();
    update();
}

void TimelineWidget::setActiveClip(std::optional<ClipLocation> location) {
    if (location.has_value() &&
        (location->track_index >= tracks_.size() ||
         location->clip_index >= tracks_[location->track_index].clips.size())) {
        location.reset();
    }
    active_clip_ = location;
    drag_frame_.reset();
    update();
}

void TimelineWidget::setActiveClipIndex(std::optional<std::size_t> clip_index) {
    if (!clip_index.has_value()) {
        setActiveClip(std::nullopt);
    } else {
        setActiveClip(ClipLocation{0, *clip_index});
    }
}

void TimelineWidget::setPlayheadFrame(std::int64_t frame_index) {
    playhead_frame_ = std::max<std::int64_t>(0, frame_index);
    const auto total = totalDuration();
    if (total > 0) {
        playhead_frame_ = std::min(playhead_frame_, total - 1);
    }
    drag_frame_.reset();
    update();
}

void TimelineWidget::setRazorMode(bool enabled) {
    razor_mode_ = enabled;
    razor_clicking_ = false;
    update();
}

bool TimelineWidget::razorMode() const noexcept {
    return razor_mode_;
}

void TimelineWidget::setMoveRequiresAlt(bool enabled) {
    move_requires_alt_ = enabled;
    if (moving_active_ || move_pending_) {
        move_pending_ = false;
        moving_active_ = false;
        move_target_track_.reset();
        releaseMouse();
    }
    update();
}

bool TimelineWidget::moveRequiresAlt() const noexcept {
    return move_requires_alt_;
}

QString TimelineWidget::formatTimecode(std::int64_t frame, double frame_rate) {
    if (!std::isfinite(frame_rate) || frame_rate <= 0.0) frame_rate = 30.0;
    const auto nonnegative_frame = std::max<std::int64_t>(0, frame);
    const auto milliseconds_value = std::clamp<long double>(
        std::round(
            static_cast<long double>(nonnegative_frame) * 1000.0L /
            static_cast<long double>(frame_rate)),
        0.0L,
        static_cast<long double>(std::numeric_limits<std::int64_t>::max()));
    const auto milliseconds = static_cast<std::int64_t>(milliseconds_value);
    const auto hours = milliseconds / (60 * 60 * 1000);
    const auto minutes = (milliseconds / (60 * 1000)) % 60;
    const auto seconds = (milliseconds / 1000) % 60;
    const auto remainder = milliseconds % 1000;
    return QString("%1:%2:%3.%4")
        .arg(static_cast<qlonglong>(hours), 2, 10, QChar('0'))
        .arg(static_cast<int>(minutes), 2, 10, QChar('0'))
        .arg(static_cast<int>(seconds), 2, 10, QChar('0'))
        .arg(static_cast<int>(remainder), 3, 10, QChar('0'));
}

void TimelineWidget::setTimelineViewportWidth(int width) {
    const auto normalized_width = std::max(0, width);
    if (timeline_viewport_width_ == normalized_width) return;
    timeline_viewport_width_ = normalized_width;
    updateHorizontalExtent();
}

bool TimelineWidget::eventFilter(QObject* watched, QEvent* event) {
    Q_UNUSED(watched);
    if (event != nullptr && event->type() == QEvent::Resize) {
        const auto* resize_event = static_cast<const QResizeEvent*>(event);
        setTimelineViewportWidth(resize_event->size().width());
    }
    return QWidget::eventFilter(watched, event);
}

QRectF TimelineWidget::trackRect(std::size_t index) const noexcept {
    const double width = std::max(0.0,
        static_cast<double>(this->width()) - left_margin - right_margin);
    const auto current_row_height = rowHeight();
    return QRectF(
        left_margin,
        top_margin + static_cast<double>(index) * (current_row_height + row_gap),
        width,
        current_row_height);
}

double TimelineWidget::rowHeight() const noexcept {
    const auto track_count = std::max<std::size_t>(1, tracks_.size());
    const double available = static_cast<double>(height()) - top_margin - 12.0 -
        static_cast<double>(track_count - 1) * row_gap;
    if (available <= 0.0) return minimum_row_height;
    return std::min(
        maximum_row_height,
        std::max(
        minimum_row_height,
        available / static_cast<double>(track_count)));
}

QRectF TimelineWidget::trackContentRect(std::size_t index) const noexcept {
    return trackRect(index).adjusted(track_header_width, 22, -6, -6);
}

QRectF TimelineWidget::clipRect(const ClipLocation& location) const noexcept {
    if (location.track_index >= tracks_.size() ||
        location.clip_index >= tracks_[location.track_index].clips.size()) {
        return {};
    }
    const auto total = displayDuration();
    if (total <= 0) return {};
    const auto& clip = tracks_[location.track_index].clips[location.clip_index];
    const auto track = trackContentRect(location.track_index);
    const double begin = static_cast<double>(clip.timeline_start_frame) / total;
    const double end = static_cast<double>(
        clip.timeline_start_frame + clip.timeline_duration_frames) / total;
    return QRectF(
        track.left() + track.width() * begin,
        track.top(),
        std::max(2.0, track.width() * (end - begin)),
        track.height());
}

double TimelineWidget::frameRate() const noexcept {
    for (const auto& track : tracks_) {
        for (const auto& clip : track.clips) {
            if (clip.frame_rate.has_value() &&
                std::isfinite(*clip.frame_rate) && *clip.frame_rate > 0.0) {
                return *clip.frame_rate;
            }
        }
    }
    return 30.0;
}

std::int64_t TimelineWidget::standardDuration() const noexcept {
    return std::max<std::int64_t>(
        1,
        static_cast<std::int64_t>(std::ceil(
            frameRate() * standard_timeline_duration_seconds)));
}

std::int64_t TimelineWidget::displayDuration() const noexcept {
    return std::max(totalDuration(), standardDuration());
}

std::int64_t TimelineWidget::totalDuration() const noexcept {
    std::int64_t result = 0;
    for (const auto& track : tracks_) {
        for (const auto& clip : track.clips) {
            result = std::max(result,
                clip.timeline_start_frame + clip.timeline_duration_frames);
        }
    }
    return result;
}

void TimelineWidget::updateHorizontalExtent() {
    const auto base_width = std::max(
        1,
        timeline_viewport_width_ > 0 ? timeline_viewport_width_ : width());
    const auto standard = standardDuration();
    const auto visual_duration = displayDuration();
    const auto scaled_width = std::ceil(
        static_cast<long double>(base_width) *
        static_cast<long double>(visual_duration) /
        static_cast<long double>(standard));
    const auto max_width = static_cast<long double>(std::numeric_limits<int>::max());
    const auto required_width = static_cast<int>(std::min(scaled_width, max_width));
    setMinimumWidth(std::max(base_width, required_width));
    updateGeometry();
}

std::optional<std::size_t> TimelineWidget::trackAt(double y) const noexcept {
    for (std::size_t index = 0; index < tracks_.size(); ++index) {
        if (trackRect(index).contains(QPointF(left_margin, y))) return index;
    }
    return std::nullopt;
}

std::optional<ClipLocation> TimelineWidget::clipAt(double x, double y) const noexcept {
    if (!trackAt(y).has_value()) return std::nullopt;
    const auto global_frame = globalFrameAt(x);
    if (!global_frame.has_value()) return std::nullopt;
    // Track zero is the visual top layer. A click in an overlap selects the
    // first visible clip in that priority order, regardless of the row under
    // the pointer.
    for (std::size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
        std::optional<ClipLocation> video_match;
        for (std::size_t clip_index = 0;
             clip_index < tracks_[track_index].clips.size();
             ++clip_index) {
            const auto& clip = tracks_[track_index].clips[clip_index];
            if (*global_frame >= clip.timeline_start_frame &&
                *global_frame < clip.timeline_start_frame +
                    clip.timeline_duration_frames) {
                if (clip.kind == ClipKind::Text) {
                    return ClipLocation{track_index, clip_index};
                }
                video_match = ClipLocation{track_index, clip_index};
            }
        }
        if (video_match.has_value()) return video_match;
    }
    return std::nullopt;
}

std::optional<std::int64_t> TimelineWidget::globalFrameAt(double x) const noexcept {
    const auto total = displayDuration();
    const auto track = trackContentRect(0);
    if (track.width() <= 0.0 || x < track.left() || x > track.right()) {
        return std::nullopt;
    }
    if (total <= 0) return 0;
    const double fraction = std::clamp((x - track.left()) / track.width(), 0.0, 1.0);
    return std::clamp<std::int64_t>(
        static_cast<std::int64_t>(std::llround(fraction * total)),
        0,
        total);
}

std::optional<std::int64_t> TimelineWidget::localFrameAt(
    const ClipLocation& location, double x) const noexcept {
    const auto rect = clipRect(location);
    if (rect.width() <= 0.0) return std::nullopt;
    const auto& clip = tracks_[location.track_index].clips[location.clip_index];
    const double fraction = std::clamp((x - rect.left()) / rect.width(), 0.0, 1.0);
    return std::clamp<std::int64_t>(
        static_cast<std::int64_t>(std::llround(
            fraction * std::max<std::int64_t>(0, clip.timeline_duration_frames - 1))),
        0,
        std::max<std::int64_t>(0, clip.timeline_duration_frames - 1));
}

std::optional<TimelineWidget::TrimEdge> TimelineWidget::trimEdgeAt(
    const ClipLocation& location, double x) const noexcept {
    const auto rect = clipRect(location);
    if (rect.width() <= 0.0 || !rect.contains(QPointF(x, rect.center().y()))) {
        return std::nullopt;
    }
    if (x - rect.left() <= edge_width) return TrimEdge::Left;
    if (rect.right() - x <= edge_width) return TrimEdge::Right;
    return std::nullopt;
}

std::optional<std::pair<std::size_t, std::size_t>>
TimelineWidget::transitionClipIndexesAt(double x, double y) const noexcept {
    const auto track_index = trackAt(y);
    if (!track_index.has_value() || *track_index >= tracks_.size()) return std::nullopt;
    const auto& track = tracks_[*track_index];
    const auto total = displayDuration();
    if (total <= 0) return std::nullopt;
    const auto content = trackContentRect(*track_index);
    for (std::size_t from_index = 0;
         from_index + 1 < track.clips.size();
         ++from_index) {
        const auto& from = track.clips[from_index];
        const auto& to = track.clips[from_index + 1];
        if (from.timeline_start_frame >
                std::numeric_limits<std::int64_t>::max() - from.timeline_duration_frames ||
            from.timeline_start_frame + from.timeline_duration_frames !=
                to.timeline_start_frame) {
            continue;
        }
        const double boundary = content.left() + content.width() *
            static_cast<double>(to.timeline_start_frame) / total;
        const auto* transition = [&]() -> const TimelineTransition* {
            for (const auto& candidate : track.transitions) {
                if (candidate.from_clip_id == from.clip_id &&
                    candidate.to_clip_id == to.clip_id) {
                    return &candidate;
                }
            }
            return nullptr;
        }();
        const double tolerance = transition == nullptr
            ? 8.0
            : std::max(
                8.0,
                content.width() * static_cast<double>(transition->duration_frames) / total);
        if (std::abs(x - boundary) <= tolerance) {
            return std::make_pair(from_index, from_index + 1);
        }
    }
    return std::nullopt;
}

void TimelineWidget::emitSelected(const ClipLocation& location) {
    active_clip_ = location;
    selected_transition_.reset();
    emit transitionSelectedAt(-1, -1, -1);
    emit clipSelectedAt(
        static_cast<qint64>(location.track_index),
        static_cast<qint64>(location.clip_index));
    emitLegacySelection(location);
}

void TimelineWidget::emitLegacySelection(const ClipLocation& location) {
    if (location.track_index == 0) {
        emit clipSelected(static_cast<qint64>(location.clip_index));
    }
}

void TimelineWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor("#171a20"));

    const auto total = displayDuration();
    const auto ruler = QRectF(
        left_margin + track_header_width,
        12.0,
        std::max(0.0, static_cast<double>(width()) -
            left_margin - right_margin - track_header_width),
        25.0);
    painter.setPen(QColor("#4b5565"));
    painter.drawLine(ruler.bottomLeft(), ruler.bottomRight());
    const auto ruler_end = std::max<std::int64_t>(1, total);
    const auto tick_count = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(std::ceil(static_cast<double>(ruler_end) / 120.0)),
        1,
        12);
    const auto tick_step = std::max<std::int64_t>(
        1,
        (ruler_end + tick_count - 1) / tick_count);
    const auto fps = frameRate();
    painter.setFont(QFont(painter.font().family(), 8));
    for (std::int64_t frame = 0; frame <= ruler_end; frame += tick_step) {
        const auto fraction = static_cast<double>(frame) / ruler_end;
        const auto x = ruler.left() + ruler.width() * fraction;
        painter.setPen(QColor("#566171"));
        painter.drawLine(QPointF(x, ruler.bottom() - 5), ruler.bottomRight() +
            QPointF(x - ruler.right(), 0));
        painter.setPen(QColor("#9aa4b2"));
        painter.drawText(
            QRectF(x + 4, ruler.top(), 96, ruler.height()),
            Qt::AlignLeft | Qt::AlignVCenter,
            TimelineWidget::formatTimecode(frame, fps));
        if (frame > ruler_end - tick_step) break;
    }

    const auto grid_content = trackContentRect(0);
    if (total > 0 && grid_content.width() > 0.0) {
        for (std::int64_t frame = 0; frame <= total; frame += tick_step) {
            const auto x = grid_content.left() + grid_content.width() *
                static_cast<double>(frame) / total;
            painter.setPen(QColor("#252d38"));
            painter.drawLine(
                QPointF(x, grid_content.top()),
                QPointF(x, trackRect(tracks_.size() - 1).bottom() - 6));
            if (frame > total - tick_step) break;
        }
    }

    for (std::size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
        const auto row = trackRect(track_index);
        const auto content = trackContentRect(track_index);
        const bool active_track = active_clip_.has_value() &&
            active_clip_->track_index == track_index;
        painter.setPen(active_track ? QColor("#d5a94b") : QColor("#3d4654"));
        painter.setBrush(active_track ? QColor("#252d3a") : QColor("#202631"));
        painter.drawRoundedRect(row, 4, 4);
        painter.setPen(active_track ? QColor("#ffcf5c") : QColor("#b8c2d1"));
        painter.drawText(row.adjusted(10, 7, -row.width() + track_header_width - 8, -row.height() + 40),
            Qt::AlignLeft | Qt::AlignVCenter,
            QString("V%1  %2")
                .arg(track_index + 1)
                .arg(text(tracks_[track_index].name)));
        painter.setPen(QColor("#7e8999"));
        painter.setFont(QFont(painter.font().family(), 8));
        painter.drawText(
            row.adjusted(10, 38, -row.width() + track_header_width - 8, -7),
            Qt::AlignLeft | Qt::AlignVCenter,
            QString("%1 clip%2")
                .arg(tracks_[track_index].clips.size())
                .arg(tracks_[track_index].clips.size() == 1 ? "" : "s"));
        painter.setPen(QColor("#384250"));
        painter.drawLine(
            QPointF(row.left() + track_header_width, row.top() + 4),
            QPointF(row.left() + track_header_width, row.bottom() - 4));

        for (std::size_t clip_index = 0;
             clip_index < tracks_[track_index].clips.size(); ++clip_index) {
            const ClipLocation location{track_index, clip_index};
            const auto rect = clipRect(location);
            const bool active = active_clip_.has_value() &&
                *active_clip_ == location;
            const bool moving = moving_active_ && moving_clip_ == location;
            const bool trimming = trimming_ && trimming_clip_ == location;
            const auto& clip = tracks_[track_index].clips[clip_index];
            const QColor track_colors[] = {
                QColor("#3c75ae"), QColor("#357f70"),
                QColor("#6d5ca8"), QColor("#9b6943")};
            const auto clip_color = clip.kind == ClipKind::Text
                ? QColor("#8c5fb3")
                : track_colors[track_index % 4];
            painter.setPen(active ? QColor("#ffcf5c") : clip_color.lighter(135));
            painter.setBrush(moving || trimming
                ? QColor("#8a5a2f")
                : active ? clip_color.lighter(115) : clip_color);
            painter.drawRoundedRect(rect, 3, 3);
            painter.setPen(QColor("#f4f7fb"));
            const auto label = QString("%1  %2%3")
                .arg(clip_index + 1)
                .arg(clip.kind == ClipKind::Text ? "[Text] " : "")
                .arg(text(clip.display_name))
                + " - " + clipDuration(tracks_[track_index].clips[clip_index]);
            painter.drawText(rect.adjusted(6, 0, -6, 0),
                Qt::AlignVCenter,
                QFontMetrics(painter.font()).elidedText(
                    label, Qt::ElideRight, std::max(1, static_cast<int>(rect.width() - 12))));

            if (active && clip.timeline_duration_frames > 0) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor("#ffe08a"));
                for (const auto property : {
                         timeline::TransformProperty::PositionX,
                         timeline::TransformProperty::PositionY,
                         timeline::TransformProperty::Scale,
                         timeline::TransformProperty::Rotation,
                         timeline::TransformProperty::Opacity}) {
                    for (const auto& keyframe : timeline::keyframesFor(
                             clip.keyframes, property)) {
                        const double fraction = static_cast<double>(keyframe.frame) /
                            std::max<std::int64_t>(1, clip.timeline_duration_frames - 1);
                        const auto key_x = rect.left() + rect.width() *
                            std::clamp(fraction, 0.0, 1.0);
                        const auto key_y = rect.top() + 7.0;
                        painter.drawPolygon({
                            QPointF(key_x, key_y - 4.0),
                            QPointF(key_x + 4.0, key_y),
                            QPointF(key_x, key_y + 4.0),
                            QPointF(key_x - 4.0, key_y)});
                    }
                }
            }
        }

        for (const auto& transition : tracks_[track_index].transitions) {
            const auto indexes = [&]() -> std::optional<std::pair<std::size_t, std::size_t>> {
                std::optional<std::size_t> from;
                std::optional<std::size_t> to;
                for (std::size_t index = 0; index < tracks_[track_index].clips.size(); ++index) {
                    if (tracks_[track_index].clips[index].clip_id == transition.from_clip_id) {
                        from = index;
                    }
                    if (tracks_[track_index].clips[index].clip_id == transition.to_clip_id) {
                        to = index;
                    }
                }
                if (!from.has_value() || !to.has_value()) return std::nullopt;
                return std::make_pair(*from, *to);
            }();
            if (!indexes.has_value() || indexes->second != indexes->first + 1 || total <= 0) {
                continue;
            }
            const auto& to = tracks_[track_index].clips[indexes->second];
            const auto boundary = content.left() + content.width() *
                static_cast<double>(to.timeline_start_frame) / total;
            const auto transition_width = content.width() *
                static_cast<double>(transition.duration_frames) / total;
            const auto selected = selected_transition_.has_value() &&
                selected_transition_->track_index == track_index &&
                selected_transition_->from_clip_index == indexes->first &&
                selected_transition_->to_clip_index == indexes->second;
            const auto left = transition.kind == TransitionKind::FadeToBlack
                ? boundary - transition_width
                : boundary;
            const auto right = transition.kind == TransitionKind::FadeToBlack
                ? boundary + transition_width
                : boundary + transition_width;
            painter.setPen(QPen(
                selected ? QColor("#fff0a3") : QColor("#d5a94b"),
                selected ? 2.0 : 1.0,
                Qt::DashLine));
            painter.setBrush(QColor(213, 169, 75, selected ? 90 : 45));
            painter.drawRect(QRectF(
                std::max(content.left(), left),
                content.top() + 2,
                std::min(content.right(), right) - std::max(content.left(), left),
                content.height() - 4));
            painter.setPen(QColor("#ffe08a"));
            painter.drawText(
                QRectF(std::max(content.left(), left), content.top() + 4,
                       std::max(0.0, std::min(content.right(), right) -
                           std::max(content.left(), left)), 18),
                Qt::AlignCenter,
                transition.kind == TransitionKind::FadeToBlack ? "Fade" : "Dissolve");
        }
    }

    if (drag_hovering_) {
        if (drop_hover_track_.has_value() && drop_hover_frame_.has_value() && total > 0) {
            const auto content = trackContentRect(*drop_hover_track_);
            const auto x = content.left() + content.width() *
                static_cast<double>(*drop_hover_frame_) / total;
            painter.setPen(QPen(QColor("#9ed8ff"), 2, Qt::DashLine));
            painter.drawLine(
                QPointF(x, content.top() - 4),
                QPointF(x, content.bottom() + 4));
            painter.setBrush(QColor("#9ed8ff"));
            painter.drawEllipse(QPointF(x, content.top() - 5), 3, 3);
        }
    }

    if (active_clip_.has_value() &&
        active_clip_->track_index < tracks_.size() &&
        active_clip_->clip_index < tracks_[active_clip_->track_index].clips.size()) {
        const auto& clip = tracks_[active_clip_->track_index].clips[active_clip_->clip_index];
        const auto content = trackContentRect(0);
        const auto content_duration = std::max<std::int64_t>(1, totalDuration());
        const auto visual_duration = std::max<std::int64_t>(1, displayDuration());
        auto global_frame = playhead_frame_;
        if (drag_frame_.has_value()) {
            global_frame = clip.timeline_start_frame + *drag_frame_;
        }
        global_frame = std::clamp<std::int64_t>(global_frame, 0, content_duration - 1);
        const auto x = content.left() + content.width() *
            static_cast<double>(global_frame) / visual_duration;
        painter.setPen(QPen(QColor("#ffcf5c"), 2));
        painter.drawLine(
            QPointF(x, trackRect(0).top() - 20),
            QPointF(x, trackRect(tracks_.size() - 1).bottom()));
        painter.setBrush(QColor("#ffcf5c"));
        painter.drawPolygon({
            QPointF(x - 4, trackRect(0).top() - 20),
            QPointF(x + 4, trackRect(0).top() - 20),
            QPointF(x, trackRect(0).top() - 13)});
    }
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat(ui::kMediaPathMimeType)) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TimelineWidget::dragLeaveEvent(QDragLeaveEvent* event) {
    drag_hovering_ = false;
    drop_hover_track_.reset();
    drop_hover_frame_.reset();
    update();
    event->accept();
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent* event) {
    const auto track = trackAt(event->position().y());
    const auto frame = globalFrameAt(event->position().x());
    const bool accepted = event->mimeData()->hasFormat(ui::kMediaPathMimeType) &&
        track.has_value() && frame.has_value();
    drag_hovering_ = accepted;
    if (accepted) {
        drop_hover_track_ = track;
        drop_hover_frame_ = frame;
    } else {
        drop_hover_track_.reset();
        drop_hover_frame_.reset();
    }
    update();
    if (accepted) event->acceptProposedAction();
    else event->ignore();
}

void TimelineWidget::dropEvent(QDropEvent* event) {
    const auto track = trackAt(event->position().y());
    const auto frame = globalFrameAt(event->position().x());
    if (!event->mimeData()->hasFormat(ui::kMediaPathMimeType) ||
        !track.has_value() || !frame.has_value()) {
        event->ignore();
        return;
    }
    const auto path = QString::fromUtf8(
        event->mimeData()->data(ui::kMediaPathMimeType));
    if (path.isEmpty()) {
        event->ignore();
        return;
    }
    drag_hovering_ = false;
    drop_hover_track_.reset();
    drop_hover_frame_.reset();
    emit mediaDropRequestedAt(path, static_cast<qint64>(*track), *frame);
    event->acceptProposedAction();
    update();
}

void TimelineWidget::showTransitionMenu(
    const QPoint& position,
    const QPoint& global_position) {
    const auto indexes = transitionClipIndexesAt(position.x(), position.y());
    const auto track_index = trackAt(position.y());
    if (!indexes.has_value() || !track_index.has_value()) return;

    selected_transition_ = SelectedTransition{
        *track_index, indexes->first, indexes->second};
    emit transitionSelectedAt(
        static_cast<qint64>(*track_index),
        static_cast<qint64>(indexes->first),
        static_cast<qint64>(indexes->second));

    const auto& track = tracks_[*track_index];
    const auto& from = track.clips[indexes->first];
    const auto& to = track.clips[indexes->second];
    const auto* existing = [&]() -> const TimelineTransition* {
        for (const auto& transition : track.transitions) {
            if (transition.from_clip_id == from.clip_id &&
                transition.to_clip_id == to.clip_id) {
                return &transition;
            }
        }
        return nullptr;
    }();

    QMenu menu(this);
    auto* dissolve = menu.addAction("Add Cross Dissolve");
    auto* fade = menu.addAction("Add Fade to Black");
    menu.addSeparator();
    auto* remove = menu.addAction("Remove Transition");
    dissolve->setEnabled(existing == nullptr);
    fade->setEnabled(existing == nullptr);
    remove->setEnabled(existing != nullptr);
    const auto* chosen = menu.exec(global_position);
    if (chosen == dissolve) {
        emit transitionAddRequestedAt(
            static_cast<qint64>(*track_index),
            static_cast<qint64>(indexes->first),
            static_cast<qint64>(indexes->second),
            0);
    } else if (chosen == fade) {
        emit transitionAddRequestedAt(
            static_cast<qint64>(*track_index),
            static_cast<qint64>(indexes->first),
            static_cast<qint64>(indexes->second),
            1);
    } else if (chosen == remove) {
        emit transitionRemoveRequestedAt(
            static_cast<qint64>(*track_index),
            static_cast<qint64>(indexes->first),
            static_cast<qint64>(indexes->second));
    }
    update();
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent* event) {
    if (suppress_next_context_menu_) {
        suppress_next_context_menu_ = false;
        event->accept();
        return;
    }
    const auto indexes = transitionClipIndexesAt(
        event->pos().x(), event->pos().y());
    const auto track_index = trackAt(event->pos().y());
    if (!indexes.has_value() || !track_index.has_value()) {
        event->ignore();
        return;
    }
    showTransitionMenu(event->pos(), event->globalPos());
    event->accept();
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        const auto indexes = transitionClipIndexesAt(
            event->position().x(), event->position().y());
        if (indexes.has_value() && trackAt(event->position().y()).has_value()) {
            suppress_next_context_menu_ = true;
            showTransitionMenu(
                event->position().toPoint(),
                event->globalPosition().toPoint());
            event->accept();
        } else {
            event->ignore();
        }
        return;
    }
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    const auto location = clipAt(event->position().x(), event->position().y());
    if (!location.has_value()) {
        if (trackAt(event->position().y()).has_value() &&
            globalFrameAt(event->position().x()).has_value()) {
            active_clip_.reset();
            selected_transition_.reset();
            emit clipSelectedAt(-1, -1);
            emit transitionSelectedAt(-1, -1, -1);
            update();
            event->accept();
        } else {
            event->ignore();
        }
        return;
    }
    const bool alt_pressed = event->modifiers().testFlag(Qt::AltModifier);
    if (razor_mode_) {
        const auto frame = localFrameAt(*location, event->position().x());
        if (!frame.has_value()) {
            event->ignore();
            return;
        }
        razor_clicking_ = true;
        razor_gesture_moved_ = false;
        razor_clip_ = *location;
        razor_frame_ = *frame;
        razor_press_position_ = event->position();
        emitSelected(*location);
        grabMouse();
        event->accept();
        return;
    }
    if (const auto indexes = transitionClipIndexesAt(
            event->position().x(), event->position().y());
        indexes.has_value()) {
        selected_transition_ = SelectedTransition{
            trackAt(event->position().y()).value(), indexes->first, indexes->second};
        emit transitionSelectedAt(
            static_cast<qint64>(selected_transition_->track_index),
            static_cast<qint64>(indexes->first),
            static_cast<qint64>(indexes->second));
        event->accept();
        update();
        return;
    }
    if (const auto edge = trimEdgeAt(*location, event->position().x()); edge.has_value()) {
        trimming_ = true;
        trimming_clip_ = *location;
        trim_edge_ = *edge;
        trim_start_frame_ = 0;
        trim_end_frame_ = tracks_[location->track_index].clips[location->clip_index].timeline_duration_frames;
        emit trimStarted();
        emitSelected(*location);
        grabMouse();
        setCursor(Qt::SizeHorCursor);
        event->accept();
        return;
    }
    if ((alt_pressed == move_requires_alt_) &&
        !transitionClipIndexesAt(event->position().x(), event->position().y()).has_value() &&
        !trimEdgeAt(*location, event->position().x()).has_value()) {
        if (!active_clip_.has_value() || *active_clip_ != *location) {
            emitSelected(*location);
        }
        move_pending_ = true;
        moving_active_ = false;
        moving_clip_ = *location;
        move_press_position_ = event->position();
        move_target_track_ = location->track_index;
        move_target_frame_ = tracks_[location->track_index].clips[location->clip_index].timeline_start_frame;
        grabMouse();
        event->accept();
        update();
        return;
    }
    if (!active_clip_.has_value() || *active_clip_ != *location) {
        emitSelected(*location);
        event->accept();
        return;
    }
    const auto frame = localFrameAt(*location, event->position().x());
    if (!frame.has_value()) {
        event->ignore();
        return;
    }
    seek_pending_ = true;
    seek_clip_ = *location;
    seek_press_position_ = event->position();
    drag_frame_ = *frame;
    grabMouse();
    event->accept();
    update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    if (move_pending_ && !moving_active_) {
        if ((event->position() - move_press_position_).manhattanLength() <= 4) {
            event->accept();
            return;
        }
        move_pending_ = false;
        moving_active_ = true;
        emit trimStarted();
    }
    if (moving_active_) {
        move_target_track_ = trackAt(event->position().y());
        if (move_target_track_.has_value()) {
            move_target_frame_ = globalFrameAt(event->position().x()).value_or(0);
        }
        update();
        event->accept();
        return;
    }
    if (trimming_) {
        const auto frame = localFrameAt(trimming_clip_, event->position().x());
        if (frame.has_value()) {
            const auto duration = tracks_[trimming_clip_.track_index]
                .clips[trimming_clip_.clip_index].timeline_duration_frames;
            if (trim_edge_ == TrimEdge::Left) {
                trim_start_frame_ = std::clamp(*frame, std::int64_t{0}, trim_end_frame_ - 1);
            } else {
                trim_end_frame_ = std::clamp(*frame + 1, trim_start_frame_ + 1, duration);
            }
        }
        update();
        event->accept();
        return;
    }
    if (razor_clicking_) {
        razor_gesture_moved_ =
            (event->position() - razor_press_position_).manhattanLength() > 4;
        update();
        event->accept();
        return;
    }
    if (seek_pending_ && !dragging_) {
        if ((event->position() - seek_press_position_).manhattanLength() > 4) {
            dragging_ = true;
            seek_pending_ = false;
            emit seekStarted();
        } else {
            event->accept();
            return;
        }
    }
    if (dragging_) {
        drag_frame_ = localFrameAt(seek_clip_, event->position().x());
        update();
        event->accept();
    } else {
        event->ignore();
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    if (moving_active_ || move_pending_) {
        const auto from = moving_clip_;
        const auto target_track = move_target_track_;
        const auto target_frame = move_target_frame_;
        const bool moved = moving_active_;
        move_pending_ = false;
        moving_active_ = false;
        move_target_track_.reset();
        releaseMouse();
        if (moved && target_track.has_value()) {
            emit clipMoveRequestedAt(
                static_cast<qint64>(from.track_index),
                static_cast<qint64>(from.clip_index),
                static_cast<qint64>(*target_track),
                target_frame);
            if (from.track_index == 0 && *target_track == 0) {
                emit clipMoveRequested(
                    static_cast<qint64>(from.clip_index),
                    static_cast<qint64>(from.clip_index));
            }
        }
        update();
        event->accept();
        return;
    }
    if (trimming_) {
        const auto location = trimming_clip_;
        const auto start = trim_start_frame_;
        const auto end = trim_end_frame_;
        const auto original = tracks_[location.track_index].clips[location.clip_index].timeline_duration_frames;
        trimming_ = false;
        unsetCursor();
        releaseMouse();
        if (start != 0 || end != original) {
            emit clipTrimRequestedAt(
                static_cast<qint64>(location.track_index),
                static_cast<qint64>(location.clip_index),
                start,
                end);
            if (location.track_index == 0) {
                emit clipTrimRequested(
                    static_cast<qint64>(location.clip_index), start, end);
            }
        }
        update();
        event->accept();
        return;
    }
    if (razor_clicking_ || razor_gesture_moved_) {
        const auto location = razor_clip_;
        const auto frame = razor_frame_;
        const bool valid = razor_clicking_ && !razor_gesture_moved_;
        razor_clicking_ = false;
        razor_gesture_moved_ = false;
        releaseMouse();
        if (valid) {
            emit clipSplitRequestedAt(
                static_cast<qint64>(location.track_index),
                static_cast<qint64>(location.clip_index), frame);
            if (location.track_index == 0) {
                emit clipSplitRequested(
                    static_cast<qint64>(location.clip_index), frame);
            }
        }
        update();
        event->accept();
        return;
    }
    if (dragging_) {
        const auto frame = drag_frame_;
        dragging_ = false;
        seek_pending_ = false;
        drag_frame_.reset();
        releaseMouse();
        if (frame.has_value()) emit seekRequested(*frame);
        update();
        event->accept();
        return;
    }
    if (seek_pending_) {
        seek_pending_ = false;
        drag_frame_.reset();
        releaseMouse();
        update();
        event->accept();
        return;
    }
    event->ignore();
}

} // namespace timeline
