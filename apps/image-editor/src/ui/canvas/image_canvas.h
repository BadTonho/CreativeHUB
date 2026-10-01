#pragma once

#include "image_document_store.h"

#include <QImage>
#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QPlainTextEdit;
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
    void setShapeCreationMode(bool enabled);
    void setTextCreationMode(bool enabled);
    void setObjectSelectionMode(bool enabled);
    void setShapeStyle(const ImageShapeData& style);
    void setTextStyle(const ImageTextData& style);
    void beginTextEditing(const ImageTextData& text, bool existing);
    void commitTextEditing();
    [[nodiscard]] bool textEditing() const noexcept;
    void setObjectPlacements(QVector<ImageObjectPlacement> placements,
                             QStringList selected_object_ids);
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
    void textCommitted(const image_editor::ImageTextData& text, bool existing);
    void textEditingStarted(const image_editor::ImageTextData& text, bool existing);
    void textEditingCancelled();
    void objectsSelected(const QStringList& object_ids, const QString& active_layer_id);
    void objectTransformStarted(const QStringList& object_ids);
    void objectsGeometryChanged(
        const QVector<image_editor::ImageObjectPlacement>& objects);

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
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

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
    [[nodiscard]] int objectHitAt(const QPointF& image_point) const;
    [[nodiscard]] int resizeHandleAt(const QPointF& image_point) const;
    [[nodiscard]] QRectF objectBounds(const QVector<ImageObjectPlacement>& objects) const;
    [[nodiscard]] QVector<ImageObjectPlacement> selectedObjects() const;
    [[nodiscard]] QVector<ImageObjectPlacement> selectionHits(const QRectF& bounds) const;
    [[nodiscard]] QVector<ImageObjectPlacement> transformObjects(
        const QVector<ImageObjectPlacement>& objects,
        qreal scale_x, qreal scale_y, const QPointF& origin,
        const QPointF& destination) const;
    void drawShapeOverlay(QPainter& painter,
                          const ImageShapeData& shape,
                          int opacity = 100) const;
    void drawTextOverlay(QPainter& painter,
                         const ImageTextData& text,
                         int opacity = 100) const;
    void drawObjectOverlay(QPainter& painter,
                           const ImageObjectPlacement& object) const;
    void beginObjectTransform(bool resize, int handle, const QPointF& image_point);
    void updateObjectTransform(const QPointF& image_point, bool freeform);
    void clearObjectInteraction();
    void updateTextEditorContentAndGeometry();
    void updateTextEditorGeometry();
    void hideTextEditorGlyphs();
    void applyTextEditorStyle();
    void finishTextEditing(bool commit);

    QImage image_;
    QImage transient_image_;
    double zoom_ = 1.0;
    QPointF pan_;
    bool fit_to_window_ = true;
    bool crop_mode_ = false;
    bool paint_mode_ = false;
    bool eraser_mode_ = false;
    bool shape_creation_mode_ = false;
    bool text_creation_mode_ = false;
    bool object_selection_mode_ = false;
    bool eraser_preview_enabled_ = false;
    bool selecting_crop_ = false;
    bool painting_ = false;
    bool erasing_ = false;
    bool resizing_brush_ = false;
    bool shift_constrain_held_ = false;
    bool panning_ = false;
    bool creating_shape_ = false;
    bool selecting_objects_ = false;
    bool selection_toggle_ = false;
    bool transforming_objects_ = false;
    bool resizing_objects_ = false;
    bool resizing_text_width_ = false;
    bool moved_interaction_ = false;
    int resizing_handle_ = -1;
    QPointF crop_start_;
    QRectF crop_selection_;
    QPointF selection_start_;
    QRectF object_selection_rect_;
    QPointF pan_start_;
    QPointF initial_pan_;
    QPointF brush_resize_start_;
    QPoint brush_resize_global_start_;
    int brush_resize_initial_diameter_ = 12;
    QVector<QPointF> paint_points_;
    ImageShapeData shape_style_;
    ImageTextData text_style_;
    ImageTextData text_editing_;
    QPlainTextEdit* text_editor_ = nullptr;
    bool text_editing_existing_ = false;
    bool text_editor_geometry_update_pending_ = false;
    qreal text_editing_initial_box_width_ = 1.0;
    bool creating_text_frame_ = false;
    QPointF text_frame_start_;
    QPointF text_frame_current_;
    ImageShapeData shape_interaction_current_;
    QVector<ImageObjectPlacement> object_placements_;
    QStringList selected_object_ids_;
    QVector<ImageObjectPlacement> transform_initial_objects_;
    QVector<ImageObjectPlacement> transform_current_objects_;
    QRectF transform_initial_bounds_;
    QPointF transform_start_;
    QPointF transform_fixed_anchor_;
    QColor brush_color_ = Qt::black;
    int brush_diameter_ = 12;
    QPointF brush_cursor_position_;
    bool brush_cursor_visible_ = false;
};

} // namespace image_editor
