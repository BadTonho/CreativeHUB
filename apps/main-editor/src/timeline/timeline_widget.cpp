#include "timeline_widget.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>

namespace timeline {
namespace {

QString fromUtf8(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

QString formatDuration(const std::optional<double>& duration) {
    if (!duration.has_value()) return "Unknown duration";
    return QString::number(*duration, 'f', 3) + " s";
}

} // namespace

TimelineWidget::TimelineWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setFocusPolicy(Qt::NoFocus);
}

void TimelineWidget::setClip(const TimelineClip* clip) {
    clip_ = clip != nullptr ? std::optional<TimelineClip>(*clip) : std::nullopt;
    playhead_frame_ = 0;
    update();
}

void TimelineWidget::clearClip() {
    clip_.reset();
    playhead_frame_ = 0;
    update();
}

void TimelineWidget::setPlayheadFrame(std::int64_t frame_index) {
    playhead_frame_ = std::max<std::int64_t>(0, frame_index);
    update();
}

std::optional<double> TimelineWidget::playheadFraction() const noexcept {
    if (!clip_.has_value()) return std::nullopt;

    if (clip_->frame_count.has_value() && *clip_->frame_count > 1) {
        const double last_frame = static_cast<double>(*clip_->frame_count - 1);
        return std::clamp(static_cast<double>(playhead_frame_) / last_frame, 0.0, 1.0);
    }

    if (clip_->duration_seconds.has_value() && clip_->frame_rate.has_value() &&
        *clip_->duration_seconds > 0.0 && *clip_->frame_rate > 0.0) {
        const double position_seconds =
            static_cast<double>(playhead_frame_) / *clip_->frame_rate;
        return std::clamp(position_seconds / *clip_->duration_seconds, 0.0, 1.0);
    }

    return std::nullopt;
}

void TimelineWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), palette().base());

    const QRectF track_rect = QRectF(12.0, 32.0, width() - 24.0, 44.0);
    if (track_rect.width() <= 0.0) return;

    painter.setPen(QColor("#596273"));
    painter.setBrush(QColor("#252b36"));
    painter.drawRoundedRect(track_rect, 4.0, 4.0);

    painter.setPen(palette().text().color());
    painter.drawText(QRectF(12.0, 8.0, width() - 24.0, 20.0), "Video Track 1");

    if (!clip_.has_value()) {
        painter.setPen(QColor("#9aa4b2"));
        painter.drawText(track_rect, Qt::AlignCenter, "Add a media item to the timeline.");
        return;
    }

    const QRectF clip_rect = track_rect.adjusted(4.0, 4.0, -4.0, -4.0);
    painter.setPen(QColor("#7db7ff"));
    painter.setBrush(QColor("#315d8c"));
    painter.drawRoundedRect(clip_rect, 3.0, 3.0);

    const QString label = fromUtf8(clip_->display_name) + " - " +
        formatDuration(clip_->duration_seconds);
    const QString elided = QFontMetrics(painter.font()).elidedText(
        label, Qt::ElideRight, static_cast<int>(clip_rect.width() - 12.0));
    painter.setPen(Qt::white);
    painter.drawText(clip_rect.adjusted(6.0, 0.0, -6.0, 0.0), Qt::AlignVCenter, elided);

    const auto fraction = playheadFraction();
    if (fraction.has_value()) {
        const qreal x = track_rect.left() + track_rect.width() * *fraction;
        painter.setPen(QPen(QColor("#ffcf5c"), 2.0));
        painter.drawLine(QPointF(x, 24.0), QPointF(x, 84.0));
    }
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    event->accept();
}

} // namespace timeline
