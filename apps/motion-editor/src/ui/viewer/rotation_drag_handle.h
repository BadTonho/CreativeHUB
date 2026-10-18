#pragma once
#include "model/composition_document.h"
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <functional>

class QWidget;
class QPainter;
namespace motion::ui {
struct RotationDragHandlers {
    // Absolute canvas pixels and logical-to-canvas scale; all angle math is Motion-owned.
    std::function<bool(QPointF,double)> begin;
    std::function<bool(QPointF)> preview;
    std::function<void()> commit, cancel;
};
class RotationDragHandle final : public QObject {
public:
    RotationDragHandle(QWidget*, std::function<std::optional<QPointF>()> pivot,
        std::function<QRectF()> canvas, std::function<model::CanvasSize()> size);
    ~RotationDragHandle() override;
    void setHandlers(RotationDragHandlers);
    void setDirection(std::optional<QPointF>);
    void setEnabled(bool);
    void cancel();
    [[nodiscard]] bool active() const noexcept { return pressed_; }
    void paint(QPainter&) const;
protected:
    bool eventFilter(QObject*,QEvent*) override;
private:
    std::optional<QPointF> center() const;
    bool hit(QPointF) const;
    bool preview(QPointF);
    QPointF canvasPoint(QPointF) const;
    void release();
    QWidget* surface_;
    std::function<std::optional<QPointF>()> pivot_;
    std::function<QRectF()> canvas_;
    std::function<model::CanvasSize()> size_;
    RotationDragHandlers handlers_;
    std::vector<QWidget*> ancestors_;
    std::optional<QPointF> direction_;
    bool enabled_=false, pressed_=false, dragged_=false;
    QPointF press_;
    QRectF frozen_canvas_;
    model::CanvasSize frozen_size_{};
};
}
