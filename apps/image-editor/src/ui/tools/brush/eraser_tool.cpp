#include "eraser_tool.h"

#include <utility>

namespace image_editor {
namespace {

BrushToolEvent makeEvent(BrushToolEventType type,
                         QVector<QPointF> points,
                         const BrushToolContext& context) {
    return {type, std::move(points), context.color, context.diameter};
}

} // namespace

QVector<BrushToolEvent> EraserTool::press(const BrushToolContext& context) {
    beginStroke(context);
    if (preview_enabled_) return {};
    return {makeEvent(BrushToolEventType::ErasePreviewRequested, points(), context)};
}

QVector<BrushToolEvent> EraserTool::move(const BrushToolContext& context) {
    if (!drawing()) return {};
    appendStrokePoint(context.image_position);
    updateCursor(context, true);
    if (preview_enabled_) return {};
    return {makeEvent(BrushToolEventType::ErasePreviewRequested, points(), context)};
}

QVector<BrushToolEvent> EraserTool::release(const BrushToolContext& context) {
    if (!drawing()) return {};
    auto stroke = finishStroke(context);
    QVector<BrushToolEvent> events{
        makeEvent(BrushToolEventType::ErasePreviewCleared, {}, context)
    };
    if (!stroke.isEmpty()) {
        events.append(makeEvent(BrushToolEventType::EraseStrokeSelected,
                                std::move(stroke), context));
    }
    return events;
}

QVector<BrushToolEvent> EraserTool::cancel() {
    if (!drawing()) return {};
    cancelStroke();
    hideCursor();
    return {{BrushToolEventType::ErasePreviewCleared, {}, Qt::black, 12}};
}

QVector<BrushToolEvent> EraserTool::setPreviewEnabled(
    bool enabled, const BrushToolContext& context) {
    if (context.mask_editing) enabled = false;
    if (preview_enabled_ == enabled) return {};
    preview_enabled_ = enabled;
    if (!drawing()) return {};
    if (!preview_enabled_) {
        return {makeEvent(BrushToolEventType::ErasePreviewRequested, points(), context)};
    }
    return {makeEvent(BrushToolEventType::ErasePreviewCleared, {}, context)};
}

void EraserTool::paintOverlay(QPainter& painter,
                              const BrushToolContext& context) const {
    if (preview_enabled_) {
        paintStrokeOverlay(painter, context, QColor(240, 80, 125, 115));
    }
    paintCursor(painter, context);
}

} // namespace image_editor
