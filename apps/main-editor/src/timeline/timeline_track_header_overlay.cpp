#include "timeline_track_header_overlay.h"

#include "timeline_widget.h"

#include <QEvent>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>

namespace timeline {

TimelineTrackHeaderOverlay::TimelineTrackHeaderOverlay(
    TimelineWidget* timeline,
    QWidget* parent)
    : QWidget(parent),
      timeline_(timeline) {
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setFocusPolicy(Qt::NoFocus);

    if (parentWidget() != nullptr) {
        parentWidget()->installEventFilter(this);
    }
    if (timeline_ != nullptr) {
        connect(
            timeline_,
            &TimelineWidget::trackHeaderVisualsChanged,
            this,
            [this]() { update(); });
    }
    updateOverlayGeometry();
    raise();
}

TimelineTrackHeaderOverlay::~TimelineTrackHeaderOverlay() {
    if (parentWidget() != nullptr) {
        parentWidget()->removeEventFilter(this);
    }
}

void TimelineTrackHeaderOverlay::setVerticalScrollOffset(int offset) {
    const auto normalized = std::max(0, offset);
    if (vertical_scroll_offset_ == normalized) return;
    vertical_scroll_offset_ = normalized;
    update();
}

bool TimelineTrackHeaderOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() &&
        event != nullptr && event->type() == QEvent::Resize) {
        updateOverlayGeometry();
    }
    return QWidget::eventFilter(watched, event);
}

void TimelineTrackHeaderOverlay::paintEvent(QPaintEvent* /*event*/) {
    if (timeline_ == nullptr) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    timeline_->paintTrackHeaderOverlay(painter, vertical_scroll_offset_);
}

void TimelineTrackHeaderOverlay::updateOverlayGeometry() {
    if (parentWidget() == nullptr || timeline_ == nullptr) return;
    setGeometry(
        0,
        0,
        timeline_->trackHeaderOverlayWidth(),
        parentWidget()->height());
    raise();
    update();
}

} // namespace timeline
