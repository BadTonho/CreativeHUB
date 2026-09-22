#pragma once

#include "media/video_frame.h"

#include <QImage>
#include <QString>
#include <QWidget>

class QLabel;
class QResizeEvent;
class QStackedLayout;

namespace rendering {
class OpenGLPreviewSurface;
}

class PreviewWidget final : public QWidget {
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    void setFrame(const media::VideoFrame& frame);
    void setFrame(media::VideoFramePtr frame);
    void clearFrame(const QString& message);
    void setGrayscaleEnabled(bool enabled);
    [[nodiscard]] bool isGrayscaleEnabled() const noexcept;
    [[nodiscard]] bool usesGpuPreview() const noexcept;

signals:
    void gpuFallbackRequested(const QString& reason, qint64 error_code);

private:
    void resizeEvent(QResizeEvent* event) override;
    void handleGpuFailure(const QString& reason, qint64 error_code);
    void ensureCpuImage();
    void updateCpuPixmap();
    [[nodiscard]] QImage grayscaleImage() const;

    QStackedLayout* stack_ = nullptr;
    rendering::OpenGLPreviewSurface* gpu_surface_ = nullptr;
    QLabel* cpu_surface_ = nullptr;
    media::VideoFramePtr current_frame_;
    QImage frame_image_;
    QString empty_message_;
    bool grayscale_enabled_ = false;
    bool gpu_enabled_ = false;
};
