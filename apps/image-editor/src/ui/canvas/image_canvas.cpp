#include "image_canvas.h"

#include "image_document_store.h"
#include "../transparency_checkerboard.h"

#include <QEvent>
#include <QCursor>
#include <QFrame>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPlainTextEdit>
#include <QPen>
#include <QTextLayout>
#include <QTextCursor>
#include <QTextOption>
#include <QSignalBlocker>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace image_editor {
namespace {

constexpr qsizetype kMaximumPaintPreviewPoints = 100'000;

QString objectId(const ImageOperation& operation) {
    switch (operation.kind) {
    case OperationKind::PaintStroke: return operation.paint_stroke.id;
    case OperationKind::EraseStroke: return operation.erase_stroke.id;
    case OperationKind::Shape: return operation.shape.id;
    case OperationKind::Text: return operation.text.id;
    default: return {};
    }
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

QPainterPath objectPath(const ImageOperation& operation, qreal extra = 0.0) {
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

QRectF visibleObjectBounds(const ImageOperation& operation) {
    return objectPath(operation).boundingRect();
}

} // namespace

ImageCanvas::ImageCanvas(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(240, 180);
    setMouseTracking(true);
    setAutoFillBackground(false);
    text_editor_ = new QPlainTextEdit(this);
    text_editor_->setObjectName(QStringLiteral("imageCanvasTextEditor"));
    text_editor_->setFrameShape(QFrame::NoFrame);
    text_editor_->setContentsMargins(0, 0, 0, 0);
    text_editor_->document()->setDocumentMargin(1.0);
    text_editor_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    text_editor_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    text_editor_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    text_editor_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    text_editor_->installEventFilter(this);
    text_editor_->hide();
    connect(text_editor_, &QPlainTextEdit::textChanged, this, [this]() {
        updateTextEditorContentAndGeometry();
    });
}

void ImageCanvas::setImage(QImage image, bool resetView) {
    if (erasing_ || !transient_image_.isNull()) emit erasePreviewCleared();
    image_ = std::move(image);
    transient_image_ = {};
    paint_points_.clear();
    painting_ = false;
    erasing_ = false;
    creating_shape_ = false;
    clearObjectInteraction();
    resizing_brush_ = false;
    if (resetView) {
        pan_ = {};
        fit_to_window_ = true;
        crop_selection_ = {};
        fitToWindow();
    }
    update();
}

void ImageCanvas::setCropMode(bool enabled) {
    if (erasing_ || !transient_image_.isNull()) emit erasePreviewCleared();
    crop_mode_ = enabled;
    if (enabled) {
        paint_mode_ = false;
        eraser_mode_ = false;
        shape_creation_mode_ = false;
        object_selection_mode_ = false;
    }
    selecting_crop_ = false;
    painting_ = false;
    erasing_ = false;
    transient_image_ = {};
    resizing_brush_ = false;
    paint_points_.clear();
    crop_selection_ = {};
    brush_cursor_visible_ = false;
    setCursor(enabled ? Qt::CrossCursor
                      : ((paint_mode_ || eraser_mode_) ? Qt::BlankCursor : Qt::ArrowCursor));
    update();
}

void ImageCanvas::setPaintMode(bool enabled) {
    if (erasing_ || !transient_image_.isNull()) emit erasePreviewCleared();
    paint_mode_ = enabled;
    if (enabled) {
        crop_mode_ = false;
        eraser_mode_ = false;
        shape_creation_mode_ = false;
        object_selection_mode_ = false;
    }
    painting_ = false;
    erasing_ = false;
    transient_image_ = {};
    resizing_brush_ = false;
    paint_points_.clear();
    selecting_crop_ = false;
    crop_selection_ = {};
    brush_cursor_visible_ = false;
    setCursor(enabled ? Qt::BlankCursor
                      : (crop_mode_ ? Qt::CrossCursor
                                    : (eraser_mode_ ? Qt::BlankCursor : Qt::ArrowCursor)));
    update();
}

void ImageCanvas::setEraserMode(bool enabled) {
    if (erasing_ || !transient_image_.isNull()) emit erasePreviewCleared();
    eraser_mode_ = enabled;
    if (enabled) {
        crop_mode_ = false;
        paint_mode_ = false;
        shape_creation_mode_ = false;
        object_selection_mode_ = false;
    }
    painting_ = false;
    erasing_ = false;
    resizing_brush_ = false;
    transient_image_ = {};
    paint_points_.clear();
    selecting_crop_ = false;
    crop_selection_ = {};
    brush_cursor_visible_ = false;
    setCursor(enabled ? Qt::BlankCursor
                      : (crop_mode_ ? Qt::CrossCursor
                                     : (paint_mode_ ? Qt::BlankCursor : Qt::ArrowCursor)));
    update();
}

void ImageCanvas::setShapeCreationMode(bool enabled) {
    shape_creation_mode_ = enabled;
    if (enabled) {
        crop_mode_ = false;
        paint_mode_ = false;
        eraser_mode_ = false;
        object_selection_mode_ = false;
        text_creation_mode_ = false;
    }
    painting_ = false;
    erasing_ = false;
    selecting_crop_ = false;
    creating_shape_ = false;
    clearObjectInteraction();
    paint_points_.clear();
    transient_image_ = {};
    crop_selection_ = {};
    brush_cursor_visible_ = false;
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void ImageCanvas::setTextCreationMode(bool enabled) {
    if (!enabled && text_editor_ != nullptr && text_editor_->isVisible()) {
        finishTextEditing(true);
    }
    text_creation_mode_ = enabled;
    if (enabled) {
        crop_mode_ = false;
        paint_mode_ = false;
        eraser_mode_ = false;
        shape_creation_mode_ = false;
        object_selection_mode_ = false;
    }
    painting_ = false;
    erasing_ = false;
    creating_shape_ = false;
    creating_text_frame_ = false;
    clearObjectInteraction();
    paint_points_.clear();
    transient_image_ = {};
    crop_selection_ = {};
    brush_cursor_visible_ = false;
    setCursor(enabled ? Qt::IBeamCursor : Qt::ArrowCursor);
    update();
}

void ImageCanvas::setObjectSelectionMode(bool enabled) {
    object_selection_mode_ = enabled;
    if (enabled) {
        crop_mode_ = false;
        paint_mode_ = false;
        eraser_mode_ = false;
        shape_creation_mode_ = false;
        text_creation_mode_ = false;
    }
    painting_ = false;
    erasing_ = false;
    selecting_crop_ = false;
    creating_shape_ = false;
    clearObjectInteraction();
    paint_points_.clear();
    transient_image_ = {};
    crop_selection_ = {};
    brush_cursor_visible_ = false;
    setCursor(Qt::ArrowCursor);
    update();
}

void ImageCanvas::setShapeStyle(const ImageShapeData& style) {
    shape_style_ = style;
    update();
}

void ImageCanvas::setTextStyle(const ImageTextData& style) {
    text_style_ = style;
    if (text_editor_ != nullptr && text_editor_->isVisible()) {
        const QString content = text_editing_.content;
        const QString id = text_editing_.id;
        const QPointF position = text_editing_.position;
        const qreal box_width = text_editing_.box_width;
        text_editing_ = style;
        text_editing_.id = id;
        text_editing_.content = content;
        text_editing_.position = position;
        text_editing_.box_width = box_width;
        applyTextEditorStyle();
        updateTextEditorContentAndGeometry();
    }
    update();
}

void ImageCanvas::beginTextEditing(const ImageTextData& text, bool existing) {
    if (text_editor_ == nullptr) return;
    if (text_editor_->isVisible()) finishTextEditing(true);
    text_editing_ = text;
    text_editing_existing_ = existing;
    text_editing_initial_box_width_ = std::max<qreal>(1.0, text.box_width);
    const QSignalBlocker blocker(text_editor_);
    text_editor_->setPlainText(text.content);
    applyTextEditorStyle();
    text_editor_->show();
    text_editor_->raise();
    updateTextEditorGeometry();
    text_editor_->setFocus(Qt::OtherFocusReason);
    text_editor_->moveCursor(QTextCursor::End);
    emit textEditingStarted(text_editing_, existing);
}

void ImageCanvas::commitTextEditing() {
    finishTextEditing(true);
}

bool ImageCanvas::textEditing() const noexcept {
    return text_editor_ != nullptr && text_editor_->isVisible();
}

void ImageCanvas::setObjectPlacements(QVector<ImageObjectPlacement> placements,
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
    update();
}

void ImageCanvas::setEraserPreviewEnabled(bool enabled) {
    eraser_preview_enabled_ = enabled;
    if (erasing_ && !enabled) emit erasePreviewRequested(paint_points_, brush_diameter_);
    if (erasing_ && enabled) {
        transient_image_ = {};
        emit erasePreviewCleared();
    }
    update();
}

void ImageCanvas::setTransientImage(QImage image) {
    if (!image.isNull() && image.size() == image_.size()) {
        transient_image_ = std::move(image);
    } else {
        transient_image_ = {};
    }
    update();
}

void ImageCanvas::setBrush(QColor color, int diameter) {
    if (color.isValid()) brush_color_ = std::move(color);
    brush_diameter_ = std::clamp(
        diameter, 1, ImageDocumentStore::kMaximumPaintBrushDiameter);
    update();
}

void ImageCanvas::fitToWindow() {
    if (image_.isNull() || width() <= 0 || height() <= 0) return;
    const double width_scale = static_cast<double>(std::max(1, width() - 48)) / image_.width();
    const double height_scale = static_cast<double>(std::max(1, height() - 48)) / image_.height();
    zoom_ = std::clamp(std::min(width_scale, height_scale), 0.01, 16.0);
    pan_ = {};
    fit_to_window_ = true;
    update();
}

QRectF ImageCanvas::imageTargetRect() const {
    if (image_.isNull()) return {};
    const QSizeF scaled(image_.width() * zoom_, image_.height() * zoom_);
    const QPointF origin((width() - scaled.width()) / 2.0 + pan_.x(),
                         (height() - scaled.height()) / 2.0 + pan_.y());
    return {origin, scaled};
}

QRect ImageCanvas::cropToImageCoordinates(const QRectF& selection) const {
    const QRectF target = imageTargetRect();
    const QRectF clipped = selection.normalized().intersected(target);
    if (clipped.isEmpty() || zoom_ <= 0.0) return {};

    const int left = std::clamp(static_cast<int>(std::floor((clipped.left() - target.left()) / zoom_)),
                                0, image_.width());
    const int top = std::clamp(static_cast<int>(std::floor((clipped.top() - target.top()) / zoom_)),
                               0, image_.height());
    const int right = std::clamp(static_cast<int>(std::ceil((clipped.right() - target.left()) / zoom_)),
                                 0, image_.width());
    const int bottom = std::clamp(static_cast<int>(std::ceil((clipped.bottom() - target.top()) / zoom_)),
                                  0, image_.height());
    return QRect(left, top, right - left, bottom - top);
}

QPointF ImageCanvas::widgetToImageCoordinates(const QPointF& position) const {
    const QRectF target = imageTargetRect();
    if (target.isEmpty() || zoom_ <= 0.0) return {};
    const qreal x = (position.x() - target.left()) / zoom_;
    const qreal y = (position.y() - target.top()) / zoom_;
    return {std::clamp(x, 0.0, static_cast<qreal>(image_.width() - 1)),
            std::clamp(y, 0.0, static_cast<qreal>(image_.height() - 1))};
}

QPointF ImageCanvas::constrainShapePoint(const QPointF& point,
                                        const QPointF& anchor,
                                        ImageShapeKind kind,
                                        bool shift) const {
    if (!shift) return point;
    const QPointF delta = point - anchor;
    if (kind == ImageShapeKind::Line) {
        const qreal length = std::hypot(delta.x(), delta.y());
        const qreal angle = std::atan2(delta.y(), delta.x());
        const qreal step = std::acos(-1.0) / 4.0;
        const qreal snapped = std::round(angle / step) * step;
        return anchor + QPointF(std::cos(snapped) * length,
                                std::sin(snapped) * length);
    }
    const qreal side = std::max(std::abs(delta.x()), std::abs(delta.y()));
    return anchor + QPointF((delta.x() < 0.0 ? -1.0 : 1.0) * side,
                            (delta.y() < 0.0 ? -1.0 : 1.0) * side);
}

int ImageCanvas::objectHitAt(const QPointF& image_point) const {
    const qreal tolerance = 9.0 / std::max(zoom_, 0.01);
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

QVector<ImageObjectPlacement> ImageCanvas::selectedObjects() const {
    QVector<ImageObjectPlacement> result;
    const auto& source = transforming_objects_ ? transform_current_objects_ : object_placements_;
    for (const auto& placement : source) {
        if (selected_object_ids_.contains(objectId(placement.operation))) {
            result.append(placement);
        }
    }
    return result;
}

QRectF ImageCanvas::objectBounds(const QVector<ImageObjectPlacement>& objects) const {
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

int ImageCanvas::resizeHandleAt(const QPointF& image_point) const {
    if (selected_object_ids_.isEmpty()) return -1;
    const auto selected = selectedObjects();
    if (selected.size() == 1 && selected.front().operation.kind == OperationKind::Text) {
        const QRectF bounds = imageTextBounds(selected.front().operation.text);
        const qreal tolerance = 9.0 / std::max(zoom_, 0.01);
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
    const qreal tolerance = 9.0 / std::max(zoom_, 0.01);
    const QPointF handles[] = {bounds.topLeft(), bounds.topRight(),
                               bounds.bottomLeft(), bounds.bottomRight()};
    for (int index = 0; index < 4; ++index) {
        if (QLineF(image_point, handles[index]).length() <= tolerance) return index;
    }
    return -1;
}

QVector<ImageObjectPlacement> ImageCanvas::selectionHits(const QRectF& bounds) const {
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

QVector<ImageObjectPlacement> ImageCanvas::transformObjects(
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
        }
        const int diameter = std::clamp(
            static_cast<int>(std::lround(operationDiameter(operation) * width_scale)),
            1, max_diameter);
        setOperationDiameter(&operation, diameter);
    }
    return transformed;
}

void ImageCanvas::beginObjectTransform(bool resize, int handle,
                                       const QPointF& image_point) {
    transform_initial_objects_ = selectedObjects();
    if (transform_initial_objects_.isEmpty()) return;
    transform_current_objects_ = transform_initial_objects_;
    transform_initial_bounds_ = objectBounds(transform_initial_objects_);
    transform_start_ = image_point;
    resizing_objects_ = resize;
    resizing_handle_ = handle;
    transforming_objects_ = true;
    moved_interaction_ = false;
    resizing_text_width_ = resize && transform_initial_objects_.size() == 1 &&
        transform_initial_objects_.front().operation.kind == OperationKind::Text;
    if (resize && resizing_text_width_) {
        const bool left_handle = handle == 4;
        transform_fixed_anchor_ = left_handle
            ? transform_initial_bounds_.topRight() : transform_initial_bounds_.topLeft();
    } else if (resize) {
        const QPointF anchors[] = {transform_initial_bounds_.bottomRight(),
                                   transform_initial_bounds_.bottomLeft(),
                                   transform_initial_bounds_.topRight(),
                                   transform_initial_bounds_.topLeft()};
        transform_fixed_anchor_ = anchors[handle];
    }
    emit objectTransformStarted(selected_object_ids_);
    update();
}

void ImageCanvas::updateObjectTransform(const QPointF& image_point, bool freeform) {
    if (!transforming_objects_ || transform_initial_objects_.isEmpty()) return;
    const QSize size = image_.size();
    if (!resizing_objects_) {
        QPointF delta = image_point - transform_start_;
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
        update();
        return;
    }

    if (resizing_text_width_) {
        QPointF dragged = image_point;
        dragged.setX(std::clamp(dragged.x(), 0.0, static_cast<qreal>(size.width() - 1)));
        const bool left_handle = resizing_handle_ == 4;
        auto& text = transform_current_objects_.front().operation.text;
        const qreal anchor_x = transform_fixed_anchor_.x();
        const qreal width = std::max<qreal>(1.0, std::abs(dragged.x() - anchor_x));
        text.position.setX(left_handle ? anchor_x - width : anchor_x);
        text.box_width = width;
        moved_interaction_ = true;
        updateTextEditorGeometry();
        update();
        return;
    }

    QPointF dragged = image_point;
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
    if (!freeform) {
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
    update();
}

void ImageCanvas::clearObjectInteraction() {
    selecting_objects_ = false;
    selection_toggle_ = false;
    transforming_objects_ = false;
    resizing_objects_ = false;
    resizing_text_width_ = false;
    moved_interaction_ = false;
    resizing_handle_ = -1;
    object_selection_rect_ = {};
    transform_initial_objects_.clear();
    transform_current_objects_.clear();
    transform_initial_bounds_ = {};
}

void ImageCanvas::drawShapeOverlay(QPainter& painter,
                                  const ImageShapeData& shape,
                                  int opacity) const {
    const QRectF target = imageTargetRect();
    const auto toWidget = [&target, this](const QPointF& point) {
        return QPointF(target.left() + point.x() * zoom_,
                       target.top() + point.y() * zoom_);
    };
    const QPointF start = toWidget(shape.start);
    const QPointF end = toWidget(shape.end);
    painter.save();
    painter.setClipRect(target);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setOpacity(std::clamp(opacity, 0, 100) / 100.0);
    if (shape.kind == ImageShapeKind::Line) {
        if (shape.stroke_enabled) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(shape.stroke_color, shape.stroke_width * zoom_,
                                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawLine(start, end);
        }
    } else {
        painter.setPen(shape.stroke_enabled
            ? QPen(shape.stroke_color, shape.stroke_width * zoom_, Qt::SolidLine,
                   Qt::SquareCap, Qt::MiterJoin)
            : QPen(Qt::NoPen));
        painter.setBrush(shape.fill_enabled ? QBrush(shape.fill_color)
                                           : QBrush(Qt::NoBrush));
        const QRectF bounds(start, end);
        if (shape.kind == ImageShapeKind::Rectangle) painter.drawRect(bounds.normalized());
        else painter.drawEllipse(bounds.normalized());
    }
    painter.restore();
}

void ImageCanvas::drawTextOverlay(QPainter& painter,
                                  const ImageTextData& text,
                                  int opacity) const {
    const QRectF target = imageTargetRect();
    painter.save();
    painter.setClipRect(target);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setOpacity(std::clamp(opacity, 0, 100) / 100.0);
    painter.translate(target.topLeft());
    painter.scale(zoom_, zoom_);
    QFont font(text.font_family);
    font.setPixelSize(text.font_pixel_size);
    painter.setFont(font);
    painter.setPen(text.color);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(text.alignment == ImageTextAlignment::Center
        ? Qt::AlignHCenter : (text.alignment == ImageTextAlignment::Right
            ? Qt::AlignRight : Qt::AlignLeft));
    QTextLayout layout(text.content, font);
    layout.setTextOption(option);
    layout.beginLayout();
    qreal height = 0.0;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(text.box_width);
        line.setPosition(QPointF(0.0, height));
        height += line.height();
    }
    layout.endLayout();
    layout.draw(&painter, text.position);
    painter.restore();
}

void ImageCanvas::drawObjectOverlay(QPainter& painter,
                                    const ImageObjectPlacement& object) const {
    const auto& operation = object.operation;
    if (operation.kind == OperationKind::Text) {
        drawTextOverlay(painter, operation.text, object.layer_opacity);
        return;
    }
    if (operation.kind == OperationKind::Shape) {
        drawShapeOverlay(painter, operation.shape, object.layer_opacity);
        return;
    }
    const QVector<QPointF>* points = nullptr;
    qreal diameter = 1.0;
    QColor color;
    if (operation.kind == OperationKind::PaintStroke) {
        points = &operation.paint_stroke.points;
        diameter = operation.paint_stroke.diameter;
        color = operation.paint_stroke.color;
    } else if (operation.kind == OperationKind::EraseStroke) {
        points = &operation.erase_stroke.points;
        diameter = operation.erase_stroke.diameter;
        color = QColor(240, 80, 125, 175);
    }
    if (points == nullptr || points->isEmpty()) return;

    const QRectF target = imageTargetRect();
    const auto toWidget = [&target, this](const QPointF& point) {
        return QPointF(target.left() + point.x() * zoom_,
                       target.top() + point.y() * zoom_);
    };
    painter.save();
    painter.setClipRect(target);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setOpacity(std::clamp(object.layer_opacity, 0, 100) / 100.0);
    QPen pen(color, diameter * zoom_, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    if (points->size() == 1) {
        const QPointF center = toWidget(points->front());
        const qreal radius = diameter * zoom_ / 2.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(center, radius, radius);
    } else {
        QPainterPath path;
        path.moveTo(toWidget(points->front()));
        for (qsizetype index = 1; index < points->size(); ++index) {
            path.lineTo(toWidget(points->at(index)));
        }
        painter.drawPath(path);
    }
    painter.restore();
}

void ImageCanvas::appendPaintPoint(const QPointF& point) {
    if (!paint_points_.isEmpty() && paint_points_.back() == point) return;
    if (paint_points_.size() >= kMaximumPaintPreviewPoints) {
        paint_points_.last() = point;
        return;
    }
    paint_points_.append(point);
}

void ImageCanvas::updateHoverCursor(const QPointF& position) {
    brush_cursor_visible_ = (paint_mode_ || eraser_mode_) &&
        imageTargetRect().contains(position);
    if (brush_cursor_visible_) brush_cursor_position_ = position;
    update();
}

void ImageCanvas::applyTextEditorStyle() {
    if (text_editor_ == nullptr) return;
    QFont font(text_editing_.font_family);
    font.setPixelSize(std::max(1, qRound(text_editing_.font_pixel_size * zoom_)));
    text_editor_->setFont(font);
    text_editor_->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { color: %1; background: rgba(255,255,255,150); "
        "border: 1px solid #299bea; padding: 0px; selection-background-color: #359bdc; }")
        .arg(text_editing_.color.name(QColor::HexArgb)));
    QTextOption option = text_editor_->document()->defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(text_editing_.alignment == ImageTextAlignment::Center
        ? Qt::AlignHCenter : (text_editing_.alignment == ImageTextAlignment::Right
            ? Qt::AlignRight : Qt::AlignLeft));
    text_editor_->document()->setDefaultTextOption(option);
}

void ImageCanvas::updateTextEditorGeometry() {
    if (text_editor_ == nullptr || !text_editor_->isVisible() || image_.isNull()) return;
    const QRectF target = imageTargetRect();
    const QRectF text_bounds = imageTextBounds(text_editing_);
    const qreal minimum_height = text_editing_.font_pixel_size * 1.5;
    const qreal height = text_editing_.content.isEmpty()
        ? minimum_height : std::max(minimum_height, text_bounds.height());
    const int width = std::max(24, qRound(text_editing_.box_width * zoom_));
    const int pixel_height = std::max(24, qRound(height * zoom_));
    const int left = qRound(target.left() + text_editing_.position.x() * zoom_);
    const int top = qRound(target.top() + text_editing_.position.y() * zoom_);
    const QRect editor_geometry(left, top, width, pixel_height);
    QFont font(text_editing_.font_family);
    font.setPixelSize(std::max(1, qRound(text_editing_.font_pixel_size * zoom_)));
    const bool keep_focus = text_editor_->hasFocus();
    const QTextCursor cursor = text_editor_->textCursor();
    if (text_editor_->geometry() != editor_geometry) {
        text_editor_->setGeometry(editor_geometry);
    }
    if (text_editor_->font() != font) text_editor_->setFont(font);
    if (keep_focus && !text_editor_->hasFocus()) {
        text_editor_->setFocus(Qt::OtherFocusReason);
        text_editor_->setTextCursor(cursor);
    }
}

void ImageCanvas::updateTextEditorContentAndGeometry() {
    if (text_editor_ == nullptr || !text_editor_->isVisible() || image_.isNull()) return;

    text_editing_.content = text_editor_->toPlainText();

    QFont font(text_editing_.font_family);
    font.setPixelSize(std::clamp(text_editing_.font_pixel_size, 1, 1024));
    const QFontMetricsF metrics(font);
    qreal content_width = 0.0;
    const QStringList lines = text_editing_.content.split(QLatin1Char('\n'),
                                                          Qt::KeepEmptyParts);
    for (const QString& line : lines) {
        content_width = std::max(content_width, metrics.horizontalAdvance(line));
    }

    const qreal available_width = std::max<qreal>(1.0,
        image_.width() - text_editing_.position.x());
    const qreal minimum_width = std::min(text_editing_initial_box_width_, available_width);
    constexpr qreal kTextEditorHorizontalInset = 4.0;
    const qreal desired_width = std::max(minimum_width,
        content_width > 0.0 ? content_width + kTextEditorHorizontalInset : minimum_width);
    text_editing_.box_width = std::clamp(desired_width, minimum_width, available_width);

    updateTextEditorGeometry();
    update();
}

void ImageCanvas::finishTextEditing(bool commit) {
    if (text_editor_ == nullptr || !text_editor_->isVisible()) return;
    text_editing_.content = text_editor_->toPlainText();
    const ImageTextData text = text_editing_;
    const bool existing = text_editing_existing_;
    text_editor_->hide();
    text_editor_->clear();
    text_editing_ = {};
    text_editing_existing_ = false;
    text_editing_initial_box_width_ = 1.0;
    if (commit) emit textCommitted(text, existing);
    else emit textEditingCancelled();
    update();
}

void ImageCanvas::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(34, 37, 43));
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (image_.isNull()) {
        painter.setPen(QColor(190, 195, 204));
        painter.drawText(rect(), Qt::AlignCenter,
                         QStringLiteral("Open an image to start editing"));
        return;
    }

    const QRectF target = imageTargetRect();
    updateTextEditorGeometry();
    painter.fillRect(target.adjusted(-2, -2, 2, 2), QColor(18, 19, 22));
    constexpr qreal checker_size = 16.0;
    painter.save();
    painter.setClipRect(target);
    const int first_column = static_cast<int>(std::floor(target.left() / checker_size));
    const int last_column = static_cast<int>(std::ceil(target.right() / checker_size));
    const int first_row = static_cast<int>(std::floor(target.top() / checker_size));
    const int last_row = static_cast<int>(std::ceil(target.bottom() / checker_size));
    for (int row = first_row; row < last_row; ++row) {
        for (int column = first_column; column < last_column; ++column) {
            const QColor color = QColor::fromRgba(((row + column) % 2 == 0)
                ? ui::kTransparencyCheckerLight : ui::kTransparencyCheckerDark);
            painter.fillRect(QRectF(column * checker_size, row * checker_size,
                                    checker_size, checker_size), color);
        }
    }
    painter.restore();
    painter.drawImage(target, transient_image_.isNull() ? image_ : transient_image_);

    if (crop_mode_ && selecting_crop_) {
        const QRectF selection = crop_selection_.normalized().intersected(target);
        painter.fillRect(selection, QColor(38, 150, 220, 36));
        QPen pen(QColor(120, 205, 255), 1.5, Qt::DashLine);
        painter.setPen(pen);
        painter.drawRect(selection);
    }

    if (paint_mode_ || eraser_mode_) {
        if ((painting_ || (erasing_ && eraser_preview_enabled_)) && !paint_points_.isEmpty()) {
            QPainterPath path;
            const auto toWidget = [&target, this](const QPointF& point) {
                return QPointF(target.left() + point.x() * zoom_,
                               target.top() + point.y() * zoom_);
            };
            path.moveTo(toWidget(paint_points_.front()));
            for (qsizetype i = 1; i < paint_points_.size(); ++i) {
                path.lineTo(toWidget(paint_points_.at(i)));
            }
            painter.save();
            painter.setClipRect(target);
            painter.setRenderHint(QPainter::Antialiasing, true);
            const QColor stroke_color = eraser_mode_
                ? QColor(240, 80, 125, 115) : brush_color_;
            QPen pen(stroke_color, brush_diameter_ * zoom_, Qt::SolidLine,
                     Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(pen);
            if (paint_points_.size() == 1) {
                const QPointF center = toWidget(paint_points_.front());
                const qreal radius = brush_diameter_ * zoom_ / 2.0;
                painter.setPen(Qt::NoPen);
                painter.setBrush(stroke_color);
                painter.drawEllipse(center, radius, radius);
            } else {
                painter.drawPath(path);
            }
            painter.restore();
        }
        if (brush_cursor_visible_ && target.contains(brush_cursor_position_)) {
            const qreal diameter = std::max(3.0, brush_diameter_ * zoom_);
            const QRectF cursor(brush_cursor_position_.x() - diameter / 2.0,
                                brush_cursor_position_.y() - diameter / 2.0,
                                diameter, diameter);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(18, 19, 22), 3.0));
            painter.drawEllipse(cursor);
            painter.setPen(QPen(QColor(242, 244, 248), 1.0));
            painter.drawEllipse(cursor);
        }
    }

    if (creating_shape_) {
        drawShapeOverlay(painter, shape_interaction_current_);
    }
    if (creating_text_frame_) {
        const QRectF frame(imageTargetRect().left() +
                               std::min(text_frame_start_.x(), text_frame_current_.x()) * zoom_,
                           imageTargetRect().top() + text_frame_start_.y() * zoom_,
                           std::abs(text_frame_current_.x() - text_frame_start_.x()) * zoom_,
                           1.0);
        painter.save();
        painter.setClipRect(target);
        painter.setPen(QPen(QColor(64, 181, 246), 1.0, Qt::DashLine));
        painter.setBrush(QColor(64, 181, 246, 24));
        painter.drawRect(frame);
        painter.restore();
    }
    if (transforming_objects_) {
        for (const auto& object : transform_current_objects_) drawObjectOverlay(painter, object);
    }
    if (object_selection_mode_ && !selected_object_ids_.isEmpty()) {
        const QRectF bounds = objectBounds(selectedObjects());
        if (!bounds.isEmpty()) {
            const QRectF widget_bounds(
                target.left() + bounds.left() * zoom_,
                target.top() + bounds.top() * zoom_,
                bounds.width() * zoom_, bounds.height() * zoom_);
            painter.save();
            painter.setClipRect(target);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(QPen(QColor(45, 155, 235), 1.0, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(widget_bounds);
            QVector<QPointF> handles;
            const auto selected = selectedObjects();
            if (selected.size() == 1 && selected.front().operation.kind == OperationKind::Text) {
                handles = {QPointF(widget_bounds.left(), widget_bounds.center().y()),
                           QPointF(widget_bounds.right(), widget_bounds.center().y())};
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
    if (object_selection_mode_ && selecting_objects_) {
        const QRectF selection = object_selection_rect_.normalized().intersected(target);
        painter.fillRect(selection, QColor(38, 150, 220, 36));
        painter.setPen(QPen(QColor(120, 205, 255), 1.2, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(selection);
    }
}

void ImageCanvas::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (fit_to_window_) fitToWindow();
    updateTextEditorGeometry();
}

void ImageCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::LeftButton && text_editor_ != nullptr &&
        text_editor_->isVisible() && !text_editor_->geometry().contains(event->position().toPoint())) {
        finishTextEditing(true);
    }
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        pan_start_ = event->position();
        initial_pan_ = pan_;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    const auto modifiers = event->modifiers();
    if ((paint_mode_ || eraser_mode_) && !crop_mode_ && event->button() == Qt::LeftButton &&
        modifiers.testFlag(Qt::ControlModifier) && modifiers.testFlag(Qt::AltModifier) &&
        imageTargetRect().contains(event->position())) {
        resizing_brush_ = true;
        brush_resize_start_ = event->position();
        brush_resize_global_start_ = event->globalPosition().toPoint();
        brush_resize_initial_diameter_ = brush_diameter_;
        brush_cursor_position_ = event->position();
        brush_cursor_visible_ = true;
        update();
        event->accept();
        return;
    }
    if (crop_mode_ && event->button() == Qt::LeftButton &&
        imageTargetRect().contains(event->position())) {
        selecting_crop_ = true;
        crop_start_ = event->position();
        crop_selection_ = QRectF(crop_start_, crop_start_);
        update();
        event->accept();
        return;
    }
    if (shape_creation_mode_ && event->button() == Qt::LeftButton &&
        imageTargetRect().contains(event->position())) {
        creating_shape_ = true;
        shape_interaction_current_ = shape_style_;
        shape_interaction_current_.id.clear();
        shape_interaction_current_.start = widgetToImageCoordinates(event->position());
        shape_interaction_current_.end = shape_interaction_current_.start;
        update();
        event->accept();
        return;
    }
    if (text_creation_mode_ && event->button() == Qt::LeftButton &&
        imageTargetRect().contains(event->position())) {
        const QPointF point = widgetToImageCoordinates(event->position());
        const int hit = objectHitAt(point);
        if (hit >= 0 && object_placements_.at(hit).operation.kind == OperationKind::Text) {
            const auto& placement = object_placements_.at(hit);
            selected_object_ids_ = {placement.operation.text.id};
            emit objectsSelected(selected_object_ids_, placement.layer_id);
            event->accept();
            return;
        }
        creating_text_frame_ = true;
        text_frame_start_ = point;
        text_frame_current_ = point;
        update();
        event->accept();
        return;
    }
    if (object_selection_mode_ && event->button() == Qt::LeftButton &&
        imageTargetRect().contains(event->position())) {
        const QPointF point = widgetToImageCoordinates(event->position());
        const int handle = resizeHandleAt(point);
        if (handle >= 0) {
            beginObjectTransform(true, handle, point);
            event->accept();
            return;
        }
        const int hit = objectHitAt(point);
        if (hit < 0) {
            selecting_objects_ = true;
            selection_toggle_ = modifiers.testFlag(Qt::ShiftModifier);
            moved_interaction_ = false;
            selection_start_ = event->position();
            object_selection_rect_ = QRectF(selection_start_, selection_start_);
            update();
            event->accept();
            return;
        }
        const ImageObjectPlacement placement = object_placements_.at(hit);
        const QString id = objectId(placement.operation);
        if (modifiers.testFlag(Qt::ShiftModifier)) {
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
            emit objectsSelected(selected_object_ids_, active_layer);
            update();
            event->accept();
            return;
        }
        if (!selected_object_ids_.contains(id)) selected_object_ids_ = {id};
        emit objectsSelected(selected_object_ids_, placement.layer_id);
        beginObjectTransform(false, -1, point);
        update();
        event->accept();
        return;
    }
    if (paint_mode_ && event->button() == Qt::LeftButton &&
        imageTargetRect().contains(event->position())) {
        painting_ = true;
        paint_points_.clear();
        paint_points_.append(widgetToImageCoordinates(event->position()));
        brush_cursor_position_ = event->position();
        brush_cursor_visible_ = true;
        update();
        event->accept();
        return;
    }
    if (eraser_mode_ && event->button() == Qt::LeftButton &&
        imageTargetRect().contains(event->position())) {
        erasing_ = true;
        paint_points_.clear();
        paint_points_.append(widgetToImageCoordinates(event->position()));
        brush_cursor_position_ = event->position();
        brush_cursor_visible_ = true;
        if (!eraser_preview_enabled_) emit erasePreviewRequested(paint_points_, brush_diameter_);
        update();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ImageCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        pan_ = initial_pan_ + event->position() - pan_start_;
        fit_to_window_ = false;
        update();
        event->accept();
        return;
    }
    if (resizing_brush_) {
        const QPointF displacement = event->position() - brush_resize_start_;
        const int adjustment = static_cast<int>(std::round(displacement.x()));
        const int diameter = std::clamp(
            brush_resize_initial_diameter_ + adjustment,
            1, ImageDocumentStore::kMaximumPaintBrushDiameter);
        if (diameter != brush_diameter_) {
            brush_diameter_ = diameter;
            emit brushDiameterChanged(brush_diameter_);
        }
        // Keep the brush preview anchored at the gesture's press point. The
        // cursor may leave the image while its horizontal displacement still
        // controls the diameter.
        update();
        event->accept();
        return;
    }
    if (selecting_crop_) {
        crop_selection_ = QRectF(crop_start_, event->position()).normalized();
        update();
        event->accept();
        return;
    }
    if (creating_text_frame_) {
        text_frame_current_ = widgetToImageCoordinates(event->position());
        update();
        event->accept();
        return;
    }
    if (creating_shape_) {
        const QPointF point = widgetToImageCoordinates(event->position());
        shape_interaction_current_.end = constrainShapePoint(
            point, shape_interaction_current_.start, shape_interaction_current_.kind,
            shift_constrain_held_ || event->modifiers().testFlag(Qt::ShiftModifier));
        update();
        event->accept();
        return;
    }
    if (selecting_objects_) {
        object_selection_rect_ = QRectF(selection_start_, event->position()).normalized();
        moved_interaction_ = moved_interaction_ ||
            QLineF(selection_start_, event->position()).length() >= 4.0;
        update();
        event->accept();
        return;
    }
    if (transforming_objects_) {
        const QPointF point = widgetToImageCoordinates(event->position());
        updateObjectTransform(point, event->modifiers().testFlag(Qt::AltModifier));
        event->accept();
        return;
    }
    if (painting_ || erasing_) {
        const QPointF point = widgetToImageCoordinates(event->position());
        appendPaintPoint(point);
        brush_cursor_position_ = event->position();
        brush_cursor_visible_ = imageTargetRect().contains(event->position());
        if (erasing_ && !eraser_preview_enabled_) {
            emit erasePreviewRequested(paint_points_, brush_diameter_);
        }
        update();
        event->accept();
        return;
    }
    if (paint_mode_ || eraser_mode_) {
        updateHoverCursor(event->position());
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton && panning_) {
        panning_ = false;
        setCursor(crop_mode_ ? Qt::CrossCursor
                             : ((paint_mode_ || eraser_mode_)
                                    ? Qt::BlankCursor : Qt::ArrowCursor));
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && resizing_brush_) {
        resizing_brush_ = false;
        brush_cursor_position_ = brush_resize_start_;
        brush_cursor_visible_ = imageTargetRect().contains(brush_resize_start_);
        QCursor::setPos(brush_resize_global_start_);
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && selecting_crop_) {
        selecting_crop_ = false;
        const QRect selection = cropToImageCoordinates(crop_selection_);
        crop_selection_ = {};
        if (selection.width() > 1 && selection.height() > 1) emit cropSelected(selection);
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && creating_text_frame_) {
        text_frame_current_ = widgetToImageCoordinates(event->position());
        const QPointF start = text_frame_start_;
        const QPointF end = text_frame_current_;
        creating_text_frame_ = false;
        update();
        const qreal width = std::abs(end.x() - start.x());
        if (width >= 4.0) {
            ImageTextData text = text_style_;
            text.id.clear();
            text.content.clear();
            text.position = QPointF(std::min(start.x(), end.x()), start.y());
            text.box_width = std::max<qreal>(1.0, width);
            beginTextEditing(text, false);
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && selecting_objects_) {
        selecting_objects_ = false;
        const bool was_drag = moved_interaction_;
        const QRectF widget_bounds = object_selection_rect_.normalized();
        object_selection_rect_ = {};
        if (was_drag) {
            const QRectF image_bounds(widgetToImageCoordinates(widget_bounds.topLeft()),
                                      widgetToImageCoordinates(widget_bounds.bottomRight()));
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
            emit objectsSelected(selected_object_ids_, active_layer);
        } else if (!selection_toggle_) {
            selected_object_ids_.clear();
            emit objectsSelected({}, {});
        }
        moved_interaction_ = false;
        selection_toggle_ = false;
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && creating_shape_) {
        const QPointF point = widgetToImageCoordinates(event->position());
        shape_interaction_current_.end = constrainShapePoint(
            point, shape_interaction_current_.start, shape_interaction_current_.kind,
            shift_constrain_held_ || event->modifiers().testFlag(Qt::ShiftModifier));
        const ImageShapeData shape = shape_interaction_current_;
        creating_shape_ = false;
        shape_interaction_current_ = {};
        update();
        if (shape.start != shape.end &&
            (shape.kind == ImageShapeKind::Line ||
             (shape.start.x() != shape.end.x() && shape.start.y() != shape.end.y()))) {
            emit shapeCreated(shape);
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && transforming_objects_) {
        const auto changed_objects = transform_current_objects_;
        const bool changed = changed_objects != transform_initial_objects_;
        clearObjectInteraction();
        transient_image_ = {};
        update();
        if (changed) emit objectsGeometryChanged(changed_objects);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && erasing_) {
        appendPaintPoint(widgetToImageCoordinates(event->position()));
        const QVector<QPointF> points = std::move(paint_points_);
        erasing_ = false;
        transient_image_ = {};
        emit erasePreviewCleared();
        brush_cursor_position_ = event->position();
        brush_cursor_visible_ = imageTargetRect().contains(event->position());
        update();
        if (!points.isEmpty()) emit eraseStrokeSelected(points, brush_diameter_);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && painting_) {
        const QPointF point = widgetToImageCoordinates(event->position());
        appendPaintPoint(point);
        const QVector<QPointF> points = std::move(paint_points_);
        painting_ = false;
        brush_cursor_position_ = event->position();
        brush_cursor_visible_ = imageTargetRect().contains(event->position());
        update();
        if (!points.isEmpty()) {
            emit paintStrokeSelected(points, brush_color_, brush_diameter_);
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ImageCanvas::wheelEvent(QWheelEvent* event) {
    if (image_.isNull()) return;
    const double old_zoom = zoom_;
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    const double new_zoom = std::clamp(old_zoom * factor, 0.01, 16.0);
    const QPointF cursor = event->position();
    const QPointF relative = (cursor - imageTargetRect().topLeft()) / old_zoom;
    zoom_ = new_zoom;
    const QSizeF scaled(image_.width() * zoom_, image_.height() * zoom_);
    const QPointF centered((width() - scaled.width()) / 2.0,
                           (height() - scaled.height()) / 2.0);
    pan_ = cursor - relative * zoom_ - centered;
    fit_to_window_ = false;
    update();
    event->accept();
}

void ImageCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !imageTargetRect().contains(event->position())) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    const int hit = objectHitAt(widgetToImageCoordinates(event->position()));
    if (hit < 0 || object_placements_.at(hit).operation.kind != OperationKind::Text) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    const auto placement = object_placements_.at(hit);
    selected_object_ids_ = {placement.operation.text.id};
    emit objectsSelected(selected_object_ids_, placement.layer_id);
    beginTextEditing(placement.operation.text, true);
    event->accept();
}

bool ImageCanvas::eventFilter(QObject* watched, QEvent* event) {
    if (watched == text_editor_ && event->type() == QEvent::ShortcutOverride) {
        auto* key_event = static_cast<QKeyEvent*>(event);
        const QString input = key_event->text();
        const auto modifiers = key_event->modifiers();
        const bool printable = !input.isEmpty() && input.front().isPrint();
        const bool altgr = modifiers.testFlag(Qt::GroupSwitchModifier);
        const bool command_modifier =
            modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::MetaModifier);
        const bool menu_modifier = modifiers.testFlag(Qt::AltModifier) && !altgr;
        if (printable && (!command_modifier || altgr) && !menu_modifier) {
            key_event->accept();
            return true;
        }
    }
    if (watched == text_editor_ && event->type() == QEvent::KeyPress) {
        auto* key_event = static_cast<QKeyEvent*>(event);
        if (key_event->key() == Qt::Key_Escape) {
            finishTextEditing(false);
            return true;
        }
        if ((key_event->key() == Qt::Key_Return || key_event->key() == Qt::Key_Enter) &&
            key_event->modifiers().testFlag(Qt::ControlModifier)) {
            finishTextEditing(true);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ImageCanvas::leaveEvent(QEvent* event) {
    if (!painting_ && !erasing_ && !resizing_brush_) {
        brush_cursor_visible_ = false;
        update();
    }
    QWidget::leaveEvent(event);
}

void ImageCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Shift) {
        shift_constrain_held_ = true;
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && creating_shape_) {
        creating_shape_ = false;
        shape_interaction_current_ = {};
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape &&
        (selecting_objects_ || transforming_objects_)) {
        clearObjectInteraction();
        transient_image_ = {};
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && erasing_) {
        erasing_ = false;
        paint_points_.clear();
        transient_image_ = {};
        emit erasePreviewCleared();
        brush_cursor_visible_ = false;
        update();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ImageCanvas::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Shift) {
        shift_constrain_held_ = false;
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

} // namespace image_editor
