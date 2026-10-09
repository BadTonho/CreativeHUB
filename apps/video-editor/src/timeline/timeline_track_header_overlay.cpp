#include "timeline_track_header_overlay.h"

#include "timeline_widget.h"

#include <QEvent>
#include <QFont>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QSignalBlocker>

#include <algorithm>
#include <cmath>

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

    if (parent != nullptr && timeline_ != nullptr) {
        video_scroll_bar_ = new QScrollBar(Qt::Vertical, parent);
        video_scroll_bar_->setObjectName(QStringLiteral("videoTrackScrollBar"));
        video_scroll_bar_->setAccessibleName(QStringLiteral("Video tracks scroll"));
        video_scroll_bar_->setToolTip(QStringLiteral("Scroll video tracks"));
        audio_scroll_bar_ = new QScrollBar(Qt::Vertical, parent);
        audio_scroll_bar_->setObjectName(QStringLiteral("audioTrackScrollBar"));
        audio_scroll_bar_->setAccessibleName(QStringLiteral("Audio tracks scroll"));
        audio_scroll_bar_->setToolTip(QStringLiteral("Scroll audio tracks"));
        connect(video_scroll_bar_, &QScrollBar::valueChanged,
                timeline_, [timeline](int value) {
                    timeline->setTrackScrollOffset(TrackKind::Video, value);
                });
        connect(audio_scroll_bar_, &QScrollBar::valueChanged,
                timeline_, [timeline](int value) {
                    timeline->setTrackScrollOffset(TrackKind::Audio, value);
                });
    }

    if (parentWidget() != nullptr) {
        parentWidget()->installEventFilter(this);
    }
    if (timeline_ != nullptr) {
        connect(
            timeline_,
            &TimelineWidget::trackHeaderVisualsChanged,
            this,
            [this]() {
                update();
                updateScrollBarGeometry();
            });
        connect(
            timeline_,
            &TimelineWidget::trackScrollMetricsChanged,
            this,
            [this]() { updateScrollBarGeometry(); });
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
    updateScrollBarGeometry();
    raise();
}

TimelineTrackHeaderOverlay::~TimelineTrackHeaderOverlay() {
    if (parentWidget() != nullptr) {
        parentWidget()->removeEventFilter(this);
    }
    delete video_scroll_bar_;
    delete audio_scroll_bar_;
}

QScrollBar* TimelineTrackHeaderOverlay::videoScrollBar() const noexcept {
    return video_scroll_bar_;
}

QScrollBar* TimelineTrackHeaderOverlay::audioScrollBar() const noexcept {
    return audio_scroll_bar_;
}

bool TimelineTrackHeaderOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event != nullptr &&
        event->type() == QEvent::Resize) {
        updateOverlayGeometry();
        updateScrollBarGeometry();
    }
    return QWidget::eventFilter(watched, event);
}

void TimelineTrackHeaderOverlay::paintEvent(QPaintEvent* /*event*/) {
    if (timeline_ == nullptr) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    timeline_->paintTrackHeaderOverlay(painter);
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

void TimelineTrackHeaderOverlay::updateScrollBarGeometry() {
    if (parentWidget() == nullptr || timeline_ == nullptr) return;
    constexpr int bar_width = 14;
    const auto configure = [this](
        QScrollBar* bar, TrackKind kind, const QRectF& viewport) {
        if (bar == nullptr) return;
        const auto maximum = timeline_->trackScrollMaximum(kind);
        const auto height = std::max(0, static_cast<int>(std::lround(viewport.height())));
        const auto y = static_cast<int>(std::lround(viewport.top()));
        const auto x = std::max(0, parentWidget()->width() - bar_width);
        bar->setGeometry(x, y, bar_width, height);
        bar->setRange(0, maximum);
        bar->setPageStep(std::max(1, height));
        bar->setSingleStep(std::max(1, static_cast<int>(std::lround(
            timeline_->trackRowHeight(kind) + TimelineGeometry::row_gap))));
        {
            const QSignalBlocker blocker(bar);
            bar->setValue(timeline_->trackScrollOffset(kind));
        }
        bar->setVisible(maximum > 0 && height > 0);
        bar->raise();
    };
    configure(
        video_scroll_bar_, TrackKind::Video,
        timeline_->trackGroupViewportRect(TrackKind::Video));
    configure(
        audio_scroll_bar_, TrackKind::Audio,
        timeline_->trackGroupViewportRect(TrackKind::Audio));
}

} // namespace timeline
