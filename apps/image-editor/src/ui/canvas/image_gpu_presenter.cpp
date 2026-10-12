#include "image_gpu_presenter.h"
#include <QOpenGLContext>
#include <QTimer>

namespace image_editor {
ImageGpuPresenter::ImageGpuPresenter(QWidget* parent) : QOpenGLWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
}
ImageGpuPresenter::~ImageGpuPresenter() {
    if(context()) QObject::disconnect(context(),nullptr,this,nullptr);
    if (context() && context()->isValid()) {
        makeCurrent(); shader_.reset(); if (vao_) glDeleteVertexArrays(1, &vao_); doneCurrent();
    }
}
void ImageGpuPresenter::setFrame(creative_suite::composition::OpenGlTextureFramePtr frame) { frame_ = std::move(frame); update(); }
void ImageGpuPresenter::fail(const QString& cause) {
    if (failed_) return; failed_ = true;
    QTimer::singleShot(0, this, [this, cause] { if (failed) failed(cause); });
}
void ImageGpuPresenter::showEvent(QShowEvent* event) {
    QOpenGLWidget::showEvent(event);
    QTimer::singleShot(0, this, [this] {
        if (isVisible() && !isValid()) fail(QStringLiteral("The canvas OpenGL context could not be created."));
    });
}
void ImageGpuPresenter::initializeGL() {
    if (!initializeOpenGLFunctions()) { fail(QStringLiteral("Canvas OpenGL functions are unavailable.")); return; }
    constexpr char vertex[] = R"GLSL(#version 150 core
void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0, 1); }
)GLSL";
    constexpr char fragment[] = R"GLSL(#version 150 core
uniform sampler2D image;
uniform vec4 target;
uniform float viewport_height;
uniform float ratio;
out vec4 color;
void main() {
    vec2 point = vec2(gl_FragCoord.x, viewport_height - gl_FragCoord.y) / ratio;
    vec2 uv = (point - target.xy) / target.zw;
    color = vec4(34.0/255.0,37.0/255.0,43.0/255.0,1);
    if (uv.x < 0 || uv.y < 0 || uv.x > 1 || uv.y > 1) {
        if (point.x >= target.x-2 && point.y >= target.y-2 &&
            point.x <= target.x+target.z+2 && point.y <= target.y+target.w+2)
            color = vec4(18.0/255.0,19.0/255.0,22.0/255.0,1);
        return;
    }
    int checker = (int(floor(point.x / 16.0)) + int(floor(point.y / 16.0))) & 1;
    vec3 background = checker == 0 ? vec3(205,208,214)/255.0 : vec3(158,162,170)/255.0;
    vec4 pixel = texture(image, vec2(uv.x, 1.0 - uv.y));
    color = vec4(pixel.rgb + background * (1.0 - pixel.a), 1);
})GLSL";
    shader_ = std::make_unique<QOpenGLShaderProgram>();
    if (!shader_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex) ||
        !shader_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) || !shader_->link()) {
        fail(QStringLiteral("Canvas shader compilation failed: %1").arg(shader_->log())); return;
    }
    glGenVertexArrays(1, &vao_);
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] {
        frame_.reset(); fail(QStringLiteral("The canvas OpenGL context was lost."));
    });
}
void ImageGpuPresenter::paintGL() {
    if (failed_ || !shader_ || !frame_ || !targetRect) return;
    std::string cause; std::int64_t code = 0;
    if (!frame_->beginUse(context(), cause, code)) { fail(QString::fromStdString(cause)); return; }
    const QRectF target = targetRect();
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB); glBindVertexArray(vao_);
    shader_->bind(); shader_->setUniformValue("image", 0);
    shader_->setUniformValue("target", QVector4D(float(target.x()),float(target.y()),float(target.width()),float(target.height())));
    shader_->setUniformValue("viewport_height", float(height()*devicePixelRatioF()));
    shader_->setUniformValue("ratio", float(devicePixelRatioF()));
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, frame_->texture()); glDrawArrays(GL_TRIANGLES,0,3);
    shader_->release(); glBindVertexArray(0);
    if (!frame_->endUse(context(), cause, code)) { fail(QString::fromStdString(cause)); return; }
    if (paintOverlays) { QPainter painter(this); paintOverlays(painter); }
}
}
