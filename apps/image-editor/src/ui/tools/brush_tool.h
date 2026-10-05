#pragma once

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QVector>

#include <optional>

namespace image_editor {

struct BrushToolContext {
    QPointF widget_position;
    QPointF image_position;
    QRectF image_target;
    qreal zoom = 1.0;
    QColor color = Qt::black;
    int diameter = 12;
    bool mask_editing = false;
    std::optional<QPainterPath> area_selection;
};

enum class BrushToolEventType {
    PaintStrokeSelected,
    MaskPaintPreviewRequested,
    ErasePreviewRequested,
    ErasePreviewCleared,
    EraseStrokeSelected
};

struct BrushToolEvent {
    BrushToolEventType type = BrushToolEventType::ErasePreviewCleared;
    QVector<QPointF> points;
    QColor color = Qt::black;
    int diameter = 12;
};

class BrushTool {
public:
    [[nodiscard]] bool drawing() const noexcept { return drawing_; }

    void updateCursor(const BrushToolContext& context, bool visible);
    void hideCursor() noexcept { cursor_visible_ = false; }
    void paintStrokeOverlay(QPainter& painter,
                            const BrushToolContext& context,
                            const QColor& color) const;
    void paintCursor(QPainter& painter, const BrushToolContext& context) const;

protected:
    void beginStroke(const BrushToolContext& context);
    void appendStrokePoint(const QPointF& point);
    [[nodiscard]] QVector<QPointF> finishStroke(const BrushToolContext& context);
    void cancelStroke() noexcept;

    [[nodiscard]] const QVector<QPointF>& points() const noexcept { return points_; }

private:
    static constexpr qsizetype kMaximumPreviewPoints = 100'000;

    QVector<QPointF> points_;
    QPointF cursor_position_;
    bool cursor_visible_ = false;
    bool drawing_ = false;
};

} // namespace image_editor
