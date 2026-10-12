#pragma once
#include <creative_suite/composition/opengl_frame_compositor.h>
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>
#include <QPainter>
#include <functional>

namespace image_editor {
// Input remains owned by ImageCanvas. This transparent-to-input child only
// presents leased textures and the existing tool overlays.
class ImageGpuPresenter final : public QOpenGLWidget, protected QOpenGLFunctions_3_2_Core {
public:
    explicit ImageGpuPresenter(QWidget* parent);
    ~ImageGpuPresenter() override;
    void setFrame(creative_suite::composition::OpenGlTextureFramePtr frame);
    std::function<QRectF()> targetRect;
    std::function<void(QPainter&)> paintOverlays;
    std::function<void(const QString&)> failed;
protected:
    void showEvent(QShowEvent* event) override;
    void initializeGL() override;
    void paintGL() override;
private:
    creative_suite::composition::OpenGlTextureFramePtr frame_;
    std::unique_ptr<QOpenGLShaderProgram> shader_;
    GLuint vao_ = 0;
    bool failed_ = false;
    void fail(const QString& cause);
};
}
