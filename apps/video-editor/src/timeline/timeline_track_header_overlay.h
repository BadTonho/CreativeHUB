#pragma once

#include <QWidget>

class QEvent;
class QLabel;

namespace timeline {

class TimelineWidget;

class TimelineTrackHeaderOverlay final : public QWidget {
public:
    explicit TimelineTrackHeaderOverlay(
        TimelineWidget* timeline,
        QWidget* parent = nullptr);
    ~TimelineTrackHeaderOverlay() override;

    void setVerticalScrollOffset(int offset);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void updateOverlayGeometry();

    TimelineWidget* timeline_ = nullptr;
    QLabel* playhead_timecode_ = nullptr;
    int vertical_scroll_offset_ = 0;
};

} // namespace timeline
