#pragma once

#include "model/composition_document.h"

#include <QWidget>

#include <cstdint>

class QLabel;
class QPushButton;
class QMouseEvent;

namespace motion::ui {

class TimelineRuler final : public QWidget {
    Q_OBJECT

public:
    explicit TimelineRuler(QWidget* parent = nullptr);

    void setDuration(std::int64_t duration_frames);
    void setCurrentFrame(std::int64_t frame) noexcept;

signals:
    void seekRequested(qint64 frame);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] std::int64_t frameAtX(int x) const noexcept;
    [[nodiscard]] int xForFrame(std::int64_t frame) const noexcept;

    std::int64_t duration_frames_ = 1;
    std::int64_t current_frame_ = 0;
    bool dragging_ = false;
};

class TimelineNavigator final : public QWidget {
    Q_OBJECT

public:
    explicit TimelineNavigator(QWidget* parent = nullptr);

    void setCompositionTiming(model::FrameRate frame_rate, std::int64_t duration_frames);
    void setCurrentFrame(std::int64_t frame);
    [[nodiscard]] std::int64_t currentFrame() const noexcept;

signals:
    void currentFrameChanged(qint64 frame);

private:
    void seekToFrame(std::int64_t frame);
    void updateControls();

    model::FrameRate frame_rate_{};
    std::int64_t duration_frames_ = 1;
    std::int64_t current_frame_ = 0;
    QPushButton* previous_frame_button_ = nullptr;
    QPushButton* next_frame_button_ = nullptr;
    QLabel* frame_label_ = nullptr;
    QLabel* frame_rate_label_ = nullptr;
    TimelineRuler* ruler_ = nullptr;
};

} // namespace motion::ui
