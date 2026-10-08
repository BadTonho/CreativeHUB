#pragma once

#include <QWidget>

class QEvent;
class QLabel;
class QScrollBar;

namespace timeline {

class TimelineWidget;

class TimelineTrackHeaderOverlay final : public QWidget {
public:
    explicit TimelineTrackHeaderOverlay(
        TimelineWidget* timeline,
        QWidget* parent = nullptr);
    ~TimelineTrackHeaderOverlay() override;

    [[nodiscard]] QScrollBar* videoScrollBar() const noexcept;
    [[nodiscard]] QScrollBar* audioScrollBar() const noexcept;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void updateOverlayGeometry();
    void updateScrollBarGeometry();

    TimelineWidget* timeline_ = nullptr;
    QLabel* playhead_timecode_ = nullptr;
    QScrollBar* video_scroll_bar_ = nullptr;
    QScrollBar* audio_scroll_bar_ = nullptr;
};

} // namespace timeline
