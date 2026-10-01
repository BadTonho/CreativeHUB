#pragma once

#include "model/composition_document.h"
#include "timeline_types.h"

#include <QWidget>

#include <cstdint>

namespace motion::ui {

class TimelineRuler final : public QWidget {
    Q_OBJECT

public:
    explicit TimelineRuler(QWidget* parent = nullptr);

    void setHeaderWidth(int width);
    void setMappingWidth(int width);
    [[nodiscard]] int mappingWidth() const noexcept;
    void setViewState(std::int64_t end_frame,
                      std::int64_t current_frame,
                      std::int64_t start_frame,
                      std::int64_t frames_per_view,
                      model::FrameRate frame_rate,
                      TimelineDisplayMode display_mode);

signals:
    void seekRequested(qint64 frame);
    void extendRangeRequested();
    void zoomStepRequested(int direction);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    [[nodiscard]] std::int64_t frameAtX(int x) const noexcept;
    [[nodiscard]] int xForFrame(std::int64_t frame) const noexcept;
    void handleDragX(int x);

    std::int64_t visible_end_frame_ = 0;
    std::int64_t current_frame_ = 0;
    std::int64_t view_start_frame_ = 0;
    std::int64_t frames_per_view_ = 1;
    model::FrameRate frame_rate_{};
    TimelineDisplayMode display_mode_ = TimelineDisplayMode::Time;
    int last_mouse_x_ = -1;
    int header_width_ = 0;
    int mapping_width_ = 0;
    bool dragging_ = false;
    bool range_extended_during_drag_ = false;
};

} // namespace motion::ui
