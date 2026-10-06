#pragma once

#include "image_document_store.h"
#include "../tools/brush/eraser_tool.h"
#include "../tools/brush/paint_tool.h"
#include "../tools/crop/crop_tool.h"
#include "../tools/eyedropper/eyedropper_tool.h"
#include "../tools/selection/area_selection_tool.h"
#include "../tools/selection/object/object_selection_tool.h"
#include "../tools/shapes/shape_tool.h"
#include "../tools/text/text_tool.h"

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
class QWheelEvent;
class QDragEnterEvent;
class QDropEvent;

namespace image_editor {

class ImageCanvas final : public QWidget {
    Q_OBJECT

public:
    enum class AreaSelectionShape { Rectangle, Ellipse };
    enum class AreaSelectionCombineMode { Replace, Add, Subtract };

    explicit ImageCanvas(QWidget* parent = nullptr);

    void setImage(QImage image, bool resetView = true);
    void setCropMode(bool enabled);
    void setPaintMode(bool enabled);
    void setEraserMode(bool enabled);
    void setShapeCreationMode(bool enabled);
    void setTextCreationMode(bool enabled);
    void setObjectSelectionMode(bool enabled);
    void setAreaSelectionMode(bool enabled);
    void setEyedropperMode(bool enabled);
    void setAreaSelectionOptions(AreaSelectionShape shape,
                                 AreaSelectionCombineMode combine_mode);
    void clearAreaSelection();
    void translateAreaSelection(const QPoint& delta);
    void cancelAreaSelectionGesture();
    [[nodiscard]] bool hasAreaSelection() const noexcept;
    [[nodiscard]] bool areaSelectionGestureActive() const noexcept;
    [[nodiscard]] bool areaSelectionMode() const noexcept { return area_selection_mode_; }
    [[nodiscard]] std::optional<QPainterPath> areaSelectionClipPath() const;
    void setShapeStyle(const ImageShapeData& style);
    void setTextStyle(const ImageTextData& style);
    void beginTextEditing(const ImageTextData& text, bool existing);
    void commitTextEditing();
    [[nodiscard]] bool textEditing() const noexcept;
    void setObjectPlacements(QVector<ImageObjectPlacement> placements,
                             QStringList selected_object_ids);
    void setEraserPreviewEnabled(bool enabled);
    void setTransientImage(QImage image);
    void setMaskEditing(bool enabled);
    [[nodiscard]] bool maskEditing() const noexcept { return mask_editing_; }
    void setBrush(QColor color, int diameter);
    void fitToWindow();
    [[nodiscard]] bool cropMode() const noexcept { return crop_mode_; }
    [[nodiscard]] bool paintMode() const noexcept { return paint_mode_; }
    [[nodiscard]] bool eraserMode() const noexcept { return eraser_mode_; }
    [[nodiscard]] bool eyedropperMode() const noexcept { return eyedropper_mode_; }
    [[nodiscard]] double zoomFactor() const noexcept { return zoom_; }

signals:
    void imagesDropped(const QStringList& paths, const QPointF& center);
    void objectsPreviewRequested(const QVector<image_editor::ImageObjectPlacement>& objects);
    void cropSelected(const QRect& image_rect);
    void paintStrokeSelected(const QVector<QPointF>& image_points,
                             const QColor& color,
                             int diameter);
    void maskPaintPreviewRequested(const QVector<QPointF>& image_points,
                                   const QColor& color, int diameter);
    void erasePreviewRequested(const QVector<QPointF>& image_points, int diameter);
    void erasePreviewCleared();
    void eraseStrokeSelected(const QVector<QPointF>& image_points, int diameter);
    void brushDiameterChanged(int diameter);
    void colorSampled(const QColor& color);
    void shapeCreated(const image_editor::ImageShapeData& shape);
    void textCommitted(const image_editor::ImageTextData& text, bool existing);
    void textEditingStarted(const image_editor::ImageTextData& text, bool existing);
    void textEditingCancelled();
    void objectsSelected(const QStringList& object_ids, const QString& active_layer_id);
    void objectTransformStarted(const QStringList& object_ids);
    void objectsGeometryChanged(
        const QVector<image_editor::ImageObjectPlacement>& objects);
    void areaSelectionChanged(bool active);
    void areaSelectionRejected(const QString& reason);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
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

private:
    [[nodiscard]] QRectF imageTargetRect() const;
    [[nodiscard]] CropToolContext cropToolContext() const;
    [[nodiscard]] QPointF widgetToCropImageCoordinates(const QPointF& position) const;
    [[nodiscard]] QPointF widgetToImageCoordinates(const QPointF& position) const;
    void updateHoverCursor(const QPointF& position);
    [[nodiscard]] BrushToolContext brushToolContext(const QPointF& position) const;
    void dispatchBrushToolEvents(const QVector<BrushToolEvent>& events);
    void updateBrushToolCursor(const QPointF& position);
    void resetBrushTools(bool clear_preview_notification);
    [[nodiscard]] ObjectSelectionToolContext objectSelectionToolContext(
        const QPointF& position, Qt::KeyboardModifiers modifiers) const;
    void dispatchObjectSelectionToolEvents(
        const QVector<ObjectSelectionToolEvent>& events);
    void drawObjectOverlay(QPainter& painter,
                           const ImageObjectPlacement& object) const;
    [[nodiscard]] QPointF unboundedImagePoint(const QPointF& position) const;
    void clearObjectInteraction();
    [[nodiscard]] TextToolContext textToolContext() const;
    void updateTextEditorGeometry();
    void finishTextEditing(bool commit);

    QImage image_;
    QImage transient_image_;
    double zoom_ = 1.0;
    QPointF pan_;
    bool fit_to_window_ = true;
    bool crop_mode_ = false;
    bool paint_mode_ = false;
    bool mask_editing_ = false;
    bool eraser_mode_ = false;
    bool shape_creation_mode_ = false;
    bool text_creation_mode_ = false;
    bool object_selection_mode_ = false;
    bool area_selection_mode_ = false;
    bool eyedropper_mode_ = false;
    bool resizing_brush_ = false;
    bool shift_constrain_held_ = false;
    bool panning_ = false;
    QPointF pan_start_;
    QPointF initial_pan_;
    QPointF brush_resize_start_;
    QPoint brush_resize_global_start_;
    int brush_resize_initial_diameter_ = 12;
    PaintTool paint_tool_;
    EraserTool eraser_tool_;
    CropTool crop_tool_;
    EyedropperTool eyedropper_tool_;
    AreaSelectionTool area_selection_tool_;
    ObjectSelectionTool object_selection_tool_;
    ShapeTool shape_tool_;
    TextTool text_tool_;
    QColor brush_color_ = Qt::black;
    int brush_diameter_ = 12;
};

} // namespace image_editor
