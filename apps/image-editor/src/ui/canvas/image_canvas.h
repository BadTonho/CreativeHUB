#pragma once

#include "image_document_store.h"

#include <QImage>
#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QVector>
#include <QWidget>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QWheelEvent;

namespace image_editor {

class ImageCanvas final : public QWidget {
    Q_OBJECT

public:
    explicit ImageCanvas(QWidget* parent = nullptr);

    void setImage(QImage image, bool resetView = true);
    void setCropMode(bool enabled);
    void setPaintMode(bool enabled);
    void setEraserMode(bool enabled);
    void setShapeMode(bool creation_enabled, bool selection_enabled);
    void setShapeStyle(const ImageShapeData& style);
    void setShapePlacements(QVector<ImageShapePlacement> placements,
                            const QString& selected_shape_id);
    void setEraserPreviewEnabled(bool enabled);
    void setTransientImage(QImage image);
    void setBrush(QColor color, int diameter);
    void fitToWindow();
    [[nodiscard]] bool cropMode() const noexcept { return crop_mode_; }
    [[nodiscard]] bool paintMode() const noexcept { return paint_mode_; }
    [[nodiscard]] bool eraserMode() const noexcept { return eraser_mode_; }
    [[nodiscard]] double zoomFactor() const noexcept { return zoom_; }

signals:
    void cropSelected(const QRect& image_rect);
    void paintStrokeSelected(const QVector<QPointF>& image_points,
                             const QColor& color,
                             int diameter);
    void erasePreviewRequested(const QVector<QPointF>& image_points, int diameter);
    void erasePreviewCleared();
    void eraseStrokeSelected(const QVector<QPointF>& image_points, int diameter);
    void brushDiameterChanged(int diameter);
    void shapeCreated(const image_editor::ImageShapeData& shape);
    void shapeSelected(const QString& shape_id, const QString& layer_id);
    void shapeTransformStarted(const QString& shape_id);
    void shapeGeometryChanged(const image_editor::ImageShapeData& shape);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] QRectF imageTargetRect() const;
    [[nodiscard]] QRect cropToImageCoordinates(const QRectF& selection) const;
    [[nodiscard]] QPointF widgetToImageCoordinates(const QPointF& position) const;
    void appendPaintPoint(const QPointF& point);
    void updateHoverCursor(const QPointF& position);
    [[nodiscard]] QPointF constrainShapePoint(const QPointF& point,
                                             const QPointF& anchor,
                                             ImageShapeKind kind,
                                             bool shift) const;
    [[nodiscard]] int shapeHitAt(const QPointF& image_point) const;
    [[nodiscard]] int selectedHandleAt(const QPointF& image_point) const;
    void drawShapeOverlay(QPainter& painter,
                          const ImageShapeData& shape,
                          int opacity = 100) const;

    QImage image_;
    QImage transient_image_;
    double zoom_ = 1.0;
    QPointF pan_;
    bool fit_to_window_ = true;
    bool crop_mode_ = false;
    bool paint_mode_ = false;
    bool eraser_mode_ = false;
    bool shape_creation_mode_ = false;
    bool shape_selection_mode_ = false;
    bool eraser_preview_enabled_ = false;
    bool selecting_crop_ = false;
    bool painting_ = false;
    bool erasing_ = false;
    bool resizing_brush_ = false;
    bool shift_constrain_held_ = false;
    bool panning_ = false;
    bool creating_shape_ = false;
    bool transforming_shape_ = false;
    bool moving_shape_ = false;
    int resizing_shape_endpoint_ = -1;
    QPointF crop_start_;
    QRectF crop_selection_;
    QPointF pan_start_;
    QPointF initial_pan_;
    QPointF brush_resize_start_;
    QPoint brush_resize_global_start_;
    int brush_resize_initial_diameter_ = 12;
    QVector<QPointF> paint_points_;
    ImageShapeData shape_style_;
    ImageShapeData shape_interaction_initial_;
    ImageShapeData shape_interaction_current_;
    QVector<ImageShapePlacement> shape_placements_;
    QString selected_shape_id_;
    QString transforming_shape_id_;
    QPointF shape_gesture_start_;
    QColor brush_color_ = Qt::black;
    int brush_diameter_ = 12;
    QPointF brush_cursor_position_;
    bool brush_cursor_visible_ = false;
};

} // namespace image_editor
