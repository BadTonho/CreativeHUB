#include "preview_widget.h"

#include <QImage>
#include <QPixmap>
#include <QResizeEvent>

#include <cstddef>

PreviewWidget::PreviewWidget(QWidget* parent)
    : QLabel(parent) {
    setAlignment(Qt::AlignCenter);
    setMinimumSize(320, 180);
    setStyleSheet("background-color: #1c2028; color: #c7d0dc; font-size: 20px;");
    clearFrame("Preview area\n\nImport media to display its first frame.");
}

void PreviewWidget::setFrame(const media::VideoFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0 || frame.stride <= 0) {
        clearFrame("Preview frame is unavailable.");
        return;
    }

    const auto expected_size = static_cast<std::size_t>(frame.stride) *
        static_cast<std::size_t>(frame.height);
    if (frame.rgba_pixels.size() < expected_size) {
        clearFrame("Preview frame is unavailable.");
        return;
    }

    const QImage image(
        frame.rgba_pixels.data(),
        frame.width,
        frame.height,
        frame.stride,
        QImage::Format_RGBA8888);
    frame_image_ = image.copy();
    updatePixmap();
}

void PreviewWidget::clearFrame(const QString& message) {
    frame_image_ = {};
    setPixmap({});
    setText(message);
}

void PreviewWidget::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    updatePixmap();
}

void PreviewWidget::updatePixmap() {
    if (frame_image_.isNull()) return;

    setText({});
    const auto available_size = contentsRect().size();
    if (!available_size.isValid()) return;

    const auto scaled = frame_image_.scaled(
        available_size,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    setPixmap(QPixmap::fromImage(scaled));
}
