#pragma once

#include "model/composition_document.h"

#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

class QLabel;
class QPushButton;
class QMouseEvent;
class QScrollArea;

namespace motion::ui {

class TimelineRuler final : public QWidget {
    Q_OBJECT

public:
    explicit TimelineRuler(QWidget* parent = nullptr);

    void setHeaderWidth(int width);
    void setVisibleEndFrame(std::int64_t frame);
    void setCurrentFrame(std::int64_t frame) noexcept;

signals:
    void seekRequested(qint64 frame);
    void extendRangeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] std::int64_t frameAtX(int x) const noexcept;
    [[nodiscard]] int xForFrame(std::int64_t frame) const noexcept;

    std::int64_t visible_end_frame_ = 0;
    std::int64_t current_frame_ = 0;
    int last_mouse_x_ = -1;
    int header_width_ = 0;
    bool dragging_ = false;
    bool range_extended_during_drag_ = false;
};

class TimelineNavigator final : public QWidget {
    Q_OBJECT

public:
    explicit TimelineNavigator(QWidget* parent = nullptr);

    void setCompositionTiming(model::FrameRate frame_rate);
    void setCurrentFrame(std::int64_t frame);
    void setLayers(const std::vector<model::CompositionLayer>& layers);
    void setSelectedLayerId(model::LayerId id);
    [[nodiscard]] std::int64_t currentFrame() const noexcept;
    [[nodiscard]] std::int64_t visibleEndFrame() const noexcept;

    void setMediaDropHandler(
        std::function<void(const std::filesystem::path&,
                           std::int64_t,
                           model::LayerId)> handler);
    void setLayerSelectedHandler(std::function<void(model::LayerId)> handler);
    void setLayerMoveHandler(
        std::function<void(model::LayerId, std::int64_t)> handler);
    void setLayerResizeHandler(
        std::function<void(model::LayerId, std::int64_t)> handler);
    void setLayerReorderHandler(
        std::function<void(model::LayerId, std::size_t)> handler);
    void setLayerVisibilityHandler(
        std::function<void(model::LayerId, bool)> handler);
    void setLayerRemoveHandler(std::function<void(model::LayerId)> handler);

signals:
    void currentFrameChanged(qint64 frame);

private:
    void seekToFrame(std::int64_t frame);
    void extendViewByOneHour() noexcept;
    void updateControls();

    model::FrameRate frame_rate_{};
    std::int64_t visible_end_frame_ = 0;
    std::int64_t current_frame_ = 0;
    QPushButton* previous_frame_button_ = nullptr;
    QPushButton* next_frame_button_ = nullptr;
    QLabel* frame_label_ = nullptr;
    QLabel* frame_rate_label_ = nullptr;
    TimelineRuler* ruler_ = nullptr;
    QWidget* layer_rows_ = nullptr;
    QScrollArea* layer_scroll_area_ = nullptr;
};

} // namespace motion::ui
