#include "composition_viewer.h"
#include "diagnostics/performance_metrics.h"

#include <QContextMenuEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QColor>
#include <QPen>
#include <QSizeF>
#include <QOpenGLWidget>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLShaderProgram>
#include <QResizeEvent>
#include <QTimer>
#include <creative_suite/diagnostics/logger.h>
#include <chrono>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace motion::ui {
namespace {

const QColor kSurroundColor(45, 48, 54);
const QColor kCanvasColor(224, 226, 230);
const QColor kCanvasBorderColor(24, 26, 30);
const QColor kGuideColor(255, 183, 54);

} // namespace

class GpuCompositionSurface final : public QOpenGLWidget {
public:
    explicit GpuCompositionSurface(CompositionViewer* owner) : QOpenGLWidget(owner), owner_(owner) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setObjectName(QStringLiteral("motion-gpu-composition-surface"));
    }
    ~GpuCompositionSurface() override {
        if (context()) QObject::disconnect(context(), nullptr, this, nullptr);
        cleanup();
    }
    bool available() const noexcept {
        auto* share = QOpenGLContext::globalShareContext();
        return functions_ && shader_ && context() && share &&
            QOpenGLContext::areSharing(context(), share) && !owner_->texture_failed_;
    }
protected:
    void showEvent(QShowEvent* event) override {
        QOpenGLWidget::showEvent(event);
        QTimer::singleShot(0, this, [this] {
            if (isVisible() && !available() && !owner_->texture_failed_)
                fail("initialize_viewer", "A sharing OpenGL viewer context is unavailable.", 0, !context() || !functions_ || !shader_);
        });
    }
    void initializeGL() override {
        functions_ = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_2_Core>(context());
        if (!functions_ || !functions_->initializeOpenGLFunctions()) {
            fail("initialize_viewer", "OpenGL 3.2 Core viewer functions are unavailable.", 0, true);
            return;
        }
        shader_ = std::make_unique<QOpenGLShaderProgram>();
        const char* vertex = R"(#version 150
            uniform vec4 target;
            out vec2 uv;
            void main() {
                vec2 p = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
                uv = p;
                gl_Position = vec4(mix(target.xy, target.zw, p), 0, 1);
            })";
        const char* fragment = R"(#version 150
            uniform sampler2D source;
            in vec2 uv;
            out vec4 color;
            void main() {
                ivec2 size = textureSize(source, 0);
                color = texelFetch(source, clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1), 0);
            })";
        if (!shader_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex) ||
            !shader_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) || !shader_->link()) {
            fail("link_viewer_shader", shader_->log().toStdString(), 0, true);
            return;
        }
        functions_->glGenVertexArrays(1, &vao_);
        const auto error = functions_->glGetError();
        if (error || !vao_) fail("create_viewer_resources", "OpenGL viewer allocation failed.", error, true);
        connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] {
            fail("destroy_viewer_context", "The OpenGL viewer context was destroyed.", 0, true);
            cleanup();
        }, Qt::DirectConnection);
    }
    void paintGL() override {
        if (!available()) return;
        auto frame = owner_->preview_frame_;
        if (frame.valid() && owner_->frame_validator_ && !owner_->frame_validator_(frame)) {
            if (owner_->rendered_frame_paint_pending_)
                diagnostics::PerformanceMetrics::instance().recordStaleResult(frame.generation);
            owner_->clearPreviewFrame();
            return;
        }
        QPainter painter(this);
        owner_->paintBackground(painter);
        if (frame.texture && frame.texture->valid() && !owner_->canvasRect().isEmpty()) {
            using Clock = std::chrono::steady_clock;
            const auto elapsed = [](auto start) { return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()); };
            auto& metrics = diagnostics::PerformanceMetrics::instance();
            painter.beginNativePainting();
            std::string cause;
            std::int64_t code = 0;
            auto started = Clock::now();
            if (!frame.texture->beginUse(context(), cause, code)) {
                painter.endNativePainting(); fail("wait_texture", cause, code, false); return;
            }
            metrics.recordTiming(diagnostics::PreviewTimingStage::GpuViewerWaitSubmission, elapsed(started));
            started = Clock::now();
            functions_->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
            functions_->glViewport(0, 0, qRound(width() * devicePixelRatioF()), qRound(height() * devicePixelRatioF()));
            functions_->glDisable(GL_BLEND);
            functions_->glDisable(GL_DEPTH_TEST);
            functions_->glDisable(GL_SCISSOR_TEST);
            const auto r = owner_->canvasRect();
            shader_->bind();
            shader_->setUniformValue("target", QVector4D(
                float(2 * r.left() / width() - 1), float(1 - 2 * r.bottom() / height()),
                float(2 * r.right() / width() - 1), float(1 - 2 * r.top() / height())));
            shader_->setUniformValue("source", 0);
            functions_->glActiveTexture(GL_TEXTURE0);
            functions_->glBindTexture(GL_TEXTURE_2D, frame.texture->texture());
            functions_->glBindVertexArray(vao_);
            functions_->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            functions_->glBindVertexArray(0);
            functions_->glBindTexture(GL_TEXTURE_2D, 0);
            shader_->release();
            metrics.recordTiming(diagnostics::PreviewTimingStage::GpuViewerDrawSubmission, elapsed(started));
            started = Clock::now();
            const bool consumed = frame.texture->endUse(context(), cause, code);
            metrics.recordTiming(diagnostics::PreviewTimingStage::GpuViewerFenceSubmission, elapsed(started));
            const auto error = functions_->glGetError();
            painter.endNativePainting();
            if (!consumed) { fail("consume_texture", cause, code, false); return; }
            if (error) { fail("draw_texture", "OpenGL viewer draw failed.", error, true); return; }
            owner_->paintOverlay(painter);
            owner_->recordPaint(true);
        } else owner_->paintOverlay(painter);
    }
private:
    void fail(const char* operation, const std::string& cause, std::int64_t code, bool cpu) {
        if (owner_->texture_failed_) return;
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error, "motion_preview", operation, cause,
            {{"error_code", std::to_string(code)}, {"generation", std::to_string(owner_->preview_frame_.generation)}});
        owner_->texture_failed_ = true;
        QTimer::singleShot(0, owner_, [owner = owner_, cpu] { owner->recoverTexturePresentation(cpu); });
    }
    void cleanup() {
        if (!context() || !context()->isValid()) { shader_.reset(); functions_ = nullptr; return; }
        makeCurrent();
        if (functions_) {
            if (vao_) functions_->glDeleteVertexArrays(1, &vao_);
        }
        vao_ = 0;
        shader_.reset();
        functions_ = nullptr;
        doneCurrent();
    }
    CompositionViewer* owner_;
    QOpenGLFunctions_3_2_Core* functions_ = nullptr;
    std::unique_ptr<QOpenGLShaderProgram> shader_;
    unsigned vao_ = 0;
};

CompositionViewer::~CompositionViewer() {
    recovery_handler_ = {};
    clearPreviewFrame();
    delete gpu_surface_;
    gpu_surface_ = nullptr;
}
void CompositionViewer::enableTexturePresentation(std::function<void(bool)> handler) {
    recovery_handler_ = std::move(handler);
    if (gpu_surface_) return;
    gpu_surface_ = new GpuCompositionSurface(this);
    gpu_surface_->setGeometry(rect());
    gpu_surface_->show();
}
void CompositionViewer::setFrameValidator(std::function<bool(const PreviewFrame&)> validator) {
    frame_validator_ = std::move(validator);
}
void CompositionViewer::setPreviewFrame(PreviewFrame frame) {
    if (!frame.valid()) { clearPreviewFrame(); return; }
    if (frame.rgba) {
        const auto rgba = frame.rgba;
        setRenderedFrame(rgba, frame.generation);
        preview_frame_ = std::move(frame);
        return;
    }
    if (!gpu_surface_ || texture_failed_) {
        // Capability failure is asynchronous; never read back on the GUI thread.
        recoverTexturePresentation(false);
        return;
    }
    rendered_frame_.reset();
    rendered_frame_generation_ = frame.generation;
    rendered_frame_paint_pending_ = frame.generation != 0;
    preview_frame_ = std::move(frame);
    gpu_surface_->show();
    gpu_surface_->update();
}
void CompositionViewer::clearPreviewFrame() {
    preview_frame_ = {};
    rendered_frame_.reset();
    rendered_frame_paint_pending_ = false;
    rendered_frame_generation_ = 0;
    if (gpu_surface_) { gpu_surface_->hide(); gpu_surface_->update(); }
    update();
}
void CompositionViewer::recoverTexturePresentation(bool use_cpu) {
    texture_failed_ = true;
    clearPreviewFrame();
    if (recovery_handler_) recovery_handler_(use_cpu);
}
bool CompositionViewer::texturePresentationAvailable() const noexcept {
    return gpu_surface_ && gpu_surface_->available();
}
std::uint64_t CompositionViewer::lastPresentedGeneration() const noexcept {
    return last_presented_generation_;
}
void CompositionViewer::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (gpu_surface_) gpu_surface_->setGeometry(rect());
}

CompositionViewer::CompositionViewer(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("motion-composition-viewer"));
    setMinimumSize(180, 240);
    setAutoFillBackground(false);
}

void CompositionViewer::setComposition(
    model::CanvasSize canvas_size,
    std::optional<QPointF> selected_layer_anchor)
{
    clearPreviewFrame();
    canvas_size_ = canvas_size;
    selected_layer_anchor_ = std::move(selected_layer_anchor);
    rendered_frame_.reset();
    rendered_frame_generation_ = 0;
    rendered_frame_paint_pending_ = false;
    update();
}

void CompositionViewer::setRenderedFrame(
    creative_suite::media::RgbaFramePtr frame,
    std::uint64_t request_generation)
{
    clearPreviewFrame();
    preview_frame_.rgba = frame;
    preview_frame_.generation = request_generation;
    rendered_frame_ = std::move(frame);
    rendered_frame_generation_ = request_generation;
    rendered_frame_paint_pending_ = rendered_frame_ != nullptr && request_generation != 0;
    update();
}

void CompositionViewer::setSelectedLayerAnchor(std::optional<QPointF> selected_layer_anchor)
{
    selected_layer_anchor_ = std::move(selected_layer_anchor);
    if (gpu_surface_) gpu_surface_->update();
    update();
}

creative_suite::media::RgbaFramePtr CompositionViewer::renderedFrame() const noexcept
{
    return rendered_frame_;
}

void CompositionViewer::setLayerContextMenuHandler(
    std::function<void(const QPoint&)> handler)
{
    layer_context_menu_handler_ = std::move(handler);
}

QRectF CompositionViewer::canvasRect() const
{
    if (canvas_size_.width <= 0 || canvas_size_.height <= 0) return {};

    const QRectF available = QRectF(rect()).adjusted(24.0, 42.0, -24.0, -24.0);
    if (available.width() <= 0.0 || available.height() <= 0.0) return {};

    const double scale = std::min(
        available.width() / static_cast<double>(canvas_size_.width),
        available.height() / static_cast<double>(canvas_size_.height));
    const QSizeF canvas_size(
        static_cast<double>(canvas_size_.width) * scale,
        static_cast<double>(canvas_size_.height) * scale);
    return QRectF(
        available.center().x() - canvas_size.width() / 2.0,
        available.center().y() - canvas_size.height() / 2.0,
        canvas_size.width(),
        canvas_size.height());
}

void CompositionViewer::contextMenuEvent(QContextMenuEvent* event)
{
    if (event == nullptr || !layer_context_menu_handler_ ||
        !canvasRect().contains(event->pos())) {
        if (event != nullptr) event->ignore();
        return;
    }

    layer_context_menu_handler_(event->globalPos());
    event->accept();
}

void CompositionViewer::paintBackground(QPainter& painter) {
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), kSurroundColor);
    const auto canvas = canvasRect();
    if (canvas.isEmpty()) return;
    painter.setPen(Qt::NoPen);
    painter.setBrush(kCanvasColor);
    painter.drawRect(canvas);
}
void CompositionViewer::paintOverlay(QPainter& painter) {
    const auto canvas_rect = canvasRect();
    if (canvas_rect.isEmpty()) return;
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(kCanvasBorderColor, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(canvas_rect);
    painter.setPen(QColor(235, 237, 240));
    painter.drawText(QRectF(16.0, 12.0, width() - 32.0, 20.0),
        Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("%1 x %2 px").arg(canvas_size_.width).arg(canvas_size_.height));
    if (!selected_layer_anchor_) return;
    const QPointF guide(canvas_rect.left() + selected_layer_anchor_->x() * canvas_rect.width(),
                        canvas_rect.top() + selected_layer_anchor_->y() * canvas_rect.height());
    if (!std::isfinite(guide.x()) || !std::isfinite(guide.y()) || !rect().contains(guide.toPoint())) return;
    painter.setPen(QPen(kGuideColor, 2.0));
    painter.drawLine(guide + QPointF(-9, 0), guide + QPointF(9, 0));
    painter.drawLine(guide + QPointF(0, -9), guide + QPointF(0, 9));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(guide, 3.5, 3.5);
}
void CompositionViewer::recordPaint(bool texture) {
    if (!rendered_frame_paint_pending_) return;
    rendered_frame_paint_pending_ = false;
    last_presented_generation_ = rendered_frame_generation_;
    auto& metrics = diagnostics::PerformanceMetrics::instance();
    metrics.recordPresentation(texture);
    metrics.recordViewerPaint(rendered_frame_generation_);
}
void CompositionViewer::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    paintBackground(painter);
    if (preview_frame_.valid() && frame_validator_ && !frame_validator_(preview_frame_)) {
        if (rendered_frame_paint_pending_)
            diagnostics::PerformanceMetrics::instance().recordStaleResult(preview_frame_.generation);
        clearPreviewFrame();
    }
    if (rendered_frame_ && rendered_frame_->width > 0 &&
        rendered_frame_->width <= std::numeric_limits<int>::max() / 4 &&
        rendered_frame_->height > 0 && rendered_frame_->stride >= rendered_frame_->width * 4 &&
        rendered_frame_->rgba_pixels.size() >= std::size_t(rendered_frame_->stride) * rendered_frame_->height) {
        QImage image(rendered_frame_->rgba_pixels.data(), rendered_frame_->width,
                     rendered_frame_->height, rendered_frame_->stride, QImage::Format_RGBA8888);
        painter.drawImage(canvasRect(), image);
        recordPaint(false);
    }
    paintOverlay(painter);
}

} // namespace motion::ui
