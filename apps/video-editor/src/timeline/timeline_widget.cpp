#include "timeline_widget.h"

#include "../ui/media_drag_mime.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QMimeData>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace timeline {
namespace {

constexpr double edge_width = 8.0;
constexpr double shared_roll_half_width = 4.0;
constexpr double shared_single_clip_handle_width = 8.0;
constexpr double ruler_minor_target_spacing_pixels = 8.0;

TrimPointerPosition trimPointer(const QPointF& position) noexcept {
    return {position.x(), position.y()};
}

std::int64_t rulerMinorStep(double pixels_per_frame) noexcept {
    if (!std::isfinite(pixels_per_frame) || pixels_per_frame <= 0.0 ||
        pixels_per_frame >= 1.0) {
        return 1;
    }

    const auto raw_step = std::max<long double>(
        1.0L,
        std::ceil(static_cast<long double>(ruler_minor_target_spacing_pixels) /
                  static_cast<long double>(pixels_per_frame)));
    const auto max_step = std::numeric_limits<std::int64_t>::max();
    std::int64_t magnitude = 1;
    while (magnitude <= max_step / 10 &&
           static_cast<long double>(magnitude) * 10.0L < raw_step) {
        magnitude *= 10;
    }

    for (const auto multiplier : {1, 2, 5}) {
        if (magnitude <= max_step / multiplier) {
            const auto candidate = magnitude * multiplier;
            if (static_cast<long double>(candidate) >= raw_step) {
                return candidate;
            }
        }
    }
    if (magnitude <= max_step / 10) return magnitude * 10;
    return max_step;
}

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

std::optional<ClipLocation> locationForClip(
    const std::vector<TimelineTrack>& tracks,
    ClipId clip_id) noexcept {
    for (std::size_t track_index = 0; track_index < tracks.size(); ++track_index) {
        for (std::size_t clip_index = 0;
             clip_index < tracks[track_index].clips.size(); ++clip_index) {
            if (tracks[track_index].clips[clip_index].clip_id == clip_id) {
                return ClipLocation{track_index, clip_index};
            }
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> indexForTrack(
    const std::vector<TimelineTrack>& tracks,
    TrackId track_id) noexcept {
    for (std::size_t index = 0; index < tracks.size(); ++index) {
        if (tracks[index].track_id == track_id) return index;
    }
    return std::nullopt;
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
    // Model changes can arrive while this widget owns the mouse grab for a
    // seek, move, trim, or blade gesture. Resetting the gesture flags alone
    // would leave the grab active and route every subsequent click back to
    // the timeline instead of the rest of the editor.
    if (QWidget::mouseGrabber() == this) releaseMouse();
    if (interaction_controller_.trimGesture().active()) unsetCursor();

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
    updateVerticalExtent();
    updateHorizontalExtent();
    interaction_controller_.cancelAll();
    clearDragPreview();
    emit trackHeaderVisualsChanged();
    update();
}

void TimelineWidget::setClips(const std::vector<TimelineClip>& clips) {
    TimelineTrack track{1, "Video 1", 1.0, false, clips};
    setTracks({track});
}

void TimelineWidget::clearClips() {
    if (QWidget::mouseGrabber() == this) releaseMouse();
    if (interaction_controller_.trimGesture().active()) unsetCursor();
    tracks_.clear();
    tracks_.push_back(TimelineTrack{1, "Video 1", 1.0, false, {}});
    updateVerticalExtent();
    updateHorizontalExtent();
    active_clip_.reset();
    playhead_frame_ = 0;
    interaction_controller_.cancelAll();
    clearDragPreview();
    selected_transition_.reset();
    emit trackHeaderVisualsChanged();
    update();
}

void TimelineWidget::setActiveClip(std::optional<ClipLocation> location) {
    if (location.has_value() &&
        (location->track_index >= tracks_.size() ||
         location->clip_index >= tracks_[location->track_index].clips.size())) {
        location.reset();
    }
    active_clip_ = location;
    interaction_controller_.clearTransientPreview();
    emit trackHeaderVisualsChanged();
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
    interaction_controller_.clearTransientPreview();
    // The ruler position is only a transient visual override while the user
    // is dragging it. Once an external playback/seek update arrives, the
    // live playhead must win even if the decoder skipped over that exact
    // frame.
    update();
}

std::int64_t TimelineWidget::playheadFrame() const noexcept {
    return playhead_frame_;
}

void TimelineWidget::setRazorMode(bool enabled) {
    if (interaction_controller_.splitPending() && QWidget::mouseGrabber() == this) {
        releaseMouse();
    }
    razor_mode_ = enabled;
    interaction_controller_.cancelSplit();
    unsetCursor();
    update();
}

bool TimelineWidget::razorMode() const noexcept {
    return razor_mode_;
}

void TimelineWidget::setMoveRequiresAlt(bool enabled) {
    move_requires_alt_ = enabled;
    if (interaction_controller_.moveActive() || interaction_controller_.movePending()) {
        interaction_controller_.cancelMove();
        clearDragPreview();
        if (QWidget::mouseGrabber() == this) releaseMouse();
    }
    update();
}

bool TimelineWidget::moveRequiresAlt() const noexcept {
    return move_requires_alt_;
}

void TimelineWidget::setSnapEnabled(bool enabled) {
    if (snap_enabled_ == enabled) return;
    snap_enabled_ = enabled;
    const auto move_target = interaction_controller_.moveTarget();
    if (interaction_controller_.moveActive() && move_target.has_value()) {
        const auto raw_frame = globalFrameAt(move_preview_position_.x());
        const auto source_location = locationForClip(
            tracks_, interaction_controller_.movingClipId());
        const auto target_index = indexForTrack(tracks_, move_target->track_id);
        if (raw_frame.has_value() && source_location.has_value() &&
            target_index.has_value()) {
            const auto& clip = tracks_[source_location->track_index]
                .clips[source_location->clip_index];
            const auto snapped = snapPlacement(
                *target_index, *raw_frame, clip.timeline_duration_frames,
                source_location);
            interaction_controller_.setMoveTarget(TimelineMoveTarget{
                move_target->track_id, snapped.start_frame, snapped.guide_frame});
        }
    } else if (interaction_controller_.dropPreview().hovering &&
               interaction_controller_.dropPreview().media &&
               interaction_controller_.dropPreview().target_track_index.has_value()) {
        auto drop = interaction_controller_.dropPreview();
        const auto raw_frame = globalFrameAt(drop.pointer_position.x());
        if (raw_frame.has_value()) {
            const auto snapped = snapPlacement(
                *drop.target_track_index,
                *raw_frame,
                drop.duration_frames);
            drop.target_frame = snapped.start_frame;
            drop.snap_guide_frame = snapped.guide_frame;
            drop.valid = !placementOverlaps(
                *drop.target_track_index, *drop.target_frame, drop.duration_frames);
            interaction_controller_.setDropPreview(std::move(drop));
        }
    }
    update();
    emit snapEnabledChanged(snap_enabled_);
}

bool TimelineWidget::snapEnabled() const noexcept {
    return snap_enabled_;
}

double TimelineWidget::zoomFactor() const noexcept {
    return zoom_factor_;
}

void TimelineWidget::setZoomFactor(double factor) {
    if (!std::isfinite(factor)) return;
    const auto normalized = std::clamp(
        factor, kMinTimelineZoomFactor, kMaxTimelineZoomFactor);
    if (std::abs(normalized - zoom_factor_) < 0.000001) return;
    zoom_factor_ = normalized;
    updateHorizontalExtent();
    update();
    emit zoomChanged(zoom_factor_);
}

double TimelineWidget::trackRowHeight() const noexcept {
    return track_row_height_;
}

void TimelineWidget::setTrackRowHeight(double height) {
    if (!std::isfinite(height)) return;
    const auto normalized = std::clamp(
        height, kMinimumTrackRowHeight, kMaximumTrackRowHeight);
    if (std::abs(normalized - track_row_height_) < 0.000001) return;
    track_row_height_ = normalized;
    updateVerticalExtent();
    emit trackHeaderVisualsChanged();
    update();
    emit trackRowHeightChanged(track_row_height_);
}

double TimelineWidget::nextZoomFactor(int direction) const noexcept {
    if (direction == 0) return zoom_factor_;
    if (direction > 0) {
        for (const auto level : kTimelineZoomLevels) {
            if (level > zoom_factor_ + 0.000001) return level;
        }
        return kTimelineZoomLevels.back();
    }
    for (auto index = kTimelineZoomLevels.size(); index-- > 0;) {
        if (kTimelineZoomLevels[index] < zoom_factor_ - 0.000001) {
            return kTimelineZoomLevels[index];
        }
    }
    return kTimelineZoomLevels.front();
}

bool TimelineWidget::canZoomIn() const noexcept {
    return zoom_factor_ < kMaxTimelineZoomFactor - 0.000001;
}

bool TimelineWidget::canZoomOut() const noexcept {
    return zoom_factor_ > kMinTimelineZoomFactor + 0.000001;
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

int TimelineWidget::trackHeaderOverlayWidth() const noexcept {
    return static_cast<int>(std::ceil(
        TimelineGeometry::left_margin + TimelineGeometry::track_header_width));
}

void TimelineWidget::paintTrackHeaderCell(
    QPainter& painter,
    std::size_t track_index,
    const QRectF& row) const {
    if (track_index >= tracks_.size()) return;

    const auto header = QRectF(
        row.left(),
        row.top(),
        TimelineGeometry::track_header_width,
        row.height());
    const bool active_track = active_clip_.has_value() &&
        active_clip_->track_index == track_index;

    painter.setPen(active_track ? QColor("#d5a94b") : QColor("#3d4654"));
    painter.setBrush(active_track ? QColor("#252d3a") : QColor("#202631"));
    QPainterPath header_path;
    constexpr double header_corner_radius = 4.0;
    header_path.moveTo(header.right(), header.top());
    header_path.lineTo(
        header.left() + header_corner_radius,
        header.top());
    header_path.quadTo(
        header.left(),
        header.top(),
        header.left(),
        header.top() + header_corner_radius);
    header_path.lineTo(
        header.left(),
        header.bottom() - header_corner_radius);
    header_path.quadTo(
        header.left(),
        header.bottom(),
        header.left() + header_corner_radius,
        header.bottom());
    header_path.lineTo(header.right(), header.bottom());
    header_path.closeSubpath();
    painter.drawPath(header_path);

    painter.setPen(active_track ? QColor("#ffcf5c") : QColor("#b8c2d1"));
    painter.drawText(
        header.adjusted(10, 7, -8, -header.height() + 40),
        Qt::AlignLeft | Qt::AlignVCenter,
        QString("V%1  %2")
            .arg(track_index + 1)
            .arg(text(tracks_[track_index].name)));

    painter.setPen(QColor("#7e8999"));
    painter.setFont(QFont(painter.font().family(), 8));
    painter.drawText(
        header.adjusted(10, 38, -8, -7),
        Qt::AlignLeft | Qt::AlignVCenter,
        QString("%1 clip%2")
            .arg(tracks_[track_index].clips.size())
            .arg(tracks_[track_index].clips.size() == 1 ? "" : "s"));

    painter.setPen(QColor("#384250"));
    painter.drawLine(
        QPointF(row.left() + TimelineGeometry::track_header_width, row.top() + 4),
        QPointF(row.left() + TimelineGeometry::track_header_width, row.bottom() - 4));
}

void TimelineWidget::paintTrackHeaderOverlay(
    QPainter& painter,
    int vertical_offset) const {
    const auto normalized_offset = std::max(0, vertical_offset);
    const auto overlay_width = static_cast<double>(trackHeaderOverlayWidth());
    const auto first_row_top = TimelineGeometry::top_margin - normalized_offset;
    const auto last_row_bottom = tracks_.empty()
        ? first_row_top
        : trackRect(tracks_.size() - 1).bottom() - normalized_offset;

    painter.save();
    painter.setClipRect(QRectF(
        0.0,
        0.0,
        overlay_width,
        static_cast<double>(painter.viewport().height())));
    if (last_row_bottom > first_row_top) {
        painter.fillRect(
            QRectF(0.0, first_row_top, overlay_width, last_row_bottom - first_row_top),
            QColor("#171a20"));
    }

    for (std::size_t track_index = 0;
         track_index < tracks_.size();
         ++track_index) {
        auto row = trackRect(track_index);
        row.moveTop(row.top() - normalized_offset);
        paintTrackHeaderCell(painter, track_index, row);
        // The overlay ends at the content boundary so the frame-zero
        // playhead remains visible. Draw the fixed divider one pixel inside
        // that boundary instead of relying on the scrolled widget's divider.
        painter.setPen(QColor("#384250"));
        painter.drawLine(
            QPointF(overlay_width - 1.0, row.top() + 4),
            QPointF(overlay_width - 1.0, row.bottom() - 4));
    }
    painter.restore();
}

bool TimelineWidget::eventFilter(QObject* watched, QEvent* event) {
    if (event != nullptr && event->type() == QEvent::Resize) {
        const auto* resize_event = static_cast<const QResizeEvent*>(event);
        setTimelineViewportWidth(resize_event->size().width());
    }

    auto* watched_widget = qobject_cast<QWidget*>(watched);
    if (watched_widget != nullptr && watched != this && event != nullptr) {
        switch (event->type()) {
        case QEvent::DragEnter: {
            auto* drag_event = static_cast<QDragEnterEvent*>(event);
            if (isSupportedDrop(drag_event->mimeData())) {
                drag_event->acceptProposedAction();
            } else {
                drag_event->ignore();
            }
            return true;
        }
        case QEvent::DragMove: {
            auto* drag_event = static_cast<QDragMoveEvent*>(event);
            const auto position = mapFrom(
                watched_widget,
                drag_event->position().toPoint());
            if (updateDropHover(drag_event->mimeData(), position)) {
                drag_event->acceptProposedAction();
            } else {
                drag_event->ignore();
            }
            return true;
        }
        case QEvent::DragLeave: {
            auto* drag_event = static_cast<QDragLeaveEvent*>(event);
            clearDropHover();
            drag_event->accept();
            return true;
        }
        case QEvent::Drop: {
            auto* drop_event = static_cast<QDropEvent*>(event);
            const auto position = mapFrom(
                watched_widget,
                drop_event->position().toPoint());
            if (processDrop(drop_event->mimeData(), position)) {
                drop_event->acceptProposedAction();
            } else {
                drop_event->ignore();
            }
            return true;
        }
        default:
            break;
        }
    }

    return QWidget::eventFilter(watched, event);
}

QRectF TimelineWidget::trackRect(std::size_t index) const noexcept {
    return geometry().trackRect(index);
}

TimelineGeometry TimelineWidget::geometry() const noexcept {
    const auto fixed_duration = interaction_controller_.trimGesture().active() &&
            interaction_controller_.trimGesture().scaleDuration() > 0
        ? std::optional<std::int64_t>{interaction_controller_.trimGesture().scaleDuration()}
        : std::nullopt;
    return TimelineGeometry(
        tracks_, QSizeF(width(), height()), track_row_height_, zoom_factor_, fixed_duration);
}

QRectF TimelineWidget::rulerRect() const noexcept {
    return geometry().rulerRect();
}

double TimelineWidget::rowHeight() const noexcept {
    return track_row_height_;
}

void TimelineWidget::updateVerticalExtent() {
    const auto track_count = std::max<std::size_t>(1, tracks_.size());
    const auto required_height = TimelineGeometry::top_margin +
        static_cast<double>(track_count) * track_row_height_ +
        static_cast<double>(track_count - 1) * TimelineGeometry::row_gap + 12.0;
    setMinimumHeight(std::max(100, static_cast<int>(std::ceil(required_height))));
    updateGeometry();
}

QRectF TimelineWidget::trackContentRect(std::size_t index) const noexcept {
    // Clips use the complete vertical extent of the track row. The header
    // remains reserved horizontally, while vertical insets would make a clip
    // appear shorter than its Timeline track for no functional reason.
    return geometry().trackContentRect(index);
}

QRectF TimelineWidget::clipRect(const ClipLocation& location) const noexcept {
    if (location.track_index >= tracks_.size() ||
        location.clip_index >= tracks_[location.track_index].clips.size()) {
        return {};
    }
    return geometry().clipRect(displayedClip(location), location.track_index);
}

const TimelineClip& TimelineWidget::displayedClip(
    const ClipLocation& location) const noexcept {
    const auto& preview = interaction_controller_.trimGesture().preview();
    if (preview.has_value()) {
        if (preview->clip_location == location) return preview->clip;
        if (preview->neighbor_location == location &&
            preview->neighbor_clip.has_value()) {
            return *preview->neighbor_clip;
        }
    }
    static const TimelineClip empty_clip;
    if (location.track_index >= tracks_.size() ||
        location.clip_index >= tracks_[location.track_index].clips.size()) {
        return empty_clip;
    }
    return tracks_[location.track_index].clips[location.clip_index];
}

std::int64_t TimelineWidget::mediaDropDuration(
    const QMimeData* mime_data) const noexcept {
    if (mime_data == nullptr) return 1;

    const auto readInteger = [mime_data](const char* mime_type)
        -> std::optional<std::int64_t> {
        if (!mime_data->hasFormat(mime_type)) return std::nullopt;
        bool ok = false;
        const auto value = QString::fromUtf8(mime_data->data(mime_type))
            .toLongLong(&ok);
        return ok && value > 0 ? std::optional<std::int64_t>(value) : std::nullopt;
    };
    const auto readDouble = [mime_data](const char* mime_type)
        -> std::optional<double> {
        if (!mime_data->hasFormat(mime_type)) return std::nullopt;
        bool ok = false;
        const auto value = QString::fromUtf8(mime_data->data(mime_type))
            .toDouble(&ok);
        return ok && std::isfinite(value) && value > 0.0
            ? std::optional<double>(value)
            : std::nullopt;
    };

    if (const auto frame_count = readInteger(ui::kMediaFrameCountMimeType);
        frame_count.has_value()) {
        return *frame_count;
    }
    const auto duration_seconds = readDouble(ui::kMediaDurationSecondsMimeType);
    const auto frame_rate = readDouble(ui::kMediaFrameRateMimeType);
    if (!duration_seconds.has_value() || !frame_rate.has_value()) return 1;

    const auto estimated = static_cast<long double>(*duration_seconds) *
        static_cast<long double>(*frame_rate);
    if (!std::isfinite(estimated) ||
        estimated >= static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return 1;
    }
    return std::max<std::int64_t>(
        1,
        static_cast<std::int64_t>(std::ceil(estimated)));
}

QString TimelineWidget::mediaDropLabel(const QMimeData* mime_data) const {
    if (mime_data == nullptr) return "Media";
    const auto name = QString::fromUtf8(
        mime_data->data(ui::kMediaDisplayNameMimeType));
    if (!name.isEmpty()) return name;
    const auto path = QString::fromUtf8(
        mime_data->data(ui::kMediaPathMimeType));
    const auto file_name = QFileInfo(path).fileName();
    return file_name.isEmpty() ? QStringLiteral("Media") : file_name;
}

bool TimelineWidget::placementOverlaps(
    std::size_t track_index,
    std::int64_t start_frame,
    std::int64_t duration_frames,
    std::optional<ClipLocation> excluded) const noexcept {
    return TimelineDropValidator::overlaps(
        tracks_, track_index, start_frame, duration_frames, excluded);
}

QRectF TimelineWidget::previewRect(
    std::size_t track_index,
    std::int64_t start_frame,
    std::int64_t duration_frames) const noexcept {
    if (track_index >= tracks_.size() || duration_frames <= 0) return {};
    const auto content = trackContentRect(track_index);
    const auto max_frame = std::numeric_limits<std::int64_t>::max();
    const auto bounded_start = std::max<std::int64_t>(0, start_frame);
    const auto bounded_end = bounded_start > max_frame - duration_frames
        ? max_frame
        : bounded_start + duration_frames;
    const auto left = contentXForFrame(bounded_start);
    const auto right = contentXForFrame(bounded_end);
    return QRectF(
        left,
        content.top(),
        std::max(2.0, right - left),
        content.height());
}

timeline::SnapPlacement TimelineWidget::snapPlacement(
    std::size_t track_index,
    std::int64_t raw_start_frame,
    std::int64_t duration_frames,
    std::optional<ClipLocation> excluded) const noexcept {
    return TimelineDropValidator::snap(
        tracks_, geometry(), snap_enabled_, track_index, raw_start_frame,
        duration_frames, excluded);
}

double TimelineWidget::frameRate() const noexcept {
    return geometry().frameRate();
}

std::int64_t TimelineWidget::standardDuration() const noexcept {
    return geometry().standardDuration();
}

std::int64_t TimelineWidget::displayDuration() const noexcept {
    return geometry().displayDuration();
}

std::int64_t TimelineWidget::totalDuration() const noexcept {
    return geometry().totalDuration();
}

double TimelineWidget::pixelsPerFrame() const noexcept {
    return geometry().pixelsPerFrame();
}

void TimelineWidget::updateHorizontalExtent() {
    const auto base_width = std::max(
        1,
        timeline_viewport_width_ > 0 ? timeline_viewport_width_ : width());
    const auto standard = standardDuration();
    const auto base_duration = std::max(totalDuration(), standard);
    const auto scaled_width = std::ceil(
        static_cast<long double>(base_width) *
        static_cast<long double>(base_duration) *
        static_cast<long double>(zoom_factor_) /
        static_cast<long double>(standard));
    const auto max_width = static_cast<long double>(std::numeric_limits<int>::max());
    const auto required_width = static_cast<int>(std::min(scaled_width, max_width));
    setMinimumWidth(std::max(base_width, required_width));
    updateGeometry();
}

std::optional<std::int64_t> TimelineWidget::frameAtContentX(double x) const noexcept {
    return globalFrameAt(x);
}

double TimelineWidget::contentXForFrame(std::int64_t frame) const noexcept {
    return geometry().contentXForFrame(frame);
}

std::optional<std::size_t> TimelineWidget::trackAt(double y) const noexcept {
    return TimelineHitTester::trackAt(geometry(), tracks_.size(), y);
}

std::optional<ClipLocation> TimelineWidget::clipAt(double x, double y) const noexcept {
    return TimelineHitTester::clipAt(
        tracks_, geometry(), x, y, interaction_controller_.trimGesture().preview());
}

std::optional<std::int64_t> TimelineWidget::globalFrameAt(double x) const noexcept {
    return geometry().frameAtContentX(x);
}

std::optional<std::int64_t> TimelineWidget::playheadFrameAtRulerX(
    double x) const noexcept {
    return geometry().playheadFrameAtRulerX(x);
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

std::optional<ClipEdge> TimelineWidget::trimEdgeAt(
    const ClipLocation& location, double x) const noexcept {
    const auto rect = clipRect(location);
    if (rect.width() <= 0.0 || !rect.contains(QPointF(x, rect.center().y()))) {
        return std::nullopt;
    }
    const auto left_distance = x - rect.left();
    const auto right_distance = rect.right() - x;
    const auto& track = tracks_[location.track_index];
    const auto& clip = track.clips[location.clip_index];
    const bool shared_left = location.clip_index > 0 &&
        track.clips[location.clip_index - 1].timeline_start_frame >= 0 &&
        track.clips[location.clip_index - 1].timeline_duration_frames > 0 &&
        track.clips[location.clip_index - 1].timeline_start_frame <=
            std::numeric_limits<std::int64_t>::max() -
                track.clips[location.clip_index - 1].timeline_duration_frames &&
        clip.timeline_start_frame >= 0 &&
        track.clips[location.clip_index - 1].timeline_start_frame +
                track.clips[location.clip_index - 1].timeline_duration_frames ==
            clip.timeline_start_frame;
    const bool shared_right = location.clip_index + 1 < track.clips.size() &&
        clip.timeline_start_frame >= 0 && clip.timeline_duration_frames > 0 &&
        clip.timeline_start_frame <= std::numeric_limits<std::int64_t>::max() -
            clip.timeline_duration_frames &&
        clip.timeline_start_frame + clip.timeline_duration_frames ==
            track.clips[location.clip_index + 1].timeline_start_frame;
    const double left_hit_width = shared_left
        ? shared_roll_half_width + shared_single_clip_handle_width
        : edge_width;
    const double right_hit_width = shared_right
        ? shared_roll_half_width + shared_single_clip_handle_width
        : edge_width;
    const bool near_left = left_distance <= left_hit_width;
    const bool near_right = right_distance <= right_hit_width;
    if (near_left && near_right) {
        return left_distance <= right_distance ? ClipEdge::Left : ClipEdge::Right;
    }
    if (near_left) return ClipEdge::Left;
    if (near_right) return ClipEdge::Right;
    return std::nullopt;
}

ClipEdgeEditMode TimelineWidget::trimEditModeAt(
    const ClipLocation& location, ClipEdge edge, double x) const noexcept {
    if (location.track_index >= tracks_.size() ||
        location.clip_index >= tracks_[location.track_index].clips.size()) {
        return ClipEdgeEditMode::Individual;
    }
    const auto& track = tracks_[location.track_index];
    const auto& clip = track.clips[location.clip_index];
    std::optional<std::int64_t> shared_boundary;
    if (edge == ClipEdge::Left && location.clip_index > 0) {
        const auto& previous = track.clips[location.clip_index - 1];
        if (previous.timeline_start_frame >= 0 &&
            previous.timeline_duration_frames > 0 &&
            clip.timeline_start_frame >= 0 &&
            previous.timeline_start_frame <= std::numeric_limits<std::int64_t>::max() -
                previous.timeline_duration_frames &&
            previous.timeline_start_frame + previous.timeline_duration_frames ==
                clip.timeline_start_frame) {
            shared_boundary = clip.timeline_start_frame;
        }
    } else if (edge == ClipEdge::Right &&
               location.clip_index + 1 < track.clips.size() &&
               clip.timeline_start_frame >= 0 &&
               clip.timeline_duration_frames > 0 &&
               clip.timeline_start_frame <= std::numeric_limits<std::int64_t>::max() -
                   clip.timeline_duration_frames) {
        const auto boundary = clip.timeline_start_frame + clip.timeline_duration_frames;
        if (boundary == track.clips[location.clip_index + 1].timeline_start_frame) {
            shared_boundary = boundary;
        }
    }
    if (!shared_boundary.has_value()) return ClipEdgeEditMode::Individual;

    const auto total = displayDuration();
    const auto content = trackContentRect(location.track_index);
    if (total <= 0 || content.width() <= 0.0) return ClipEdgeEditMode::Individual;
    const auto boundary_x = content.left() + content.width() *
        static_cast<double>(*shared_boundary) / static_cast<double>(total);
    return std::abs(x - boundary_x) <= shared_roll_half_width
        ? ClipEdgeEditMode::Rolling
        : ClipEdgeEditMode::Individual;
}

void TimelineWidget::updateTrimHoverCursor(const QPointF& position) {
    if (!interaction_controller_.trimGesture().active() && !razor_mode_) {
        const auto location = clipAt(position.x(), position.y());
        if (location.has_value() &&
            trimEdgeAt(*location, position.x()).has_value()) {
            if (cursor().shape() != Qt::SizeHorCursor) {
                setCursor(Qt::SizeHorCursor);
            }
            return;
        }
    }
    if (testAttribute(Qt::WA_SetCursor)) unsetCursor();
}

std::optional<std::int64_t> TimelineWidget::trimBoundaryAt(double x) const noexcept {
    if (tracks_.empty()) return std::nullopt;
    const auto content = trackContentRect(
        interaction_controller_.trimGesture().location().track_index);
    if (content.width() <= 0.0) return std::nullopt;
    const auto clamped_x = std::clamp(x, content.left(), content.right());
    return globalFrameAt(clamped_x);
}

std::optional<std::pair<std::size_t, std::size_t>>
TimelineWidget::transitionClipIndexesAt(double x, double y) const noexcept {
    return TimelineHitTester::transitionPairAt(
        tracks_, geometry(), x, y, interaction_controller_.trimGesture().preview());
}

void TimelineWidget::emitSelected(const ClipLocation& location) {
    active_clip_ = location;
    selected_transition_.reset();
    emit transitionSelectionCleared();
    const auto& clip = tracks_[location.track_index].clips[location.clip_index];
    emit clipSelected(tracks_[location.track_index].track_id, clip.clip_id);
}

void TimelineWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor("#171a20"));

    const auto total = displayDuration();
    const auto ruler = rulerRect();
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
    const auto ruler_content = trackContentRect(0);
    const auto minor_frame_width = pixelsPerFrame();
    const auto minor_step = rulerMinorStep(minor_frame_width);
    if (minor_frame_width > 0.0 && minor_frame_width < 1.0 &&
        ruler_content.width() > 0.0) {
        const auto dirty = event != nullptr
            ? QRectF(event->rect())
            : QRectF(rect());
        const auto first_x = std::max({
            ruler_content.left(), ruler.left(), dirty.left()});
        const auto last_x = std::min({
            ruler_content.right(), ruler.right(), dirty.right()});
        if (last_x >= first_x) {
            const auto first_visible_frame = std::max<std::int64_t>(
                0,
                static_cast<std::int64_t>(std::floor(
                    (first_x - ruler_content.left()) / minor_frame_width)) - 1);
            const auto last_visible_frame = std::min<std::int64_t>(
                ruler_end,
                static_cast<std::int64_t>(std::ceil(
                    (last_x - ruler_content.left()) / minor_frame_width)) + 1);
            const auto first_frame = first_visible_frame -
                first_visible_frame % minor_step;

            painter.save();
            painter.setPen(QPen(QColor(82, 94, 110, 100), 1.0));
            for (auto frame = first_frame;
                 frame <= last_visible_frame;
                 frame += minor_step) {
                if (frame % tick_step != 0) {
                    const auto x = contentXForFrame(frame);
                    painter.drawLine(
                        QPointF(x, ruler.top()),
                        QPointF(x, ruler.bottom()));
                }
                if (frame > last_visible_frame - minor_step) break;
            }
            painter.restore();
        }
    }
    const auto fps = frameRate();
    painter.setFont(QFont(painter.font().family(), 8));
    for (std::int64_t frame = 0; frame <= ruler_end; frame += tick_step) {
        const auto x = contentXForFrame(frame);
        painter.setPen(QColor("#252d38"));
        painter.drawLine(QPointF(x, ruler.top()), QPointF(x, ruler.bottom()));
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

    for (std::size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
        const auto row = trackRect(track_index);
        const auto content = trackContentRect(track_index);
        const bool active_track = active_clip_.has_value() &&
            active_clip_->track_index == track_index;
        painter.setPen(active_track ? QColor("#d5a94b") : QColor("#3d4654"));
        painter.setBrush(active_track ? QColor("#252d3a") : QColor("#202631"));
        painter.drawRoundedRect(row, 4, 4);
        paintTrackHeaderCell(painter, track_index, row);

        for (std::size_t clip_index = 0;
             clip_index < tracks_[track_index].clips.size(); ++clip_index) {
            const ClipLocation location{track_index, clip_index};
            const auto rect = clipRect(location);
            const auto& clip = displayedClip(location);
            const bool active = active_clip_.has_value() &&
                *active_clip_ == location;
            const auto clip_id = clip.clip_id;
            const bool moving = interaction_controller_.moveActive() &&
                interaction_controller_.movingClipId() == clip_id;
            const bool trimming = interaction_controller_.trimGesture().active() &&
                interaction_controller_.trimGesture().location() == location;
            const QColor track_colors[] = {
                QColor("#3c75ae"), QColor("#357f70"),
                QColor("#6d5ca8"), QColor("#9b6943")};
            const auto clip_color = clip.kind == ClipKind::Text
                ? QColor("#8c5fb3")
                : track_colors[track_index % 4];
            if (moving) {
                painter.setPen(QColor(255, 255, 255, 90));
                painter.setBrush(QColor(
                    clip_color.red(), clip_color.green(), clip_color.blue(), 55));
            } else {
                painter.setPen(active ? QColor("#ffcf5c") : clip_color.lighter(135));
                painter.setBrush(trimming
                    ? QColor("#8a5a2f")
                    : active ? clip_color.lighter(115) : clip_color);
            }
            painter.drawRoundedRect(rect, 3, 3);
            painter.setPen(moving ? QColor(244, 247, 251, 100) : QColor("#f4f7fb"));
            const auto label = QString("%1  %2%3")
                .arg(clip_index + 1)
                .arg(clip.kind == ClipKind::Text ? "[Text] " : "")
                .arg(text(clip.display_name))
                + " - " + clipDuration(clip);
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

        const auto& trim_preview = interaction_controller_.trimGesture().preview();
        if (interaction_controller_.trimGesture().active() && trim_preview.has_value() &&
            trim_preview->clip_location.track_index == track_index &&
            !trim_preview->neighbor_clip.has_value()) {
            const auto rect = clipRect(trim_preview->clip_location);
            const auto edge_x = interaction_controller_.trimGesture().edge() == ClipEdge::Left
                ? rect.left()
                : rect.right();
            painter.setPen(QPen(QColor("#fff0a3"), 2.0, Qt::SolidLine));
            painter.drawLine(
                QPointF(edge_x, row.top() + 2.0),
                QPointF(edge_x, row.bottom() - 2.0));
        }

        for (const auto& transition : tracks_[track_index].transitions) {
            const auto indexes = [&]() -> std::optional<std::pair<std::size_t, std::size_t>> {
                std::optional<std::size_t> from;
                std::optional<std::size_t> to;
                for (std::size_t index = 0; index < tracks_[track_index].clips.size(); ++index) {
                    if (displayedClip(ClipLocation{track_index, index}).clip_id ==
                        transition.from_clip_id) {
                        from = index;
                    }
                    if (displayedClip(ClipLocation{track_index, index}).clip_id ==
                        transition.to_clip_id) {
                        to = index;
                    }
                }
                if (!from.has_value() || !to.has_value()) return std::nullopt;
                return std::make_pair(*from, *to);
            }();
            if (!indexes.has_value() || indexes->second != indexes->first + 1 || total <= 0) {
                continue;
            }
            const auto& from = displayedClip(
                ClipLocation{track_index, indexes->first});
            const auto& to = displayedClip(
                ClipLocation{track_index, indexes->second});
            if (transition.duration_frames > std::min(
                    from.timeline_duration_frames,
                    to.timeline_duration_frames)) {
                continue;
            }
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

    // At frame-level zoom, draw only the frame boundaries that intersect the
    // current paint region. Keep these guides inside the time ruler so clip
    // content remains visually clear at high zoom levels.
    const auto frame_grid_content = trackContentRect(0);
    const auto frame_width = pixelsPerFrame();
    if (ruler_end > 0 && frame_width >= 1.0 &&
        frame_grid_content.width() > 0.0) {
        const auto dirty = event != nullptr
            ? QRectF(event->rect())
            : QRectF(rect());
        const auto first_x = std::max({
            frame_grid_content.left(), ruler.left(), dirty.left()});
        const auto last_x = std::min({
            frame_grid_content.right(), ruler.right(), dirty.right()});
        if (last_x >= first_x) {
            const auto first_frame = std::max<std::int64_t>(
                0,
                static_cast<std::int64_t>(std::floor(
                    (first_x - frame_grid_content.left()) / frame_width)) - 1);
            const auto last_frame = std::min<std::int64_t>(
                ruler_end,
                static_cast<std::int64_t>(std::ceil(
                    (last_x - frame_grid_content.left()) / frame_width)) + 1);

            painter.save();
            painter.setPen(QPen(QColor(92, 105, 122, 105), 1.0));
            for (auto frame = first_frame; frame <= last_frame; ++frame) {
                const auto x = contentXForFrame(frame);
                painter.drawLine(
                    QPointF(x, ruler.top()),
                    QPointF(x, ruler.bottom()));
                if (frame == last_frame) break;
            }
            painter.restore();
        }
    }

    TimelineInteractionPaintState interaction_paint;
    interaction_paint.tracks = &tracks_;
    const auto paint_geometry = geometry();
    interaction_paint.geometry = &paint_geometry;
    const auto move_target = interaction_controller_.moveTarget();
    if (interaction_controller_.moveActive()) {
        interaction_paint.moving_clip = locationForClip(
            tracks_, interaction_controller_.movingClipId());
        if (move_target.has_value()) {
            interaction_paint.move_target_track =
                indexForTrack(tracks_, move_target->track_id);
            interaction_paint.move_target_frame = move_target->timeline_start_frame;
            interaction_paint.snap_guide_frame = move_target->snap_guide_frame;
        }
    }
    const auto& drop = interaction_controller_.dropPreview();
    interaction_paint.invalid_marker_position =
        interaction_controller_.moveActive() ? move_preview_position_ : drop.pointer_position;
    interaction_paint.media_drop_hovered = drop.hovering && drop.media;
    interaction_paint.drop_hover_track = drop.hovering
        ? drop.target_track_index : std::nullopt;
    interaction_paint.drop_hover_frame = drop.hovering
        ? drop.target_frame : std::nullopt;
    interaction_paint.drop_duration_frames = drop.duration_frames;
    interaction_paint.drop_label = drop.label;
    interaction_paint.drop_valid = drop.valid;
    if (!interaction_paint.moving_clip.has_value()) {
        interaction_paint.snap_guide_frame = drop.snap_guide_frame;
    }
    TimelineInteractionPainter::paint(painter, interaction_paint);

    const bool active_clip_valid = active_clip_.has_value() &&
        active_clip_->track_index < tracks_.size() &&
        active_clip_->clip_index < tracks_[active_clip_->track_index].clips.size();
    // The playhead is independent from clip selection. Keep it visible while
    // the timeline has content, including after a gap click clears selection.
    if (interaction_controller_.rulerSeekPending() || active_clip_valid || totalDuration() > 0) {
        const auto content = trackContentRect(0);
        const auto content_duration = std::max<std::int64_t>(1, totalDuration());
        const auto visual_duration = std::max<std::int64_t>(1, displayDuration());
        const auto ruler_frame = interaction_controller_.rulerPreviewFrame();
        auto global_frame = ruler_frame.value_or(playhead_frame_);
        const auto drag_frame = interaction_controller_.seekPreviewLocalFrame();
        if (!ruler_frame.has_value() && active_clip_valid && drag_frame.has_value()) {
            const auto& clip = tracks_[active_clip_->track_index]
                .clips[active_clip_->clip_index];
            global_frame = clip.timeline_start_frame + *drag_frame;
        }
        global_frame = std::clamp<std::int64_t>(global_frame, 0, content_duration - 1);
        const auto x = std::clamp(
            interaction_controller_.rulerContentX().value_or(content.left() + content.width() *
                static_cast<double>(global_frame) / visual_duration),
            content.left(),
            content.right());
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
    if (isSupportedDrop(event->mimeData())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TimelineWidget::dragLeaveEvent(QDragLeaveEvent* event) {
    clearDropHover();
    event->accept();
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (updateDropHover(event->mimeData(), event->position())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TimelineWidget::dropEvent(QDropEvent* event) {
    if (processDrop(event->mimeData(), event->position())) {
        event->acceptProposedAction();
        update();
    } else {
        clearDropHover();
        event->ignore();
    }
}

bool TimelineWidget::isSupportedDrop(const QMimeData* mime_data) const noexcept {
    return mime_data != nullptr &&
        (mime_data->hasFormat(ui::kMediaPathMimeType) ||
         mime_data->hasFormat(ui::kEffectIdMimeType));
}

void TimelineWidget::clearDragPreview() {
    interaction_controller_.clearDropGhost();
}

void TimelineWidget::clearDropHover() {
    interaction_controller_.clearDropPreview();
    update();
}

bool TimelineWidget::updateDropHover(
    const QMimeData* mime_data,
    const QPointF& position) {
    const auto track = trackAt(position.y());
    const auto frame = globalFrameAt(position.x());
    const bool supported = isSupportedDrop(mime_data);
    bool accepted = supported &&
        track.has_value() && frame.has_value();
    TimelineDropPreview preview;
    preview.hovering = supported;
    preview.pointer_position = position;
    if (accepted) {
        preview.target_track_index = track;
        preview.target_frame = frame;
    }
    if (supported && mime_data->hasFormat(ui::kMediaPathMimeType)) {
        preview.media = true;
        preview.duration_frames = mediaDropDuration(mime_data);
        preview.label = mediaDropLabel(mime_data);
        if (accepted) {
            const auto snapped = snapPlacement(
                *track,
                *frame,
                preview.duration_frames);
            preview.target_frame = snapped.start_frame;
            preview.snap_guide_frame = snapped.guide_frame;
            preview.valid = !placementOverlaps(
                *track, *preview.target_frame, preview.duration_frames);
        }
    } else if (supported && mime_data->hasFormat(ui::kEffectIdMimeType)) {
        const auto effect_id = QString::fromUtf8(
            mime_data->data(ui::kEffectIdMimeType));
        const bool is_transition_effect =
            effect_id == QStringLiteral("transitions.cross_dissolve") ||
            effect_id == QStringLiteral("transitions.fade_to_black");
        if (is_transition_effect) {
            const auto indexes = transitionClipIndexesAt(
                position.x(), position.y());
            if (!indexes.has_value() || !track.has_value()) {
                accepted = false;
                preview.target_track_index.reset();
                preview.target_frame.reset();
            } else {
                const auto& clips = tracks_[*track].clips;
                preview.target_track_index = track;
                preview.target_frame = clips[indexes->second].timeline_start_frame;
            }
        }
    }
    interaction_controller_.setDropPreview(std::move(preview));
    update();
    return accepted;
}

bool TimelineWidget::processDrop(
    const QMimeData* mime_data,
    const QPointF& position) {
    const auto track = trackAt(position.y());
    const auto frame = globalFrameAt(position.x());
    const bool is_media_drop = mime_data != nullptr &&
        mime_data->hasFormat(ui::kMediaPathMimeType);
    const bool is_effect_drop = mime_data != nullptr &&
        mime_data->hasFormat(ui::kEffectIdMimeType);
    if ((!is_media_drop && !is_effect_drop) ||
        !track.has_value() || !frame.has_value()) {
        return false;
    }

    auto target_frame = *frame;
    if (is_media_drop) {
        target_frame = snapPlacement(
            *track,
            *frame,
            mediaDropDuration(mime_data)).start_frame;
    }
    if (is_effect_drop) {
        const auto effect_id = QString::fromUtf8(
            mime_data->data(ui::kEffectIdMimeType));
        const bool is_cross_dissolve = effect_id ==
            QStringLiteral("transitions.cross_dissolve");
        const bool is_fade_to_black = effect_id ==
            QStringLiteral("transitions.fade_to_black");
        if (is_cross_dissolve || is_fade_to_black) {
            const auto indexes = transitionClipIndexesAt(
                position.x(), position.y());
            if (!indexes.has_value()) return false;
            clearDropHover();
            const auto& track_value = tracks_[*track];
            emit transitionAddRequested(
                track_value.track_id,
                track_value.clips[indexes->first].clip_id,
                track_value.clips[indexes->second].clip_id,
                is_cross_dissolve ? 0 : 1);
            return true;
        }
    }
    clearDropHover();
    if (is_media_drop) {
        const auto path = QString::fromUtf8(
            mime_data->data(ui::kMediaPathMimeType));
        if (path.isEmpty()) return false;
        emit mediaDropRequested(
            path,
            tracks_[*track].track_id,
            target_frame);
    } else {
        const auto effect_id = QString::fromUtf8(
            mime_data->data(ui::kEffectIdMimeType));
        if (effect_id.isEmpty()) return false;
        emit effectDropRequested(effect_id, tracks_[*track].track_id, *frame);
    }
    return true;
}

void TimelineWidget::showTransitionMenu(
    const QPoint& position,
    const QPoint& global_position) {
    const auto indexes = transitionClipIndexesAt(position.x(), position.y());
    const auto track_index = trackAt(position.y());
    if (!indexes.has_value() || !track_index.has_value()) return;

    selected_transition_ = SelectedTransition{
        *track_index, indexes->first, indexes->second};
    const auto& track = tracks_[*track_index];
    const auto& from = track.clips[indexes->first];
    const auto& to = track.clips[indexes->second];
    emit transitionSelected(track.track_id, from.clip_id, to.clip_id);
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
        emit transitionAddRequested(track.track_id, from.clip_id, to.clip_id, 0);
    } else if (chosen == fade) {
        emit transitionAddRequested(track.track_id, from.clip_id, to.clip_id, 1);
    } else if (chosen == remove) {
        emit transitionRemoveRequested(track.track_id, from.clip_id, to.clip_id);
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

void TimelineWidget::leaveEvent(QEvent* event) {
    if (!interaction_controller_.trimGesture().active()) unsetCursor();
    QWidget::leaveEvent(event);
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
    if (rulerRect().contains(event->position())) {
        const auto ruler = rulerRect();
        const auto ruler_x = std::clamp(
            event->position().x(), ruler.left(), ruler.right());
        const auto frame = playheadFrameAtRulerX(ruler_x);
        if (!frame.has_value()) {
            event->ignore();
            return;
        }
        interaction_controller_.beginRulerSeek(*frame, ruler_x);
        emit seekStarted();
        grabMouse();
        update();
        event->accept();
        return;
    }
    const auto location = clipAt(event->position().x(), event->position().y());
    if (!location.has_value()) {
        if (trackAt(event->position().y()).has_value() &&
            globalFrameAt(event->position().x()).has_value()) {
            active_clip_.reset();
            selected_transition_.reset();
            emit clipSelectionCleared();
            emit transitionSelectionCleared();
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
        emitSelected(*location);
        const auto& clip = tracks_[location->track_index].clips[location->clip_index];
        interaction_controller_.beginSplit(
            clip.clip_id, *frame, event->position());
        grabMouse();
        event->accept();
        return;
    }
    const auto edge = trimEdgeAt(*location, event->position().x());
    const auto transition_indexes = transitionClipIndexesAt(
        event->position().x(), event->position().y());
    const auto trim_mode = edge.has_value()
        ? trimEditModeAt(*location, *edge, event->position().x())
        : ClipEdgeEditMode::Individual;
    if (transition_indexes.has_value() && edge.has_value()) {
        const auto& clip = tracks_[location->track_index].clips[location->clip_index];
        const auto scale_duration = displayDuration();
        const auto original_boundary = *edge == ClipEdge::Left
            ? clip.timeline_start_frame
            : clip.timeline_start_frame + clip.timeline_duration_frames;
        interaction_controller_.trimGesture().begin(
            tracks_, *location, *edge, trim_mode, original_boundary,
            scale_duration, trimPointer(event->position()), *transition_indexes);
        grabMouse();
        setCursor(Qt::SizeHorCursor);
        event->accept();
        update();
        return;
    }
    if (transition_indexes.has_value()) {
        selected_transition_ = SelectedTransition{
            trackAt(event->position().y()).value(),
            transition_indexes->first,
            transition_indexes->second};
        const auto& track = tracks_[selected_transition_->track_index];
        emit transitionSelected(
            track.track_id,
            track.clips[transition_indexes->first].clip_id,
            track.clips[transition_indexes->second].clip_id);
        event->accept();
        update();
        return;
    }
    if (edge.has_value()) {
        const auto scale_duration = displayDuration();
        const auto& clip = tracks_[location->track_index].clips[location->clip_index];
        const auto original_boundary = *edge == ClipEdge::Left
            ? clip.timeline_start_frame
            : clip.timeline_start_frame + clip.timeline_duration_frames;
        emitSelected(*location);
        emit trimStarted();
        interaction_controller_.trimGesture().begin(
            tracks_, *location, *edge, trim_mode, original_boundary,
            scale_duration, trimPointer(event->position()));
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
        const auto& clip = tracks_[location->track_index].clips[location->clip_index];
        interaction_controller_.beginMove(
            clip.clip_id, tracks_[location->track_index].track_id,
            clip.timeline_start_frame, event->position());
        move_preview_position_ = event->position();
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
    const auto& clip = tracks_[location->track_index].clips[location->clip_index];
    interaction_controller_.beginSeek(
        clip.clip_id, clip.timeline_start_frame, *frame, event->position());
    grabMouse();
    event->accept();
    update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    if (interaction_controller_.rulerSeekPending()) {
        const auto ruler = rulerRect();
        const auto ruler_x = std::clamp(
            event->position().x(), ruler.left(), ruler.right());
        if (const auto frame = playheadFrameAtRulerX(ruler_x);
            frame.has_value()) {
            interaction_controller_.updateRulerSeek(*frame, ruler_x);
            update();
        }
        event->accept();
        return;
    }
    if (interaction_controller_.movePending()) {
        if (!interaction_controller_.updateMoveActivation(event->position())) {
            event->accept();
            return;
        }
        emit trimStarted();
    }
    if (interaction_controller_.moveActive()) {
        move_preview_position_ = event->position();
        const auto move_target_index = trackAt(event->position().y());
        std::optional<TimelineMoveTarget> target;
        if (move_target_index.has_value()) {
            const auto raw_frame = globalFrameAt(event->position().x());
            if (raw_frame.has_value()) {
                const auto source_location = locationForClip(
                    tracks_, interaction_controller_.movingClipId());
                if (!source_location.has_value()) {
                    interaction_controller_.setMoveTarget(std::nullopt);
                    update();
                    event->accept();
                    return;
                }
                const auto& clip = tracks_[source_location->track_index]
                    .clips[source_location->clip_index];
                const auto snapped = snapPlacement(
                    *move_target_index, *raw_frame, clip.timeline_duration_frames,
                    source_location);
                target = TimelineMoveTarget{
                    tracks_[*move_target_index].track_id,
                    snapped.start_frame, snapped.guide_frame};
            } else {
                target = TimelineMoveTarget{
                    tracks_[*move_target_index].track_id, 0, std::nullopt};
            }
        }
        interaction_controller_.setMoveTarget(target);
        update();
        event->accept();
        return;
    }
    if (interaction_controller_.trimGesture().active()) {
        const auto movement = interaction_controller_.trimGesture().move(
            tracks_, trimBoundaryAt(event->position().x()),
            trimPointer(event->position()));
        if (movement.started) {
            const auto active_gesture = interaction_controller_.trimGesture();
            emitSelected(active_gesture.location());
            emit trimStarted();
            // Selection can synchronously refresh the widget's tracks and
            // cancel the gesture. The old path resumed this trim afterward.
            interaction_controller_.trimGesture() = active_gesture;
            grabMouse();
            setCursor(Qt::SizeHorCursor);
        }
        if (movement.repaint) update();
        event->accept();
        return;
    }
    if (interaction_controller_.splitPending()) {
        interaction_controller_.updateSplit(event->position());
        update();
        event->accept();
        return;
    }
    if (interaction_controller_.seekPending()) {
        std::optional<std::int64_t> local_frame;
        if (const auto location = locationForClip(
                tracks_, interaction_controller_.seekClipId());
            location.has_value()) {
            local_frame = localFrameAt(*location, event->position().x());
        }
        const auto started = interaction_controller_.updateSeek(
            event->position(), local_frame);
        if (started) {
            emit seekStarted();
        } else {
            event->accept();
            return;
        }
    }
    if (interaction_controller_.seekDragging()) {
        std::optional<std::int64_t> local_frame;
        if (const auto location = locationForClip(
                tracks_, interaction_controller_.seekClipId());
            location.has_value()) {
            local_frame = localFrameAt(*location, event->position().x());
        }
        static_cast<void>(interaction_controller_.updateSeek(
            event->position(), local_frame));
        update();
        event->accept();
    } else {
        updateTrimHoverCursor(event->position());
        event->ignore();
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    if (interaction_controller_.rulerSeekPending()) {
        const auto frame = interaction_controller_.finishRulerSeek();
        releaseMouse();
        if (frame.has_value()) emit seekRequested(*frame);
        update();
        event->accept();
        return;
    }
    if (interaction_controller_.moveActive() || interaction_controller_.movePending()) {
        const auto request = interaction_controller_.finishMove();
        clearDragPreview();
        releaseMouse();
        if (request.has_value()) {
            emit clipMoveRequested(
                request->clip_id, request->target_track_id,
                request->timeline_start_frame);
        }
        updateTrimHoverCursor(event->position());
        update();
        event->accept();
        return;
    }
    if (interaction_controller_.trimGesture().active()) {
        const auto result = interaction_controller_.trimGesture().finish(
            tracks_, trimBoundaryAt(event->position().x()),
            trimPointer(event->position()));
        unsetCursor();
        releaseMouse();
        if (result.kind == TrimGestureFinish::Kind::SelectTransition &&
            result.transition_pair.has_value()) {
            const auto& pair = *result.transition_pair;
            selected_transition_ = SelectedTransition{
                result.location.track_index, pair.first, pair.second};
            const auto& track = tracks_[result.location.track_index];
            emit transitionSelected(
                track.track_id,
                track.clips[pair.first].clip_id,
                track.clips[pair.second].clip_id);
        } else if (result.kind == TrimGestureFinish::Kind::RequestTrim) {
            const auto clip_id = tracks_[result.location.track_index]
                .clips[result.location.clip_index].clip_id;
            emit clipEdgeTrimRequested(
                clip_id,
                static_cast<qint64>(result.edge),
                result.boundary_frame,
                static_cast<qint64>(result.mode));
        }
        updateTrimHoverCursor(event->position());
        update();
        event->accept();
        return;
    }
    if (interaction_controller_.splitPending()) {
        const auto request = interaction_controller_.finishSplit();
        releaseMouse();
        if (request.has_value()) {
            emit clipSplitRequested(request->clip_id, request->local_frame);
        }
        update();
        event->accept();
        return;
    }
    if (interaction_controller_.seekDragging()) {
        const auto frame = interaction_controller_.finishSeek();
        releaseMouse();
        if (frame.has_value()) emit seekRequested(*frame);
        updateTrimHoverCursor(event->position());
        update();
        event->accept();
        return;
    }
    if (interaction_controller_.seekPending()) {
        static_cast<void>(interaction_controller_.finishSeek());
        releaseMouse();
        updateTrimHoverCursor(event->position());
        update();
        event->accept();
        return;
    }
    event->ignore();
}

void TimelineWidget::wheelEvent(QWheelEvent* event) {
    if (event == nullptr) return;
    const auto modifiers = event->modifiers();
    if (modifiers.testFlag(Qt::ControlModifier)) {
        const auto vertical_delta = event->angleDelta().y();
        if (vertical_delta == 0) {
            event->ignore();
            return;
        }
        const auto next_factor = nextZoomFactor(vertical_delta > 0 ? 1 : -1);
        if (std::abs(next_factor - zoom_factor_) < 0.000001) {
            event->accept();
            return;
        }
        emit zoomRequested(next_factor);
        event->accept();
        return;
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        const auto pixel_delta = event->pixelDelta().y();
        const auto angle_delta = event->angleDelta().y();
        const auto height_delta = pixel_delta != 0
            ? static_cast<double>(pixel_delta)
            : static_cast<double>(angle_delta) / 8.0;
        if (std::abs(height_delta) < 0.000001) {
            event->ignore();
            return;
        }
        setTrackRowHeight(track_row_height_ + height_delta);
        event->accept();
        return;
    }
    event->ignore();
}

} // namespace timeline
