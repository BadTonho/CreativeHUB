#include <creative_suite/composition/opengl_frame_compositor.h>

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLContextGroup>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVersionFunctionsFactory>
#include <QThread>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <mutex>
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
uniform int color_adjustment_count;
uniform GEOMETRY_VEC2 center;
uniform GEOMETRY_VEC2 displayed_size;
uniform GEOMETRY_VEC2 rotation_cs;
uniform float opacity;
uniform bool axis_aligned;
layout(std140) uniform SourceLookupX { ivec4 lookup_x[1024]; };
layout(std140) uniform SourceLookupY { ivec4 lookup_y[1024]; };
layout(std140) uniform ColorAdjustments { vec4 color_adjustments[256]; };
out vec4 color;
int mappedX(int i) { return lookup_x[i / 4][i % 4]; }
int mappedY(int i) { return lookup_y[i / 4][i % 4]; }
vec3 adjustColor(vec3 rgb, vec4 parameters) {
    rgb += parameters.x;
    rgb = (rgb - vec3(0.5)) * parameters.y + vec3(0.5);
    float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    rgb = vec3(luma) + (rgb - vec3(luma)) * parameters.z;
    return floor(clamp(rgb, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
}
void main() {
    ivec2 size = textureSize(source_image, 0);
    ivec2 pixel;
    if (axis_aligned) {
        pixel = ivec2(mappedX(int(gl_FragCoord.x)),
            mappedY(int(canvas_height - gl_FragCoord.y)));
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
    for (int i = 0; i < color_adjustment_count; ++i)
        source.rgb = adjustColor(source.rgb, color_adjustments[i]);
    color = vec4(source.rgb, source.a * opacity);
}
)GLSL";
} // namespace

struct OpenGlTexturePoolBudget::Impl {
    mutable std::mutex mutex;
    std::uint64_t bytes = 0;
    unsigned targets = 0;
};
OpenGlTexturePoolBudget::OpenGlTexturePoolBudget() : impl_(std::make_unique<Impl>()) {}
OpenGlTexturePoolBudget::~OpenGlTexturePoolBudget() = default;
std::uint64_t OpenGlTexturePoolBudget::bytes() const noexcept {
    std::lock_guard lock(impl_->mutex); return impl_->bytes;
}
unsigned OpenGlTexturePoolBudget::targets() const noexcept {
    std::lock_guard lock(impl_->mutex); return impl_->targets;
}

struct OpenGlTextureFrame::State {
    int width = 0, height = 0;
    GLuint texture = 0;
    std::uint64_t session = 0;
    QOpenGLContextGroup* group = nullptr;
    mutable std::mutex mutex;
    std::atomic_bool valid{true};
    GLsync producer = nullptr;
    std::vector<GLsync> consumers;
    bool unsafe = false;
};
OpenGlTextureFrame::OpenGlTextureFrame(std::shared_ptr<State> state) : state_(std::move(state)) {}
OpenGlTextureFrame::~OpenGlTextureFrame() = default;
int OpenGlTextureFrame::width() const noexcept { return state_->width; }
int OpenGlTextureFrame::height() const noexcept { return state_->height; }
unsigned OpenGlTextureFrame::texture() const noexcept { return state_->texture; }
std::uint64_t OpenGlTextureFrame::session() const noexcept { return state_->session; }
bool OpenGlTextureFrame::valid() const noexcept { return state_->valid.load(); }
bool OpenGlTextureFrame::beginUse(QOpenGLContext* context, std::string& cause, std::int64_t& code) const {
    code = 0;
    std::lock_guard lock(state_->mutex);
    if (!valid() || !context || QOpenGLContext::currentContext() != context ||
        context->shareGroup() != state_->group || state_->unsafe || state_->consumers.size() >= 128) {
        cause = "The texture lease or consumer sharing context is unavailable."; return false;
    }
    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_2_Core>(context);
    if (!gl || !gl->initializeOpenGLFunctions()) {
        cause = "Consumer OpenGL functions are unavailable."; return false;
    }
    gl->glWaitSync(state_->producer, 0, GL_TIMEOUT_IGNORED);
    code = gl->glGetError();
    if (code) cause = "OpenGL failed to submit the producer fence wait.";
    return code == 0;
}
bool OpenGlTextureFrame::endUse(QOpenGLContext* context, std::string& cause, std::int64_t& code) const {
    code = 0;
    std::lock_guard lock(state_->mutex);
    if (!valid() || !context || QOpenGLContext::currentContext() != context ||
        context->shareGroup() != state_->group) {
        cause = "The texture consumer context was lost."; state_->unsafe = true; return false;
    }
    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_2_Core>(context);
    if (!gl || !gl->initializeOpenGLFunctions()) {
        cause = "Consumer OpenGL functions are unavailable."; state_->unsafe = true; return false;
    }
    if (state_->consumers.size() >= 128) {
        gl->glFinish();
        cause = "The bounded consumer fence registry is full.";
        state_->unsafe = true;
        return false;
    }
    const auto fence = gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (fence) state_->consumers.push_back(fence);
    gl->glFlush();
    code = gl->glGetError();
    if (!fence || code) {
        // Exceptional recovery only: without a consumption fence, complete
        // this consumer's commands before its lease can be returned.
        gl->glFinish();
        cause = "OpenGL failed to publish the consumer fence."; state_->unsafe = true; return false;
    }
    return true;
}

struct OpenGlFrameCompositor::Impl {
    QOffscreenSurface* surface;
    std::unique_ptr<QOpenGLContext> context;
    QOpenGLFunctions_3_2_Core* gl = nullptr;
    std::unique_ptr<QOpenGLShaderProgram> program;
    std::unique_ptr<QOpenGLFramebufferObject> output;
    GLuint texture = 0;
    GLuint vao = 0;
    GLuint lookup_buffers[3]{};
    std::uint64_t peak_known_bytes = 0;
    std::vector<GLint> source_lookup;
    int source_width = 0;
    int source_height = 0;
    int texture_limit = 0;
    bool precise_geometry = false;
    using Uniform2dv = void (QOPENGLF_APIENTRYP)(GLint, GLsizei, const GLdouble*);
    Uniform2dv uniform2dv = nullptr;
    OpenGlPrecisionPolicy precision;
    QOpenGLContext* share_context = nullptr;
    std::shared_ptr<OpenGlTexturePoolBudget> budget;
    struct Target {
        std::unique_ptr<QOpenGLFramebufferObject> output;
        std::shared_ptr<OpenGlTextureFrame::State> state;
        std::uint64_t bytes = 0;
    };
    std::vector<Target> targets;
    QOpenGLFramebufferObject* selected_output = nullptr;
    std::uint64_t session = 0;
    bool retiring = false;

    explicit Impl(QOffscreenSurface* s, OpenGlPrecisionPolicy policy, QOpenGLContext* sharing,
        std::shared_ptr<OpenGlTexturePoolBudget> pool_budget)
        : surface(s), precision(policy), share_context(sharing),
          budget(pool_budget ? std::move(pool_budget) : std::make_shared<OpenGlTexturePoolBudget>()) {
        static std::atomic<std::uint64_t> next_session{0}; session = ++next_session;
    }
    ~Impl() {
        if (context && context->makeCurrent(surface)) {
            for (auto& target : targets) {
                if (target.state) {
                    std::lock_guard lock(target.state->mutex);
                    target.state->valid.store(false);
                    if (target.state->unsafe) gl->glFinish(); // Exceptional teardown only.
                    if (target.state->producer) {
                        gl->glWaitSync(target.state->producer, 0, GL_TIMEOUT_IGNORED);
                        gl->glDeleteSync(target.state->producer);
                        target.state->producer = nullptr;
                    }
                    for (auto fence : target.state->consumers) {
                        gl->glWaitSync(fence, 0, GL_TIMEOUT_IGNORED); gl->glDeleteSync(fence);
                    }
                    target.state->consumers.clear();
                }
                target.output.reset();
            }
            output.reset();
            program.reset();
            if (texture) gl->glDeleteTextures(1, &texture);
            if (vao) gl->glDeleteVertexArrays(1, &vao);
            gl->glDeleteBuffers(3, lookup_buffers);
            context->doneCurrent();
        }
        for (auto& target : targets) {
            if (target.state) target.state->valid.store(false);
            std::lock_guard lock(budget->impl_->mutex);
            budget->impl_->bytes -= target.bytes; --budget->impl_->targets;
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
        if (share_context) context->setShareContext(share_context);
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
        GLint block_size = 0, fragment_blocks = 0;
        gl->glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &block_size);
        gl->glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_BLOCKS, &fragment_blocks);
        if (block_size < 16384 || fragment_blocks < 3)
            return result(OpenGlCompositionStatus::Failed, "check-uniform-limits",
                "Two 16 KiB geometry blocks and one color adjustment block are required.");
        gl->glGenBuffers(3, lookup_buffers);
        for (unsigned axis = 0; axis < 2; ++axis) {
            gl->glBindBuffer(GL_UNIFORM_BUFFER, lookup_buffers[axis]);
            gl->glBufferData(GL_UNIFORM_BUFFER, 16384, nullptr, GL_DYNAMIC_DRAW);
            const auto block = gl->glGetUniformBlockIndex(program->programId(),
                axis == 0 ? "SourceLookupX" : "SourceLookupY");
            gl->glUniformBlockBinding(program->programId(), block, axis);
        }
        gl->glBindBuffer(GL_UNIFORM_BUFFER, lookup_buffers[2]);
        gl->glBufferData(GL_UNIFORM_BUFFER, 256 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        const auto adjustments_block = gl->glGetUniformBlockIndex(
            program->programId(), "ColorAdjustments");
        if (adjustments_block == GL_INVALID_INDEX)
            return result(OpenGlCompositionStatus::Failed, "compile-shaders",
                "The color adjustment shader block is unavailable.");
        gl->glUniformBlockBinding(program->programId(), adjustments_block, 2);
        rememberResourcePeak();
        return result(OpenGlCompositionStatus::Complete);
    }
    OpenGlCompositionResult activate() {
        if (!program || !program->isLinked()) {
            if (context) return result(OpenGlCompositionStatus::Failed, "initialize",
                "The previous context initialization failed.");
            return initialize();
        }
        if (QOpenGLContext::currentContext() != context.get() && !context->makeCurrent(surface))
            return result(OpenGlCompositionStatus::Failed, "make-current",
                "Cannot reactivate the worker OpenGL context.");
        return result(OpenGlCompositionStatus::Complete);
    }
    OpenGlCompositionResult collect() {
        for (auto& target : targets) {
            if (!target.state) continue;
            auto& state = *target.state;
            std::lock_guard lock(state.mutex);
            if (state.unsafe) return result(OpenGlCompositionStatus::Failed, "consumer-fence",
                "A consumer could not protect its texture draw.");
            for (auto it = state.consumers.begin(); it != state.consumers.end();) {
                const auto status = gl->glClientWaitSync(*it, 0, 0);
                if (status == GL_WAIT_FAILED) return result(OpenGlCompositionStatus::Failed,
                    "collect-fence", "Cannot query the texture consumer fence.", gl->glGetError());
                if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) {
                    gl->glDeleteSync(*it); it = state.consumers.erase(it);
                } else ++it;
            }
            if (target.state.use_count() == 1 && state.producer && state.consumers.empty()) {
                const auto status = gl->glClientWaitSync(state.producer, 0, 0);
                if (status == GL_WAIT_FAILED) return result(OpenGlCompositionStatus::Failed,
                    "collect-fence", "Cannot query the texture producer fence.", gl->glGetError());
                if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) {
                    gl->glDeleteSync(state.producer); state.producer = nullptr;
                }
            }
        }
        return result(OpenGlCompositionStatus::Complete);
    }
    bool available(const Target& target) const {
        if (!target.state) return true;
        std::lock_guard lock(target.state->mutex);
        return target.state.use_count() == 1 && !target.state->producer &&
            target.state->consumers.empty() && !target.state->unsafe;
    }
    OpenGlResourceUsage resources() const noexcept {
        std::uint64_t bytes = static_cast<std::uint64_t>(source_width) * source_height * 4;
        if (output && output->isValid()) bytes += static_cast<std::uint64_t>(output->width()) * output->height() * 4;
        for (const auto& target : targets) if (target.output && target.output->isValid()) bytes += target.bytes;
        const std::uint64_t geometry = (lookup_buffers[0] ? 16384ULL : 0) +
            (lookup_buffers[1] ? 16384ULL : 0) + (lookup_buffers[2] ? 4096ULL : 0);
        return {bytes, geometry, std::max(peak_known_bytes, bytes + geometry)};
    }
    void rememberResourcePeak() noexcept { peak_known_bytes = resources().peak_known_bytes; }
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

OpenGlFrameCompositor::OpenGlFrameCompositor(QOffscreenSurface* surface, OpenGlPrecisionPolicy precision,
    QOpenGLContext* share_context, std::shared_ptr<OpenGlTexturePoolBudget> budget)
    : impl_(std::make_unique<Impl>(surface, precision, share_context, std::move(budget))) {}
OpenGlFrameCompositor::~OpenGlFrameCompositor() = default;

OpenGlCompositionResult OpenGlFrameCompositor::compose(int width, int height,
    const std::vector<CompositionLayer>& layers, const CancellationPredicate& cancel,
    OpenGlCompositionTimings* timings) {
    return render(width, height, layers, cancel, timings, true);
}

OpenGlTextureCompositionResult OpenGlFrameCompositor::composeTexture(int width, int height,
    const std::vector<CompositionLayer>& layers, const CancellationPredicate& cancel,
    OpenGlCompositionTimings* timings) {
    if (timings) *timings = {};
    auto& p = *impl_;
    struct Guard { Impl& p; ~Guard() { if (p.context) p.context->doneCurrent(); } } guard{p};
    const auto converted = [](OpenGlCompositionResult r) {
        return OpenGlTextureCompositionResult{r.status, {}, std::move(r.operation), std::move(r.cause), r.error_code};
    };
    try {
        if (cancel && cancel()) return {OpenGlCompositionStatus::Cancelled};
        if (p.retiring) return {OpenGlCompositionStatus::Unsupported, {}, "retired-session",
            "This compositor session no longer publishes textures."};
        auto active = p.activate();
        if (active.status != OpenGlCompositionStatus::Complete) return converted(std::move(active));
        if (!p.share_context || !QOpenGLContext::areSharing(p.context.get(), p.share_context))
            return {OpenGlCompositionStatus::Unsupported, {}, "check-sharing",
                "The producer has no verified shared context."};
        if (!bounded(width, height, p.texture_limit) ||
            static_cast<std::uint64_t>(width) * height * 4 > OpenGlTexturePoolBudget::maximum_bytes)
            return {OpenGlCompositionStatus::Unsupported, {}, "check-pool-limits",
                "Canvas exceeds the shared texture pool budget or device limits."};
        auto collected = p.collect();
        if (collected.status != OpenGlCompositionStatus::Complete) return converted(std::move(collected));
        std::size_t index = p.targets.size();
        for (std::size_t i = 0; i < p.targets.size(); ++i) {
            if (p.available(p.targets[i])) {
                index = i;
                if (p.targets[i].output && p.targets[i].output->width() == width &&
                    p.targets[i].output->height() == height) break;
            }
        }
        const auto bytes = static_cast<std::uint64_t>(width) * height * 4;
        const bool adding = index == p.targets.size();
        const auto old_bytes = adding ? 0 : p.targets[index].bytes;
        p.targets.reserve(OpenGlTexturePoolBudget::maximum_targets);
        {
            std::lock_guard lock(p.budget->impl_->mutex);
            if ((adding && p.budget->impl_->targets == OpenGlTexturePoolBudget::maximum_targets) ||
                p.budget->impl_->bytes - old_bytes + bytes > OpenGlTexturePoolBudget::maximum_bytes)
                return {OpenGlCompositionStatus::Busy};
            if (adding) { p.targets.emplace_back(); ++p.budget->impl_->targets; }
            p.budget->impl_->bytes = p.budget->impl_->bytes - old_bytes + bytes;
        }
        auto& target = p.targets[index];
        target.bytes = bytes;
        if (!target.output || target.output->width() != width || target.output->height() != height) {
            target.output.reset();
            QOpenGLFramebufferObjectFormat format; format.setInternalTextureFormat(GL_RGBA8);
            target.output = std::make_unique<QOpenGLFramebufferObject>(width, height, format);
            if (!target.output->isValid()) return {OpenGlCompositionStatus::Failed, {},
                "allocate-texture-target", "Cannot allocate a shared RGBA8 target."};
            p.rememberResourcePeak();
        }
        target.state = std::make_shared<OpenGlTextureFrame::State>();
        // Allocate the bounded consumer registry on the worker, never while drawing.
        target.state->consumers.reserve(128);
        target.state->width = width; target.state->height = height;
        target.state->texture = target.output->texture(); target.state->session = p.session;
        target.state->group = p.context->shareGroup();
        p.selected_output = target.output.get();
        auto rendered = render(width, height, layers, cancel, timings, false);
        p.selected_output = nullptr;
        if (rendered.status != OpenGlCompositionStatus::Complete) return converted(std::move(rendered));
        p.gl->glBindTexture(GL_TEXTURE_2D, target.state->texture);
        p.gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        p.gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        p.gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        p.gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        const auto fence_started = Clock::now();
        target.state->producer = p.gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        p.gl->glFlush();
        if (timings) timings->producer_fence_submission_nanoseconds = elapsed(fence_started);
        const auto code = p.gl->glGetError();
        if (!target.state->producer || code) return {OpenGlCompositionStatus::Failed, {},
            "publish-texture-fence", "Cannot publish the texture producer fence.", code};
        if (cancel && cancel()) return {OpenGlCompositionStatus::Cancelled};
        return {OpenGlCompositionStatus::Complete,
            OpenGlTextureFramePtr(new OpenGlTextureFrame(target.state))};
    } catch (const std::exception& error) {
        p.selected_output = nullptr;
        return {OpenGlCompositionStatus::Failed, {}, "compose-texture", error.what()};
    }
}

OpenGlCompositionResult OpenGlFrameCompositor::readback(const OpenGlTextureFramePtr& frame,
    const CancellationPredicate& cancel, OpenGlCompositionTimings* timings) {
    if (timings) *timings = {};
    auto& p = *impl_;
    struct Guard { Impl& p; ~Guard() { if (p.context) p.context->doneCurrent(); } } guard{p};
    try {
        if (cancel && cancel()) return result(OpenGlCompositionStatus::Cancelled);
        if (!frame || !frame->valid() || frame->session() != p.session)
            return result(OpenGlCompositionStatus::Unsupported, "read-texture", "Texture lease is no longer available.");
        auto active = p.activate();
        if (active.status != OpenGlCompositionStatus::Complete) return active;
        auto target = std::find_if(p.targets.begin(), p.targets.end(), [&](const auto& t) {
            return t.state == frame->state_;
        });
        if (target == p.targets.end() || !target->output->bind())
            return result(OpenGlCompositionStatus::Failed, "read-texture", "Cannot bind the leased framebuffer.");
        const auto started = Clock::now();
        p.gl->glWaitSync(frame->state_->producer, 0, GL_TIMEOUT_IGNORED);
        media::RgbaFrame rgba{frame->width(), frame->height(), frame->width() * 4, {}};
        rgba.rgba_pixels.resize(static_cast<std::size_t>(rgba.stride) * rgba.height);
        p.gl->glPixelStorei(GL_PACK_ALIGNMENT, 1); p.gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        if (cancel && cancel()) return result(OpenGlCompositionStatus::Cancelled);
        p.gl->glReadPixels(0, 0, rgba.width, rgba.height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.rgba_pixels.data());
        if (timings) {
            timings->readback_bytes = rgba.rgba_pixels.size(); timings->readback_nanoseconds = elapsed(started);
        }
        if (auto code = p.gl->glGetError(); code != GL_NO_ERROR)
            return result(OpenGlCompositionStatus::Failed, "read-texture", "Texture readback failed.", code);
        if (cancel && cancel()) return result(OpenGlCompositionStatus::Cancelled);
        for (int y = 0; y < rgba.height / 2; ++y) {
            auto first = rgba.rgba_pixels.begin() + static_cast<std::size_t>(y) * rgba.stride;
            auto last = rgba.rgba_pixels.begin() + static_cast<std::size_t>(rgba.height - 1 - y) * rgba.stride;
            std::swap_ranges(first, first + rgba.stride, last);
        }
        if (timings) timings->readback_nanoseconds = elapsed(started);
        if (cancel && cancel()) return result(OpenGlCompositionStatus::Cancelled);
        return {OpenGlCompositionStatus::Complete, std::move(rgba)};
    } catch (const std::exception& error) { return result(OpenGlCompositionStatus::Failed, "read-texture", error.what()); }
}

void OpenGlFrameCompositor::retireTextureFrames() noexcept { impl_->retiring = true; }

OpenGlCompositionResult OpenGlFrameCompositor::collectReleasedTextureFrames() {
    auto& p = *impl_;
    if (p.targets.empty()) return result(OpenGlCompositionStatus::Complete);
    auto active = p.activate();
    if (active.status != OpenGlCompositionStatus::Complete) return active;
    auto collected = p.collect();
    if (p.retiring) {
        // A displayed lease must not retain unused slots and starve the next
        // activation. Only completed, unleased targets can be deleted here.
        for (auto it = p.targets.begin(); it != p.targets.end();) {
            if (!p.available(*it)) { ++it; continue; }
            it->output.reset();
            {
                std::lock_guard lock(p.budget->impl_->mutex);
                p.budget->impl_->bytes -= it->bytes;
                --p.budget->impl_->targets;
            }
            it = p.targets.erase(it);
        }
    }
    p.context->doneCurrent(); return collected;
}
bool OpenGlFrameCompositor::hasPendingTextureFrames() const {
    return std::any_of(impl_->targets.begin(), impl_->targets.end(), [&](const auto& t) {
        return !impl_->available(t);
    });
}
std::uint64_t OpenGlFrameCompositor::texturePoolBytes() const noexcept { return impl_->budget->bytes(); }
unsigned OpenGlFrameCompositor::texturePoolOccupancy() const {
    return static_cast<unsigned>(std::count_if(impl_->targets.begin(), impl_->targets.end(),
        [](const auto& t) { return t.state && t.state.use_count() > 1; }));
}
OpenGlResourceUsage OpenGlFrameCompositor::resourceUsage() const noexcept { return impl_->resources(); }

OpenGlCompositionResult OpenGlFrameCompositor::render(int width, int height,
    const std::vector<CompositionLayer>& layers, const CancellationPredicate& cancel,
    OpenGlCompositionTimings* timings, bool read_output) {
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
        bool release;
        ~CurrentGuard() { if (release && p.context) p.context->doneCurrent(); }
    } guard{p, read_output};
    try {
        auto activated = p.activate();
        if (activated.status != OpenGlCompositionStatus::Complete) return activated;
        auto* gl = p.gl;
        if (!bounded(width, height, p.texture_limit))
            return result(OpenGlCompositionStatus::Unsupported, "check-limits",
                "Canvas exceeds the device texture limit or 256 MiB texture budget.");
        for (const auto& layer : layers) {
            if (layer.gpu_color_adjustments.size() > 256)
                return result(OpenGlCompositionStatus::Unsupported, "check-effects",
                    "Color adjustment stack exceeds the 256-effect GPU limit.");
            for (const auto& adjustment : layer.gpu_color_adjustments) {
                if (!std::isfinite(adjustment.brightness) || adjustment.brightness < -100.0 ||
                    adjustment.brightness > 100.0 ||
                    !std::isfinite(adjustment.contrast_percent) ||
                    adjustment.contrast_percent < 0.0 || adjustment.contrast_percent > 200.0 ||
                    !std::isfinite(adjustment.saturation_percent) ||
                    adjustment.saturation_percent < 0.0 || adjustment.saturation_percent > 200.0)
                    return result(OpenGlCompositionStatus::Unsupported, "check-effects",
                        "Color adjustment parameters are outside the shared effect contract.");
            }
            if (usable(layer) && layer.transform.opacity > 0 &&
                layer.transform.rotation_degrees != 0 && !p.precise_geometry)
                return result(OpenGlCompositionStatus::Unsupported, "check-precision",
                    "Exact rotated nearest sampling requires ARB_gpu_shader_fp64 and ARB_gpu_shader5.");
            if (usable(layer) && !bounded(layer.frame->width, layer.frame->height, p.texture_limit))
                return result(OpenGlCompositionStatus::Unsupported, "check-limits",
                    "Source exceeds the device texture limit or 256 MiB texture budget.");
            if (usable(layer) && layer.transform.rotation_degrees == 0.0 &&
                (width > 4096 || height > 4096))
                return result(OpenGlCompositionStatus::Unsupported, "check-limits",
                    "Exact nearest sampling exceeds the 4096-entry lookup budget for an axis.");
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
        if (read_output && (!p.output || p.output->width() != width || p.output->height() != height)) {
            QOpenGLFramebufferObjectFormat format;
            format.setInternalTextureFormat(GL_RGBA8);
            p.output = std::make_unique<QOpenGLFramebufferObject>(width, height, format);
            if (!p.output->isValid()) return result(OpenGlCompositionStatus::Failed,
                "allocate-framebuffer", "Cannot allocate the output RGBA8 framebuffer.");
            p.rememberResourcePeak();
        }
        auto* output = read_output ? p.output.get() : p.selected_output;
        const auto setup_started = Clock::now();
        if (!output || !output->bind() || !p.program->bind())
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
        gl->glBindBufferBase(GL_UNIFORM_BUFFER, 0, p.lookup_buffers[0]);
        gl->glBindBufferBase(GL_UNIFORM_BUFFER, 1, p.lookup_buffers[1]);
        gl->glBindBufferBase(GL_UNIFORM_BUFFER, 2, p.lookup_buffers[2]);
        p.program->setUniformValue("source_image", 0);
        p.program->setUniformValue("canvas_height", static_cast<float>(height));
        measured.draw_submission_nanoseconds += elapsed(setup_started);
        for (const auto& layer : layers) {
            if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
            if (!usable(layer) || layer.transform.opacity == 0) continue;
            const auto& f = *layer.frame;
            p.program->setUniformValue("color_adjustment_count",
                static_cast<int>(layer.gpu_color_adjustments.size()));
            const auto color_adjustment_started = layer.gpu_color_adjustments.empty()
                ? Clock::time_point{} : Clock::now();
            if (!layer.gpu_color_adjustments.empty()) {
                std::array<std::array<float, 4>, 256> packed_adjustments{};
                for (std::size_t i = 0; i < layer.gpu_color_adjustments.size(); ++i) {
                    const auto& adjustment = layer.gpu_color_adjustments[i];
                    packed_adjustments[i] = {
                        static_cast<float>(adjustment.brightness / 100.0),
                        static_cast<float>(adjustment.contrast_percent / 100.0),
                        static_cast<float>(adjustment.saturation_percent / 100.0), 0.0F};
                }
                const auto byte_count = static_cast<GLsizeiptr>(
                    layer.gpu_color_adjustments.size() * sizeof(packed_adjustments[0]));
                gl->glBindBuffer(GL_UNIFORM_BUFFER, p.lookup_buffers[2]);
                gl->glBufferSubData(GL_UNIFORM_BUFFER, 0, byte_count,
                    packed_adjustments.data());
                measured.uploaded_bytes += static_cast<std::uint64_t>(byte_count);
                measured.color_adjustment_count += layer.gpu_color_adjustments.size();
            }
            const auto upload_started = Clock::now();
            if (p.source_width != f.width || p.source_height != f.height) {
                gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, f.width, f.height, 0, GL_RGBA,
                    GL_UNSIGNED_BYTE, nullptr);
                p.source_width = f.width;
                p.source_height = f.height;
                p.rememberResourcePeak();
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
                const int y_offset = (width + 3) / 4 * 4;
                p.source_lookup.resize(static_cast<std::size_t>(y_offset + (height + 3) / 4 * 4));
                const auto map = [&](int count, double center, double displayed, int source, int offset) {
                    for (int i = 0; i < count; ++i) {
                        const double d = i + .5 - center;
                        p.source_lookup[static_cast<std::size_t>(offset + i)] = std::abs(d) > displayed * .5
                            ? -1 : std::clamp(static_cast<int>(std::floor((d / displayed + .5) * source)), 0, source - 1);
                    }
                };
                map(width, t.position_x * width, displayed_width, f.width, 0);
                map(height, t.position_y * height, displayed_height, f.height, y_offset);
                const auto geometry_upload_started = Clock::now();
                gl->glBindBuffer(GL_UNIFORM_BUFFER, p.lookup_buffers[0]);
                gl->glBufferSubData(GL_UNIFORM_BUFFER, 0,
                    static_cast<GLsizeiptr>(y_offset * sizeof(GLint)), p.source_lookup.data());
                gl->glBindBuffer(GL_UNIFORM_BUFFER, p.lookup_buffers[1]);
                gl->glBufferSubData(GL_UNIFORM_BUFFER, 0,
                    static_cast<GLsizeiptr>((p.source_lookup.size() - y_offset) * sizeof(GLint)),
                    p.source_lookup.data() + y_offset);
                geometry_upload_ns = elapsed(geometry_upload_started);
                measured.upload_nanoseconds += geometry_upload_ns;
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
            if (!layer.gpu_color_adjustments.empty())
                measured.color_adjustment_submission_nanoseconds +=
                    elapsed(color_adjustment_started);
            measured.draw_submission_nanoseconds += elapsed(draw_started) - geometry_upload_ns;
            if (const auto code = gl->glGetError(); code != GL_NO_ERROR)
                return result(OpenGlCompositionStatus::Failed, "draw-layer",
                    "OpenGL reported an upload or drawing error.", code);
        }
        if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
        if (!read_output) return result(OpenGlCompositionStatus::Complete);
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
