#include "image_canvas.h"

#include "image_document_store.h"
#include "../transparency_checkerboard.h"

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace image_editor {
namespace {

constexpr qsizetype kMaximumPaintPreviewPoints = 100'000;

} // namespace

ImageCanvas::ImageCanvas(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(240, 180);
    setMouseTracking(true);
    setAutoFillBackground(false);
}

void ImageCanvas::setImage(QImage image, bool resetView) {
    image_ = std::move(image);
    paint_points_.clear();
    painting_ = false;
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
    crop_mode_ = enabled;
    if (enabled) paint_mode_ = false;
    selecting_crop_ = false;
    painting_ = false;
    resizing_brush_ = false;
    paint_points_.clear();
    crop_selection_ = {};
    brush_cursor_visible_ = false;
    setCursor(enabled ? Qt::CrossCursor
                      : (paint_mode_ ? Qt::BlankCursor : Qt::ArrowCursor));
    update();
}

void ImageCanvas::setPaintMode(bool enabled) {
    paint_mode_ = enabled;
    if (enabled) crop_mode_ = false;
    painting_ = false;
    resizing_brush_ = false;
    paint_points_.clear();
    selecting_crop_ = false;
    crop_selection_ = {};
    brush_cursor_visible_ = false;
    setCursor(enabled ? Qt::BlankCursor
                      : (crop_mode_ ? Qt::CrossCursor : Qt::ArrowCursor));
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

void ImageCanvas::appendPaintPoint(const QPointF& point) {
    if (!paint_points_.isEmpty() && paint_points_.back() == point) return;
    if (paint_points_.size() >= kMaximumPaintPreviewPoints) {
        paint_points_.last() = point;
        return;
    }
    paint_points_.append(point);
}

void ImageCanvas::updateHoverCursor(const QPointF& position) {
    brush_cursor_visible_ = paint_mode_ && imageTargetRect().contains(position);
    if (brush_cursor_visible_) brush_cursor_position_ = position;
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
    painter.drawImage(target, image_);

    if (crop_mode_ && selecting_crop_) {
        const QRectF selection = crop_selection_.normalized().intersected(target);
        painter.fillRect(selection, QColor(38, 150, 220, 36));
        QPen pen(QColor(120, 205, 255), 1.5, Qt::DashLine);
        painter.setPen(pen);
        painter.drawRect(selection);
    }

    if (paint_mode_) {
        if (painting_ && !paint_points_.isEmpty()) {
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
            QPen pen(brush_color_, brush_diameter_ * zoom_, Qt::SolidLine,
                     Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(pen);
            if (paint_points_.size() == 1) {
                const QPointF center = toWidget(paint_points_.front());
                const qreal radius = brush_diameter_ * zoom_ / 2.0;
                painter.setPen(Qt::NoPen);
                painter.setBrush(brush_color_);
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
}

void ImageCanvas::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (fit_to_window_) fitToWindow();
}

void ImageCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        pan_start_ = event->position();
        initial_pan_ = pan_;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    const auto modifiers = event->modifiers();
    if (paint_mode_ && !crop_mode_ && event->button() == Qt::LeftButton &&
        modifiers.testFlag(Qt::ControlModifier) && modifiers.testFlag(Qt::AltModifier) &&
        imageTargetRect().contains(event->position())) {
        resizing_brush_ = true;
        brush_resize_start_ = event->position();
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
    if (painting_) {
        const QPointF point = widgetToImageCoordinates(event->position());
        appendPaintPoint(point);
        brush_cursor_position_ = event->position();
        brush_cursor_visible_ = imageTargetRect().contains(event->position());
        update();
        event->accept();
        return;
    }
    if (paint_mode_) {
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
                             : (paint_mode_ ? Qt::BlankCursor : Qt::ArrowCursor));
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && resizing_brush_) {
        resizing_brush_ = false;
        brush_cursor_position_ = event->position();
        brush_cursor_visible_ = imageTargetRect().contains(event->position());
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

void ImageCanvas::leaveEvent(QEvent* event) {
    if (!painting_ && !resizing_brush_) {
        brush_cursor_visible_ = false;
        update();
    }
    QWidget::leaveEvent(event);
}

} // namespace image_editor
