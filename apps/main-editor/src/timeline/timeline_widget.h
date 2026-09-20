#pragma once

#include "timeline_model.h"

#include <QWidget>

#include <cstdint>
#include <optional>

class QMouseEvent;
class QPaintEvent;

namespace timeline {

class TimelineWidget final : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    void setClip(const TimelineClip* clip);
    void clearClip();
    void setPlayheadFrame(std::int64_t frame_index);

signals:
    void seekStarted();
    void seekRequested(qint64 frame_index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] std::optional<std::int64_t> frameAtPosition(double x) const noexcept;
    [[nodiscard]] std::optional<double> playheadFraction() const noexcept;
    [[nodiscard]] double displayedPlayheadFrame() const noexcept;

    std::optional<TimelineClip> clip_;
    std::int64_t playhead_frame_ = 0;
    std::optional<std::int64_t> drag_frame_;
    bool dragging_ = false;
};

} // namespace timeline
