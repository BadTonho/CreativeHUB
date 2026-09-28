#pragma once

#include "model/composition_document.h"

#include <QPointF>
#include <QWidget>

#include <optional>

class QPaintEvent;

namespace motion::ui {

class CompositionViewer final : public QWidget {
public:
    explicit CompositionViewer(QWidget* parent = nullptr);

    void setComposition(
        model::CanvasSize canvas_size,
        std::optional<QPointF> selected_layer_anchor);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    model::CanvasSize canvas_size_{0, 0};
    std::optional<QPointF> selected_layer_anchor_;
};

} // namespace motion::ui
