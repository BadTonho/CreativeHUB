#include "image_canvas.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace image_editor {

ImageCanvas::ImageCanvas(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(240, 180);
    setMouseTracking(true);
    setAutoFillBackground(false);
}

void ImageCanvas::setImage(QImage image) {
    image_ = std::move(image);
    pan_ = {};
    fit_to_window_ = true;
    crop_selection_ = {};
    fitToWindow();
    update();
}

void ImageCanvas::setCropMode(bool enabled) {
    crop_mode_ = enabled;
    selecting_crop_ = false;
    crop_selection_ = {};
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
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
    painter.drawImage(target, image_);

    if (crop_mode_ && selecting_crop_) {
        const QRectF selection = crop_selection_.normalized().intersected(target);
        painter.fillRect(selection, QColor(38, 150, 220, 36));
        QPen pen(QColor(120, 205, 255), 1.5, Qt::DashLine);
        painter.setPen(pen);
        painter.drawRect(selection);
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
    if (crop_mode_ && event->button() == Qt::LeftButton &&
        imageTargetRect().contains(event->position())) {
        selecting_crop_ = true;
        crop_start_ = event->position();
        crop_selection_ = QRectF(crop_start_, crop_start_);
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
    if (selecting_crop_) {
        crop_selection_ = QRectF(crop_start_, event->position()).normalized();
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton && panning_) {
        panning_ = false;
        setCursor(crop_mode_ ? Qt::CrossCursor : Qt::ArrowCursor);
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

void ImageCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && crop_mode_) {
        setCropMode(false);
        emit cropModeCancelled();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace image_editor
