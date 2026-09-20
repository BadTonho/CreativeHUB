#pragma once

#include "timeline_model.h"

#include <QWidget>

#include <cstdint>
#include <optional>

class QMouseEvent;
class QPaintEvent;

namespace timeline {

class TimelineWidget final : public QWidget {
public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    void setClip(const TimelineClip* clip);
    void clearClip();
    void setPlayheadFrame(std::int64_t frame_index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] std::optional<double> playheadFraction() const noexcept;

    std::optional<TimelineClip> clip_;
    std::int64_t playhead_frame_ = 0;
};

} // namespace timeline
