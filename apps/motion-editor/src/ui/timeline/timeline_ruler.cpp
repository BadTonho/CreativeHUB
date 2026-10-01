#include "timeline_ruler.h"
#include "timeline_navigator_math.h"
#include "timeline_view_mapping.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>
#include <QWheelEvent>

#include <algorithm>
#include <limits>

namespace motion::ui {
namespace {

constexpr std::int64_t kMaximumFrame = std::numeric_limits<std::int64_t>::max();

QString formatTimelinePosition(std::int64_t frame, model::FrameRate rate,
                               TimelineDisplayMode mode)
{
    if (mode == TimelineDisplayMode::Frames)
        return QString::number(static_cast<qlonglong>(frame));
    const auto text = detail::formatElapsedTime(frame, rate.numerator, rate.denominator);
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::int64_t firstTickAtOrAfter(std::int64_t start, std::int64_t step) noexcept
{
    if (step <= 1) return start;
    const auto remainder = start % step;
    return remainder == 0 ? start : detail::saturatingFrameAdd(start, step - remainder);
}

} // namespace
TimelineRuler::TimelineRuler(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("motion-timeline-ruler"));
    setMinimumHeight(82);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::PointingHandCursor);
}

void TimelineRuler::setHeaderWidth(int width)
{
    header_width_ = std::max(0, width);
    update();
}

void TimelineRuler::setMappingWidth(int width)
{
    mapping_width_ = std::max(0, width);
    update();
}

int TimelineRuler::mappingWidth() const noexcept
{
    return mapping_width_ > 0 ? mapping_width_ : width();
}

void TimelineRuler::setViewState(std::int64_t end_frame,
                                 std::int64_t current_frame,
                                 std::int64_t start_frame,
                                 std::int64_t frames_per_view,
                                 model::FrameRate frame_rate,
                                 TimelineDisplayMode display_mode)
{
    visible_end_frame_ = std::max<std::int64_t>(0, end_frame);
    current_frame_ = std::clamp(current_frame, std::int64_t{0}, visible_end_frame_);
    view_start_frame_ = std::clamp(start_frame, std::int64_t{0}, visible_end_frame_);
    frames_per_view_ = std::max<std::int64_t>(1, frames_per_view);
    frame_rate_ = frame_rate;
    display_mode_ = display_mode;
    update();
}

void TimelineRuler::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(35, 39, 46));
    painter.setRenderHint(QPainter::Antialiasing, true);

    const TimelineViewMapping mapping{
        mappingWidth(), header_width_, visible_end_frame_, view_start_frame_, frames_per_view_};
    const int axis_left = mapping.axisLeft();
    const int axis_right = mapping.axisRight();
    const int axis_y = 48;
    const int label_y = 17;

    painter.setPen(QPen(QColor(110, 118, 129), 1));
    painter.drawLine(axis_left, axis_y, axis_right, axis_y);

    const auto view_end = mapping.viewEndFrame();
    const QFontMetrics font_metrics(painter.font());
    const int label_width = std::max(
        font_metrics.horizontalAdvance(formatTimelinePosition(
            view_start_frame_, frame_rate_, display_mode_)),
        font_metrics.horizontalAdvance(formatTimelinePosition(
            view_end, frame_rate_, display_mode_))) + 12;
    const int label_box_width = std::max(96, label_width);
    const auto tick_step = detail::timelineRulerTickStep(
        frames_per_view_, mapping.axisWidth(), label_width);
    auto tick = firstTickAtOrAfter(view_start_frame_, tick_step);
    if (view_start_frame_ <= view_end && tick > view_end) {
        tick = view_start_frame_;
    }
    int previous_tick_x = std::numeric_limits<int>::min();
    const auto drawTick = [&](std::int64_t frame) {
        const int x = mapping.xForFrame(frame);
        if (x == previous_tick_x) {
            return;
        }
        previous_tick_x = x;
        painter.drawLine(x, axis_y - 5, x, axis_y + 5);
        painter.setPen(QColor(196, 201, 208));
        const auto label = formatTimelinePosition(frame, frame_rate_, display_mode_);
        painter.drawText(
            QRect(x - label_box_width / 2, label_y, label_box_width, 18),
            Qt::AlignHCenter | Qt::AlignVCenter,
            label);
        painter.setPen(QPen(QColor(110, 118, 129), 1));
    };
    if (view_start_frame_ <= view_end && view_start_frame_ % tick_step != 0) {
        drawTick(view_start_frame_);
    }
    for (auto frame = tick; frame <= view_end;) {
        drawTick(frame);
        if (frame > kMaximumFrame - tick_step) {
            break;
        }
        frame += tick_step;
    }
    if (view_end > view_start_frame_ && view_end % tick_step != 0) {
        drawTick(view_end);
    }

    if (current_frame_ >= view_start_frame_ && current_frame_ <= view_end) {
        const int playhead_x = mapping.xForFrame(current_frame_);
        painter.setPen(QPen(QColor(255, 183, 54), 2));
        painter.drawLine(playhead_x, 10, playhead_x, height() - 8);
        painter.setBrush(QColor(255, 183, 54));
        painter.setPen(Qt::NoPen);
        QPolygon marker;
        marker << QPoint(playhead_x - 6, 8) << QPoint(playhead_x + 6, 8)
               << QPoint(playhead_x, 17);
        painter.drawPolygon(marker);
    }
}

void TimelineRuler::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    dragging_ = true;
    range_extended_during_drag_ = false;
    last_mouse_x_ = event->position().toPoint().x();
    emit seekRequested(static_cast<qint64>(frameAtX(last_mouse_x_)));
    event->accept();
}

void TimelineRuler::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_ || (event->buttons() & Qt::LeftButton) == 0) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const int mouse_x = event->position().toPoint().x();
    if (mouse_x == last_mouse_x_) {
        event->accept();
        return;
    }
    last_mouse_x_ = mouse_x;
    handleDragX(mouse_x);
    event->accept();
}

void TimelineRuler::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        const int mouse_x = event->position().toPoint().x();
        if (mouse_x != last_mouse_x_) {
            last_mouse_x_ = mouse_x;
            handleDragX(mouse_x);
        }
        last_mouse_x_ = -1;
        range_extended_during_drag_ = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void TimelineRuler::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const int delta = event->angleDelta().y() != 0
            ? event->angleDelta().y() : event->pixelDelta().y();
        if (delta != 0) {
            emit zoomStepRequested(delta > 0 ? 1 : -1);
            event->accept();
            return;
        }
    }
    event->ignore();
}

std::int64_t TimelineRuler::frameAtX(int x) const noexcept
{
    return TimelineViewMapping{
        mappingWidth(), header_width_, visible_end_frame_, view_start_frame_, frames_per_view_
    }.frameAtX(x);
}

int TimelineRuler::xForFrame(std::int64_t frame) const noexcept
{
    return TimelineViewMapping{
        mappingWidth(), header_width_, visible_end_frame_, view_start_frame_, frames_per_view_
    }.xForFrame(frame);
}

void TimelineRuler::handleDragX(int x)
{
    const TimelineViewMapping mapping{
        mappingWidth(), header_width_, visible_end_frame_, view_start_frame_, frames_per_view_};
    const bool view_at_range_end = mapping.viewEndFrame() >= visible_end_frame_;
    const int range_end_x = mapping.xForFrame(visible_end_frame_);
    if (view_at_range_end && x > range_end_x) {
        if (!range_extended_during_drag_) {
            range_extended_during_drag_ = true;
            emit extendRangeRequested();
        }
        return;
    }
    emit seekRequested(static_cast<qint64>(frameAtX(x)));
}


} // namespace motion::ui

