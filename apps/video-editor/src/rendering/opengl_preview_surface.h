#pragma once

#include "media/video_frame.h"

#include <QOpenGLWidget>
#include <QString>
#include <QtGlobal>

#include <memory>
#include <vector>

class QOpenGLFunctions_3_2_Core;
class QOpenGLShaderProgram;
class QShowEvent;

namespace rendering {

class OpenGLPreviewSurface final : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit OpenGLPreviewSurface(QWidget* parent = nullptr);
    ~OpenGLPreviewSurface() override;

    void setFrame(media::VideoFramePtr frame, quint64 delivery_trace_id = 0);
    void clearFrame();
    void setGrayscaleEnabled(bool enabled);

signals:
    void gpuFailure(const QString& message, qint64 error_code);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;
    void showEvent(QShowEvent* event) override;

private:
    bool uploadPendingFrame();
    void updateVertexBuffer();
    void failGpu(const QString& message, unsigned int error_code = 0);
    void releaseResources();

    QOpenGLFunctions_3_2_Core* functions_ = nullptr;
    std::unique_ptr<QOpenGLShaderProgram> shader_program_;
    unsigned int vertex_array_ = 0;
    unsigned int vertex_buffer_ = 0;
    unsigned int texture_ = 0;
    int texture_width_ = 0;
    int texture_height_ = 0;
    int video_width_ = 0;
    int video_height_ = 0;
    media::VideoFramePtr pending_frame_;
    quint64 pending_delivery_trace_id_ = 0;
    quint64 uploaded_delivery_trace_id_ = 0;
    quint64 drawn_delivery_trace_id_ = 0;
    quint64 last_swapped_delivery_trace_id_ = 0;
    std::vector<std::uint8_t> packed_pixels_;
    bool pending_frame_valid_ = false;
    bool frame_available_ = false;
    bool grayscale_enabled_ = false;
    bool initialized_ = false;
    bool gpu_failed_ = false;
};

} // namespace rendering
