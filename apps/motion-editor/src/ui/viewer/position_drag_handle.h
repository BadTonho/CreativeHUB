#pragma once
#include "model/composition_document.h"
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <functional>

class QWidget;
class QPainter;
namespace motion::ui {
struct PositionDragHandlers {
    std::function<bool()> begin;
    std::function<bool(QPointF)> preview;
    std::function<void()> commit, cancel;
};
// Pointer capture and logical mapping only; controller/worker own all edits.
class PositionDragHandle final : public QObject {
public:
    PositionDragHandle(QWidget*, std::function<std::optional<QPointF>()> pivot,
        std::function<QRectF()> canvas, std::function<model::CanvasSize()> size);
    ~PositionDragHandle() override;
    void setHandlers(PositionDragHandlers);
    void setEnabled(bool);
    void cancel();
    [[nodiscard]] bool active() const noexcept { return pressed_; }
    void paint(QPainter&) const;
protected:
    bool eventFilter(QObject*,QEvent*) override;
private:
    std::optional<QPointF> center() const;
    bool hit(QPointF) const;
    bool preview(QPointF point);
    void release();
    QWidget* surface_;
    std::function<std::optional<QPointF>()> pivot_;
    std::function<QRectF()> canvas_;
    std::function<model::CanvasSize()> size_;
    PositionDragHandlers handlers_;
    std::vector<QWidget*> ancestors_;
    bool enabled_=false, pressed_=false, dragged_=false;
    QPointF press_;
    QRectF frozen_canvas_;
    model::CanvasSize frozen_size_{};
};
}
