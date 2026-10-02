#include <creative_suite/composition/opengl_frame_compositor.h>

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVersionFunctionsFactory>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace creative_suite::composition {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::uint64_t maximum_texture_bytes = 256ULL * 1024 * 1024;
std::uint64_t elapsed(Clock::time_point start) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
}
OpenGlCompositionResult result(OpenGlCompositionStatus status,
    const char* operation = "", std::string cause = {}, std::int64_t code = 0) {
    return {status, {}, operation, std::move(cause), code};
}
bool usable(const CompositionLayer& layer) {
    if (!layer.frame || !animation::validTransform(layer.transform)) return false;
    const auto& f = *layer.frame;
    return f.width > 0 && f.height > 0 &&
        static_cast<std::int64_t>(f.stride) >= static_cast<std::int64_t>(f.width) * 4 &&
        static_cast<std::uint64_t>(f.stride) * f.height <= f.rgba_pixels.size();
}
bool bounded(int width, int height, int limit) {
    return width > 0 && height > 0 && width <= limit && height <= limit &&
        static_cast<std::uint64_t>(width) * height * 4 <= maximum_texture_bytes;
}
constexpr char vertex_shader[] = R"GLSL(#version 150 core
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";
constexpr char fragment_shader[] = R"GLSL(
uniform sampler2D source_image;
uniform float canvas_height;
uniform GEOMETRY_VEC2 center;
uniform GEOMETRY_VEC2 displayed_size;
uniform GEOMETRY_VEC2 rotation_cs;
uniform float opacity;
uniform bool axis_aligned;
uniform int lookup_y_offset;
layout(std140) uniform SourceLookup { ivec4 lookup[1024]; };
out vec4 color;
int mapped(int i) { return lookup[i / 4][i % 4]; }
void main() {
    ivec2 size = textureSize(source_image, 0);
    ivec2 pixel;
    if (axis_aligned) {
        pixel = ivec2(mapped(int(gl_FragCoord.x)),
            mapped(lookup_y_offset + int(canvas_height - gl_FragCoord.y)));
        if (any(lessThan(pixel, ivec2(0)))) discard;
    } else {
        PRECISE GEOMETRY_VEC2 d = GEOMETRY_VEC2(gl_FragCoord.x, canvas_height - gl_FragCoord.y) - center;
        PRECISE GEOMETRY_VEC2 local = GEOMETRY_VEC2(rotation_cs.x * d.x + rotation_cs.y * d.y,
                     -rotation_cs.y * d.x + rotation_cs.x * d.y);
        if (any(greaterThan(abs(local), displayed_size * 0.5))) discard;
        PRECISE GEOMETRY_VEC2 sample_position = (local / displayed_size + 0.5) * GEOMETRY_VEC2(size);
        pixel = clamp(ivec2(floor(sample_position)),
                       ivec2(0), size - ivec2(1));
    }
    vec4 source = texelFetch(source_image, pixel, 0);
    color = vec4(source.rgb, source.a * opacity);
}
)GLSL";
} // namespace

struct OpenGlFrameCompositor::Impl {
    QOffscreenSurface* surface;
    std::unique_ptr<QOpenGLContext> context;
    QOpenGLFunctions_3_2_Core* gl = nullptr;
    std::unique_ptr<QOpenGLShaderProgram> program;
    std::unique_ptr<QOpenGLFramebufferObject> output;
    GLuint texture = 0;
    GLuint vao = 0;
    GLuint lookup_buffer = 0;
    std::vector<GLint> source_lookup;
    int source_width = 0;
    int source_height = 0;
    int texture_limit = 0;
    bool precise_geometry = false;
    using Uniform2dv = void (QOPENGLF_APIENTRYP)(GLint, GLsizei, const GLdouble*);
    Uniform2dv uniform2dv = nullptr;
    OpenGlPrecisionPolicy precision;

    explicit Impl(QOffscreenSurface* s, OpenGlPrecisionPolicy policy) : surface(s), precision(policy) {}
    ~Impl() {
        if (context && context->makeCurrent(surface)) {
            output.reset();
            program.reset();
            if (texture) gl->glDeleteTextures(1, &texture);
            if (vao) gl->glDeleteVertexArrays(1, &vao);
            if (lookup_buffer) gl->glDeleteBuffers(1, &lookup_buffer);
            context->doneCurrent();
        }
    }
    OpenGlCompositionResult initialize() {
        if (!surface || !surface->isValid() || !QOpenGLContext::supportsThreadedOpenGL())
            return result(OpenGlCompositionStatus::Failed, "create-context",
                "A valid offscreen surface and threaded OpenGL support are required.");
        context = std::make_unique<QOpenGLContext>();
        auto requested = surface->format();
        requested.setRenderableType(QSurfaceFormat::OpenGL);
        requested.setVersion(3, 2);
        requested.setProfile(QSurfaceFormat::CoreProfile);
        context->setFormat(requested);
        if (!context->create() || !context->makeCurrent(surface))
            return result(OpenGlCompositionStatus::Failed, "create-context",
                "Cannot create or activate the worker OpenGL context.");
        const auto format = context->format();
        if (format.renderableType() != QSurfaceFormat::OpenGL ||
            format.profile() != QSurfaceFormat::CoreProfile ||
            format.majorVersion() < 3 ||
            (format.majorVersion() == 3 && format.minorVersion() < 2))
            return result(OpenGlCompositionStatus::Failed, "check-context",
                "OpenGL 3.2 Core is unavailable.");
        gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_2_Core>(context.get());
        if (!gl || !gl->initializeOpenGLFunctions())
            return result(OpenGlCompositionStatus::Failed, "load-functions",
                "Cannot initialize OpenGL 3.2 Core functions.");
        precise_geometry = precision == OpenGlPrecisionPolicy::Automatic &&
            context->hasExtension("GL_ARB_gpu_shader_fp64") &&
            context->hasExtension("GL_ARB_gpu_shader5");
        if (precise_geometry) {
            uniform2dv = reinterpret_cast<Uniform2dv>(context->getProcAddress("glUniform2dv"));
            if (!uniform2dv) return result(OpenGlCompositionStatus::Failed, "load-functions",
                "The driver advertises precise geometry but cannot load glUniform2dv.");
        }
        const QByteArray fragment = QByteArray("#version 150 core\n") +
            (precise_geometry ?
                "#extension GL_ARB_gpu_shader_fp64 : require\n#extension GL_ARB_gpu_shader5 : require\n"
                "#define GEOMETRY_VEC2 dvec2\n#define PRECISE precise\n" :
                "#define GEOMETRY_VEC2 vec2\n#define PRECISE\n") + fragment_shader;
        program = std::make_unique<QOpenGLShaderProgram>();
        if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex_shader) ||
            !program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) ||
            !program->link())
            return result(OpenGlCompositionStatus::Failed, "compile-shaders",
                program->log().toStdString());
        gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_limit);
        gl->glGenTextures(1, &texture);
        gl->glGenVertexArrays(1, &vao);
        gl->glGenBuffers(1, &lookup_buffer);
        gl->glBindBuffer(GL_UNIFORM_BUFFER, lookup_buffer);
        gl->glBufferData(GL_UNIFORM_BUFFER, 16384, nullptr, GL_DYNAMIC_DRAW);
        const auto block = gl->glGetUniformBlockIndex(program->programId(), "SourceLookup");
        gl->glUniformBlockBinding(program->programId(), block, 0);
        return result(OpenGlCompositionStatus::Complete);
    }
};

std::unique_ptr<QOffscreenSurface> OpenGlFrameCompositor::createSurface() {
    auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (!app || QThread::currentThread() != app->thread()) return {};
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(0);
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(0);
    auto surface = std::make_unique<QOffscreenSurface>();
    // Surface creation resolves the native buffer configuration on the GUI
    // thread. Only the worker creates a context, using that resolved format.
    surface->setFormat(format);
    surface->create();
    return surface->isValid() ? std::move(surface) : nullptr;
}

OpenGlFrameCompositor::OpenGlFrameCompositor(QOffscreenSurface* surface, OpenGlPrecisionPolicy precision)
    : impl_(std::make_unique<Impl>(surface, precision)) {}
OpenGlFrameCompositor::~OpenGlFrameCompositor() = default;

OpenGlCompositionResult OpenGlFrameCompositor::compose(int width, int height,
    const std::vector<CompositionLayer>& layers, const CancellationPredicate& cancel,
    OpenGlCompositionTimings* timings) {
    OpenGlCompositionTimings measured;
    struct TimingsGuard {
        OpenGlCompositionTimings* output;
        const OpenGlCompositionTimings& measured;
        ~TimingsGuard() { if (output) *output = measured; }
    } timings_guard{timings, measured};
    if (timings) *timings = {};
    const auto cancelled = [&] { return cancel && cancel(); };
    if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
    auto& p = *impl_;
    struct CurrentGuard {
        Impl& p;
        ~CurrentGuard() { if (p.context) p.context->doneCurrent(); }
    } guard{p};
    try {
        if (!p.program || !p.program->isLinked()) {
            // A failed instance is discarded by its caller, never reused.
            if (p.context) return result(OpenGlCompositionStatus::Failed,
                "initialize", "The previous context initialization failed.");
            auto initialized = p.initialize();
            if (initialized.status != OpenGlCompositionStatus::Complete) return initialized;
        } else if (!p.context->makeCurrent(p.surface)) {
            return result(OpenGlCompositionStatus::Failed, "make-current",
                "Cannot reactivate the worker OpenGL context.");
        }
        auto* gl = p.gl;
        if (!bounded(width, height, p.texture_limit))
            return result(OpenGlCompositionStatus::Unsupported, "check-limits",
                "Canvas exceeds the device texture limit or 256 MiB texture budget.");
        for (const auto& layer : layers) {
            if (usable(layer) && layer.transform.opacity > 0 &&
                layer.transform.rotation_degrees != 0 && !p.precise_geometry)
                return result(OpenGlCompositionStatus::Unsupported, "check-precision",
                    "Exact rotated nearest sampling requires ARB_gpu_shader_fp64 and ARB_gpu_shader5.");
            if (usable(layer) && !bounded(layer.frame->width, layer.frame->height, p.texture_limit))
                return result(OpenGlCompositionStatus::Unsupported, "check-limits",
                    "Source exceeds the device texture limit or 256 MiB texture budget.");
            if (usable(layer) && layer.transform.rotation_degrees == 0.0 &&
                static_cast<std::int64_t>(width) + height > 4096)
                return result(OpenGlCompositionStatus::Unsupported, "check-limits",
                    "Exact nearest sampling exceeds the 4096-entry geometry lookup budget.");
            if (usable(layer)) {
                const auto& t = layer.transform;
                const auto& f = *layer.frame;
                const double fit = std::min(static_cast<double>(width) / f.width,
                    static_cast<double>(height) / f.height);
                const double displayed_width = f.width * fit * t.scale;
                const double displayed_height = f.height * fit * t.scale;
                const auto representable = [](double value) {
                    return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max();
                };
                if (!representable(t.position_x * width) || !representable(t.position_y * height) ||
                    !representable(displayed_width) || !representable(displayed_height) ||
                    static_cast<float>(displayed_width) <= 0 || static_cast<float>(displayed_height) <= 0)
                    return result(OpenGlCompositionStatus::Unsupported, "check-geometry",
                        "Layer geometry cannot be represented by the GPU shader.");
            }
        }
        if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
        if (!p.output || p.output->width() != width || p.output->height() != height) {
            QOpenGLFramebufferObjectFormat format;
            format.setInternalTextureFormat(GL_RGBA8);
            p.output = std::make_unique<QOpenGLFramebufferObject>(width, height, format);
            if (!p.output->isValid()) return result(OpenGlCompositionStatus::Failed,
                "allocate-framebuffer", "Cannot allocate the output RGBA8 framebuffer.");
        }
        const auto setup_started = Clock::now();
        if (!p.output->bind() || !p.program->bind())
            return result(OpenGlCompositionStatus::Failed, "bind-output",
                "Cannot bind the output framebuffer or shader program.");
        gl->glViewport(0, 0, width, height);
        gl->glDisable(GL_DEPTH_TEST);
        gl->glDisable(GL_STENCIL_TEST);
        gl->glDisable(GL_SCISSOR_TEST);
        gl->glDisable(GL_CULL_FACE);
        gl->glDisable(GL_DITHER);
        gl->glDisable(GL_FRAMEBUFFER_SRGB);
        gl->glDisable(GL_MULTISAMPLE);
        gl->glEnable(GL_BLEND);
        gl->glBlendEquation(GL_FUNC_ADD);
        gl->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        gl->glClearColor(0, 0, 0, 1);
        gl->glClear(GL_COLOR_BUFFER_BIT);
        gl->glBindVertexArray(p.vao);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, p.texture);
        gl->glBindBufferBase(GL_UNIFORM_BUFFER, 0, p.lookup_buffer);
        p.program->setUniformValue("source_image", 0);
        p.program->setUniformValue("canvas_height", static_cast<float>(height));
        measured.draw_submission_nanoseconds += elapsed(setup_started);
        for (const auto& layer : layers) {
            if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
            if (!usable(layer) || layer.transform.opacity == 0) continue;
            const auto& f = *layer.frame;
            const auto upload_started = Clock::now();
            if (p.source_width != f.width || p.source_height != f.height) {
                gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, f.width, f.height, 0, GL_RGBA,
                    GL_UNSIGNED_BYTE, nullptr);
                p.source_width = f.width;
                p.source_height = f.height;
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            if (f.stride % 4 == 0) {
                gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, f.stride / 4);
                gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f.width, f.height, GL_RGBA,
                    GL_UNSIGNED_BYTE, f.rgba_pixels.data());
            } else {
                gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                for (int row = 0; row < f.height; ++row)
                    gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, row, f.width, 1, GL_RGBA,
                        GL_UNSIGNED_BYTE, f.rgba_pixels.data() + static_cast<std::size_t>(row) * f.stride);
            }
            gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            measured.upload_nanoseconds += elapsed(upload_started);
            measured.uploaded_bytes += static_cast<std::uint64_t>(f.width) * f.height * 4;
            ++measured.uploaded_layers;
            if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
            const auto draw_started = Clock::now();
            std::uint64_t geometry_upload_ns = 0;
            const auto& t = layer.transform;
            const double fit = std::min(static_cast<double>(width) / f.width,
                static_cast<double>(height) / f.height);
            const double displayed_width = f.width * fit * t.scale;
            const double displayed_height = f.height * fit * t.scale;
            const double radians = t.rotation_degrees * std::numbers::pi / 180.0;
            const bool axis = t.rotation_degrees == 0.0;
            p.program->setUniformValue("axis_aligned", axis);
            if (axis) {
                // Double-precision geometry matches the CPU's nearest sampling
                // exactly at texel boundaries. Only O(width+height) indices are
                // transferred, never rasterized pixels or another source texture.
                p.source_lookup.resize(static_cast<std::size_t>((width + height + 3) / 4) * 4);
                const auto map = [&](int count, double center, double displayed, int source, int offset) {
                    for (int i = 0; i < count; ++i) {
                        const double d = i + .5 - center;
                        p.source_lookup[static_cast<std::size_t>(offset + i)] = std::abs(d) > displayed * .5
                            ? -1 : std::clamp(static_cast<int>(std::floor((d / displayed + .5) * source)), 0, source - 1);
                    }
                };
                map(width, t.position_x * width, displayed_width, f.width, 0);
                map(height, t.position_y * height, displayed_height, f.height, width);
                const auto geometry_upload_started = Clock::now();
                gl->glBindBuffer(GL_UNIFORM_BUFFER, p.lookup_buffer);
                gl->glBufferSubData(GL_UNIFORM_BUFFER, 0,
                    static_cast<GLsizeiptr>(p.source_lookup.size() * sizeof(GLint)), p.source_lookup.data());
                geometry_upload_ns = elapsed(geometry_upload_started);
                measured.upload_nanoseconds += geometry_upload_ns;
                p.program->setUniformValue("lookup_y_offset", width);
                measured.uploaded_bytes += p.source_lookup.size() * sizeof(GLint);
            }
            const auto set_geometry = [&](const char* name, double x, double y) {
                if (p.precise_geometry) {
                    const GLdouble values[]{x, y};
                    p.uniform2dv(p.program->uniformLocation(name), 1, values);
                } else {
                    p.program->setUniformValue(name, QVector2D(static_cast<float>(x), static_cast<float>(y)));
                }
            };
            set_geometry("center", t.position_x * width, t.position_y * height);
            set_geometry("displayed_size", displayed_width, displayed_height);
            set_geometry("rotation_cs", std::cos(radians), std::sin(radians));
            p.program->setUniformValue("opacity", static_cast<float>(t.opacity));
            gl->glDrawArrays(GL_TRIANGLES, 0, 3);
            measured.draw_submission_nanoseconds += elapsed(draw_started) - geometry_upload_ns;
            if (const auto code = gl->glGetError(); code != GL_NO_ERROR)
                return result(OpenGlCompositionStatus::Failed, "draw-layer",
                    "OpenGL reported an upload or drawing error.", code);
        }
        if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
        const auto read_started = Clock::now();
        media::RgbaFrame frame{width, height, width * 4, {}};
        frame.rgba_pixels.resize(static_cast<std::size_t>(frame.stride) * height);
        gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
        gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        gl->glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, frame.rgba_pixels.data());
        measured.readback_nanoseconds = elapsed(read_started);
        measured.readback_bytes = frame.rgba_pixels.size();
        if (const auto code = gl->glGetError(); code != GL_NO_ERROR)
            return result(OpenGlCompositionStatus::Failed, "readback",
                "OpenGL reported a framebuffer readback error.", code);
        if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
        for (int y = 0; y < height / 2; ++y) {
            auto first = frame.rgba_pixels.begin() + static_cast<std::size_t>(y) * frame.stride;
            auto last = frame.rgba_pixels.begin() + static_cast<std::size_t>(height - 1 - y) * frame.stride;
            std::swap_ranges(first, first + frame.stride, last);
        }
        measured.readback_nanoseconds = elapsed(read_started);
        measured.readback_bytes = frame.rgba_pixels.size();
        if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
        return {OpenGlCompositionStatus::Complete, std::move(frame), {}, {}, 0};
    } catch (const std::exception& error) {
        return result(OpenGlCompositionStatus::Failed, "compose", error.what());
    }
}

} // namespace creative_suite::composition
