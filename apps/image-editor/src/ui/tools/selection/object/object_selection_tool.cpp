#include "object_selection_tool.h"

#include <QLineF>
#include <QPainterPathStroker>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace image_editor {
namespace {

QVector<QPointF>* operationPoints(ImageOperation* operation) {
    if (operation == nullptr) return nullptr;
    if (operation->kind == OperationKind::PaintStroke) return &operation->paint_stroke.points;
    if (operation->kind == OperationKind::EraseStroke) return &operation->erase_stroke.points;
    return nullptr;
}

int operationDiameter(const ImageOperation& operation) {
    if (operation.kind == OperationKind::PaintStroke) return operation.paint_stroke.diameter;
    if (operation.kind == OperationKind::EraseStroke) return operation.erase_stroke.diameter;
    if (operation.kind == OperationKind::Shape) return operation.shape.stroke_width;
    return 1;
}

void setOperationDiameter(ImageOperation* operation, int diameter) {
    if (operation == nullptr) return;
    if (operation->kind == OperationKind::PaintStroke) operation->paint_stroke.diameter = diameter;
    else if (operation->kind == OperationKind::EraseStroke) operation->erase_stroke.diameter = diameter;
    else if (operation->kind == OperationKind::Shape) operation->shape.stroke_width = diameter;
}

QPainterPath strokePath(const QVector<QPointF>& points, qreal width) {
    QPainterPath path;
    if (points.isEmpty()) return path;
    if (points.size() == 1) {
        const qreal radius = std::max<qreal>(0.5, width / 2.0);
        path.addEllipse(points.front(), radius, radius);
        return path;
    }
    path.moveTo(points.front());
    for (qsizetype index = 1; index < points.size(); ++index) {
        path.lineTo(points.at(index));
    }
    QPainterPathStroker stroker;
    stroker.setWidth(width);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(path);
}

} // namespace

QString ObjectSelectionTool::objectId(const ImageOperation& operation) {
    switch (operation.kind) {
    case OperationKind::PaintStroke: return operation.paint_stroke.id;
    case OperationKind::EraseStroke: return operation.erase_stroke.id;
    case OperationKind::Shape: return operation.shape.id;
    case OperationKind::Text: return operation.text.id;
    case OperationKind::RasterImage: return operation.raster.id;
    default: return {};
    }
}

QPainterPath ObjectSelectionTool::objectPath(const ImageOperation& operation,
                                             qreal extra) {
    if (operation.kind == OperationKind::RasterImage) {
        QPainterPath path;
        path.addRect(QRectF(QPointF(), QSizeF(operation.raster.source_size)));
        return operation.raster.transform.map(path);
    }
    if (operation.kind == OperationKind::PaintStroke) {
        return strokePath(operation.paint_stroke.points,
                          operation.paint_stroke.diameter + extra * 2.0);
    }
    if (operation.kind == OperationKind::EraseStroke) {
        return strokePath(operation.erase_stroke.points,
                          operation.erase_stroke.diameter + extra * 2.0);
    }
    if (operation.kind == OperationKind::Text) {
        QPainterPath path;
        path.addRect(imageTextBounds(operation.text));
        return path;
    }
    if (operation.kind != OperationKind::Shape) return {};

    const auto& shape = operation.shape;
    QPainterPath path;
    if (shape.kind == ImageShapeKind::Line) {
        path.moveTo(shape.start);
        path.lineTo(shape.end);
    } else if (shape.kind == ImageShapeKind::Rectangle) {
        path.addRect(QRectF(shape.start, shape.end).normalized());
    } else {
        path.addEllipse(QRectF(shape.start, shape.end).normalized());
    }
    QPainterPath result;
    if (shape.kind != ImageShapeKind::Line && shape.fill_enabled) result = path;
    if (shape.stroke_enabled) {
        QPainterPathStroker stroker;
        stroker.setWidth(std::max<qreal>(1.0, shape.stroke_width + extra * 2.0));
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        result = result.united(stroker.createStroke(path));
    }
    return result;
}

QRectF ObjectSelectionTool::visibleObjectBounds(const ImageOperation& operation) {
    return objectPath(operation).boundingRect();
}

void ObjectSelectionTool::setObjects(QVector<ImageObjectPlacement> placements,
                                     QStringList selected_object_ids) {
    object_placements_ = std::move(placements);
    QStringList visible_ids;
    for (const auto& placement : object_placements_) {
        const QString id = objectId(placement.operation);
        if (!id.isEmpty()) visible_ids.append(id);
    }
    selected_object_ids_.clear();
    for (const auto& id : selected_object_ids) {
        if (visible_ids.contains(id) && !selected_object_ids_.contains(id)) {
            selected_object_ids_.append(id);
        }
    }
}

int ObjectSelectionTool::hitTestAt(const QPointF& image_point, qreal zoom) const {
    const qreal tolerance = 9.0 / std::max(zoom, 0.01);
    for (qsizetype index = 0; index < object_placements_.size(); ++index) {
        const auto& operation = object_placements_.at(index).operation;
        if (operation.kind == OperationKind::PaintStroke &&
            operation.paint_stroke.color.alpha() == 0) continue;
        if (operation.kind == OperationKind::Shape &&
            (!operation.shape.stroke_enabled || operation.shape.stroke_color.alpha() == 0) &&
            (!operation.shape.fill_enabled || operation.shape.fill_color.alpha() == 0)) continue;
        if (operation.kind == OperationKind::Text && operation.text.color.alpha() == 0) continue;
        if (objectPath(operation, tolerance).contains(image_point)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

ObjectSelectionToolResult ObjectSelectionTool::selectObjectAt(int index) {
    ObjectSelectionToolResult result;
    if (index < 0 || index >= object_placements_.size()) return result;
    const auto& placement = object_placements_.at(index);
    const QString id = objectId(placement.operation);
    if (id.isEmpty()) return result;
    selected_object_ids_ = {id};
    result.handled = true;
    appendSelectionEvent(result, placement.layer_id);
    return result;
}

QVector<ImageObjectPlacement> ObjectSelectionTool::selectedObjects() const {
    QVector<ImageObjectPlacement> result;
    const auto& source = transforming_objects_ ? transform_current_objects_ : object_placements_;
    for (const auto& placement : source) {
        if (selected_object_ids_.contains(objectId(placement.operation))) {
            result.append(placement);
        }
    }
    return result;
}

QRectF ObjectSelectionTool::objectBounds(
    const QVector<ImageObjectPlacement>& objects) const {
    QRectF bounds;
    bool first = true;
    for (const auto& object : objects) {
        const QRectF object_bounds = visibleObjectBounds(object.operation);
        if (object_bounds.isEmpty()) continue;
        bounds = first ? object_bounds : bounds.united(object_bounds);
        first = false;
    }
    return first ? QRectF{} : bounds;
}

QPointF ObjectSelectionTool::rotationHandle(const ImageOperation& operation,
                                            qreal zoom) const {
    const auto& raster = operation.raster;
    const QPointF center = raster.transform.map(
        QPointF(raster.source_size.width() / 2.0, raster.source_size.height() / 2.0));
    const QPointF top = raster.transform.map(QPointF(raster.source_size.width() / 2.0, 0));
    QPointF direction = top - center;
    const qreal length = std::hypot(direction.x(), direction.y());
    return top + direction / std::max(length, 0.001) * (28.0 / std::max(zoom, 0.01));
}

int ObjectSelectionTool::resizeHandleAt(const QPointF& image_point, qreal zoom) const {
    if (selected_object_ids_.isEmpty()) return -1;
    const auto selected = selectedObjects();
    if (selected.size() == 1 && selected.front().operation.kind == OperationKind::RasterImage) {
        const auto& operation = selected.front().operation;
        const qreal tolerance = 9.0 / std::max(zoom, 0.01);
        if (QLineF(image_point, rotationHandle(operation, zoom)).length() <= tolerance) return 6;
        const auto& raster = operation.raster;
        const qreal width = raster.source_size.width();
        const qreal height = raster.source_size.height();
        const QPointF corners[] = {{0, 0}, {width, 0}, {0, height}, {width, height}};
        for (int index = 0; index < 4; ++index) {
            if (QLineF(image_point, raster.transform.map(corners[index])).length() <= tolerance)
                return index;
        }
        return -1;
    }
    if (selected.size() == 1 && selected.front().operation.kind == OperationKind::Text) {
        const QRectF bounds = imageTextBounds(selected.front().operation.text);
        const qreal tolerance = 9.0 / std::max(zoom, 0.01);
        const QPointF handles[] = {
            QPointF(bounds.left(), bounds.center().y()),
            QPointF(bounds.right(), bounds.center().y())};
        for (int index = 0; index < 2; ++index) {
            if (QLineF(image_point, handles[index]).length() <= tolerance) return 4 + index;
        }
        return -1;
    }
    const QRectF bounds = objectBounds(selectedObjects());
    if (bounds.isEmpty()) return -1;
    const qreal tolerance = 9.0 / std::max(zoom, 0.01);
    const QPointF handles[] = {bounds.topLeft(), bounds.topRight(),
                               bounds.bottomLeft(), bounds.bottomRight()};
    for (int index = 0; index < 4; ++index) {
        if (QLineF(image_point, handles[index]).length() <= tolerance) return index;
    }
    return -1;
}

QVector<ImageObjectPlacement> ObjectSelectionTool::selectionHits(
    const QRectF& bounds) const {
    QVector<ImageObjectPlacement> hits;
    if (bounds.isEmpty()) return hits;
    QPainterPath selection;
    selection.addRect(bounds.normalized());
    for (const auto& placement : object_placements_) {
        const auto& operation = placement.operation;
        if (operation.kind == OperationKind::PaintStroke &&
            operation.paint_stroke.color.alpha() == 0) continue;
        if (operation.kind == OperationKind::Shape &&
            (!operation.shape.stroke_enabled || operation.shape.stroke_color.alpha() == 0) &&
            (!operation.shape.fill_enabled || operation.shape.fill_color.alpha() == 0)) continue;
        if (operation.kind == OperationKind::Text && operation.text.color.alpha() == 0) continue;
        const QPainterPath geometry = objectPath(operation);
        if (geometry.intersects(selection) || selection.contains(geometry)) hits.append(placement);
    }
    return hits;
}

QVector<ImageObjectPlacement> ObjectSelectionTool::transformObjects(
    const QVector<ImageObjectPlacement>& objects, qreal scale_x, qreal scale_y,
    const QPointF& origin, const QPointF& destination) const {
    QVector<ImageObjectPlacement> transformed = objects;
    const qreal width_scale = std::sqrt(std::abs(scale_x * scale_y));
    const int max_diameter = ImageDocumentStore::kMaximumPaintBrushDiameter;
    for (auto& placement : transformed) {
        auto& operation = placement.operation;
        const auto map_point = [origin, destination, scale_x, scale_y](const QPointF& point) {
            return QPointF(destination.x() + (point.x() - origin.x()) * scale_x,
                           destination.y() + (point.y() - origin.y()) * scale_y);
        };
        if (auto* points = operationPoints(&operation)) {
            for (QPointF& point : *points) point = map_point(point);
        } else if (operation.kind == OperationKind::Shape) {
            operation.shape.start = map_point(operation.shape.start);
            operation.shape.end = map_point(operation.shape.end);
        } else if (operation.kind == OperationKind::Text) {
            operation.text.position = map_point(operation.text.position);
            operation.text.box_width = std::max<qreal>(1.0,
                operation.text.box_width * std::abs(scale_x));
        } else if (operation.kind == OperationKind::RasterImage) {
            operation.raster.transform *= QTransform(scale_x, 0, 0, scale_y,
                destination.x() - origin.x() * scale_x,
                destination.y() - origin.y() * scale_y);
        }
        const int diameter = std::clamp(
            static_cast<int>(std::lround(operationDiameter(operation) * width_scale)),
            1, max_diameter);
        setOperationDiameter(&operation, diameter);
    }
    return transformed;
}

bool ObjectSelectionTool::rasterTransform() const noexcept {
    return transform_initial_objects_.size() == 1 &&
        transform_initial_objects_.front().operation.kind == OperationKind::RasterImage;
}

bool ObjectSelectionTool::transformContainsRaster() const noexcept {
    return std::any_of(transform_current_objects_.cbegin(), transform_current_objects_.cend(),
        [](const ImageObjectPlacement& placement) {
            return placement.operation.kind == OperationKind::RasterImage;
        });
}

void ObjectSelectionTool::appendSelectionEvent(
    ObjectSelectionToolResult& result, const QString& active_layer_id) const {
    ObjectSelectionToolEvent event;
    event.type = ObjectSelectionToolEventType::ObjectsSelected;
    event.object_ids = selected_object_ids_;
    event.active_layer_id = active_layer_id;
    result.events.append(std::move(event));
}

void ObjectSelectionTool::beginObjectTransform(
    bool resize, int handle, const ObjectSelectionToolContext& context,
    ObjectSelectionToolResult& result) {
    transform_initial_objects_ = selectedObjects();
    if (transform_initial_objects_.isEmpty()) return;
    transform_current_objects_ = transform_initial_objects_;
    transform_initial_bounds_ = objectBounds(transform_initial_objects_);
    transform_start_ = context.unbounded_image_position;
    resizing_objects_ = resize;
    rotating_objects_ = handle == 6;
    resizing_handle_ = handle;
    transforming_objects_ = true;
    moved_interaction_ = false;
    resizing_text_width_ = resize && transform_initial_objects_.size() == 1 &&
        transform_initial_objects_.front().operation.kind == OperationKind::Text;
    if (resize && resizing_text_width_) {
        const bool left_handle = handle == 4;
        transform_fixed_anchor_ = left_handle
            ? transform_initial_bounds_.topRight() : transform_initial_bounds_.topLeft();
    } else if (resize && !rotating_objects_) {
        const QPointF anchors[] = {transform_initial_bounds_.bottomRight(),
                                   transform_initial_bounds_.bottomLeft(),
                                   transform_initial_bounds_.topRight(),
                                   transform_initial_bounds_.topLeft()};
        transform_fixed_anchor_ = anchors[handle];
    }
    ObjectSelectionToolEvent event;
    event.type = ObjectSelectionToolEventType::TransformStarted;
    event.object_ids = selected_object_ids_;
    result.events.append(std::move(event));
}

ObjectSelectionToolResult ObjectSelectionTool::press(
    const ObjectSelectionToolContext& context) {
    ObjectSelectionToolResult result;
    const int handle = resizeHandleAt(context.unbounded_image_position, context.zoom);
    if (!context.pointer_on_image && handle < 0) return result;

    result.handled = true;
    if (handle >= 0) {
        beginObjectTransform(true, handle, context, result);
        return result;
    }

    const int hit = hitTestAt(context.unbounded_image_position, context.zoom);
    if (hit < 0) {
        selecting_objects_ = true;
        selection_toggle_ = context.shift;
        moved_interaction_ = false;
        selection_start_ = context.widget_position;
        object_selection_rect_ = QRectF(selection_start_, selection_start_);
        return result;
    }

    const ImageObjectPlacement placement = object_placements_.at(hit);
    const QString id = objectId(placement.operation);
    if (context.shift) {
        if (selected_object_ids_.contains(id)) selected_object_ids_.removeAll(id);
        else selected_object_ids_.append(id);
        QString active_layer = placement.layer_id;
        if (!selected_object_ids_.contains(id)) {
            active_layer.clear();
            for (const auto& selected : object_placements_) {
                if (selected_object_ids_.contains(objectId(selected.operation))) {
                    active_layer = selected.layer_id;
                    break;
                }
            }
        }
        appendSelectionEvent(result, active_layer);
        return result;
    }

    if (!selected_object_ids_.contains(id)) selected_object_ids_ = {id};
    appendSelectionEvent(result, placement.layer_id);
    result.deferred_transform_start = ObjectSelectionToolTransformRequest{
        context, false, -1};
    return result;
}

ObjectSelectionToolResult ObjectSelectionTool::beginTransform(
    const ObjectSelectionToolTransformRequest& request) {
    ObjectSelectionToolResult result;
    result.handled = true;
    beginObjectTransform(request.resize, request.handle, request.context, result);
    return result;
}

QPointF ObjectSelectionTool::widgetToImageCoordinates(
    const QPointF& position, const ObjectSelectionToolContext& context) const {
    if (context.image_target.isEmpty() || context.zoom <= 0.0) return {};
    const qreal max_x = std::max<qreal>(0.0, context.image_bounds.width() - 1.0);
    const qreal max_y = std::max<qreal>(0.0, context.image_bounds.height() - 1.0);
    const qreal x = (position.x() - context.image_target.left()) / context.zoom;
    const qreal y = (position.y() - context.image_target.top()) / context.zoom;
    return {std::clamp(x, 0.0, max_x), std::clamp(y, 0.0, max_y)};
}

ObjectSelectionToolResult ObjectSelectionTool::move(
    const ObjectSelectionToolContext& context) {
    ObjectSelectionToolResult result;
    if (selecting_objects_) {
        result.handled = true;
        object_selection_rect_ = QRectF(selection_start_, context.widget_position).normalized();
        moved_interaction_ = moved_interaction_ ||
            QLineF(selection_start_, context.widget_position).length() >= 4.0;
        return result;
    }
    if (transforming_objects_) {
        result.handled = true;
        updateObjectTransform(context, result);
    }
    return result;
}

void ObjectSelectionTool::updateObjectTransform(
    const ObjectSelectionToolContext& context, ObjectSelectionToolResult& result) {
    if (!transforming_objects_ || transform_initial_objects_.isEmpty()) return;
    if (rasterTransform()) {
        const auto& original = transform_initial_objects_.front().operation.raster;
        auto& raster = transform_current_objects_.front().operation.raster;
        raster.transform = original.transform;
        if (rotating_objects_) {
            const QPointF pivot = original.transform.map(QPointF(
                original.source_size.width() / 2.0, original.source_size.height() / 2.0));
            const auto a = transform_start_ - pivot;
            const auto b = context.unbounded_image_position - pivot;
            qreal degrees = (std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x())) *
                180.0 / std::acos(-1.0);
            if (context.shift) {
                const qreal base = std::atan2(original.transform.m12(),
                    original.transform.m11()) * 180.0 / std::acos(-1.0);
                degrees = std::round((degrees + base) / 15.0) * 15.0 - base;
            }
            QTransform rotation;
            rotation.translate(pivot.x(), pivot.y());
            rotation.rotate(degrees);
            rotation.translate(-pivot.x(), -pivot.y());
            raster.transform *= rotation;
        } else if (resizing_objects_) {
            const qreal width = original.source_size.width();
            const qreal height = original.source_size.height();
            const QPointF corners[] = {{0, 0}, {width, 0}, {0, height}, {width, height}};
            const QPointF anchor = corners[3 - resizing_handle_];
            const QPointF initial = corners[resizing_handle_] - anchor;
            const QPointF dragged = original.transform.inverted().map(
                context.unbounded_image_position) - anchor;
            qreal scale_x = std::max(0.001, dragged.x() / initial.x());
            qreal scale_y = std::max(0.001, dragged.y() / initial.y());
            if (!context.alt) scale_x = scale_y = std::max(scale_x, scale_y);
            const QTransform local(scale_x, 0, 0, scale_y,
                anchor.x() * (1 - scale_x), anchor.y() * (1 - scale_y));
            raster.transform = local * original.transform;
        } else {
            const QPointF delta = context.unbounded_image_position - transform_start_;
            raster.transform *= QTransform::fromTranslate(delta.x(), delta.y());
        }
        moved_interaction_ = true;
        ObjectSelectionToolEvent event;
        event.type = ObjectSelectionToolEventType::PreviewRequested;
        event.objects = transform_current_objects_;
        result.events.append(std::move(event));
        return;
    }

    const QSize size = context.image_bounds.size().toSize();
    if (!resizing_objects_) {
        QPointF delta = context.image_position - transform_start_;
        delta.setX(std::clamp(delta.x(), -transform_initial_bounds_.left(),
            std::max(0.0, size.width() - transform_initial_bounds_.right())));
        delta.setY(std::clamp(delta.y(), -transform_initial_bounds_.top(),
            std::max(0.0, size.height() - transform_initial_bounds_.bottom())));
        moved_interaction_ = moved_interaction_ || !qFuzzyIsNull(delta.x()) ||
            !qFuzzyIsNull(delta.y());
        transform_current_objects_ = transformObjects(
            transform_initial_objects_, 1.0, 1.0,
            transform_initial_bounds_.topLeft(),
            transform_initial_bounds_.topLeft() + delta);
        if (transformContainsRaster()) {
            ObjectSelectionToolEvent event;
            event.type = ObjectSelectionToolEventType::PreviewRequested;
            event.objects = transform_current_objects_;
            result.events.append(std::move(event));
        }
        return;
    }

    if (resizing_text_width_) {
        QPointF dragged = context.image_position;
        dragged.setX(std::clamp(dragged.x(), 0.0,
            static_cast<qreal>(size.width() - 1)));
        const bool left_handle = resizing_handle_ == 4;
        auto& text = transform_current_objects_.front().operation.text;
        const qreal anchor_x = transform_fixed_anchor_.x();
        const qreal width = std::max<qreal>(1.0, std::abs(dragged.x() - anchor_x));
        text.position.setX(left_handle ? anchor_x - width : anchor_x);
        text.box_width = width;
        moved_interaction_ = true;
        return;
    }

    QPointF dragged = context.image_position;
    dragged.setX(std::clamp(dragged.x(), 0.0, static_cast<qreal>(size.width() - 1)));
    dragged.setY(std::clamp(dragged.y(), 0.0, static_cast<qreal>(size.height() - 1)));
    const bool left_handle = resizing_handle_ == 0 || resizing_handle_ == 2;
    const bool top_handle = resizing_handle_ == 0 || resizing_handle_ == 1;
    const qreal original_width = std::max<qreal>(1.0, transform_initial_bounds_.width());
    const qreal original_height = std::max<qreal>(1.0, transform_initial_bounds_.height());
    qreal target_width = std::abs(dragged.x() - transform_fixed_anchor_.x());
    qreal target_height = std::abs(dragged.y() - transform_fixed_anchor_.y());
    target_width = std::max<qreal>(1.0, target_width);
    target_height = std::max<qreal>(1.0, target_height);
    qreal scale_x = target_width / original_width;
    qreal scale_y = target_height / original_height;
    if (!context.alt) {
        qreal scale = std::max(scale_x, scale_y);
        const qreal available_width = left_handle
            ? transform_fixed_anchor_.x()
            : (size.width() - 1.0 - transform_fixed_anchor_.x());
        const qreal available_height = top_handle
            ? transform_fixed_anchor_.y()
            : (size.height() - 1.0 - transform_fixed_anchor_.y());
        scale = std::min(scale, std::min(available_width / original_width,
                                         available_height / original_height));
        scale_x = std::max<qreal>(1.0 / original_width, scale);
        scale_y = std::max<qreal>(1.0 / original_height, scale);
    } else {
        const qreal available_width = left_handle
            ? transform_fixed_anchor_.x()
            : (size.width() - 1.0 - transform_fixed_anchor_.x());
        const qreal available_height = top_handle
            ? transform_fixed_anchor_.y()
            : (size.height() - 1.0 - transform_fixed_anchor_.y());
        scale_x = std::clamp(scale_x, 1.0 / original_width,
            std::max(1.0 / original_width, available_width / original_width));
        scale_y = std::clamp(scale_y, 1.0 / original_height,
            std::max(1.0 / original_height, available_height / original_height));
    }
    const qreal new_width = original_width * scale_x;
    const qreal new_height = original_height * scale_y;
    QPointF new_top_left = transform_initial_bounds_.topLeft();
    if (left_handle) new_top_left.setX(transform_fixed_anchor_.x() - new_width);
    if (top_handle) new_top_left.setY(transform_fixed_anchor_.y() - new_height);
    moved_interaction_ = true;
    transform_current_objects_ = transformObjects(
        transform_initial_objects_, scale_x, scale_y,
        transform_initial_bounds_.topLeft(), new_top_left);
    if (transformContainsRaster()) {
        ObjectSelectionToolEvent event;
        event.type = ObjectSelectionToolEventType::PreviewRequested;
        event.objects = transform_current_objects_;
        result.events.append(std::move(event));
    }
}

ObjectSelectionToolResult ObjectSelectionTool::release(
    const ObjectSelectionToolContext& context) {
    ObjectSelectionToolResult result;
    if (selecting_objects_) {
        result.handled = true;
        selecting_objects_ = false;
        const bool was_drag = moved_interaction_;
        const QRectF widget_bounds = object_selection_rect_.normalized();
        object_selection_rect_ = {};
        if (was_drag) {
            const QRectF image_bounds(
                widgetToImageCoordinates(widget_bounds.topLeft(), context),
                widgetToImageCoordinates(widget_bounds.bottomRight(), context));
            const auto hits = selectionHits(image_bounds.normalized());
            if (!selection_toggle_) selected_object_ids_.clear();
            for (const auto& hit : hits) {
                const QString id = objectId(hit.operation);
                if (selection_toggle_ && selected_object_ids_.contains(id)) {
                    selected_object_ids_.removeAll(id);
                } else if (!selected_object_ids_.contains(id)) {
                    selected_object_ids_.append(id);
                }
            }
            QString active_layer;
            for (const auto& placement : object_placements_) {
                if (selected_object_ids_.contains(objectId(placement.operation))) {
                    active_layer = placement.layer_id;
                    break;
                }
            }
            appendSelectionEvent(result, active_layer);
        } else if (!selection_toggle_) {
            selected_object_ids_.clear();
            appendSelectionEvent(result);
        }
        moved_interaction_ = false;
        selection_toggle_ = false;
        return result;
    }
    if (transforming_objects_) {
        result.handled = true;
        const auto changed_objects = transform_current_objects_;
        const bool changed = changed_objects != transform_initial_objects_;
        clearGesture();
        if (changed) {
            ObjectSelectionToolEvent event;
            event.type = ObjectSelectionToolEventType::GeometryChanged;
            event.objects = changed_objects;
            result.events.append(std::move(event));
        }
    }
    return result;
}

bool ObjectSelectionTool::cancelGesture() noexcept {
    if (!gestureActive()) return false;
    clearGesture();
    return true;
}

void ObjectSelectionTool::clearGesture() noexcept {
    selecting_objects_ = false;
    selection_toggle_ = false;
    transforming_objects_ = false;
    resizing_objects_ = false;
    rotating_objects_ = false;
    resizing_text_width_ = false;
    moved_interaction_ = false;
    resizing_handle_ = -1;
    object_selection_rect_ = {};
    transform_initial_objects_.clear();
    transform_current_objects_.clear();
    transform_initial_bounds_ = {};
}

void ObjectSelectionTool::paintOverlay(
    QPainter& painter, const ObjectSelectionToolRenderContext& context) const {
    if (context.selection_mode_active && !selected_object_ids_.isEmpty()) {
        const QRectF bounds = objectBounds(selectedObjects());
        if (!bounds.isEmpty()) {
            const QRectF widget_bounds(
                context.image_target.left() + bounds.left() * context.zoom,
                context.image_target.top() + bounds.top() * context.zoom,
                bounds.width() * context.zoom, bounds.height() * context.zoom);
            painter.save();
            painter.setClipRect(context.image_target);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(QPen(QColor(45, 155, 235), 1.0, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(widget_bounds);
            QVector<QPointF> handles;
            const auto selected = selectedObjects();
            if (selected.size() == 1 && selected.front().operation.kind == OperationKind::Text) {
                handles = {QPointF(widget_bounds.left(), widget_bounds.center().y()),
                           QPointF(widget_bounds.right(), widget_bounds.center().y())};
            } else if (selected.size() == 1 &&
                       selected.front().operation.kind == OperationKind::RasterImage) {
                painter.setClipping(false);
                const auto& operation = selected.front().operation;
                const auto& raster = operation.raster;
                const qreal width = raster.source_size.width();
                const qreal height = raster.source_size.height();
                const QPointF corners[] = {{0, 0}, {width, 0}, {0, height}, {width, height}};
                for (const auto& corner : corners) {
                    handles.append(context.image_target.topLeft() +
                                   raster.transform.map(corner) * context.zoom);
                }
                const QPolygonF outline{handles[0], handles[1], handles[3], handles[2]};
                painter.drawPolygon(outline);
                const QPointF handle = context.image_target.topLeft() +
                    rotationHandle(operation, context.zoom) * context.zoom;
                painter.drawLine((handles[0] + handles[1]) / 2, handle);
                painter.setBrush(QColor(244, 247, 251));
                painter.drawEllipse(handle, 5, 5);
            } else {
                handles = {widget_bounds.topLeft(), widget_bounds.topRight(),
                           widget_bounds.bottomLeft(), widget_bounds.bottomRight()};
            }
            for (const QPointF& handle : handles) {
                const QRectF box(handle.x() - 4.0, handle.y() - 4.0, 8.0, 8.0);
                painter.setPen(QPen(QColor(25, 28, 34), 1.0));
                painter.setBrush(QColor(244, 247, 251));
                painter.drawRect(box);
            }
            painter.restore();
        }
    }
    if (context.selection_mode_active && selecting_objects_) {
        const QRectF selection = object_selection_rect_.normalized()
            .intersected(context.image_target);
        painter.fillRect(selection, QColor(38, 150, 220, 36));
        painter.setPen(QPen(QColor(120, 205, 255), 1.2, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(selection);
    }
}

} // namespace image_editor
