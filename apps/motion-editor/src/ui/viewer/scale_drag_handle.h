#pragma once
#include "model/layer_scale_drag.h"
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <functional>

class QWidget;
class QPainter;
namespace motion::ui {
struct ScaleDragHandlers {
    // Initial lever and pointer displacement in canvas pixels. Motion owns math.
    std::function<bool(model::ScaleDragControl,QPointF)> begin;
    std::function<bool(QPointF)> preview;
    std::function<void()> commit, cancel;
};
class ScaleDragHandle final : public QObject {
public:
    ScaleDragHandle(QWidget*, std::function<std::optional<QPointF>()> pivot,
        std::function<QRectF()> canvas, std::function<model::CanvasSize()> size);
    ~ScaleDragHandle() override;
    void setHandlers(ScaleDragHandlers);
    void setDirections(std::optional<std::array<QPointF,3>>);
    void setIndicator(std::optional<QPointF>);
    void setEnabled(bool);
    void cancel();
    [[nodiscard]] bool active() const noexcept { return pressed_; }
    void paint(QPainter&) const;
protected:
    bool eventFilter(QObject*,QEvent*) override;
private:
    std::optional<QPointF> center(std::size_t index) const;
    std::optional<model::ScaleDragControl> hit(QPointF) const;
    bool preview(QPointF);
    QPointF canvasPoint(QPointF) const;
    void release();
    QWidget* surface_;
    std::function<std::optional<QPointF>()> pivot_;
    std::function<QRectF()> canvas_;
    std::function<model::CanvasSize()> size_;
    ScaleDragHandlers handlers_;
    std::vector<QWidget*> ancestors_;
    std::optional<std::array<QPointF,3>> directions_;
    std::optional<QPointF> indicator_;
    bool enabled_=false, pressed_=false, dragged_=false;
    QPointF press_;
    QRectF frozen_canvas_;
    model::CanvasSize frozen_size_{};
};
}
