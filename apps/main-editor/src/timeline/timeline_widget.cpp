#include "timeline_widget.h"

#include "../ui/media_drag_mime.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetrics>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPointF>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace timeline {
namespace {

constexpr double track_left = 12.0;
constexpr double track_right = 12.0;
constexpr double track_top = 32.0;
constexpr double track_height = 44.0;

QString fromUtf8(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

QString formatDuration(const std::optional<double>& duration) {
    if (!duration.has_value()) return "Unknown duration";
    return QString::number(*duration, 'f', 3) + " s";
}

QRectF trackRect(const QWidget* widget) {
    return QRectF(
        track_left,
        track_top,
        static_cast<double>(widget->width()) - track_left - track_right,
        track_height);
}

} // namespace

TimelineWidget::TimelineWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setFocusPolicy(Qt::NoFocus);
    setAcceptDrops(true);
}

void TimelineWidget::setClips(const std::vector<TimelineClip>& clips) {
    clips_ = clips;
    if (active_clip_index_.has_value() &&
        *active_clip_index_ >= clips_.size()) {
        active_clip_index_.reset();
        playhead_frame_ = 0;
    }
    drag_frame_.reset();
    dragging_ = false;
    drag_hovering_ = false;
    update();
}

void TimelineWidget::clearClips() {
    clips_.clear();
    active_clip_index_.reset();
    playhead_frame_ = 0;
    drag_frame_.reset();
    dragging_ = false;
    drag_hovering_ = false;
    update();
}

void TimelineWidget::setActiveClipIndex(
    std::optional<std::size_t> clip_index) {
    if (clip_index.has_value() && *clip_index >= clips_.size()) {
        active_clip_index_.reset();
    } else {
        active_clip_index_ = clip_index;
    }

    playhead_frame_ = 0;
    drag_frame_.reset();
    dragging_ = false;
    update();
}

void TimelineWidget::setPlayheadFrame(std::int64_t frame_index) {
    playhead_frame_ = std::max<std::int64_t>(0, frame_index);
    if (const auto index = activeClipIndex(); index.has_value()) {
        const auto duration = clips_[*index].timeline_duration_frames;
        if (duration > 0) {
            playhead_frame_ = std::min(playhead_frame_, duration - 1);
        }
    }
    drag_frame_.reset();
    update();
}

bool TimelineWidget::isTrackPosition(const QPointF& position) const noexcept {
    const auto rect = trackRect(this);
    return rect.width() > 0.0 && rect.contains(position);
}

std::optional<std::size_t> TimelineWidget::activeClipIndex() const noexcept {
    if (!active_clip_index_.has_value() ||
        *active_clip_index_ >= clips_.size()) {
        return std::nullopt;
    }
    return active_clip_index_;
}

std::optional<std::size_t> TimelineWidget::clipIndexAtPosition(
    double x) const noexcept {
    if (clips_.empty()) return std::nullopt;

    const auto track = trackRect(this).adjusted(4.0, 4.0, -4.0, -4.0);
    const auto total_duration = clips_.back().timeline_start_frame +
        clips_.back().timeline_duration_frames;
    if (track.width() <= 0.0 || total_duration <= 0 ||
        x < track.left() || x > track.right()) {
        return std::nullopt;
    }

    for (std::size_t index = 0; index < clips_.size(); ++index) {
        const auto& clip = clips_[index];
        const double left = track.left() + track.width() *
            static_cast<double>(clip.timeline_start_frame) /
            static_cast<double>(total_duration);
        const double right = track.left() + track.width() *
            static_cast<double>(clip.timeline_start_frame +
                                clip.timeline_duration_frames) /
            static_cast<double>(total_duration);
        if (x >= left && x <= right) return index;
    }

    return std::nullopt;
}

std::optional<std::int64_t> TimelineWidget::frameAtPosition(double x) const noexcept {
    const auto index = activeClipIndex();
    if (!index.has_value()) return std::nullopt;

    const auto& clip = clips_[*index];
    if (clip.timeline_duration_frames <= 0) return std::nullopt;

    const auto track = trackRect(this).adjusted(4.0, 4.0, -4.0, -4.0);
    const auto total_duration = clips_.empty()
        ? 0
        : clips_.back().timeline_start_frame +
            clips_.back().timeline_duration_frames;
    if (track.width() <= 0.0 || total_duration <= 0 ||
        x < track.left() || x > track.right()) {
        return std::nullopt;
    }

    const double clip_left = track.left() + track.width() *
        static_cast<double>(clip.timeline_start_frame) /
        static_cast<double>(total_duration);
    const double clip_right = track.left() + track.width() *
        static_cast<double>(clip.timeline_start_frame +
                            clip.timeline_duration_frames) /
        static_cast<double>(total_duration);
    if (x < clip_left || x > clip_right) return std::nullopt;

    const double clip_width = clip_right - clip_left;
    if (clip_width <= 0.0) return std::nullopt;

    const double fraction = std::clamp((x - clip_left) / clip_width, 0.0, 1.0);
    return static_cast<std::int64_t>(std::llround(
        fraction * static_cast<double>(clip.timeline_duration_frames - 1)));
}

std::optional<double> TimelineWidget::playheadFraction() const noexcept {
    const auto index = activeClipIndex();
    if (!index.has_value()) return std::nullopt;

    const auto duration = clips_[*index].timeline_duration_frames;
    if (duration <= 1) return 0.0;

    return std::clamp(
        displayedPlayheadFrame() / static_cast<double>(duration - 1),
        0.0,
        1.0);
}

double TimelineWidget::displayedPlayheadFrame() const noexcept {
    return static_cast<double>(drag_frame_.value_or(playhead_frame_));
}

void TimelineWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), palette().base());

    const auto track = trackRect(this);
    if (track.width() <= 0.0) return;

    painter.setPen(drag_hovering_ ? QColor("#8cc8ff") : QColor("#596273"));
    painter.setBrush(drag_hovering_ ? QColor("#315d8c") : QColor("#252b36"));
    painter.drawRoundedRect(track, 4.0, 4.0);

    painter.setPen(palette().text().color());
    painter.drawText(
        QRectF(track_left, 8.0, track.width(), 20.0),
        "Video Track 1");

    if (clips_.empty()) {
        painter.setPen(QColor("#9aa4b2"));
        painter.drawText(
            track,
            Qt::AlignCenter,
            "Add a media item to the timeline.");
        return;
    }

    const auto total_duration = clips_.back().timeline_start_frame +
        clips_.back().timeline_duration_frames;
    if (total_duration <= 0) return;

    const auto active_index = activeClipIndex();
    const auto clip_track = track.adjusted(4.0, 4.0, -4.0, -4.0);
    for (std::size_t index = 0; index < clips_.size(); ++index) {
        const auto& clip = clips_[index];
        const double start_fraction = static_cast<double>(
            clip.timeline_start_frame) / static_cast<double>(total_duration);
        const double end_fraction = static_cast<double>(
            clip.timeline_start_frame + clip.timeline_duration_frames) /
            static_cast<double>(total_duration);
        const double left = clip_track.left() + clip_track.width() * start_fraction;
        const double right = clip_track.left() + clip_track.width() * end_fraction;
        const QRectF clip_rect(
            left,
            clip_track.top(),
            std::max(2.0, right - left),
            clip_track.height());
        const bool active = active_index.has_value() && *active_index == index;

        painter.setPen(active ? QColor("#ffcf5c") : QColor("#7db7ff"));
        painter.setBrush(active ? QColor("#386e9f") : QColor("#315d8c"));
        painter.drawRoundedRect(clip_rect, 3.0, 3.0);

        const QString label = fromUtf8(clip.display_name) + " - " +
            formatDuration(clip.duration_seconds);
        const QString elided = QFontMetrics(painter.font()).elidedText(
            label,
            Qt::ElideRight,
            static_cast<int>(clip_rect.width() - 12.0));
        painter.setPen(Qt::white);
        painter.drawText(
            clip_rect.adjusted(6.0, 0.0, -6.0, 0.0),
            Qt::AlignVCenter,
            elided);
    }

    if (const auto fraction = playheadFraction(); fraction.has_value() &&
        active_index.has_value()) {
        const auto& clip = clips_[*active_index];
        const double start_fraction = static_cast<double>(
            clip.timeline_start_frame) / static_cast<double>(total_duration);
        const double end_fraction = static_cast<double>(
            clip.timeline_start_frame + clip.timeline_duration_frames) /
            static_cast<double>(total_duration);
        const qreal left = clip_track.left() + clip_track.width() * start_fraction;
        const qreal width = clip_track.width() * (end_fraction - start_fraction);
        const qreal x = left + width * *fraction;
        painter.setPen(QPen(QColor("#ffcf5c"), 2.0));
        painter.drawLine(QPointF(x, 24.0), QPointF(x, 84.0));
    }
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat(ui::kMediaPathMimeType)) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void TimelineWidget::dragLeaveEvent(QDragLeaveEvent* event) {
    drag_hovering_ = false;
    update();
    event->accept();
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent* event) {
    const bool accepted = event->mimeData()->hasFormat(ui::kMediaPathMimeType) &&
        isTrackPosition(event->position());
    drag_hovering_ = accepted;
    update();

    if (accepted) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TimelineWidget::dropEvent(QDropEvent* event) {
    const bool accepted = event->mimeData()->hasFormat(ui::kMediaPathMimeType) &&
        isTrackPosition(event->position());
    drag_hovering_ = false;
    update();

    if (!accepted) {
        event->ignore();
        return;
    }

    const QString source_path = QString::fromUtf8(
        event->mimeData()->data(ui::kMediaPathMimeType));
    if (source_path.isEmpty()) {
        event->ignore();
        return;
    }

    emit mediaDropRequested(source_path);
    event->acceptProposedAction();
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

    if (!isTrackPosition(event->position())) {
        event->ignore();
        return;
    }

    const auto clip_index = clipIndexAtPosition(event->position().x());
    if (!clip_index.has_value()) {
        event->ignore();
        return;
    }

    if (!activeClipIndex().has_value() ||
        *activeClipIndex() != *clip_index) {
        emit clipSelected(static_cast<qint64>(*clip_index));
        event->accept();
        return;
    }

    const auto frame = frameAtPosition(event->position().x());
    if (!frame.has_value()) {
        event->ignore();
        return;
    }

    dragging_ = true;
    drag_frame_ = *frame;
    grabMouse();
    emit seekStarted();
    update();
    event->accept();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) {
        event->ignore();
        return;
    }

    if (const auto frame = frameAtPosition(event->position().x()); frame.has_value()) {
        drag_frame_ = *frame;
        update();
    }
    event->accept();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (!dragging_ || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

    if (const auto frame = frameAtPosition(event->position().x()); frame.has_value()) {
        drag_frame_ = *frame;
    }

    dragging_ = false;
    releaseMouse();
    if (drag_frame_.has_value()) emit seekRequested(*drag_frame_);
    update();
    event->accept();
}

} // namespace timeline
