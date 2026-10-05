#pragma once

#include "image_document_store.h"

#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QStringList>
#include <QVector>

#include <optional>

namespace image_editor {

struct ObjectSelectionToolContext {
    QPointF widget_position;
    QPointF image_position;
    QPointF unbounded_image_position;
    QRectF image_bounds;
    QRectF image_target;
    qreal zoom = 1.0;
    bool pointer_on_image = false;
    bool shift = false;
    bool alt = false;
};

struct ObjectSelectionToolRenderContext {
    QRectF image_target;
    qreal zoom = 1.0;
    bool selection_mode_active = false;
};

enum class ObjectSelectionToolEventType {
    ObjectsSelected,
    TransformStarted,
    PreviewRequested,
    GeometryChanged
};

struct ObjectSelectionToolEvent {
    ObjectSelectionToolEventType type = ObjectSelectionToolEventType::ObjectsSelected;
    QStringList object_ids;
    QString active_layer_id;
    QVector<ImageObjectPlacement> objects;
};

struct ObjectSelectionToolTransformRequest {
    ObjectSelectionToolContext context;
    bool resize = false;
    int handle = -1;
};

struct ObjectSelectionToolResult {
    bool handled = false;
    QVector<ObjectSelectionToolEvent> events;
    std::optional<ObjectSelectionToolTransformRequest> deferred_transform_start;
};

class ObjectSelectionTool final {
public:
    void setObjects(QVector<ImageObjectPlacement> placements,
                    QStringList selected_object_ids);

    [[nodiscard]] const QVector<ImageObjectPlacement>& objects() const noexcept {
        return object_placements_;
    }
    [[nodiscard]] const QStringList& selectedObjectIds() const noexcept {
        return selected_object_ids_;
    }
    [[nodiscard]] int hitTestAt(const QPointF& image_point, qreal zoom) const;
    [[nodiscard]] ObjectSelectionToolResult selectObjectAt(int index);

    [[nodiscard]] ObjectSelectionToolResult press(
        const ObjectSelectionToolContext& context);
    [[nodiscard]] ObjectSelectionToolResult beginTransform(
        const ObjectSelectionToolTransformRequest& request);
    [[nodiscard]] ObjectSelectionToolResult move(
        const ObjectSelectionToolContext& context);
    [[nodiscard]] ObjectSelectionToolResult release(
        const ObjectSelectionToolContext& context);
    [[nodiscard]] bool cancelGesture() noexcept;
    void clearGesture() noexcept;

    [[nodiscard]] bool gestureActive() const noexcept {
        return selecting_objects_ || transforming_objects_;
    }
    [[nodiscard]] bool selectingObjects() const noexcept { return selecting_objects_; }
    [[nodiscard]] bool transformingObjects() const noexcept { return transforming_objects_; }
    [[nodiscard]] bool transformContainsRaster() const noexcept;
    [[nodiscard]] bool resizingTextWidth() const noexcept { return resizing_text_width_; }
    [[nodiscard]] const QVector<ImageObjectPlacement>& currentTransformObjects() const noexcept {
        return transform_current_objects_;
    }

    void paintOverlay(QPainter& painter,
                      const ObjectSelectionToolRenderContext& context) const;

private:
    [[nodiscard]] int resizeHandleAt(const QPointF& image_point, qreal zoom) const;
    [[nodiscard]] QVector<ImageObjectPlacement> selectedObjects() const;
    [[nodiscard]] QRectF objectBounds(
        const QVector<ImageObjectPlacement>& objects) const;
    [[nodiscard]] QVector<ImageObjectPlacement> selectionHits(const QRectF& bounds) const;
    [[nodiscard]] QVector<ImageObjectPlacement> transformObjects(
        const QVector<ImageObjectPlacement>& objects,
        qreal scale_x, qreal scale_y, const QPointF& origin,
        const QPointF& destination) const;
    void beginObjectTransform(bool resize, int handle,
                              const ObjectSelectionToolContext& context,
                              ObjectSelectionToolResult& result);
    void updateObjectTransform(const ObjectSelectionToolContext& context,
                               ObjectSelectionToolResult& result);
    void appendSelectionEvent(ObjectSelectionToolResult& result,
                              const QString& active_layer_id = {}) const;
    [[nodiscard]] QPointF rotationHandle(const ImageOperation& operation,
                                         qreal zoom) const;
    [[nodiscard]] bool rasterTransform() const noexcept;
    [[nodiscard]] QPointF widgetToImageCoordinates(
        const QPointF& position, const ObjectSelectionToolContext& context) const;
    [[nodiscard]] static QString objectId(const ImageOperation& operation);
    [[nodiscard]] static QPainterPath objectPath(
        const ImageOperation& operation, qreal extra = 0.0);
    [[nodiscard]] static QRectF visibleObjectBounds(const ImageOperation& operation);

    QVector<ImageObjectPlacement> object_placements_;
    QStringList selected_object_ids_;

    bool selecting_objects_ = false;
    bool selection_toggle_ = false;
    bool transforming_objects_ = false;
    bool resizing_objects_ = false;
    bool rotating_objects_ = false;
    bool resizing_text_width_ = false;
    bool moved_interaction_ = false;
    int resizing_handle_ = -1;
    QPointF selection_start_;
    QRectF object_selection_rect_;
    QVector<ImageObjectPlacement> transform_initial_objects_;
    QVector<ImageObjectPlacement> transform_current_objects_;
    QRectF transform_initial_bounds_;
    QPointF transform_start_;
    QPointF transform_fixed_anchor_;
};

} // namespace image_editor
