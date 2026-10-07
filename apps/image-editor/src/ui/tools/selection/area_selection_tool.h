#pragma once

#include "../lasso/lasso_tool.h"

#include <QPainterPath>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <QVector>
#include <optional>

class QPainter;

namespace image_editor {

struct AreaSelectionToolRenderContext {
    QRectF image_bounds;
    QRectF image_target;
    QTransform image_to_widget;
};

class AreaSelectionTool final {
public:
    enum class Shape { Rectangle, Ellipse };
    enum class CombineMode { Replace, Add, Subtract };

    enum class FinishStatus { NoSelection, Applied, Rejected };

    struct FinishResult {
        FinishStatus status = FinishStatus::NoSelection;
        QString rejection_reason;
    };

    void setOptions(Shape shape, CombineMode combine_mode) noexcept;
    void setLassoMode(bool enabled) noexcept;
    void beginGesture(const QPointF& image_position) noexcept;
    void updateGesture(const QPointF& image_position) noexcept;
    [[nodiscard]] FinishResult finishGesture(const QPointF& image_position,
                                             const QRectF& image_bounds);
    [[nodiscard]] bool cancelGesture() noexcept;
    [[nodiscard]] bool clearSelection() noexcept;
    [[nodiscard]] bool translateSelection(const QPoint& delta) noexcept;

    [[nodiscard]] bool hasSelection() const noexcept { return selection_active_; }
    [[nodiscard]] bool gestureActive() const noexcept { return gesture_active_; }
    [[nodiscard]] std::optional<QPainterPath> clipPath(
        const QRectF& image_bounds) const;
    [[nodiscard]] QPainterPath previewPath(const QRectF& image_bounds) const;
    void paintOverlay(QPainter& painter,
                      const AreaSelectionToolRenderContext& context) const;

private:
    [[nodiscard]] QPainterPath visibleSelectionPath(
        const QRectF& image_bounds) const;
    [[nodiscard]] QPainterPath gesturePath(const QRectF& image_bounds) const;
    [[nodiscard]] QPainterPath combinedPath(const QPainterPath& gesture,
                                            const QRectF& image_bounds) const;
    void resetGesture() noexcept;

    Shape shape_ = Shape::Rectangle;
    CombineMode combine_mode_ = CombineMode::Replace;
    bool gesture_active_ = false;
    bool lasso_mode_ = false;
    QPointF gesture_start_;
    QPointF gesture_current_;
    LassoTool lasso_tool_;
    QPainterPath selection_path_;
    bool selection_active_ = false;
};

} // namespace image_editor
