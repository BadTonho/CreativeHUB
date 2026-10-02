#pragma once

#include "rendering/preview_frame_payload.h"

#include <QImage>
#include <QString>
#include <QWidget>

class QResizeEvent;
class QStackedLayout;
class PreviewCpuSurface;

namespace rendering {
class OpenGLPreviewSurface;
}

class PreviewWidget final : public QWidget {
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    ~PreviewWidget() override;

    void setFrame(const media::VideoFrame& frame);
    void setFrame(media::VideoFramePtr frame, quint64 delivery_trace_id = 0);
    void setFrame(rendering::PreviewFramePayload frame, quint64 delivery_trace_id = 0);
    void setDeliveryEpoch(quint64 epoch, bool retry_texture_delivery = false) noexcept;
    void releaseGpuFrames();
    [[nodiscard]] rendering::PreviewFramePayload currentPayload() const { return current_payload_; }
    [[nodiscard]] bool textureDeliveryAvailable() const noexcept;
    void clearFrame(const QString& message);
    void setGrayscaleEnabled(bool enabled);
    [[nodiscard]] bool isGrayscaleEnabled() const noexcept;
    [[nodiscard]] bool usesGpuPreview() const noexcept;

signals:
    void gpuFallbackRequested(const QString& reason, qint64 error_code);
    void gpuTextureDeliveryAvailabilityChanged(bool available);

private:
    void resizeEvent(QResizeEvent* event) override;
    void handleGpuFailure(const QString& reason, qint64 error_code);
    void ensureCpuImage();
    void updateCpuPixmap();
    [[nodiscard]] QImage grayscaleImage() const;

    QStackedLayout* stack_ = nullptr;
    rendering::OpenGLPreviewSurface* gpu_surface_ = nullptr;
    PreviewCpuSurface* cpu_surface_ = nullptr;
    media::VideoFramePtr current_frame_;
    rendering::PreviewFramePayload current_payload_;
    quint64 delivery_epoch_ = 0;
    QImage frame_image_;
    QString empty_message_;
    bool grayscale_enabled_ = false;
    bool gpu_enabled_ = false;
};
