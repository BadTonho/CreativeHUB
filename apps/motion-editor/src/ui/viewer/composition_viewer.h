#pragma once

#include "model/composition_document.h"

#include <creative_suite/media/video_frame.h>

#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>
#include <cstdint>

class QPaintEvent;
class QContextMenuEvent;

namespace motion::ui {

class CompositionViewer final : public QWidget {
public:
    explicit CompositionViewer(QWidget* parent = nullptr);

    void setComposition(
        model::CanvasSize canvas_size,
        std::optional<QPointF> selected_layer_anchor);
    void setSelectedLayerAnchor(std::optional<QPointF> selected_layer_anchor);
    void setRenderedFrame(creative_suite::media::RgbaFramePtr frame,
                          std::uint64_t request_generation = 0);
    [[nodiscard]] creative_suite::media::RgbaFramePtr renderedFrame() const noexcept;
    void setLayerContextMenuHandler(std::function<void(const QPoint&)> handler);

protected:
    void paintEvent(QPaintEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    [[nodiscard]] QRectF canvasRect() const;

    model::CanvasSize canvas_size_{0, 0};
    std::optional<QPointF> selected_layer_anchor_;
    creative_suite::media::RgbaFramePtr rendered_frame_;
    std::uint64_t rendered_frame_generation_ = 0;
    bool rendered_frame_paint_pending_ = false;
    std::function<void(const QPoint&)> layer_context_menu_handler_;
};

} // namespace motion::ui
