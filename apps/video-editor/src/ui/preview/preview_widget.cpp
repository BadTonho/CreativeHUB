#include "ui/preview/preview_widget.h"

#include "rendering/opengl_preview_surface.h"
#include "rendering/vulkan_preview_surface.h"
#include <creative_suite/composition/graphics_backend.h>
#include "rendering/preview_performance_metrics.h"

#include <QImage>
#include <QCoreApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QResizeEvent>
#include <QStackedLayout>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

class PreviewCpuSurface final : public QLabel {
public:
    using QLabel::QLabel;

    void setComparison(QImage live, QImage reference,
        creative_suite::composition::FrameComparisonMode mode, float fraction) {
        live_ = std::move(live);
        reference_ = std::move(reference);
        comparison_mode_ = mode;
        reference_fraction_ = std::clamp(fraction, 0.0F, 1.0F);
        update();
    }

    void setWipeDraggedHandler(std::function<void(float)> handler) {
        wipe_dragged_handler_ = std::move(handler);
    }

    void setDeliveryTraceId(quint64 trace_id) noexcept {
        if (delivery_trace_id_ != 0 && delivery_trace_id_ != trace_id) {
            rendering::PreviewPerformanceMetrics::instance().recordFrameDeliveryDrop(
                delivery_trace_id_,
                rendering::PreviewFrameDeliveryDropReason::PreviewOverwritten);
        }
        delivery_trace_id_ = trace_id;
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        if (comparison_mode_ != creative_suite::composition::FrameComparisonMode::Off &&
            !live_.isNull() && !reference_.isNull()) {
            QPainter painter(this);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.fillRect(rect(), QColor(28, 32, 40));
            if (comparison_mode_ == creative_suite::composition::FrameComparisonMode::SplitScreen) {
                const int split = width() / 2;
                painter.drawImage(fitRect(reference_.size(), QRect(0, 0, split, height())), reference_);
                painter.drawImage(fitRect(live_.size(), QRect(split, 0,
                    width() - split, height())), live_);
                painter.setPen(QPen(QColor(220, 228, 238, 190), 1));
                painter.drawLine(split, 0, split, height());
            } else {
                const QRect live_rect = fitRect(live_.size(), rect());
                painter.drawImage(live_rect, live_);
                const QRect reference_rect = fitRect(reference_.size(), live_rect);
                const int split = live_rect.left() + int(std::lround(
                    double(live_rect.width()) * reference_fraction_));
                painter.save();
                painter.setClipRect(QRect(live_rect.left(), live_rect.top(),
                    split - live_rect.left(), live_rect.height()));
                painter.drawImage(reference_rect, reference_);
                painter.restore();
                if (reference_fraction_ > 0.0F && reference_fraction_ < 1.0F) {
                    painter.setPen(QPen(QColor(235, 240, 248, 230), 2));
                    painter.drawLine(split, live_rect.top(), split, live_rect.bottom());
                }
            }
            return;
        }
        QLabel::paintEvent(event);
        if (delivery_trace_id_ == 0) return;
        rendering::PreviewPerformanceMetrics::instance().recordFrameDeliveryStage(
            delivery_trace_id_,
            rendering::PreviewFrameDeliveryStage::CpuPainted);
        delivery_trace_id_ = 0;
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (comparison_mode_ == creative_suite::composition::FrameComparisonMode::Wipe &&
            !live_.isNull() && event->button() == Qt::LeftButton) {
            const QRect image_rect = fitRect(live_.size(), rect());
            const int split = image_rect.left() + int(std::lround(
                double(image_rect.width()) * reference_fraction_));
            if (std::abs(event->position().x() - split) <= 12.0) {
                dragging_wipe_ = true;
                updateWipePosition(event->position().toPoint());
                event->accept();
                return;
            }
        }
        QLabel::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (dragging_wipe_) {
            updateWipePosition(event->position().toPoint());
            event->accept();
            return;
        }
        QLabel::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (dragging_wipe_ && event->button() == Qt::LeftButton) {
            updateWipePosition(event->position().toPoint());
            dragging_wipe_ = false;
            event->accept();
            return;
        }
        QLabel::mouseReleaseEvent(event);
    }

private:
    static QRect fitRect(QSize source, QRect target) {
        if (!source.isValid() || !target.isValid()) return {};
        QSize fitted = source;
        fitted.scale(target.size(), Qt::KeepAspectRatio);
        return QRect(target.center().x() - fitted.width() / 2,
            target.center().y() - fitted.height() / 2, fitted.width(), fitted.height());
    }

    void updateWipePosition(const QPoint& point) {
        const QRect image_rect = fitRect(live_.size(), rect());
        if (image_rect.width() <= 0) return;
        const float fraction = std::clamp(float(point.x() - image_rect.left()) /
            float(image_rect.width()), 0.0F, 1.0F);
        if (wipe_dragged_handler_) wipe_dragged_handler_(fraction);
    }

    quint64 delivery_trace_id_ = 0;
    QImage live_;
    QImage reference_;
    creative_suite::composition::FrameComparisonMode comparison_mode_ =
        creative_suite::composition::FrameComparisonMode::Off;
    float reference_fraction_ = 0.5F;
    bool dragging_wipe_ = false;
    std::function<void(float)> wipe_dragged_handler_;
};

PreviewWidget::PreviewWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumSize(320, 180);
    setStyleSheet("background-color: #1c2028; color: #c7d0dc; font-size: 20px;");

    stack_ = new QStackedLayout(this);
    stack_->setContentsMargins(0, 0, 0, 0);

    cpu_surface_ = new PreviewCpuSurface(this);
    cpu_surface_->setObjectName("previewCpuSurface");
    cpu_surface_->setAlignment(Qt::AlignCenter);
    cpu_surface_->setMinimumSize(320, 180);
    cpu_surface_->setStyleSheet(
        "background-color: #1c2028; color: #c7d0dc; font-size: 20px;");
    stack_->addWidget(cpu_surface_);

    gpu_enabled_ = qEnvironmentVariableIntValue("CREATIVE_SUITE_DISABLE_GPU_PREVIEW") != 1 && creative_suite::composition::sessionGraphicsBackend() != creative_suite::composition::GraphicsBackend::Cpu;
    if (gpu_enabled_ && creative_suite::composition::sessionGraphicsBackend() == creative_suite::composition::GraphicsBackend::Vulkan) {
        vulkan_surface_ = new rendering::VulkanPreviewSurface(this);
        stack_->addWidget(vulkan_surface_);
        stack_->setCurrentWidget(vulkan_surface_);
        connect(vulkan_surface_, &rendering::VulkanPreviewSurface::gpuFailure, this, &PreviewWidget::handleGpuFailure);
    } else if (gpu_enabled_) {
        gpu_surface_ = new rendering::OpenGLPreviewSurface(this);
        stack_->addWidget(gpu_surface_);
        stack_->setCurrentWidget(gpu_surface_);
        connect(
            gpu_surface_,
            &rendering::OpenGLPreviewSurface::gpuFailure,
            this,
            &PreviewWidget::handleGpuFailure);
        connect(gpu_surface_, &rendering::OpenGLPreviewSurface::textureDeliveryAvailabilityChanged,
            this, &PreviewWidget::gpuTextureDeliveryAvailabilityChanged);
        connect(gpu_surface_, &rendering::OpenGLPreviewSurface::gpuTextureFailure,
            this, &PreviewWidget::gpuFallbackRequested);
    }

    cpu_surface_->setWipeDraggedHandler([this](float fraction) {
        setComparison(comparison_mode_, fraction);
        emit wipePositionDragged(fraction);
    });
    if (gpu_surface_ != nullptr) {
        connect(gpu_surface_, &rendering::OpenGLPreviewSurface::wipePositionDragged,
            this, [this](float fraction) {
                setComparison(comparison_mode_, fraction);
                emit wipePositionDragged(fraction);
            });
        connect(gpu_surface_, &rendering::OpenGLPreviewSurface::comparisonUnavailable,
            this, [this](const QString& cause, qint64 error_code) {
                setComparison(creative_suite::composition::FrameComparisonMode::Off,
                    reference_fraction_);
                emit comparisonUnavailable(cause, error_code);
            });
    }
    if (vulkan_surface_ != nullptr) {
        connect(vulkan_surface_, &rendering::VulkanPreviewSurface::wipePositionDragged,
            this, [this](float fraction) {
                setComparison(comparison_mode_, fraction);
                emit wipePositionDragged(fraction);
            });
        connect(vulkan_surface_, &rendering::VulkanPreviewSurface::comparisonUnavailable,
            this, [this](const QString& cause, qint64 error_code) {
                setComparison(creative_suite::composition::FrameComparisonMode::Off,
                    reference_fraction_);
                emit comparisonUnavailable(cause, error_code);
            });
    }

    clearFrame(QCoreApplication::translate(
        "PreviewWidget", "Preview area\n\nImport media to display its first frame."));
}

void PreviewWidget::setAspectConstrained(bool constrained) {
    const QSize minimum = constrained ? QSize(0, 0) : QSize(320, 180);
    setMinimumSize(minimum);
    if (cpu_surface_ != nullptr) cpu_surface_->setMinimumSize(minimum);
}

void PreviewWidget::setFrame(const media::VideoFrame& frame) {
    setFrame(std::make_shared<const media::VideoFrame>(frame));
}

PreviewWidget::~PreviewWidget() {
    if (gpu_surface_) disconnect(gpu_surface_, nullptr, this, nullptr);
    releaseGpuFrames();
}

void PreviewWidget::setFrame(
    media::VideoFramePtr frame,
    quint64 delivery_trace_id) {
    rendering::PreviewFramePayload payload{std::move(frame)};
    payload.delivery_epoch = delivery_epoch_;
    setFrame(std::move(payload), delivery_trace_id);
}

void PreviewWidget::setFrame(rendering::PreviewFramePayload frame, quint64 delivery_trace_id) {
    if (frame.delivery_epoch != delivery_epoch_) {
        rendering::PreviewPerformanceMetrics::instance().recordFrameDeliveryDrop(
            delivery_trace_id, rendering::PreviewFrameDeliveryDropReason::StaleGeneration);
        return;
    }
    if (!frame.valid() || frame.width() <= 0 || frame.height() <= 0 ||
        (frame.rgba && frame.rgba->stride < frame.width() * 4)) {
        rendering::PreviewPerformanceMetrics::instance().recordFrameDeliveryDrop(
            delivery_trace_id,
            rendering::PreviewFrameDeliveryDropReason::InvalidFrame);
        clearFrame(QCoreApplication::translate("PreviewWidget", "Preview frame is unavailable."));
        return;
    }

    if (frame.rgba && frame.rgba->rgba_pixels.size() <
        static_cast<std::size_t>(frame.rgba->stride) * static_cast<std::size_t>(frame.height())) {
        rendering::PreviewPerformanceMetrics::instance().recordFrameDeliveryDrop(
            delivery_trace_id,
            rendering::PreviewFrameDeliveryDropReason::InvalidFrame);
        clearFrame(QCoreApplication::translate("PreviewWidget", "Preview frame is unavailable."));
        return;
    }

    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    rendering::PreviewPerformanceScope timing(
        metrics,
        rendering::PreviewTiming::PreviewSubmit);
    metrics.recordSubmittedFrame(frame.width(), frame.height());
    metrics.recordPreviewDelivery(bool(frame.gpu));
    metrics.recordFrameDeliveryStage(
        delivery_trace_id,
        rendering::PreviewFrameDeliveryStage::PreviewSubmitted);
    current_payload_ = std::move(frame);
    current_frame_ = current_payload_.rgba;
    if (parentWidget() != nullptr && parentWidget()->layout() != nullptr) {
        parentWidget()->layout()->invalidate();
    }

    if (vulkan_surface_ != nullptr && gpu_enabled_) {
        cpu_surface_->setDeliveryTraceId(0); frame_image_ = {};
        stack_->setCurrentWidget(vulkan_surface_);
        vulkan_surface_->setFrame(current_payload_, delivery_trace_id);
        return;
    }
    if (gpu_surface_ != nullptr && gpu_enabled_) {
        stack_->setCurrentWidget(gpu_surface_);
        cpu_surface_->setDeliveryTraceId(0);
        frame_image_ = {};
        gpu_surface_->setFrame(current_payload_, delivery_trace_id);
        return;
    }

    if (current_payload_.gpu) {
        emit gpuFallbackRequested(QCoreApplication::translate(
            "PreviewWidget", "The shared texture cannot be presented by the current viewer."), -1);
        return;
    }

    stack_->setCurrentWidget(cpu_surface_);
    if (gpu_surface_) gpu_surface_->clearFrame();
    cpu_surface_->setDeliveryTraceId(delivery_trace_id);
    frame_image_ = {};
    ensureCpuImage();
    updateCpuPixmap();
    metrics.recordCpuPresentedFrame();
}

void PreviewWidget::clearFrame(const QString& message) {
    current_payload_ = {};
    current_frame_.reset();
    frame_image_ = {};
    empty_message_ = message;
    if (parentWidget() != nullptr && parentWidget()->layout() != nullptr) {
        parentWidget()->layout()->invalidate();
    }
    if (cpu_surface_ != nullptr) {
        cpu_surface_->setDeliveryTraceId(0);
        cpu_surface_->setComparison({}, {},
            creative_suite::composition::FrameComparisonMode::Off, reference_fraction_);
        cpu_surface_->setPixmap({});
        cpu_surface_->setText(empty_message_);
    }
    if (gpu_surface_ != nullptr) gpu_surface_->clearFrame();
    if (vulkan_surface_) vulkan_surface_->clearFrame();
    if (vulkan_surface_) stack_->setCurrentWidget(cpu_surface_);
}

void PreviewWidget::releaseGpuFrames() {
    current_payload_.gpu.reset();
    if (vulkan_surface_) vulkan_surface_->clearFrame();
    if (gpu_surface_) gpu_surface_->clearFrame();
}

void PreviewWidget::setDeliveryEpoch(quint64 epoch, bool retry_texture_delivery) noexcept {
    delivery_epoch_ = epoch;
    if (vulkan_surface_) vulkan_surface_->setDeliveryEpoch(epoch);
    if (retry_texture_delivery && gpu_surface_) gpu_surface_->retryTextureDelivery();
}

bool PreviewWidget::textureDeliveryAvailable() const noexcept {
    return usesGpuPreview() && (vulkan_surface_ ? vulkan_surface_->available() : gpu_surface_->textureDeliveryAvailable());
}

void PreviewWidget::setGrayscaleEnabled(bool enabled) {
    grayscale_enabled_ = enabled;
    if (vulkan_surface_ && gpu_enabled_) { vulkan_surface_->setGrayscaleEnabled(enabled); return; }
    if (gpu_surface_ != nullptr && gpu_enabled_) {
        gpu_surface_->setGrayscaleEnabled(enabled);
        return;
    }
    updateCpuPixmap();
}

void PreviewWidget::setComparisonReferenceImage(QImage image) {
    reference_image_ = image.isNull() ? QImage{} : image.convertToFormat(QImage::Format_RGBA8888);
    if (gpu_surface_) gpu_surface_->setComparisonReferenceImage(reference_image_);
    if (vulkan_surface_) vulkan_surface_->setComparisonReferenceImage(reference_image_);
    updateCpuPixmap();
}

void PreviewWidget::setComparison(
    creative_suite::composition::FrameComparisonMode mode, float reference_fraction) {
    comparison_mode_ = mode;
    reference_fraction_ = std::clamp(reference_fraction, 0.0F, 1.0F);
    if (gpu_surface_) gpu_surface_->setComparison(comparison_mode_, reference_fraction_);
    if (vulkan_surface_) vulkan_surface_->setComparison(comparison_mode_, reference_fraction_);
    updateCpuPixmap();
}

bool PreviewWidget::isGrayscaleEnabled() const noexcept {
    return grayscale_enabled_;
}

bool PreviewWidget::usesGpuPreview() const noexcept {
    return gpu_enabled_ && (gpu_surface_ != nullptr || (vulkan_surface_ && vulkan_surface_->available()));
}

void PreviewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (!gpu_enabled_) updateCpuPixmap();
}

void PreviewWidget::handleGpuFailure(const QString& reason, qint64 error_code) {
    if (vulkan_surface_ && error_code == 0 && current_payload_.rgba) {
        stack_->setCurrentWidget(cpu_surface_); frame_image_ = {}; ensureCpuImage(); updateCpuPixmap();
        rendering::PreviewPerformanceMetrics::instance().recordCpuPresentedFrame();
        return;
    }
    gpu_enabled_ = false;
    // Keep the previous GPU presentation until asynchronous recovery supplies RGBA.
    if (!current_payload_.gpu && stack_ != nullptr && cpu_surface_ != nullptr) {
        stack_->setCurrentWidget(cpu_surface_);
    }
    frame_image_ = {};
    ensureCpuImage();
    if (!current_payload_.gpu) updateCpuPixmap();
    emit gpuFallbackRequested(reason, error_code);
}

void PreviewWidget::ensureCpuImage() {
    if (!frame_image_.isNull() || current_frame_ == nullptr) return;

    const auto& frame = *current_frame_;
    const QImage image(
        frame.rgba_pixels.data(),
        frame.width,
        frame.height,
        frame.stride,
        QImage::Format_RGBA8888);
    frame_image_ = image.copy();
}

void PreviewWidget::updateCpuPixmap() {
    if (cpu_surface_ == nullptr) return;
    ensureCpuImage();
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
    if (comparison_mode_ != creative_suite::composition::FrameComparisonMode::Off &&
        !reference_image_.isNull()) {
        auto reference = reference_image_;
        if (grayscale_enabled_) {
            reference = reference.convertToFormat(QImage::Format_RGBA8888);
            for (int y = 0; y < reference.height(); ++y) {
                auto* pixels = reference.scanLine(y);
                for (int x = 0; x < reference.width(); ++x) {
                    auto* pixel = pixels + x * 4;
                    const auto luminance = static_cast<std::uint8_t>(std::clamp(
                        0.299 * pixel[0] + 0.587 * pixel[1] + 0.114 * pixel[2],
                        0.0, 255.0));
                    pixel[0] = pixel[1] = pixel[2] = luminance;
                }
            }
        }
        cpu_surface_->setPixmap({});
        cpu_surface_->setComparison(image, reference, comparison_mode_, reference_fraction_);
        return;
    }
    cpu_surface_->setComparison({}, {}, creative_suite::composition::FrameComparisonMode::Off,
        reference_fraction_);
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
