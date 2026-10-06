#include "image_canvas.h"

#include "image_document_store.h"
#include "../transparency_checkerboard.h"
#include "../../core/diagnostics/image_editor_performance_metrics.h"

#include <QEvent>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPen>
#include <QWheelEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

namespace image_editor {
namespace {

} // namespace

ImageCanvas::ImageCanvas(QWidget* parent) : QWidget(parent), text_tool_(this) {
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(240, 180);
    setMouseTracking(true);
    setAutoFillBackground(false);
    connect(&text_tool_, &TextTool::textCommitted,
            this, &ImageCanvas::textCommitted);
    connect(&text_tool_, &TextTool::textEditingStarted,
            this, &ImageCanvas::textEditingStarted);
    connect(&text_tool_, &TextTool::textEditingCancelled,
            this, &ImageCanvas::textEditingCancelled);
    connect(&text_tool_, &TextTool::repaintRequested, this,
            qOverload<>(&ImageCanvas::update));
}

void ImageCanvas::setImage(QImage image, bool resetView) {
    resetBrushTools(true);
    image_ = std::move(image);
    transient_image_ = {};
    static_cast<void>(shape_tool_.cancelGesture());
    clearObjectInteraction();
    resizing_brush_ = false;
    if (resetView) {
        pan_ = {};
        fit_to_window_ = true;
        static_cast<void>(crop_tool_.cancelGesture());
        clearAreaSelection();
        fitToWindow();
    }
    update();
}

void ImageCanvas::setCropMode(bool enabled) {
    resetBrushTools(true);
    crop_mode_ = enabled;
    if (enabled) {
        area_selection_mode_ = false;
        paint_mode_ = false;
        eraser_mode_ = false;
        shape_creation_mode_ = false;
        object_selection_mode_ = false;
    }
    static_cast<void>(crop_tool_.cancelGesture());
    transient_image_ = {};
    resizing_brush_ = false;
    setCursor(enabled ? Qt::CrossCursor
                      : ((paint_mode_ || eraser_mode_) ? Qt::BlankCursor : Qt::ArrowCursor));
    update();
}

void ImageCanvas::setPaintMode(bool enabled) {
    resetBrushTools(true);
    paint_mode_ = enabled;
    if (enabled) {
        area_selection_mode_ = false;
        crop_mode_ = false;
        eraser_mode_ = false;
        shape_creation_mode_ = false;
        object_selection_mode_ = false;
    }
    transient_image_ = {};
    resizing_brush_ = false;
    static_cast<void>(crop_tool_.cancelGesture());
    setCursor(enabled ? Qt::BlankCursor
                      : (crop_mode_ ? Qt::CrossCursor
                                    : (eraser_mode_ ? Qt::BlankCursor : Qt::ArrowCursor)));
    update();
}

void ImageCanvas::setEraserMode(bool enabled) {
    resetBrushTools(true);
    eraser_mode_ = enabled;
    if (enabled) {
        area_selection_mode_ = false;
        crop_mode_ = false;
        paint_mode_ = false;
        shape_creation_mode_ = false;
        object_selection_mode_ = false;
    }
    resizing_brush_ = false;
    transient_image_ = {};
    static_cast<void>(crop_tool_.cancelGesture());
    setCursor(enabled ? Qt::BlankCursor
                      : (crop_mode_ ? Qt::CrossCursor
                                     : (paint_mode_ ? Qt::BlankCursor : Qt::ArrowCursor)));
    update();
}

void ImageCanvas::setShapeCreationMode(bool enabled) {
    shape_creation_mode_ = enabled;
    if (enabled) {
        area_selection_mode_ = false;
        crop_mode_ = false;
        paint_mode_ = false;
        eraser_mode_ = false;
        object_selection_mode_ = false;
        text_creation_mode_ = false;
    }
    static_cast<void>(crop_tool_.cancelGesture());
    static_cast<void>(shape_tool_.cancelGesture());
    clearObjectInteraction();
    transient_image_ = {};
    resetBrushTools(false);
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void ImageCanvas::setTextCreationMode(bool enabled) {
    if (!enabled && text_tool_.editing()) {
        finishTextEditing(true);
    }
    text_creation_mode_ = enabled;
    if (enabled) {
        area_selection_mode_ = false;
        crop_mode_ = false;
        paint_mode_ = false;
        eraser_mode_ = false;
        shape_creation_mode_ = false;
        object_selection_mode_ = false;
    }
    static_cast<void>(shape_tool_.cancelGesture());
    static_cast<void>(text_tool_.cancelFrame());
    clearObjectInteraction();
    transient_image_ = {};
    static_cast<void>(crop_tool_.cancelGesture());
    resetBrushTools(false);
    setCursor(enabled ? Qt::IBeamCursor : Qt::ArrowCursor);
    update();
}

void ImageCanvas::setObjectSelectionMode(bool enabled) {
    object_selection_mode_ = enabled;
    if (enabled) {
        area_selection_mode_ = false;
        crop_mode_ = false;
        paint_mode_ = false;
        eraser_mode_ = false;
        shape_creation_mode_ = false;
        text_creation_mode_ = false;
    }
    static_cast<void>(crop_tool_.cancelGesture());
    static_cast<void>(shape_tool_.cancelGesture());
    clearObjectInteraction();
    transient_image_ = {};
    resetBrushTools(false);
    setCursor(Qt::ArrowCursor);
    update();
}

void ImageCanvas::setAreaSelectionMode(bool enabled) {
    area_selection_mode_ = enabled;
    if (enabled) {
        crop_mode_ = false;
        paint_mode_ = false;
        eraser_mode_ = false;
        shape_creation_mode_ = false;
        text_creation_mode_ = false;
        object_selection_mode_ = false;
    }
    static_cast<void>(area_selection_tool_.cancelGesture());
    static_cast<void>(crop_tool_.cancelGesture());
    static_cast<void>(shape_tool_.cancelGesture());
    static_cast<void>(text_tool_.cancelFrame());
    clearObjectInteraction();
    transient_image_ = {};
    resetBrushTools(false);
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void ImageCanvas::setEyedropperMode(bool enabled) {
    if (eyedropper_mode_ == enabled) return;
    if (enabled && text_tool_.editing()) finishTextEditing(true);
    resetBrushTools(true);
    eyedropper_mode_ = enabled;
    if (enabled) {
        crop_mode_ = false;
        paint_mode_ = false;
        eraser_mode_ = false;
        shape_creation_mode_ = false;
        text_creation_mode_ = false;
        object_selection_mode_ = false;
        area_selection_mode_ = false;
        static_cast<void>(crop_tool_.cancelGesture());
        static_cast<void>(area_selection_tool_.cancelGesture());
        static_cast<void>(shape_tool_.cancelGesture());
        static_cast<void>(text_tool_.cancelFrame());
        clearObjectInteraction();
        transient_image_ = {};
        resizing_brush_ = false;
    }
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void ImageCanvas::setBucketFillMode(bool enabled) {
    if (bucket_fill_mode_ == enabled) return;
    if (enabled && text_tool_.editing()) finishTextEditing(true);
    if (enabled) {
        resetBrushTools(true);
        crop_mode_ = paint_mode_ = eraser_mode_ = shape_creation_mode_ = false;
        text_creation_mode_ = object_selection_mode_ = area_selection_mode_ = false;
        eyedropper_mode_ = false;
        resizing_brush_ = false;
        static_cast<void>(crop_tool_.cancelGesture());
        static_cast<void>(area_selection_tool_.cancelGesture());
        static_cast<void>(shape_tool_.cancelGesture());
        static_cast<void>(text_tool_.cancelFrame());
        clearObjectInteraction();
        transient_image_ = {};
    }
    bucket_fill_mode_ = enabled;
    setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}

void ImageCanvas::setBucketFillTolerance(int tolerance) {
    bucket_fill_tolerance_ = std::clamp(tolerance, 0, 255);
}

void ImageCanvas::setAreaSelectionOptions(AreaSelectionShape shape,
                                          AreaSelectionCombineMode combine_mode) {
    const auto tool_shape = shape == AreaSelectionShape::Ellipse
        ? AreaSelectionTool::Shape::Ellipse : AreaSelectionTool::Shape::Rectangle;
    AreaSelectionTool::CombineMode tool_combine_mode =
        AreaSelectionTool::CombineMode::Replace;
    switch (combine_mode) {
    case AreaSelectionCombineMode::Add:
        tool_combine_mode = AreaSelectionTool::CombineMode::Add;
        break;
    case AreaSelectionCombineMode::Subtract:
        tool_combine_mode = AreaSelectionTool::CombineMode::Subtract;
        break;
    case AreaSelectionCombineMode::Replace:
        break;
    }
    area_selection_tool_.setOptions(tool_shape, tool_combine_mode);
}

void ImageCanvas::clearAreaSelection() {
    const bool changed = area_selection_tool_.clearSelection();
    if (changed) emit areaSelectionChanged(false);
    update();
}

void ImageCanvas::translateAreaSelection(const QPoint& delta) {
    if (area_selection_tool_.translateSelection(delta)) update();
}

void ImageCanvas::cancelAreaSelectionGesture() {
    if (area_selection_tool_.cancelGesture()) update();
}

std::optional<QPainterPath> ImageCanvas::areaSelectionClipPath() const {
    return area_selection_tool_.clipPath(
        QRectF(QPointF(0.0, 0.0), QSizeF(image_.size())));
}

bool ImageCanvas::hasAreaSelection() const noexcept {
    return area_selection_tool_.hasSelection();
}

bool ImageCanvas::areaSelectionGestureActive() const noexcept {
    return area_selection_tool_.gestureActive();
}

void ImageCanvas::setShapeStyle(const ImageShapeData& style) {
    shape_tool_.setStyle(style);
    update();
}

void ImageCanvas::setTextStyle(const ImageTextData& style) {
    text_tool_.setStyle(style);
    update();
}

void ImageCanvas::beginTextEditing(const ImageTextData& text, bool existing) {
    text_tool_.beginEditing(text, existing, textToolContext());
}

void ImageCanvas::commitTextEditing() {
    finishTextEditing(true);
}

bool ImageCanvas::textEditing() const noexcept {
    return text_tool_.editing();
}

void ImageCanvas::setObjectPlacements(QVector<ImageObjectPlacement> placements,
                                      QStringList selected_object_ids) {
    object_selection_tool_.setObjects(std::move(placements),
                                      std::move(selected_object_ids));
    update();
}

void ImageCanvas::setEraserPreviewEnabled(bool enabled) {
    dispatchBrushToolEvents(
        eraser_tool_.setPreviewEnabled(enabled, brushToolContext({})));
    update();
}

void ImageCanvas::setMaskEditing(bool enabled) {
    if (mask_editing_ == enabled) return;
    mask_editing_ = enabled;
    resetBrushTools(false);
    transient_image_ = {};
    emit erasePreviewCleared();
    if (enabled) {
        dispatchBrushToolEvents(
            eraser_tool_.setPreviewEnabled(false, brushToolContext({})));
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
    updateTextEditorGeometry();
    update();
}

QRectF ImageCanvas::imageTargetRect() const {
    if (image_.isNull()) return {};
    const QSizeF scaled(image_.width() * zoom_, image_.height() * zoom_);
    const QPointF origin((width() - scaled.width()) / 2.0 + pan_.x(),
                         (height() - scaled.height()) / 2.0 + pan_.y());
    return {origin, scaled};
}

TextToolContext ImageCanvas::textToolContext() const {
    return {image_.size(), imageTargetRect(), zoom_};
}

CropToolContext ImageCanvas::cropToolContext() const {
    return {image_.size(), imageTargetRect(), zoom_};
}

QPointF ImageCanvas::widgetToCropImageCoordinates(const QPointF& position) const {
    const QRectF target = imageTargetRect();
    if (target.isEmpty() || zoom_ <= 0.0) return {};
    const qreal x = (position.x() - target.left()) / zoom_;
    const qreal y = (position.y() - target.top()) / zoom_;
    // Crop coordinates describe image edges, so the far edge is width/height
    // rather than the last pixel center used by widgetToImageCoordinates().
    return {std::clamp(x, 0.0, static_cast<qreal>(image_.width())),
            std::clamp(y, 0.0, static_cast<qreal>(image_.height()))};
}

QPointF ImageCanvas::widgetToImageCoordinates(const QPointF& position) const {
    const QRectF target = imageTargetRect();
    if (target.isEmpty() || zoom_ <= 0.0) return {};
    const qreal x = (position.x() - target.left()) / zoom_;
    const qreal y = (position.y() - target.top()) / zoom_;
    return {std::clamp(x, 0.0, static_cast<qreal>(image_.width() - 1)),
            std::clamp(y, 0.0, static_cast<qreal>(image_.height() - 1))};
}

QPointF ImageCanvas::unboundedImagePoint(const QPointF& position) const {
    return (position - imageTargetRect().topLeft()) / std::max(zoom_, 0.01);
}
namespace {
QStringList dropPaths(const QMimeData* mime) {
    QStringList paths;
    const QStringList extensions{"png", "jpg", "jpeg", "bmp", "webp", "tif", "tiff"};
    if (!mime->hasUrls()) return paths;
    for (const auto& url : mime->urls()) {
        if (!url.isLocalFile() || !extensions.contains(QFileInfo(url.toLocalFile()).suffix().toLower())) return {};
        paths.append(url.toLocalFile());
    }
    return paths;
}
}
void ImageCanvas::dragEnterEvent(QDragEnterEvent* event) {
    if (!image_.isNull() && !dropPaths(event->mimeData()).isEmpty()) event->acceptProposedAction();
}
void ImageCanvas::dropEvent(QDropEvent* event) {
    const auto paths = dropPaths(event->mimeData());
    if (image_.isNull() || paths.isEmpty() || !imageTargetRect().contains(event->position())) return;
    event->acceptProposedAction();
    emit imagesDropped(paths, unboundedImagePoint(event->position()));
}

void ImageCanvas::clearObjectInteraction() {
    object_selection_tool_.clearGesture();
}

void ImageCanvas::drawObjectOverlay(QPainter& painter,
                                    const ImageObjectPlacement& object) const {
    const auto& operation = object.operation;
    if (operation.kind == OperationKind::Text) {
        TextTool::paintText(painter, operation.text,
                            textToolContext(), object.layer_opacity);
        return;
    }
    if (operation.kind == OperationKind::Shape) {
        ShapeTool::paintShape(painter, operation.shape,
                              {imageTargetRect(), zoom_}, object.layer_opacity);
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

void ImageCanvas::updateHoverCursor(const QPointF& position) {
    updateBrushToolCursor(position);
    update();
}

BrushToolContext ImageCanvas::brushToolContext(const QPointF& position) const {
    BrushToolContext context;
    context.widget_position = position;
    context.image_position = widgetToImageCoordinates(position);
    context.image_target = imageTargetRect();
    context.zoom = zoom_;
    context.color = brush_color_;
    context.diameter = brush_diameter_;
    context.mask_editing = mask_editing_;
    context.area_selection = areaSelectionClipPath();
    return context;
}

ObjectSelectionToolContext ImageCanvas::objectSelectionToolContext(
    const QPointF& position, Qt::KeyboardModifiers modifiers) const {
    ObjectSelectionToolContext context;
    context.widget_position = position;
    context.image_position = widgetToImageCoordinates(position);
    context.unbounded_image_position = unboundedImagePoint(position);
    context.image_bounds = QRectF(QPointF(0.0, 0.0), QSizeF(image_.size()));
    context.image_target = imageTargetRect();
    context.zoom = zoom_;
    context.pointer_on_image = context.image_target.contains(position);
    context.shift = modifiers.testFlag(Qt::ShiftModifier);
    context.alt = modifiers.testFlag(Qt::AltModifier);
    return context;
}

void ImageCanvas::dispatchObjectSelectionToolEvents(
    const QVector<ObjectSelectionToolEvent>& events) {
    for (const auto& event : events) {
        switch (event.type) {
        case ObjectSelectionToolEventType::ObjectsSelected:
            emit objectsSelected(event.object_ids, event.active_layer_id);
            break;
        case ObjectSelectionToolEventType::TransformStarted:
            emit objectTransformStarted(event.object_ids);
            break;
        case ObjectSelectionToolEventType::PreviewRequested:
            emit objectsPreviewRequested(event.objects);
            break;
        case ObjectSelectionToolEventType::GeometryChanged:
            emit objectsGeometryChanged(event.objects);
            break;
        }
    }
}

void ImageCanvas::dispatchBrushToolEvents(const QVector<BrushToolEvent>& events) {
    for (const auto& event : events) {
        switch (event.type) {
        case BrushToolEventType::PaintStrokeSelected:
            emit paintStrokeSelected(event.points, event.color, event.diameter);
            break;
        case BrushToolEventType::MaskPaintPreviewRequested:
            emit maskPaintPreviewRequested(event.points, event.color, event.diameter);
            break;
        case BrushToolEventType::ErasePreviewRequested:
            emit erasePreviewRequested(event.points, event.diameter);
            break;
        case BrushToolEventType::ErasePreviewCleared:
            transient_image_ = {};
            emit erasePreviewCleared();
            break;
        case BrushToolEventType::EraseStrokeSelected:
            emit eraseStrokeSelected(event.points, event.diameter);
            break;
        }
    }
}

void ImageCanvas::updateBrushToolCursor(const QPointF& position) {
    const auto context = brushToolContext(position);
    const bool visible = context.image_target.contains(position);
    if (paint_mode_) paint_tool_.updateCursor(context, visible);
    else paint_tool_.hideCursor();
    if (eraser_mode_) eraser_tool_.updateCursor(context, visible);
    else eraser_tool_.hideCursor();
}

void ImageCanvas::resetBrushTools(bool clear_preview_notification) {
    const bool should_clear_preview =
        eraser_tool_.drawing() || (paint_tool_.drawing() && mask_editing_) ||
        !transient_image_.isNull();
    (void)paint_tool_.cancel(mask_editing_);
    (void)eraser_tool_.cancel();
    paint_tool_.hideCursor();
    eraser_tool_.hideCursor();
    if (clear_preview_notification && should_clear_preview) {
        emit erasePreviewCleared();
    }
    transient_image_ = {};
}

void ImageCanvas::updateTextEditorGeometry() {
    text_tool_.setViewContext(textToolContext());
}

void ImageCanvas::finishTextEditing(bool commit) {
    text_tool_.finishEditing(commit);
}

void ImageCanvas::paintEvent(QPaintEvent*) {
    ImageEditorPerformanceScope paint_scope(
        ImageEditorPerformanceMetrics::instance(),
        ImageEditorPerformanceStage::CanvasPaint);
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
    AreaSelectionToolRenderContext selection_context;
    selection_context.image_bounds = QRectF(QPointF(0.0, 0.0), QSizeF(image_.size()));
    selection_context.image_target = target;
    selection_context.image_to_widget.translate(target.left(), target.top());
    selection_context.image_to_widget.scale(zoom_, zoom_);
    area_selection_tool_.paintOverlay(painter, selection_context);
    // While editing, QPlainTextEdit draws the live text, caret, and selection
    // together. Painting a second copy here duplicates selection; TextTool
    // paints committed text objects below.

    if (crop_mode_) crop_tool_.paintOverlay(painter, cropToolContext());

    const auto brush_context = brushToolContext({});
    if (paint_mode_) paint_tool_.paintOverlay(painter, brush_context);
    if (eraser_mode_) eraser_tool_.paintOverlay(painter, brush_context);

    shape_tool_.paintOverlay(painter, {target, zoom_});
    text_tool_.paintFramePreview(painter, textToolContext());
    if (object_selection_tool_.transformingObjects() &&
        !object_selection_tool_.transformContainsRaster()) {
        for (const auto& object : object_selection_tool_.currentTransformObjects())
            drawObjectOverlay(painter, object);
    }
    ObjectSelectionToolRenderContext object_selection_context;
    object_selection_context.image_target = target;
    object_selection_context.zoom = zoom_;
    object_selection_context.selection_mode_active = object_selection_mode_;
    object_selection_tool_.paintOverlay(painter, object_selection_context);
}

void ImageCanvas::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (fit_to_window_) fitToWindow();
    updateTextEditorGeometry();
}

void ImageCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::LeftButton && text_tool_.editing() &&
        !text_tool_.editor()->geometry().contains(event->position().toPoint())) {
        finishTextEditing(true);
        if (text_creation_mode_ && imageTargetRect().contains(event->position())) {
            event->accept();
            return;
        }
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
    if (eyedropper_mode_ && event->button() == Qt::LeftButton) {
        if (!image_.isNull() && imageTargetRect().contains(event->position())) {
            const auto sampled = eyedropper_tool_.sample(
                image_, widgetToImageCoordinates(event->position()));
            if (sampled) emit colorSampled(*sampled);
        }
        event->accept();
        return;
    }
    if (bucket_fill_mode_ && event->button() == Qt::LeftButton) {
        if (!image_.isNull() && imageTargetRect().contains(event->position())) {
            const auto seed = bucket_fill_tool_.seedAt(
                widgetToImageCoordinates(event->position()), image_.size());
            if (seed) emit bucketFillRequested(*seed, bucket_fill_tolerance_, brush_color_);
        }
        event->accept();
        return;
    }
    if ((paint_mode_ || eraser_mode_) && !crop_mode_ && event->button() == Qt::LeftButton &&
        modifiers.testFlag(Qt::ControlModifier) && modifiers.testFlag(Qt::AltModifier) &&
        imageTargetRect().contains(event->position())) {
        resizing_brush_ = true;
        brush_resize_start_ = event->position();
        brush_resize_global_start_ = event->globalPosition().toPoint();
        brush_resize_initial_diameter_ = brush_diameter_;
        updateBrushToolCursor(event->position());
        update();
        event->accept();
        return;
    }
    if (crop_mode_ && event->button() == Qt::LeftButton &&
        imageTargetRect().contains(event->position())) {
        crop_tool_.beginGesture(widgetToCropImageCoordinates(event->position()));
        update();
        event->accept();
        return;
    }
    if (area_selection_mode_ && event->button() == Qt::LeftButton &&
        imageTargetRect().contains(event->position())) {
        area_selection_tool_.beginGesture(
            widgetToImageCoordinates(event->position()));
        update();
        event->accept();
        return;
    }
    if (shape_creation_mode_ && event->button() == Qt::LeftButton &&
        imageTargetRect().contains(event->position())) {
        shape_tool_.beginGesture(widgetToImageCoordinates(event->position()));
        update();
        event->accept();
        return;
    }
    if (text_creation_mode_ && event->button() == Qt::LeftButton &&
        imageTargetRect().contains(event->position())) {
        const QPointF point = widgetToImageCoordinates(event->position());
        const int hit = object_selection_tool_.hitTestAt(point, zoom_);
        const auto& placements = object_selection_tool_.objects();
        if (hit >= 0 && placements.at(hit).operation.kind == OperationKind::Text) {
            const auto result = object_selection_tool_.selectObjectAt(hit);
            dispatchObjectSelectionToolEvents(result.events);
            event->accept();
            return;
        }
        text_tool_.beginFrame(point);
        event->accept();
        return;
    }
    if (object_selection_mode_ && event->button() == Qt::LeftButton && !image_.isNull()) {
        const auto context = objectSelectionToolContext(event->position(), modifiers);
        const auto result = object_selection_tool_.press(context);
        if (result.handled) {
            const auto deferred_transform_start = result.deferred_transform_start;
            dispatchObjectSelectionToolEvents(result.events);
            if (deferred_transform_start) {
                const auto transform = object_selection_tool_.beginTransform(
                    *deferred_transform_start);
                dispatchObjectSelectionToolEvents(transform.events);
            }
            update();
            event->accept();
            return;
        }
    }
    if (paint_mode_ && event->button() == Qt::LeftButton &&
        imageTargetRect().contains(event->position())) {
        dispatchBrushToolEvents(paint_tool_.press(brushToolContext(event->position())));
        update();
        event->accept();
        return;
    }
    if (eraser_mode_ && event->button() == Qt::LeftButton &&
        imageTargetRect().contains(event->position())) {
        dispatchBrushToolEvents(eraser_tool_.press(brushToolContext(event->position())));
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
        updateTextEditorGeometry();
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
    if (crop_tool_.gestureActive()) {
        crop_tool_.updateGesture(widgetToCropImageCoordinates(event->position()));
        update();
        event->accept();
        return;
    }
    if (area_selection_tool_.gestureActive()) {
        area_selection_tool_.updateGesture(
            widgetToImageCoordinates(event->position()));
        update();
        event->accept();
        return;
    }
    if (text_tool_.frameGestureActive()) {
        text_tool_.updateFrame(widgetToImageCoordinates(event->position()));
        event->accept();
        return;
    }
    if (shape_tool_.gestureActive()) {
        shape_tool_.updateGesture(
            widgetToImageCoordinates(event->position()),
            shift_constrain_held_ || event->modifiers().testFlag(Qt::ShiftModifier));
        update();
        event->accept();
        return;
    }
    if (object_selection_tool_.gestureActive()) {
        const auto result = object_selection_tool_.move(
            objectSelectionToolContext(event->position(), event->modifiers()));
        dispatchObjectSelectionToolEvents(result.events);
        if (object_selection_tool_.resizingTextWidth()) updateTextEditorGeometry();
        update();
        event->accept();
        return;
    }
    if (paint_tool_.drawing() || eraser_tool_.drawing()) {
        const auto context = brushToolContext(event->position());
        dispatchBrushToolEvents(paint_tool_.move(context));
        dispatchBrushToolEvents(eraser_tool_.move(context));
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
        updateBrushToolCursor(brush_resize_start_);
        QCursor::setPos(brush_resize_global_start_);
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && crop_tool_.gestureActive()) {
        const auto selection = crop_tool_.finishGesture(
            widgetToCropImageCoordinates(event->position()), image_.size());
        if (selection) emit cropSelected(*selection);
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && area_selection_tool_.gestureActive()) {
        const auto result = area_selection_tool_.finishGesture(
            widgetToImageCoordinates(event->position()),
            QRectF(QPointF(0.0, 0.0), QSizeF(image_.size())));
        if (result.status == AreaSelectionTool::FinishStatus::Applied) {
            emit areaSelectionChanged(true);
        } else if (result.status == AreaSelectionTool::FinishStatus::Rejected) {
            emit areaSelectionRejected(result.rejection_reason);
        }
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && text_tool_.frameGestureActive()) {
        const auto text = text_tool_.finishFrame(
            widgetToImageCoordinates(event->position()), textToolContext());
        if (text) beginTextEditing(*text, false);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && object_selection_tool_.gestureActive()) {
        const bool was_transforming = object_selection_tool_.transformingObjects();
        const auto result = object_selection_tool_.release(
            objectSelectionToolContext(event->position(), event->modifiers()));
        if (was_transforming) transient_image_ = {};
        dispatchObjectSelectionToolEvents(result.events);
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && shape_tool_.gestureActive()) {
        const auto shape = shape_tool_.finishGesture(
            widgetToImageCoordinates(event->position()),
            shift_constrain_held_ || event->modifiers().testFlag(Qt::ShiftModifier));
        update();
        if (shape) emit shapeCreated(*shape);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && eraser_tool_.drawing()) {
        dispatchBrushToolEvents(
            eraser_tool_.release(brushToolContext(event->position())));
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && paint_tool_.drawing()) {
        dispatchBrushToolEvents(
            paint_tool_.release(brushToolContext(event->position())));
        update();
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
    updateTextEditorGeometry();
    update();
    event->accept();
}

void ImageCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !imageTargetRect().contains(event->position())) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    const int hit = object_selection_tool_.hitTestAt(
        widgetToImageCoordinates(event->position()), zoom_);
    const auto& placements = object_selection_tool_.objects();
    if (hit < 0 || placements.at(hit).operation.kind != OperationKind::Text) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    const auto placement = placements.at(hit);
    const auto selection = object_selection_tool_.selectObjectAt(hit);
    dispatchObjectSelectionToolEvents(selection.events);
    beginTextEditing(placement.operation.text, true);
    event->accept();
}

void ImageCanvas::leaveEvent(QEvent* event) {
    if (!paint_tool_.drawing() && !eraser_tool_.drawing() && !resizing_brush_) {
        paint_tool_.hideCursor();
        eraser_tool_.hideCursor();
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
    if (event->key() == Qt::Key_Escape && shape_tool_.gestureActive()) {
        static_cast<void>(shape_tool_.cancelGesture());
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && area_selection_tool_.gestureActive()) {
        cancelAreaSelectionGesture();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && object_selection_tool_.gestureActive()) {
        clearObjectInteraction();
        transient_image_ = {};
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape &&
        (eraser_tool_.drawing() || (paint_tool_.drawing() && mask_editing_))) {
        dispatchBrushToolEvents(eraser_tool_.cancel());
        dispatchBrushToolEvents(paint_tool_.cancel(mask_editing_));
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
