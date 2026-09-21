#include "preview_widget.h"

#include "rendering/opengl_preview_surface.h"
#include "rendering/preview_performance_metrics.h"

#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QResizeEvent>
#include <QStackedLayout>

#include <algorithm>
#include <cstddef>
#include <cstdint>

PreviewWidget::PreviewWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumSize(320, 180);
    setStyleSheet("background-color: #1c2028; color: #c7d0dc; font-size: 20px;");

    stack_ = new QStackedLayout(this);
    stack_->setContentsMargins(0, 0, 0, 0);

    cpu_surface_ = new QLabel(this);
    cpu_surface_->setAlignment(Qt::AlignCenter);
    cpu_surface_->setMinimumSize(320, 180);
    cpu_surface_->setStyleSheet(
        "background-color: #1c2028; color: #c7d0dc; font-size: 20px;");
    stack_->addWidget(cpu_surface_);

    gpu_enabled_ = qEnvironmentVariableIntValue("CREATIVE_SUITE_DISABLE_GPU_PREVIEW") != 1;
    if (gpu_enabled_) {
        gpu_surface_ = new rendering::OpenGLPreviewSurface(this);
        stack_->addWidget(gpu_surface_);
        stack_->setCurrentWidget(gpu_surface_);
        connect(
            gpu_surface_,
            &rendering::OpenGLPreviewSurface::gpuFailure,
            this,
            &PreviewWidget::handleGpuFailure);
    }

    clearFrame("Preview area\n\nImport media to display its first frame.");
}

void PreviewWidget::setFrame(const media::VideoFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0 || frame.stride < frame.width * 4) {
        clearFrame("Preview frame is unavailable.");
        return;
    }

    const auto expected_size = static_cast<std::size_t>(frame.stride) *
        static_cast<std::size_t>(frame.height);
    if (frame.rgba_pixels.size() < expected_size) {
        clearFrame("Preview frame is unavailable.");
        return;
    }

    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    rendering::PreviewPerformanceScope timing(
        metrics,
        rendering::PreviewTiming::PreviewSubmit);
    metrics.recordSubmittedFrame(frame.width, frame.height);

    const QImage image(
        frame.rgba_pixels.data(),
        frame.width,
        frame.height,
        frame.stride,
        QImage::Format_RGBA8888);
    frame_image_ = image.copy();
    updateCpuPixmap();
    if (gpu_surface_ != nullptr && gpu_enabled_) gpu_surface_->setFrame(frame);
}

void PreviewWidget::clearFrame(const QString& message) {
    frame_image_ = {};
    empty_message_ = message;
    if (cpu_surface_ != nullptr) {
        cpu_surface_->setPixmap({});
        cpu_surface_->setText(empty_message_);
    }
    if (gpu_surface_ != nullptr) gpu_surface_->clearFrame();
}

void PreviewWidget::setGrayscaleEnabled(bool enabled) {
    grayscale_enabled_ = enabled;
    updateCpuPixmap();
    if (gpu_surface_ != nullptr && gpu_enabled_) {
        gpu_surface_->setGrayscaleEnabled(enabled);
    }
}

bool PreviewWidget::isGrayscaleEnabled() const noexcept {
    return grayscale_enabled_;
}

void PreviewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateCpuPixmap();
}

void PreviewWidget::handleGpuFailure(const QString& reason, qint64 error_code) {
    gpu_enabled_ = false;
    if (stack_ != nullptr && cpu_surface_ != nullptr) {
        stack_->setCurrentWidget(cpu_surface_);
    }
    updateCpuPixmap();
    emit gpuFallbackRequested(reason, error_code);
}

void PreviewWidget::updateCpuPixmap() {
    if (cpu_surface_ == nullptr) return;
    if (frame_image_.isNull()) {
        cpu_surface_->setPixmap({});
        cpu_surface_->setText(empty_message_);
        return;
    }

    const auto image = grayscale_enabled_ ? grayscaleImage() : frame_image_;
    const auto available_size = cpu_surface_->contentsRect().size();
    if (!available_size.isValid()) return;

    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    rendering::PreviewPerformanceScope timing(
        metrics,
        rendering::PreviewTiming::CpuSurface);

    cpu_surface_->setText({});
    cpu_surface_->setPixmap(QPixmap::fromImage(
        image.scaled(available_size, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

QImage PreviewWidget::grayscaleImage() const {
    QImage result = frame_image_.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < result.height(); ++y) {
        auto* pixels = result.scanLine(y);
        for (int x = 0; x < result.width(); ++x) {
            auto* pixel = pixels + (x * 4);
            const auto luminance = static_cast<std::uint8_t>(
                std::clamp(
                    0.299 * static_cast<double>(pixel[0]) +
                        0.587 * static_cast<double>(pixel[1]) +
                        0.114 * static_cast<double>(pixel[2]),
                    0.0,
                    255.0));
            pixel[0] = luminance;
            pixel[1] = luminance;
            pixel[2] = luminance;
        }
    }
    return result;
}
