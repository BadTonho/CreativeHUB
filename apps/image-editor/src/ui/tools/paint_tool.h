#pragma once

#include "brush_tool.h"

namespace image_editor {

class PaintTool final : public BrushTool {
public:
    [[nodiscard]] QVector<BrushToolEvent> press(const BrushToolContext& context);
    [[nodiscard]] QVector<BrushToolEvent> move(const BrushToolContext& context);
    [[nodiscard]] QVector<BrushToolEvent> release(const BrushToolContext& context);
    [[nodiscard]] QVector<BrushToolEvent> cancel(bool clear_mask_preview);
    void paintOverlay(QPainter& painter, const BrushToolContext& context) const;
};

} // namespace image_editor
