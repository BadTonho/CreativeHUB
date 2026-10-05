#pragma once

#include "image_document_store.h"

#include <QRectF>

#include <optional>

class QPainter;

namespace image_editor {

struct ShapeToolRenderContext {
    QRectF image_target;
    qreal zoom = 1.0;
};

class ShapeTool final {
public:
    void setStyle(const ImageShapeData& style) { style_ = style; }

    void beginGesture(const QPointF& image_position) noexcept;
    void updateGesture(const QPointF& image_position, bool constrain) noexcept;
    [[nodiscard]] std::optional<ImageShapeData> finishGesture(
        const QPointF& image_position, bool constrain) noexcept;
    [[nodiscard]] bool cancelGesture() noexcept;

    [[nodiscard]] bool gestureActive() const noexcept { return gesture_active_; }
    [[nodiscard]] std::optional<ImageShapeData> preview() const;
    void paintOverlay(QPainter& painter,
                      const ShapeToolRenderContext& context) const;

    static void paintShape(QPainter& painter,
                           const ImageShapeData& shape,
                           const ShapeToolRenderContext& context,
                           int opacity = 100);

private:
    [[nodiscard]] static QPointF constrainPoint(const QPointF& point,
                                                const QPointF& anchor,
                                                ImageShapeKind kind,
                                                bool constrain) noexcept;
    [[nodiscard]] static bool hasValidGeometry(const ImageShapeData& shape) noexcept;

    ImageShapeData style_;
    ImageShapeData gesture_shape_;
    bool gesture_active_ = false;
};

} // namespace image_editor
