#include "timeline_navigator.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace motion::ui {
namespace {

constexpr int kRulerHorizontalPadding = 20;
constexpr std::int64_t kMaximumFrame = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kSecondsPerHour = 60 * 60;

QString formatFrameRate(model::FrameRate frame_rate)
{
    return QString::number(frame_rate.asDouble(), 'g', 6);
}

std::int64_t framesPerHour(model::FrameRate frame_rate) noexcept
{
    const auto hour_frames_numerator = frame_rate.numerator * kSecondsPerHour;
    const auto frame_count = (hour_frames_numerator + frame_rate.denominator - 1)
        / frame_rate.denominator;
    return std::max<std::int64_t>(1, frame_count);
}

std::int64_t initialVisibleEndFrame(model::FrameRate frame_rate) noexcept
{
    return framesPerHour(frame_rate) - 1;
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

void TimelineRuler::setVisibleEndFrame(std::int64_t frame)
{
    visible_end_frame_ = std::max<std::int64_t>(0, frame);
    update();
}

void TimelineRuler::setCurrentFrame(std::int64_t frame) noexcept
{
    current_frame_ = std::max<std::int64_t>(0, frame);
    update();
}

void TimelineRuler::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(35, 39, 46));
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int axis_left = std::min(kRulerHorizontalPadding, width() / 2);
    const int axis_right = std::max(axis_left, width() - kRulerHorizontalPadding);
    const int axis_y = 48;
    const int label_y = 17;
    const int axis_width = std::max(0, axis_right - axis_left);
    const auto last_frame = visible_end_frame_;

    painter.setPen(QPen(QColor(110, 118, 129), 1));
    painter.drawLine(axis_left, axis_y, axis_right, axis_y);

    const int label_digits = QString::number(static_cast<qlonglong>(last_frame)).size();
    const int minimum_tick_spacing = std::max(112, label_digits * 8 + 32);
    const int divisions = last_frame == 0 || axis_width <= 0
        ? 0
        : std::clamp(axis_width / minimum_tick_spacing, 1, 8);
    for (int division = 0; division <= divisions; ++division) {
        const long double fraction = divisions == 0
            ? 0.0L
            : static_cast<long double>(division) / static_cast<long double>(divisions);
        const int x = axis_left + static_cast<int>(std::llround(fraction * axis_width));
        const auto frame = frameAtX(x);
        painter.drawLine(x, axis_y - 5, x, axis_y + 5);
        painter.setPen(QColor(196, 201, 208));
        painter.drawText(
            QRect(x - 48, label_y, 96, 18),
            Qt::AlignHCenter | Qt::AlignVCenter,
            QString::number(static_cast<qlonglong>(frame)));
        painter.setPen(QPen(QColor(110, 118, 129), 1));
    }

    const int playhead_x = xForFrame(current_frame_);
    painter.setPen(QPen(QColor(255, 183, 54), 2));
    painter.drawLine(playhead_x, 10, playhead_x, height() - 8);
    painter.setBrush(QColor(255, 183, 54));
    painter.setPen(Qt::NoPen);
    QPolygon marker;
    marker << QPoint(playhead_x - 6, 8) << QPoint(playhead_x + 6, 8)
           << QPoint(playhead_x, 17);
    painter.drawPolygon(marker);
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
    if (mouse_x >= width()) {
        if (!range_extended_during_drag_) {
            range_extended_during_drag_ = true;
            emit extendRangeRequested();
            emit seekRequested(static_cast<qint64>(frameAtX(mouse_x)));
        }
    } else {
        emit seekRequested(static_cast<qint64>(frameAtX(mouse_x)));
    }
    event->accept();
}

void TimelineRuler::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        const int mouse_x = event->position().toPoint().x();
        if (mouse_x != last_mouse_x_) {
            last_mouse_x_ = mouse_x;
            if (mouse_x >= width()) {
                if (!range_extended_during_drag_) {
                    range_extended_during_drag_ = true;
                    emit extendRangeRequested();
                    emit seekRequested(static_cast<qint64>(frameAtX(mouse_x)));
                }
            } else {
                emit seekRequested(static_cast<qint64>(frameAtX(mouse_x)));
            }
        }
        last_mouse_x_ = -1;
        range_extended_during_drag_ = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

std::int64_t TimelineRuler::frameAtX(int x) const noexcept
{
    const auto last_frame = visible_end_frame_;
    if (last_frame == 0) {
        return 0;
    }

    const int axis_left = std::min(kRulerHorizontalPadding, width() / 2);
    const int axis_right = std::max(axis_left, width() - kRulerHorizontalPadding);
    const int axis_width = axis_right - axis_left;
    if (axis_width <= 0 || x <= axis_left) {
        return 0;
    }
    if (x >= axis_right) {
        return last_frame;
    }

    const long double fraction = static_cast<long double>(x - axis_left)
        / static_cast<long double>(axis_width);
    const long double rounded = std::floor(
        fraction * static_cast<long double>(last_frame) + 0.5L);
    if (rounded >= static_cast<long double>(last_frame)) {
        return last_frame;
    }
    return std::max<std::int64_t>(0, static_cast<std::int64_t>(rounded));
}

int TimelineRuler::xForFrame(std::int64_t frame) const noexcept
{
    const int axis_left = std::min(kRulerHorizontalPadding, width() / 2);
    const int axis_right = std::max(axis_left, width() - kRulerHorizontalPadding);
    const int axis_width = axis_right - axis_left;
    const auto last_frame = visible_end_frame_;
    if (axis_width <= 0 || last_frame == 0) {
        return axis_left;
    }

    const long double fraction = static_cast<long double>(frame)
        / static_cast<long double>(last_frame);
    return axis_left + static_cast<int>(std::llround(fraction * axis_width));
}

TimelineNavigator::TimelineNavigator(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("motion-timeline"));
    setMinimumHeight(124);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(4);

    auto* controls = new QHBoxLayout();
    controls->setContentsMargins(0, 0, 0, 0);
    previous_frame_button_ = new QPushButton(QStringLiteral("Previous frame"), this);
    previous_frame_button_->setObjectName(QStringLiteral("motion-timeline-previous-frame"));
    frame_label_ = new QLabel(this);
    frame_label_->setObjectName(QStringLiteral("motion-timeline-frame-readout"));
    frame_rate_label_ = new QLabel(this);
    frame_rate_label_->setObjectName(QStringLiteral("motion-timeline-frame-rate"));
    next_frame_button_ = new QPushButton(QStringLiteral("Next frame"), this);
    next_frame_button_->setObjectName(QStringLiteral("motion-timeline-next-frame"));
    controls->addWidget(previous_frame_button_);
    controls->addWidget(frame_label_);
    controls->addStretch(1);
    controls->addWidget(frame_rate_label_);
    controls->addWidget(next_frame_button_);
    layout->addLayout(controls);

    ruler_ = new TimelineRuler(this);
    ruler_->setToolTip(QStringLiteral(
        "Starts with one hour. Drag beyond the right edge to extend by one hour per drag. "
        "This range is not the composition end."));
    layout->addWidget(ruler_, 1);

    connect(previous_frame_button_, &QPushButton::clicked, this, [this] {
        seekToFrame(current_frame_ - (current_frame_ > 0 ? 1 : 0));
    });
    connect(next_frame_button_, &QPushButton::clicked, this, [this] {
        if (current_frame_ < visible_end_frame_) {
            seekToFrame(current_frame_ + 1);
        }
    });
    connect(ruler_, &TimelineRuler::seekRequested, this, [this](qint64 frame) {
        seekToFrame(static_cast<std::int64_t>(frame));
    });
    connect(ruler_, &TimelineRuler::extendRangeRequested, this, [this] {
        extendViewByOneHour();
    });

    updateControls();
}

void TimelineNavigator::setCompositionTiming(
    model::FrameRate frame_rate)
{
    frame_rate_ = frame_rate;
    current_frame_ = 0;
    visible_end_frame_ = initialVisibleEndFrame(frame_rate);
    ruler_->setVisibleEndFrame(visible_end_frame_);
    ruler_->setCurrentFrame(current_frame_);
    updateControls();
}

void TimelineNavigator::setCurrentFrame(std::int64_t frame)
{
    seekToFrame(frame);
}

std::int64_t TimelineNavigator::currentFrame() const noexcept
{
    return current_frame_;
}

std::int64_t TimelineNavigator::visibleEndFrame() const noexcept
{
    return visible_end_frame_;
}

void TimelineNavigator::seekToFrame(std::int64_t frame)
{
    const auto bounded_frame = std::clamp(frame, std::int64_t{0}, visible_end_frame_);
    if (bounded_frame == current_frame_) {
        return;
    }

    current_frame_ = bounded_frame;
    ruler_->setCurrentFrame(current_frame_);
    updateControls();
    emit currentFrameChanged(static_cast<qint64>(current_frame_));
}

void TimelineNavigator::extendViewByOneHour() noexcept
{
    const auto remaining_frames = kMaximumFrame - visible_end_frame_;
    const auto extension_frames = std::min(framesPerHour(frame_rate_), remaining_frames);
    if (extension_frames == 0) {
        return;
    }

    visible_end_frame_ += extension_frames;
    ruler_->setVisibleEndFrame(visible_end_frame_);
    updateControls();
}

void TimelineNavigator::updateControls()
{
    frame_label_->setText(QStringLiteral("Frame %1")
        .arg(static_cast<qlonglong>(current_frame_)));
    frame_rate_label_->setText(QStringLiteral("FPS: %1").arg(formatFrameRate(frame_rate_)));
    previous_frame_button_->setEnabled(current_frame_ > 0);
    next_frame_button_->setEnabled(current_frame_ < visible_end_frame_);
}

} // namespace motion::ui
