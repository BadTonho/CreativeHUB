#pragma once

#include "brush_tool.h"

namespace image_editor {

class EraserTool final : public BrushTool {
public:
    [[nodiscard]] QVector<BrushToolEvent> press(const BrushToolContext& context);
    [[nodiscard]] QVector<BrushToolEvent> move(const BrushToolContext& context);
    [[nodiscard]] QVector<BrushToolEvent> release(const BrushToolContext& context);
    [[nodiscard]] QVector<BrushToolEvent> cancel();
    [[nodiscard]] QVector<BrushToolEvent> setPreviewEnabled(
        bool enabled, const BrushToolContext& context);
    void paintOverlay(QPainter& painter, const BrushToolContext& context) const;

private:
    bool preview_enabled_ = false;
};

} // namespace image_editor
