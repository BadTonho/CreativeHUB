#include "opengl_preview_surface.h"

#include "preview_performance_metrics.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVersionFunctionsFactory>
#include <QShowEvent>
#include <QSurfaceFormat>
#include <QTimer>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace rendering {
namespace {

constexpr const char* kVertexShader = R"(#version 150 core
in vec2 a_position;
in vec2 a_texcoord;
out vec2 v_texcoord;

void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_texcoord = a_texcoord;
}
)";

constexpr const char* kFragmentShader = R"(#version 150 core
uniform sampler2D u_texture;
uniform bool u_grayscale;

in vec2 v_texcoord;
out vec4 frag_color;

void main() {
    vec4 color = texture(u_texture, v_texcoord);
    if (u_grayscale) {
        float luminance = dot(color.rgb, vec3(0.299, 0.587, 0.114));
        color = vec4(vec3(luminance), color.a);
    }
    frag_color = color;
}
)";

bool hasValidFrame(const media::VideoFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0 || frame.stride < frame.width * 4) {
        return false;
    }

    const auto height = static_cast<std::size_t>(frame.height);
    const auto stride = static_cast<std::size_t>(frame.stride);
    return height <= std::numeric_limits<std::size_t>::max() / stride &&
        frame.rgba_pixels.size() >= height * stride;
}

} // namespace

OpenGLPreviewSurface::OpenGLPreviewSurface(QWidget* parent)
    : QOpenGLWidget(parent) {
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setVersion(3, 2);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    setFormat(format);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    setAutoFillBackground(false);
    setStyleSheet("background-color: #1c2028;");
    connect(this, &QOpenGLWidget::frameSwapped, this, [this]() {
        if (drawn_delivery_trace_id_ == 0 ||
            drawn_delivery_trace_id_ == last_swapped_delivery_trace_id_) {
            return;
        }
        PreviewPerformanceMetrics::instance().recordFrameDeliveryStage(
            drawn_delivery_trace_id_,
            PreviewFrameDeliveryStage::QtFrameSwapped);
        last_swapped_delivery_trace_id_ = drawn_delivery_trace_id_;
    });
}

OpenGLPreviewSurface::~OpenGLPreviewSurface() {
    // QOpenGLWidget destroys its context after this derived object's members.
    // Prevent a cleanup callback from reaching already-destroyed C++ state.
    if (context()) disconnect(context(), nullptr, this, nullptr);
    releaseResources();
}

void OpenGLPreviewSurface::setFrame(
    media::VideoFramePtr frame,
    quint64 delivery_trace_id) {
    setFrame(PreviewFramePayload{std::move(frame)}, delivery_trace_id);
}

void OpenGLPreviewSurface::setFrame(PreviewFramePayload frame, quint64 delivery_trace_id) {
    if (gpu_failed_) return;
    if (frame.gpu && texture_delivery_failed_) {
        if (!retry_texture_delivery_) {
            PreviewPerformanceMetrics::instance().recordFrameDeliveryDrop(delivery_trace_id,
                PreviewFrameDeliveryDropReason::GpuFailure);
            return;
        }
        texture_delivery_failed_ = false;
        retry_texture_delivery_ = false;
    }

    if (!frame.valid() || (frame.rgba && !hasValidFrame(*frame.rgba))) {
        auto& metrics = PreviewPerformanceMetrics::instance();
        metrics.recordFrameDeliveryDrop(
            delivery_trace_id,
            PreviewFrameDeliveryDropReason::InvalidFrame);
        if (pending_frame_valid_) {
            metrics.recordFrameDeliveryDrop(
                pending_delivery_trace_id_,
                PreviewFrameDeliveryDropReason::PreviewOverwritten);
        }
        if (drawn_delivery_trace_id_ != last_swapped_delivery_trace_id_) {
            metrics.recordFrameDeliveryDrop(
                drawn_delivery_trace_id_,
                PreviewFrameDeliveryDropReason::PreviewOverwritten);
        }
        pending_frame_.reset();
        pending_gpu_frame_.reset();
        current_gpu_frame_.reset();
        pending_frame_valid_ = false;
        pending_delivery_trace_id_ = 0;
        frame_available_ = false;
        update();
        return;
    }

    auto& metrics = PreviewPerformanceMetrics::instance();
    if (pending_frame_valid_) {
        metrics.recordOverwrittenFrame();
        metrics.recordFrameDeliveryDrop(
            pending_delivery_trace_id_,
            PreviewFrameDeliveryDropReason::PreviewOverwritten);
    }
    if (drawn_delivery_trace_id_ != last_swapped_delivery_trace_id_) {
        metrics.recordFrameDeliveryDrop(
            drawn_delivery_trace_id_,
            PreviewFrameDeliveryDropReason::PreviewOverwritten);
    }
    pending_frame_ = std::move(frame.rgba);
    pending_gpu_frame_ = std::move(frame.gpu);
    pending_delivery_trace_id_ = delivery_trace_id;
    pending_frame_valid_ = true;
    update();
}

void OpenGLPreviewSurface::clearFrame() {
    texture_delivery_failed_ = false;
    retry_texture_delivery_ = false;
    auto& metrics = PreviewPerformanceMetrics::instance();
    metrics.recordFrameDeliveryDrop(
        pending_delivery_trace_id_,
        PreviewFrameDeliveryDropReason::PreviewOverwritten);
    if (drawn_delivery_trace_id_ != last_swapped_delivery_trace_id_) {
        metrics.recordFrameDeliveryDrop(
            drawn_delivery_trace_id_,
            PreviewFrameDeliveryDropReason::PreviewOverwritten);
    }
    pending_frame_.reset();
    pending_gpu_frame_.reset();
    current_gpu_frame_.reset();
    pending_delivery_trace_id_ = 0;
    pending_frame_valid_ = false;
    frame_available_ = false;
    update();
}

void OpenGLPreviewSurface::setGrayscaleEnabled(bool enabled) {
    grayscale_enabled_ = enabled;
    update();
}

void OpenGLPreviewSurface::initializeGL() {
    if (context() == nullptr) {
        failGpu("OpenGL context creation failed.");
        return;
    }

    functions_ = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_2_Core>(
        context());
    if (functions_ == nullptr || !functions_->initializeOpenGLFunctions()) {
        failGpu("OpenGL 3.2 Core functions are unavailable.");
        return;
    }

    shader_program_ = std::make_unique<QOpenGLShaderProgram>();
    if (!shader_program_->addShaderFromSourceCode(
            QOpenGLShader::Vertex,
            kVertexShader)) {
        failGpu("The OpenGL vertex shader could not be compiled: " +
                shader_program_->log());
        return;
    }
    if (!shader_program_->addShaderFromSourceCode(
            QOpenGLShader::Fragment,
            kFragmentShader)) {
        failGpu("The OpenGL fragment shader could not be compiled: " +
                shader_program_->log());
        return;
    }
    if (!shader_program_->link()) {
        failGpu("The OpenGL preview shader program could not be linked: " +
                shader_program_->log());
        return;
    }

    functions_->glGenVertexArrays(1, &vertex_array_);
    functions_->glGenBuffers(1, &vertex_buffer_);
    functions_->glGenTextures(1, &texture_);
    const auto resource_error = functions_->glGetError();
    if (resource_error != GL_NO_ERROR ||
        vertex_array_ == 0 || vertex_buffer_ == 0 || texture_ == 0) {
        failGpu("OpenGL preview resources could not be created.",
                resource_error);
        return;
    }

    initialized_ = true;
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] {
        emit textureDeliveryAvailabilityChanged(false);
        releaseResources();
        failGpu("The OpenGL preview context was destroyed.");
    }, Qt::DirectConnection);
    functions_->glClearColor(0.1098F, 0.1255F, 0.1569F, 1.0F);
    updateVertexBuffer();
    emit textureDeliveryAvailabilityChanged(textureDeliveryAvailable());
}

bool OpenGLPreviewSurface::textureDeliveryAvailable() const noexcept {
    auto* share = QOpenGLContext::globalShareContext();
    return initialized_ && !gpu_failed_ && context() && share &&
        QOpenGLContext::areSharing(context(), share);
}

void OpenGLPreviewSurface::paintGL() {
    if (gpu_failed_ || !initialized_ || functions_ == nullptr) return;
    if (texture_delivery_failed_ && !pending_frame_) return;

    if (pending_frame_valid_ && !uploadPendingFrame()) return;
    if (!frame_available_ || shader_program_ == nullptr) {
        functions_->glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    auto& metrics = PreviewPerformanceMetrics::instance();
    PreviewPerformanceScope timing(metrics, PreviewTiming::GpuPaint);
    std::string cause;
    std::int64_t code = 0;
    if (current_gpu_frame_) {
        PreviewPerformanceScope sync_timing(metrics, PreviewTiming::GpuFenceWaitSubmission);
        if (!current_gpu_frame_->beginUse(context(), cause, code)) {
            failTextureDelivery(QString::fromStdString(cause), code);
            return;
        }
    }
    functions_->glClear(GL_COLOR_BUFFER_BIT);

    shader_program_->bind();
    shader_program_->setUniformValue("u_texture", 0);
    shader_program_->setUniformValue("u_grayscale", grayscale_enabled_);

    functions_->glActiveTexture(GL_TEXTURE0);
    functions_->glBindTexture(GL_TEXTURE_2D, current_gpu_frame_ ? current_gpu_frame_->texture() : texture_);
    functions_->glBindVertexArray(vertex_array_);
    functions_->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    functions_->glBindVertexArray(0);
    functions_->glBindTexture(GL_TEXTURE_2D, 0);
    shader_program_->release();
    if (current_gpu_frame_) {
        PreviewPerformanceScope sync_timing(metrics, PreviewTiming::GpuConsumerFenceSubmission);
        if (!current_gpu_frame_->endUse(context(), cause, code)) {
            failTextureDelivery(QString::fromStdString(cause), code);
            return;
        }
    }
    metrics.recordGpuPresentedFrame();
    drawn_delivery_trace_id_ = uploaded_delivery_trace_id_;
    metrics.recordFrameDeliveryStage(
        drawn_delivery_trace_id_,
        PreviewFrameDeliveryStage::GpuDrawn);

    const auto error = functions_->glGetError();
    if (error != GL_NO_ERROR) {
        failGpu("OpenGL preview rendering failed.", error);
    }
}

void OpenGLPreviewSurface::resizeGL(int width, int height) {
    if (functions_ == nullptr || gpu_failed_) return;
    functions_->glViewport(0, 0, width, height);
    updateVertexBuffer();
}

void OpenGLPreviewSurface::showEvent(QShowEvent* event) {
    QOpenGLWidget::showEvent(event);
    QTimer::singleShot(0, this, [this]() {
        if (isVisible() && context() == nullptr && !gpu_failed_) {
            failGpu("OpenGL context creation failed.");
        }
    });
}

bool OpenGLPreviewSurface::uploadPendingFrame() {
    if (pending_frame_valid_ && pending_gpu_frame_) {
        current_gpu_frame_ = std::move(pending_gpu_frame_);
        pending_frame_.reset();
        video_width_ = current_gpu_frame_->width();
        video_height_ = current_gpu_frame_->height();
        updateVertexBuffer();
        uploaded_delivery_trace_id_ = pending_delivery_trace_id_;
        pending_delivery_trace_id_ = 0;
        pending_frame_valid_ = false;
        frame_available_ = true;
        PreviewPerformanceMetrics::instance().recordFrameDeliveryStage(
            uploaded_delivery_trace_id_, PreviewFrameDeliveryStage::GpuTextureAccepted);
        return true;
    }
    if (!pending_frame_valid_ || pending_frame_ == nullptr ||
        functions_ == nullptr || texture_ == 0) {
        return true;
    }

    const auto& frame = *pending_frame_;
    if (!hasValidFrame(frame)) {
        pending_frame_.reset();
        pending_delivery_trace_id_ = 0;
        pending_frame_valid_ = false;
        frame_available_ = false;
        return true;
    }

    auto& metrics = PreviewPerformanceMetrics::instance();
    PreviewPerformanceScope timing(metrics, PreviewTiming::GpuUpload);
    const bool orientation_changed = bool(current_gpu_frame_);
    current_gpu_frame_.reset();
    texture_delivery_failed_ = false;
    const bool geometry_changed = orientation_changed || video_width_ != frame.width || video_height_ != frame.height;
    video_width_ = frame.width;
    video_height_ = frame.height;

    const std::uint8_t* pixels = frame.rgba_pixels.data();
    const auto packed_stride = static_cast<std::size_t>(frame.width) * 4U;
    if (static_cast<std::size_t>(frame.stride) != packed_stride) {
        packed_pixels_.resize(packed_stride * static_cast<std::size_t>(frame.height));
        for (int row = 0; row < frame.height; ++row) {
            const auto* source = frame.rgba_pixels.data() +
                static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.stride);
            auto* destination = packed_pixels_.data() +
                static_cast<std::size_t>(row) * packed_stride;
            std::copy(source, source + packed_stride, destination);
        }
        pixels = packed_pixels_.data();
    }

    functions_->glBindTexture(GL_TEXTURE_2D, texture_);
    functions_->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const bool dimensions_changed = texture_width_ != frame.width ||
        texture_height_ != frame.height;
    if (dimensions_changed) {
        functions_->glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            frame.width,
            frame.height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            pixels);
        texture_width_ = frame.width;
        texture_height_ = frame.height;
        video_width_ = frame.width;
        video_height_ = frame.height;
        updateVertexBuffer();
    } else {
        functions_->glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            0,
            0,
            frame.width,
            frame.height,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            pixels);
    }
    functions_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    functions_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    functions_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    functions_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (geometry_changed && !dimensions_changed) updateVertexBuffer();

    const auto error = functions_->glGetError();
    functions_->glBindTexture(GL_TEXTURE_2D, 0);
    if (error != GL_NO_ERROR) {
        failGpu("OpenGL preview texture upload failed.", error);
        return false;
    }

    pending_frame_.reset();
    uploaded_delivery_trace_id_ = pending_delivery_trace_id_;
    metrics.recordViewerUploadBytes(static_cast<std::uint64_t>(video_width_) * video_height_ * 4);
    pending_delivery_trace_id_ = 0;
    pending_frame_valid_ = false;
    frame_available_ = true;
    metrics.recordFrameDeliveryStage(
        uploaded_delivery_trace_id_,
        PreviewFrameDeliveryStage::GpuUploaded);
    return true;
}

void OpenGLPreviewSurface::updateVertexBuffer() {
    if (!initialized_ || functions_ == nullptr || shader_program_ == nullptr ||
        vertex_array_ == 0 || vertex_buffer_ == 0) {
        return;
    }

    const float widget_width = static_cast<float>(width());
    const float widget_height = static_cast<float>(height());
    const float video_aspect = video_height_ > 0
        ? static_cast<float>(video_width_) / static_cast<float>(video_height_)
        : 1.0F;
    const float widget_aspect = widget_height > 0.0F
        ? widget_width / widget_height
        : video_aspect;

    float half_width = 1.0F;
    float half_height = 1.0F;
    if (widget_aspect > video_aspect) {
        half_width = video_aspect / widget_aspect;
    } else if (widget_aspect > 0.0F) {
        half_height = widget_aspect / video_aspect;
    }

    const float bottom = current_gpu_frame_ && current_gpu_frame_->bottomLeftOrigin() ? 0.0F : 1.0F;
    const float top = 1.0F - bottom;
    const float vertices[] = {
        -half_width, -half_height, 0.0F, bottom,
         half_width, -half_height, 1.0F, bottom,
        -half_width,  half_height, 0.0F, top,
         half_width,  half_height, 1.0F, top,
    };

    shader_program_->bind();
    functions_->glBindVertexArray(vertex_array_);
    functions_->glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    functions_->glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(sizeof(vertices)),
        vertices,
        GL_DYNAMIC_DRAW);

    const auto position_location = shader_program_->attributeLocation("a_position");
    const auto texture_location = shader_program_->attributeLocation("a_texcoord");
    if (position_location < 0 || texture_location < 0) {
        functions_->glBindVertexArray(0);
        functions_->glBindBuffer(GL_ARRAY_BUFFER, 0);
        shader_program_->release();
        failGpu("The OpenGL preview shader attributes are unavailable.");
        return;
    }

    functions_->glEnableVertexAttribArray(static_cast<unsigned int>(position_location));
    functions_->glVertexAttribPointer(
        static_cast<unsigned int>(position_location),
        2,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(4U * sizeof(float)),
        nullptr);
    functions_->glEnableVertexAttribArray(static_cast<unsigned int>(texture_location));
    functions_->glVertexAttribPointer(
        static_cast<unsigned int>(texture_location),
        2,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(4U * sizeof(float)),
        reinterpret_cast<const void*>(2U * sizeof(float)));
    functions_->glBindBuffer(GL_ARRAY_BUFFER, 0);
    functions_->glBindVertexArray(0);
    shader_program_->release();

    const auto error = functions_->glGetError();
    if (error != GL_NO_ERROR) failGpu("OpenGL preview geometry setup failed.", error);
}

void OpenGLPreviewSurface::failGpu(const QString& message, unsigned int error_code) {
    if (gpu_failed_) return;
    auto& metrics = PreviewPerformanceMetrics::instance();
    metrics.recordGpuFailure();
    metrics.recordFrameDeliveryDrop(
        pending_delivery_trace_id_ != 0
            ? pending_delivery_trace_id_
            : uploaded_delivery_trace_id_,
        PreviewFrameDeliveryDropReason::GpuFailure);
    pending_delivery_trace_id_ = 0;
    gpu_failed_ = true;
    initialized_ = false;
    emit textureDeliveryAvailabilityChanged(false);
    emit gpuFailure(message, static_cast<qint64>(error_code));
}

void OpenGLPreviewSurface::failTextureDelivery(const QString& message, qint64 error_code) {
    if (texture_delivery_failed_) return;
    texture_delivery_failed_ = true;
    retry_texture_delivery_ = false;
    auto& metrics = PreviewPerformanceMetrics::instance();
    metrics.recordGpuFailure();
    metrics.recordTextureDeliveryFallback();
    metrics.recordFrameDeliveryDrop(uploaded_delivery_trace_id_, PreviewFrameDeliveryDropReason::GpuFailure);
    emit gpuTextureFailure(message, error_code);
}

void OpenGLPreviewSurface::releaseResources() {
    pending_gpu_frame_.reset();
    current_gpu_frame_.reset();
    if (context() == nullptr || functions_ == nullptr) return;

    makeCurrent();
    if (texture_ != 0) functions_->glDeleteTextures(1, &texture_);
    if (vertex_buffer_ != 0) functions_->glDeleteBuffers(1, &vertex_buffer_);
    if (vertex_array_ != 0) functions_->glDeleteVertexArrays(1, &vertex_array_);
    texture_ = 0;
    vertex_buffer_ = 0;
    vertex_array_ = 0;
    shader_program_.reset();
    initialized_ = false;
    functions_ = nullptr;
    doneCurrent();
}

} // namespace rendering
