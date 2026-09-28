#pragma once

#include "model/composition_document.h"

#include <creative_suite/media/video_frame.h>

#include <QPointF>
#include <QWidget>

#include <memory>
#include <optional>

class QPaintEvent;

namespace motion::ui {

class CompositionViewer final : public QWidget {
public:
    explicit CompositionViewer(QWidget* parent = nullptr);

    void setComposition(
        model::CanvasSize canvas_size,
        std::optional<QPointF> selected_layer_anchor);
    void setSelectedLayerAnchor(std::optional<QPointF> selected_layer_anchor);
    void setRenderedFrame(creative_suite::media::RgbaFramePtr frame);
    [[nodiscard]] creative_suite::media::RgbaFramePtr renderedFrame() const noexcept;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    model::CanvasSize canvas_size_{0, 0};
    std::optional<QPointF> selected_layer_anchor_;
    creative_suite::media::RgbaFramePtr rendered_frame_;
};

} // namespace motion::ui
