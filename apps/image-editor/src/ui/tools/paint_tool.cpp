#include "paint_tool.h"

#include <utility>

namespace image_editor {
namespace {

BrushToolEvent makeEvent(BrushToolEventType type,
                         QVector<QPointF> points,
                         const BrushToolContext& context) {
    return {type, std::move(points), context.color, context.diameter};
}

} // namespace

QVector<BrushToolEvent> PaintTool::press(const BrushToolContext& context) {
    beginStroke(context);
    if (!context.mask_editing) return {};
    return {makeEvent(BrushToolEventType::MaskPaintPreviewRequested, points(), context)};
}

QVector<BrushToolEvent> PaintTool::move(const BrushToolContext& context) {
    if (!drawing()) return {};
    appendStrokePoint(context.image_position);
    updateCursor(context, true);
    if (!context.mask_editing) return {};
    return {makeEvent(BrushToolEventType::MaskPaintPreviewRequested, points(), context)};
}

QVector<BrushToolEvent> PaintTool::release(const BrushToolContext& context) {
    if (!drawing()) return {};
    auto stroke = finishStroke(context);
    QVector<BrushToolEvent> events;
    if (context.mask_editing) {
        events.append(makeEvent(BrushToolEventType::ErasePreviewCleared, {}, context));
    }
    if (!stroke.isEmpty()) {
        events.append(makeEvent(BrushToolEventType::PaintStrokeSelected,
                                std::move(stroke), context));
    }
    return events;
}

QVector<BrushToolEvent> PaintTool::cancel(bool clear_mask_preview) {
    if (!drawing()) return {};
    cancelStroke();
    hideCursor();
    if (!clear_mask_preview) return {};
    return {{BrushToolEventType::ErasePreviewCleared, {}, Qt::black, 12}};
}

void PaintTool::paintOverlay(QPainter& painter,
                             const BrushToolContext& context) const {
    if (!context.mask_editing) paintStrokeOverlay(painter, context, context.color);
    paintCursor(painter, context);
}

} // namespace image_editor
