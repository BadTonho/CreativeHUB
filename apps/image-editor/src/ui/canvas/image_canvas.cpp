#include "image_canvas.h"

#include "image_document_store.h"
#include "../transparency_checkerboard.h"

#include <QEvent>
#include <QApplication>
#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QCursor>
#include <QFrame>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBlock>
#include <QPen>
#include <QTextLayout>
#include <QTextCursor>
#include <QTextOption>
#include <QSignalBlocker>
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

ImageCanvas::ImageCanvas(QWidget* parent) : QWidget(parent) {
    setAcceptDrops(true);
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
    resetBrushTools(true);
    image_ = std::move(image);
    transient_image_ = {};
    creating_shape_ = false;
    clearObjectInteraction();
    resizing_brush_ = false;
    if (resetView) {
        pan_ = {};
        fit_to_window_ = true;
        crop_selection_ = {};
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
    selecting_crop_ = false;
    transient_image_ = {};
    resizing_brush_ = false;
    crop_selection_ = {};
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
    selecting_crop_ = false;
    crop_selection_ = {};
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
    selecting_crop_ = false;
    crop_selection_ = {};
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
    selecting_crop_ = false;
    creating_shape_ = false;
    clearObjectInteraction();
    transient_image_ = {};
    crop_selection_ = {};
    resetBrushTools(false);
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void ImageCanvas::setTextCreationMode(bool enabled) {
    if (!enabled && text_editor_ != nullptr && text_editor_->isVisible()) {
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
    creating_shape_ = false;
    creating_text_frame_ = false;
    clearObjectInteraction();
    transient_image_ = {};
    crop_selection_ = {};
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
    selecting_crop_ = false;
    creating_shape_ = false;
    clearObjectInteraction();
    transient_image_ = {};
    crop_selection_ = {};
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
    selecting_crop_ = false;
    creating_shape_ = false;
    creating_text_frame_ = false;
    clearObjectInteraction();
    transient_image_ = {};
    resetBrushTools(false);
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
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
    if (auto* application = QCoreApplication::instance()) {
        application->installEventFilter(this);
    }
    text_editor_->setFocus(Qt::OtherFocusReason);
    text_editor_->moveCursor(existing ? QTextCursor::Start : QTextCursor::End);
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

void ImageCanvas::applyTextEditorStyle() {
    if (text_editor_ == nullptr) return;
    QFont font(text_editing_.font_family);
    font.setPixelSize(std::max(1, qRound(text_editing_.font_pixel_size * zoom_)));
    text_editor_->setFont(font);
    text_editor_->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { color: %1; background: rgba(255,255,255,24); "
        "border: 1px solid #299bea; padding: 0px; "
        "selection-background-color: #359bdc; selection-color: #ffffff; }")
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
    const int left = qRound(target.left() + text_editing_.position.x() * zoom_);
    const int top = qRound(target.top() + text_editing_.position.y() * zoom_);
    QFont font(text_editing_.font_family);
    font.setPixelSize(std::max(1, qRound(text_editing_.font_pixel_size * zoom_)));
    const bool keep_focus = text_editor_->hasFocus();
    const QTextCursor cursor = text_editor_->textCursor();
    if (text_editor_->font() != font) text_editor_->setFont(font);
    // Lay out at the new width before measuring height. The native editor's
    // rounded font size and fixed screen-pixel margins can wrap differently
    // from the canvas renderer, especially below 100% zoom.
    QRect editor_geometry(left, top, width, text_editor_->height());
    if (text_editor_->geometry() != editor_geometry) {
        text_editor_->setGeometry(editor_geometry);
    }
    qreal native_height = 0.0;
    auto* document_layout = text_editor_->document()->documentLayout();
    for (QTextBlock block = text_editor_->document()->begin(); block.isValid();
         block = block.next()) {
        native_height += document_layout->blockBoundingRect(block).height();
    }
    const qreal vertical_inset = text_editor_->height() - text_editor_->viewport()->height()
        + 2.0 * text_editor_->document()->documentMargin();
    const int pixel_height = std::max({24, qRound(height * zoom_),
        static_cast<int>(std::ceil(native_height + vertical_inset))});
    editor_geometry.setHeight(pixel_height);
    if (text_editor_->geometry() != editor_geometry) {
        text_editor_->setGeometry(editor_geometry);
    }
    // The growing box contains every line, so retain the start of the text in
    // view after a temporary wrap during the input event.
    text_editor_->verticalScrollBar()->setValue(0);
    text_editor_->horizontalScrollBar()->setValue(0);
    if (keep_focus) {
        if (!text_editor_->hasFocus()) text_editor_->setFocus(Qt::OtherFocusReason);
        // Relayout after a resize or font change must not discard the caret or
        // selection placed by the latest mouse or keyboard input.
        if (text_editor_->textCursor() != cursor) text_editor_->setTextCursor(cursor);
    }
}

void ImageCanvas::updateTextEditorContentAndGeometry() {
    if (text_editor_ == nullptr || !text_editor_->isVisible() || image_.isNull()) return;

    text_editing_.content = text_editor_->toPlainText();

    QFont font(text_editing_.font_family);
    font.setPixelSize(std::clamp(text_editing_.font_pixel_size, 1, 1024));
    const QFontMetricsF metrics(font);
    const QFontMetricsF native_metrics(text_editor_->font(), text_editor_->viewport());
    qreal content_width = 0.0;
    qreal native_content_width = 0.0;
    const QStringList lines = text_editing_.content.split(QLatin1Char('\n'),
                                                          Qt::KeepEmptyParts);
    for (const QString& line : lines) {
        content_width = std::max(content_width, metrics.horizontalAdvance(line));
        native_content_width = std::max(native_content_width,
            native_metrics.horizontalAdvance(line));
    }

    const qreal available_width = std::max<qreal>(1.0,
        image_.width() - text_editing_.position.x());
    const qreal minimum_width = std::min(text_editing_initial_box_width_, available_width);
    constexpr qreal kTextEditorHorizontalInset = 4.0;
    const qreal native_inset = text_editor_->width() - text_editor_->viewport()->width()
        + 2.0 * text_editor_->document()->documentMargin()
        + text_editor_->cursorWidth() + 2.0;
    const qreal desired_width = text_editing_.content.isEmpty() ? minimum_width
        : std::max({minimum_width, content_width + kTextEditorHorizontalInset,
            (native_content_width + native_inset) / zoom_});
    text_editing_.box_width = std::clamp(desired_width, minimum_width, available_width);

    // Resizing QPlainTextEdit synchronously from its textChanged signal can
    // interrupt its active layout/key handling. Apply the latest geometry
    // after the input event finishes instead.
    if (!text_editor_geometry_update_pending_) {
        text_editor_geometry_update_pending_ = true;
        QMetaObject::invokeMethod(this, [this]() {
            text_editor_geometry_update_pending_ = false;
            updateTextEditorGeometry();
            if (text_editor_ != nullptr && text_editor_->isVisible()) {
                text_editor_->viewport()->repaint();
            }
        }, Qt::QueuedConnection);
    }
    update();
}

void ImageCanvas::finishTextEditing(bool commit) {
    if (text_editor_ == nullptr || !text_editor_->isVisible()) return;
    text_editing_.content = text_editor_->toPlainText();
    const ImageTextData text = text_editing_;
    const bool existing = text_editing_existing_;
    if (auto* application = QCoreApplication::instance()) {
        application->removeEventFilter(this);
    }
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
    // together. Painting a second copy here makes selection appear duplicated
    // and misaligned as the editor grows. Committed text uses drawTextOverlay.

    if (crop_mode_ && selecting_crop_) {
        const QRectF selection = crop_selection_.normalized().intersected(target);
        painter.fillRect(selection, QColor(38, 150, 220, 36));
        QPen pen(QColor(120, 205, 255), 1.5, Qt::DashLine);
        painter.setPen(pen);
        painter.drawRect(selection);
    }

    const auto brush_context = brushToolContext({});
    if (paint_mode_) paint_tool_.paintOverlay(painter, brush_context);
    if (eraser_mode_) eraser_tool_.paintOverlay(painter, brush_context);

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
    if (event->button() == Qt::LeftButton && text_editor_ != nullptr &&
        text_editor_->isVisible() && !text_editor_->geometry().contains(event->position().toPoint())) {
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
        selecting_crop_ = true;
        crop_start_ = event->position();
        crop_selection_ = QRectF(crop_start_, crop_start_);
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
        const int hit = object_selection_tool_.hitTestAt(point, zoom_);
        const auto& placements = object_selection_tool_.objects();
        if (hit >= 0 && placements.at(hit).operation.kind == OperationKind::Text) {
            const auto result = object_selection_tool_.selectObjectAt(hit);
            dispatchObjectSelectionToolEvents(result.events);
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
    if (selecting_crop_) {
        crop_selection_ = QRectF(crop_start_, event->position()).normalized();
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
    if (event->button() == Qt::LeftButton && selecting_crop_) {
        selecting_crop_ = false;
        const QRect selection = cropToImageCoordinates(crop_selection_);
        crop_selection_ = {};
        if (selection.width() > 1 && selection.height() > 1) emit cropSelected(selection);
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
    if (event->button() == Qt::LeftButton && creating_text_frame_) {
        text_frame_current_ = widgetToImageCoordinates(event->position());
        const QPointF start = text_frame_start_;
        const QPointF end = text_frame_current_;
        creating_text_frame_ = false;
        update();
        const qreal width = std::abs(end.x() - start.x());
        ImageTextData text = text_style_;
        text.id.clear();
        text.content.clear();
        const bool dragged_to_set_width = width >= 4.0;
        const qreal left = dragged_to_set_width ? std::min(start.x(), end.x()) : start.x();
        text.position = QPointF(left, start.y());
        const qreal available_width = std::max<qreal>(1.0, image_.width() - left);
        const qreal initial_width = dragged_to_set_width ? width : text_style_.box_width;
        text.box_width = std::clamp(initial_width, 1.0, available_width);
        beginTextEditing(text, false);
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

bool ImageCanvas::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ShortcutOverride && text_editor_ != nullptr &&
        text_editor_->isVisible()) {
        QWidget* focus_widget = QApplication::focusWidget();
        const bool text_editor_has_focus = focus_widget == text_editor_ ||
            (focus_widget != nullptr && text_editor_->isAncestorOf(focus_widget));
        auto* key_event = static_cast<QKeyEvent*>(event);
        const auto modifiers = key_event->modifiers();
        const bool altgr = modifiers.testFlag(Qt::GroupSwitchModifier);
        const bool command_modifier =
            modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::MetaModifier);
        const bool menu_modifier = modifiers.testFlag(Qt::AltModifier) && !altgr;
        // Unmodified key presses belong to the focused text editor even when a
        // platform sends ShortcutOverride without the corresponding text. This
        // prevents one-key window shortcuts from swallowing typed characters.
        if (text_editor_has_focus && (!command_modifier || altgr) && !menu_modifier) {
            key_event->accept();
            // Accept the override so Qt does not activate a shortcut, but let
            // the event reach the focused widget and continue its key handling.
            return false;
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
    if (event->key() == Qt::Key_Escape && creating_shape_) {
        creating_shape_ = false;
        shape_interaction_current_ = {};
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
