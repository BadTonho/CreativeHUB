#include "timeline_track_header_overlay.h"

#include "timeline_widget.h"

#include <QEvent>
#include <QFont>
#include <QLabel>
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

    playhead_timecode_ = new QLabel(this);
    playhead_timecode_->setObjectName(QStringLiteral("timelinePlayheadTimecode"));
    playhead_timecode_->setAccessibleName(QStringLiteral("Timeline playhead timecode"));
    playhead_timecode_->setToolTip(QStringLiteral("Current global Timeline time"));
    playhead_timecode_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    playhead_timecode_->setFont(QFont(playhead_timecode_->font().family(), 8));
    playhead_timecode_->setStyleSheet(
        QStringLiteral("QLabel { color: #9aa4b2; background: transparent; }"));

    if (parentWidget() != nullptr) {
        parentWidget()->installEventFilter(this);
    }
    if (timeline_ != nullptr) {
        connect(
            timeline_,
            &TimelineWidget::trackHeaderVisualsChanged,
            this,
            [this]() { update(); });
        connect(
            timeline_,
            &TimelineWidget::playheadVisualChanged,
            this,
            [this]() {
                if (timeline_ != nullptr && playhead_timecode_ != nullptr) {
                    playhead_timecode_->setText(timeline_->playheadTimecode());
                }
            });
        playhead_timecode_->setText(timeline_->playheadTimecode());
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
    if (playhead_timecode_ != nullptr) {
        playhead_timecode_->setGeometry(12, 12, std::max(0, width() - 24), 25);
    }
    raise();
    update();
}

} // namespace timeline
