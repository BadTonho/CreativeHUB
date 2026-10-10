#include <creative_suite/composition/opengl_frame_compositor.h>
#include "windows_video_interop.h"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLContextGroup>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVersionFunctionsFactory>
#include <QVector3D>
#include <QThread>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <mutex>
#include <stdexcept>

namespace creative_suite::composition {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::uint64_t maximum_texture_bytes = 256ULL * 1024 * 1024;
constexpr std::uint64_t maximum_effect_scratch_bytes = 64ULL * 1024 * 1024;
std::uint64_t elapsed(Clock::time_point start) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
}
OpenGlCompositionResult result(OpenGlCompositionStatus status,
    const char* operation = "", std::string cause = {}, std::int64_t code = 0,
    int layer_index = -1) {
    return {status, {}, operation, std::move(cause), code, layer_index};
}
bool usable(const CompositionLayer& layer) {
    if (!layer.frame || !animation::validTransform(layer.transform)) return false;
    const auto& f = *layer.frame;
    if (layer.native_frame) return f.width == layer.native_frame->width() && f.height == layer.native_frame->height();
    if (layer.texture_frame) return layer.texture_frame->valid() &&
        f.width == layer.texture_frame->width() && f.height == layer.texture_frame->height();
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
uniform bool source_bottom_left;
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
    if (source_bottom_left) pixel.y = size.y - 1 - pixel.y;
    vec4 source = texelFetch(source_image, pixel, 0);
    for (int i = 0; i < color_adjustment_count; ++i)
        source.rgb = adjustColor(source.rgb, color_adjustments[i]);
    color = vec4(source.rgb, source.a * opacity);
}
)GLSL";
constexpr char image_fragment_shader[] = R"GLSL(
uniform sampler2D background_image;
uniform sampler2D foreground_image;
uniform bool background_bottom_left;
uniform bool foreground_bottom_left;
uniform int operation;
uniform int canvas_height;
uniform GEOMETRY_VEC2 center;
uniform GEOMETRY_VEC2 rotation_cs;
uniform GEOMETRY_VEC2 scale_opacity;
uniform GEOMETRY_VEC2 merge_scale;
uniform ivec2 merge_origin;
uniform ivec2 merge_size;
uniform int color_count;
layout(std140) uniform ColorAdjustments { vec4 color_adjustments[256]; };
out vec4 color;
vec4 background(ivec2 pixel) {
    if (background_bottom_left) pixel.y = textureSize(background_image, 0).y - 1 - pixel.y;
    return texelFetch(background_image, pixel, 0);
}
vec4 foreground(ivec2 pixel) {
    if (foreground_bottom_left) pixel.y = textureSize(foreground_image, 0).y - 1 - pixel.y;
    return texelFetch(foreground_image, pixel, 0);
}
void main() {
    ivec2 pixel = ivec2(int(gl_FragCoord.x), canvas_height - 1 - int(gl_FragCoord.y));
    ivec2 size = textureSize(background_image, 0);
    if (operation == 1) {
        PRECISE GEOMETRY_VEC2 d = GEOMETRY_VEC2(pixel) + 0.5 - center;
        PRECISE GEOMETRY_VEC2 local = GEOMETRY_VEC2(rotation_cs.x * d.x + rotation_cs.y * d.y,
            -rotation_cs.y * d.x + rotation_cs.x * d.y);
        pixel = ivec2(floor(local / scale_opacity.x + GEOMETRY_VEC2(size) * 0.5));
        if (any(lessThan(pixel, ivec2(0))) || any(greaterThanEqual(pixel, size))) { color = vec4(0); return; }
        color = background(pixel);
        color.a = float(floor(GEOMETRY_VEC2(floor(color.a * 255.0 + 0.5), 0).x * scale_opacity.y + 0.5)) / 255.0;
        return;
    }
    color = background(pixel);
    if (operation == 2) {
        for (int i = 0; i < color_count; ++i) {
            vec4 p = color_adjustments[i];
            vec3 rgb = (color.rgb + p.x - vec3(0.5)) * p.y + vec3(0.5);
            float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
            color.rgb = floor(clamp(vec3(luma) + (rgb - vec3(luma)) * p.z, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
        }
        return;
    }
    if (operation == 3) {
        ivec2 local = pixel - merge_origin;
        if (any(lessThan(local, ivec2(0))) || any(greaterThanEqual(local, merge_size))) return;
        ivec2 position = clamp(ivec2(GEOMETRY_VEC2(local) / merge_scale.x), ivec2(0),
            textureSize(foreground_image, 0) - ivec2(1));
        ivec4 bg = ivec4(floor(color * 255.0 + 0.5));
        ivec4 fg = ivec4(floor(foreground(position) * 255.0 + 0.5));
        int alpha = fg.a * 255 + bg.a * (255 - fg.a);
        ivec3 numerator = fg.rgb * fg.a * 255 + bg.rgb * bg.a * (255 - fg.a);
        ivec3 rgb = alpha == 0 ? ivec3(0) : (numerator + ivec3(alpha / 2)) / alpha;
        color = vec4(vec3(rgb), float((alpha + 127) / 255)) / 255.0;
    }
}
)GLSL";
constexpr char effect_fragment_shader[] = R"GLSL(#version 150 core
uniform sampler2D source_image;
uniform int effect_kind;
uniform vec3 color_parameters;
uniform int blur_radius;
uniform bool horizontal_pass;
uniform bool premultiply_alpha;
uniform bool unpremultiply_alpha;
out vec4 color;
vec3 adjustColor(vec3 rgb) {
    rgb += color_parameters.x;
    rgb = (rgb - vec3(0.5)) * color_parameters.y + vec3(0.5);
    float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    rgb = vec3(luma) + (rgb - vec3(luma)) * color_parameters.z;
    return floor(clamp(rgb, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
}
void main() {
    ivec2 size = textureSize(source_image, 0);
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    if (effect_kind == 0) {
        vec4 source = texelFetch(source_image, pixel, 0);
        source.rgb = adjustColor(source.rgb);
        color = source;
        return;
    }
    if (premultiply_alpha) {
        vec4 source = texelFetch(source_image, pixel, 0);
        ivec3 bytes = ivec3(floor(source.rgb * 255.0 + 0.5));
        int alpha = int(floor(source.a * 255.0 + 0.5));
        bytes = (bytes * alpha + ivec3(127)) / 255;
        color = vec4(vec3(bytes) / 255.0, source.a);
        return;
    }
    if (unpremultiply_alpha) {
        vec4 source = texelFetch(source_image, pixel, 0);
        int alpha = int(floor(source.a * 255.0 + 0.5));
        ivec3 bytes = ivec3(floor(source.rgb * 255.0 + 0.5));
        if (alpha == 0) bytes = ivec3(0);
        else bytes = min(ivec3(255), (bytes * 255 + ivec3(alpha / 2)) / alpha);
        color = vec4(vec3(bytes) / 255.0, source.a);
        return;
    }
    ivec2 step = horizontal_pass ? ivec2(1, 0) : ivec2(0, 1);
    ivec4 sum = ivec4(0);
    for (int offset = -blur_radius; offset <= blur_radius; ++offset) {
        ivec2 sample_pixel = clamp(pixel + step * offset, ivec2(0), size - ivec2(1));
        sum += ivec4(floor(texelFetch(source_image, sample_pixel, 0) * 255.0 + 0.5));
    }
    int divisor = blur_radius * 2 + 1;
    ivec4 averaged = (sum + ivec4(divisor / 2)) / divisor;
    color = vec4(averaged) / 255.0;
}
)GLSL";
} // namespace

struct OpenGlTexturePoolBudget::Impl {
    mutable std::mutex mutex;
    std::uint64_t bytes = 0;
    unsigned targets = 0;
    std::uint64_t byte_limit = maximum_bytes;
    unsigned target_limit = maximum_targets;
};
OpenGlTexturePoolBudget::OpenGlTexturePoolBudget(std::uint64_t byte_limit, unsigned target_limit)
    : impl_(std::make_unique<Impl>()) {
    if (!byte_limit || byte_limit > 256ULL * 1024 * 1024 || !target_limit || target_limit > 8)
        throw std::invalid_argument("The texture pool limit must be within 256 MiB and eight targets.");
    impl_->byte_limit = byte_limit;
    impl_->target_limit = target_limit;
}
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
    std::unique_ptr<QOpenGLShaderProgram> effect_program;
    std::unique_ptr<QOpenGLShaderProgram> image_program;
    const OpenGlImageOperation* image_operation = nullptr;
    GLuint image_textures[2]{};
    std::uint64_t image_texture_bytes[2]{};
    std::unique_ptr<detail::WindowsVideoInterop> video_interop;
    std::unique_ptr<QOpenGLFramebufferObject> output;
    std::unique_ptr<QOpenGLFramebufferObject> effect_scratch;
    GLuint texture = 0;
    GLuint effect_source_fbo = 0;
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
    struct ReadbackSlot {
        GLuint buffer = 0;
        GLsync fence = nullptr;
        std::uint64_t ticket = 0;
        std::uint64_t bytes = 0;
        int width = 0;
        int height = 0;
        bool busy = false;
    };
    std::vector<ReadbackSlot> readback_slots;
    std::uint64_t next_readback_ticket = 0;

    explicit Impl(QOffscreenSurface* s, OpenGlPrecisionPolicy policy, QOpenGLContext* sharing,
        std::shared_ptr<OpenGlTexturePoolBudget> pool_budget)
        : surface(s), precision(policy), share_context(sharing),
          budget(pool_budget ? std::move(pool_budget) : std::make_shared<OpenGlTexturePoolBudget>()) {
        static std::atomic<std::uint64_t> next_session{0}; session = ++next_session;
    }
    ~Impl() {
        if (context && context->makeCurrent(surface)) {
            video_interop.reset();
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
            effect_scratch.reset();
            program.reset();
            effect_program.reset();
            image_program.reset();
            gl->glDeleteTextures(2, image_textures);
            if (effect_source_fbo) gl->glDeleteFramebuffers(1, &effect_source_fbo);
            if (texture) gl->glDeleteTextures(1, &texture);
            if (vao) gl->glDeleteVertexArrays(1, &vao);
            gl->glDeleteBuffers(3, lookup_buffers);
            for (auto& slot : readback_slots) {
                if (slot.fence) gl->glDeleteSync(slot.fence);
                if (slot.buffer) gl->glDeleteBuffers(1, &slot.buffer);
            }
            readback_slots.clear();
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
        effect_program = std::make_unique<QOpenGLShaderProgram>();
        if (!effect_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex_shader) ||
            !effect_program->addShaderFromSourceCode(QOpenGLShader::Fragment,
                effect_fragment_shader) || !effect_program->link())
            return result(OpenGlCompositionStatus::Failed, "compile-effect-shaders",
                effect_program->log().toStdString());
        image_program = std::make_unique<QOpenGLShaderProgram>();
        const auto image_fragment = fragment.left(fragment.indexOf("uniform sampler2D")) + image_fragment_shader;
        if (!image_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex_shader) ||
            !image_program->addShaderFromSourceCode(QOpenGLShader::Fragment, image_fragment) || !image_program->link())
            return result(OpenGlCompositionStatus::Failed, "compile-image-shaders", image_program->log().toStdString());
        gl->glGenTextures(2, image_textures);
        gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_limit);
        gl->glGenTextures(1, &texture);
        gl->glGenFramebuffers(1, &effect_source_fbo);
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
    OpenGlCompositionResult renderImage(const OpenGlImageOperation& operation,
        const OpenGlFrameCompositor::CancellationPredicate& cancel, OpenGlCompositionTimings& measured) {
        if (!selected_output || !selected_output->bind() || !image_program->bind())
            return result(OpenGlCompositionStatus::Failed, "bind-image-output", "Cannot bind the image operation target.");
        const auto width = operation.background.width(), height = operation.background.height();
        if (!bounded(width, height, texture_limit) || operation.colors.size() > 256 ||
            !animation::validTransform(operation.transform))
            return result(OpenGlCompositionStatus::Unsupported, "check-image-operation", "Image geometry or effects exceed the supported contract.");
        if (operation.kind == OpenGlImageOperationKind::Transform &&
            operation.transform.rotation_degrees != 0 && !precise_geometry)
            return result(OpenGlCompositionStatus::Unsupported, "check-precision", "Rotated intermediates require precise GPU geometry.");
        struct Uses {
            QOpenGLContext* context;
            std::vector<OpenGlTextureFramePtr> frames;
            ~Uses() {
                for (const auto& frame : frames) {
                    std::string cause; std::int64_t code = 0;
                    (void)frame->endUse(context, cause, code);
                }
            }
        } uses{context.get()};
        const auto bind = [&](const OpenGlImageInput& input, unsigned slot) -> OpenGlCompositionResult {
            gl->glActiveTexture(GL_TEXTURE0 + slot);
            if (input.native) {
                if (slot != 0) return result(OpenGlCompositionStatus::Unsupported, "native-image-input",
                    "Import native sources before merging them.");
                if (!video_interop) video_interop = std::make_unique<detail::WindowsVideoInterop>(context.get(), gl);
                const auto started = Clock::now();
                auto imported = video_interop->begin(input.native);
                if (imported.status != OpenGlCompositionStatus::Complete) return imported;
                gl->glBindTexture(GL_TEXTURE_2D, video_interop->texture());
                ++measured.native_video_imports;
                measured.native_video_conversion_nanoseconds += elapsed(started);
                rememberResourcePeak();
            } else if (input.texture) {
                std::string cause; std::int64_t code = 0;
                if (!input.texture->beginUse(context.get(), cause, code))
                    return result(OpenGlCompositionStatus::Failed, "consume-image-texture", cause, code);
                uses.frames.push_back(input.texture);
                gl->glBindTexture(GL_TEXTURE_2D, input.texture->texture());
            } else {
                if (!input.rgba || !usable(CompositionLayer{input.rgba}) ||
                    !bounded(input.width(), input.height(), texture_limit))
                    return result(OpenGlCompositionStatus::Unsupported, "check-image-input", "Image operation requires a valid source.");
                const auto started = Clock::now();
                gl->glBindTexture(GL_TEXTURE_2D, image_textures[slot]);
                gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, input.width(), input.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                for (int y = 0; y < input.height(); ++y)
                    gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, input.width(), 1, GL_RGBA, GL_UNSIGNED_BYTE,
                        input.rgba->rgba_pixels.data() + static_cast<std::size_t>(y) * input.rgba->stride);
                image_texture_bytes[slot] = static_cast<std::uint64_t>(input.width()) * input.height() * 4;
                measured.uploaded_bytes += image_texture_bytes[slot];
                ++measured.uploaded_layers;
                measured.upload_nanoseconds += elapsed(started);
                rememberResourcePeak();
            }
            return result(OpenGlCompositionStatus::Complete);
        };
        auto bound = bind(operation.background, 0);
        struct InteropGuard {
            detail::WindowsVideoInterop* adapter;
            ~InteropGuard() { if (adapter) (void)adapter->end(); }
        } interop_guard{operation.background.native ? video_interop.get() : nullptr};
        if (bound.status != OpenGlCompositionStatus::Complete) return bound;
        if (operation.kind == OpenGlImageOperationKind::Merge) {
            bound = bind(operation.foreground, 1);
            if (bound.status != OpenGlCompositionStatus::Complete) return bound;
        }
        const auto started = Clock::now();
        gl->glViewport(0, 0, width, height);
        gl->glDisable(GL_BLEND); gl->glDisable(GL_DEPTH_TEST); gl->glDisable(GL_STENCIL_TEST);
        gl->glDisable(GL_SCISSOR_TEST); gl->glDisable(GL_CULL_FACE); gl->glDisable(GL_DITHER);
        gl->glDisable(GL_FRAMEBUFFER_SRGB); gl->glDisable(GL_MULTISAMPLE);
        gl->glBindVertexArray(vao);
        image_program->setUniformValue("background_image", 0);
        image_program->setUniformValue("foreground_image", 1);
        image_program->setUniformValue("background_bottom_left", bool(operation.background.texture));
        image_program->setUniformValue("foreground_bottom_left", bool(operation.foreground.texture));
        image_program->setUniformValue("operation", static_cast<int>(operation.kind));
        image_program->setUniformValue("canvas_height", height);
        image_program->setUniformValue("color_count", static_cast<int>(operation.colors.size()));
        const auto geometry = [&](const char* name, double x, double y) {
            if (precise_geometry) {
                const GLdouble values[]{x, y}; uniform2dv(image_program->uniformLocation(name), 1, values);
            } else image_program->setUniformValue(name, QVector2D(float(x), float(y)));
        };
        const auto& transform = operation.transform;
        const double angle = transform.rotation_degrees * std::numbers::pi / 180.0;
        geometry("center", transform.position_x * width, transform.position_y * height);
        geometry("rotation_cs", std::cos(angle), std::sin(angle));
        geometry("scale_opacity", transform.scale, std::clamp(transform.opacity, 0.0, 1.0));
        if (operation.kind == OpenGlImageOperationKind::Merge) {
            const double scale = std::min(double(width) / operation.foreground.width(),
                double(height) / operation.foreground.height());
            const int fitted_width = std::max(1, int(std::lround(operation.foreground.width() * scale)));
            const int fitted_height = std::max(1, int(std::lround(operation.foreground.height() * scale)));
            geometry("merge_scale", scale, 0);
            gl->glUniform2i(image_program->uniformLocation("merge_origin"), (width - fitted_width) / 2, (height - fitted_height) / 2);
            gl->glUniform2i(image_program->uniformLocation("merge_size"), fitted_width, fitted_height);
        }
        std::array<std::array<float, 4>, 256> colors{};
        for (std::size_t i = 0; i < operation.colors.size(); ++i) {
            const auto& value = operation.colors[i];
            if (!std::isfinite(value.brightness) || value.brightness < -100 || value.brightness > 100 ||
                !std::isfinite(value.contrast_percent) || value.contrast_percent < 0 || value.contrast_percent > 200 ||
                !std::isfinite(value.saturation_percent) || value.saturation_percent < 0 || value.saturation_percent > 200)
                return result(OpenGlCompositionStatus::Unsupported, "check-image-color", "Invalid image color parameters.");
            colors[i] = {float(value.brightness / 100), float(value.contrast_percent / 100),
                float(value.saturation_percent / 100), 0};
        }
        gl->glBindBuffer(GL_UNIFORM_BUFFER, lookup_buffers[2]);
        gl->glBufferSubData(GL_UNIFORM_BUFFER, 0, operation.colors.size() * sizeof(colors[0]), colors.data());
        gl->glBindBufferBase(GL_UNIFORM_BUFFER, 2, lookup_buffers[2]);
        gl->glUniformBlockBinding(image_program->programId(), gl->glGetUniformBlockIndex(image_program->programId(), "ColorAdjustments"), 2);
        measured.color_adjustment_count += operation.colors.size();
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        measured.draw_submission_nanoseconds += elapsed(started);
        if (const auto code = gl->glGetError(); code != GL_NO_ERROR)
            return result(OpenGlCompositionStatus::Failed, "draw-image-operation", "Image operation failed.", code);
        for (const auto& frame : uses.frames) {
            std::string cause; std::int64_t code = 0;
            if (!frame->endUse(context.get(), cause, code))
                return result(OpenGlCompositionStatus::Failed, "protect-image-texture", cause, code);
        }
        uses.frames.clear();
        if (interop_guard.adapter) {
            auto released = interop_guard.adapter->end(); interop_guard.adapter = nullptr;
            if (released.status != OpenGlCompositionStatus::Complete) return released;
        }
        if (cancel && cancel()) return result(OpenGlCompositionStatus::Cancelled);
        return result(OpenGlCompositionStatus::Complete);
    }
    OpenGlCompositionResult collect() {
        for (auto& target : targets) {
            if (!target.state) continue;
            auto& state = *target.state;
            std::lock_guard lock(state.mutex);
            if (state.unsafe) return result(OpenGlCompositionStatus::Failed, "consumer-fence",
                "A consumer could not protect its texture draw.");
            if (image_operation && target.state.use_count() == 1) {
                // Internal graph evaluation may reuse a released intermediate
                // immediately. Synchronize on the GPU before overwriting it.
                if (state.producer) {
                    gl->glWaitSync(state.producer, 0, GL_TIMEOUT_IGNORED);
                    gl->glDeleteSync(state.producer); state.producer = nullptr;
                }
                for (const auto fence : state.consumers) {
                    gl->glWaitSync(fence, 0, GL_TIMEOUT_IGNORED); gl->glDeleteSync(fence);
                }
                state.consumers.clear();
            }
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
        bytes += image_texture_bytes[0] + image_texture_bytes[1];
        if (video_interop) bytes += video_interop->reservedBytes();
        if (output && output->isValid()) bytes += static_cast<std::uint64_t>(output->width()) * output->height() * 4;
        for (const auto& target : targets) if (target.output && target.output->isValid()) bytes += target.bytes;
        if (effect_scratch && effect_scratch->isValid())
            bytes += static_cast<std::uint64_t>(effect_scratch->width()) *
                effect_scratch->height() * 4;
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

int OpenGlImageInput::width() const noexcept { return native ? native->width() : texture ? texture->width() : rgba ? rgba->width : 0; }
int OpenGlImageInput::height() const noexcept { return native ? native->height() : texture ? texture->height() : rgba ? rgba->height : 0; }

OpenGlTextureCompositionResult OpenGlFrameCompositor::importFrame(
    const media::NativeVideoFramePtr& frame, const CancellationPredicate& cancel, OpenGlCompositionTimings* timings) {
    OpenGlImageOperation operation;
    operation.background.native = frame;
    return processImage(operation, cancel, timings);
}

OpenGlCompositionResult OpenGlFrameCompositor::copyToNative(const OpenGlTextureFramePtr& source,
    const std::shared_ptr<const media::NativeVideoFrame>& destination, const CancellationPredicate& cancel) {
    auto& p = *impl_;
    struct Guard { Impl& p; ~Guard() { if (p.context) p.context->doneCurrent(); } } guard{p};
    try {
        if (cancel && cancel()) return {OpenGlCompositionStatus::Cancelled};
        if (!source || !destination || source->width() != destination->width() || source->height() != destination->height())
            return {OpenGlCompositionStatus::Unsupported, {}, "check-native-output", "Source and encoding surface dimensions must match."};
        auto active = p.activate(); if (active.status != OpenGlCompositionStatus::Complete) return active;
        std::string cause; std::int64_t code = 0;
        if (!source->beginUse(p.context.get(), cause, code)) return {OpenGlCompositionStatus::Failed, {}, "wait-native-output", cause, code};
        if (!p.video_interop) p.video_interop = std::make_unique<detail::WindowsVideoInterop>(p.context.get(), p.gl);
        auto result = p.video_interop->write(source->texture(), destination);
        if (!source->endUse(p.context.get(), cause, code)) return {OpenGlCompositionStatus::Failed, {}, "release-native-output", cause, code};
        p.rememberResourcePeak();
        if (cancel && cancel()) return {OpenGlCompositionStatus::Cancelled};
        return result;
    } catch (const std::exception& error) { return {OpenGlCompositionStatus::Failed, {}, "copy-native-output", error.what()}; }
}

OpenGlTextureCompositionResult OpenGlFrameCompositor::processImage(
    const OpenGlImageOperation& operation, const CancellationPredicate& cancel, OpenGlCompositionTimings* timings) {
    auto& p = *impl_;
    struct Guard { Impl& p; ~Guard() { p.image_operation = nullptr; } } guard{p};
    p.image_operation = &operation;
    return composeTexture(operation.background.width(), operation.background.height(), {}, cancel, timings);
}

OpenGlCompositionResult OpenGlFrameCompositor::compose(int width, int height,
    const std::vector<CompositionLayer>& layers, const CancellationPredicate& cancel,
    OpenGlCompositionTimings* timings) {
    return render(width, height, layers, cancel, timings, true);
}

OpenGlCompositionResult OpenGlFrameCompositor::prepareAsyncReadback(
    int width, int height, std::uint64_t staging_budget_bytes,
    unsigned maximum_slots, unsigned* prepared_slots,
    bool fail_allocation_for_testing) {
    if (prepared_slots) *prepared_slots = 0;
    auto& p = *impl_;
    struct Guard { Impl& p; ~Guard() { if (p.context) p.context->doneCurrent(); } } guard{p};
    try {
        auto active = p.activate();
        if (active.status != OpenGlCompositionStatus::Complete) return active;
        if (!bounded(width, height, p.texture_limit))
            return result(OpenGlCompositionStatus::Unsupported, "check-pbo-limits",
                "Canvas exceeds the device limits for asynchronous readback.");
        const auto frame_bytes = static_cast<std::uint64_t>(width) * height * 4;
        maximum_slots = std::min(maximum_slots, 2U);
        const auto affordable = frame_bytes == 0 ? 0 : staging_budget_bytes / frame_bytes;
        const auto slot_count = static_cast<unsigned>(std::min<std::uint64_t>(maximum_slots, affordable));
        if (slot_count == 0 || frame_bytes > static_cast<std::uint64_t>(
                std::numeric_limits<GLsizeiptr>::max()))
            return result(OpenGlCompositionStatus::Unsupported, "pbo-budget",
                "The export staging budget cannot hold an asynchronous readback slot.");
        if (fail_allocation_for_testing)
            return result(OpenGlCompositionStatus::Failed, "allocate-pbo",
                "Injected asynchronous readback allocation failure for regression coverage.");
        if (std::any_of(p.readback_slots.begin(), p.readback_slots.end(),
                [](const auto& slot) { return slot.busy; }))
            return result(OpenGlCompositionStatus::Busy, "pbo-reconfigure",
                "Pending readback tickets must be collected before resizing the PBO pool.");
        if (p.output && p.output->width() == width && p.output->height() == height &&
            p.readback_slots.size() == slot_count &&
            std::all_of(p.readback_slots.begin(), p.readback_slots.end(),
                [frame_bytes](const auto& slot) { return slot.bytes == frame_bytes; })) {
            if (prepared_slots) *prepared_slots = slot_count;
            return result(OpenGlCompositionStatus::Complete);
        }

        if (!p.output || p.output->width() != width || p.output->height() != height) {
            QOpenGLFramebufferObjectFormat format;
            format.setInternalTextureFormat(GL_RGBA8);
            p.output = std::make_unique<QOpenGLFramebufferObject>(width, height, format);
            if (!p.output->isValid()) return result(OpenGlCompositionStatus::Failed,
                "allocate-framebuffer", "Cannot allocate the output RGBA8 framebuffer.");
            p.rememberResourcePeak();
        }
        for (auto& slot : p.readback_slots) {
            if (slot.fence) p.gl->glDeleteSync(slot.fence);
            if (slot.buffer) p.gl->glDeleteBuffers(1, &slot.buffer);
        }
        p.readback_slots.clear();
        p.readback_slots.resize(slot_count);
        while (p.gl->glGetError() != GL_NO_ERROR) {}
        for (auto& slot : p.readback_slots) {
            p.gl->glGenBuffers(1, &slot.buffer);
            p.gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.buffer);
            p.gl->glBufferData(GL_PIXEL_PACK_BUFFER,
                static_cast<GLsizeiptr>(frame_bytes), nullptr, GL_STREAM_READ);
            slot.bytes = frame_bytes;
            slot.width = width;
            slot.height = height;
            if (!slot.buffer || p.gl->glGetError() != GL_NO_ERROR) {
                for (auto& allocated : p.readback_slots) {
                    if (allocated.buffer) p.gl->glDeleteBuffers(1, &allocated.buffer);
                }
                p.readback_slots.clear();
                return result(OpenGlCompositionStatus::Failed, "allocate-pbo",
                    "The driver could not allocate the bounded pixel-pack buffer pool.",
                    p.gl->glGetError());
            }
        }
        p.gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        if (prepared_slots) *prepared_slots = slot_count;
        return result(OpenGlCompositionStatus::Complete);
    } catch (const std::exception& error) {
        return result(OpenGlCompositionStatus::Failed, "prepare-pbo", error.what());
    }
}

OpenGlReadbackResult OpenGlFrameCompositor::submitAsyncReadback(
    int width, int height, const std::vector<CompositionLayer>& layers,
    const CancellationPredicate& cancel) {
    auto& p = *impl_;
    struct Guard { Impl& p; ~Guard() { if (p.context) p.context->doneCurrent(); } } guard{p};
    const auto cancelled = [&] { return cancel && cancel(); };
    try {
        if (cancelled()) return {OpenGlCompositionStatus::Cancelled};
        auto active = p.activate();
        if (active.status != OpenGlCompositionStatus::Complete)
            return {active.status, {}, {}, std::move(active.operation), std::move(active.cause), active.error_code};
        const auto free_slot = std::find_if(p.readback_slots.begin(), p.readback_slots.end(),
            [](const auto& slot) { return !slot.busy; });
        if (free_slot == p.readback_slots.end())
            return {OpenGlCompositionStatus::Busy, {}, {}, "pbo-backpressure",
                "All bounded asynchronous readback slots are occupied."};
        if (free_slot->width != width || free_slot->height != height || !p.output ||
            p.output->width() != width || p.output->height() != height)
            return {OpenGlCompositionStatus::Unsupported, {}, {}, "pbo-size",
                "The asynchronous readback pool was not prepared for this canvas."};
        p.selected_output = p.output.get();
        OpenGlCompositionTimings composition_timings;
        auto rendered = render(width, height, layers, cancel, &composition_timings, false);
        p.selected_output = nullptr;
        if (rendered.status != OpenGlCompositionStatus::Complete) {
            OpenGlReadbackResult failed{rendered.status, {}, {}, std::move(rendered.operation),
                std::move(rendered.cause), rendered.error_code};
            failed.composition_timings = composition_timings;
            return failed;
        }
        if (cancelled()) return {OpenGlCompositionStatus::Cancelled};
        const auto submit_started = Clock::now();
        if (!p.output->bind())
            return {OpenGlCompositionStatus::Failed, {}, {}, "bind-pbo-source",
                "Cannot bind the completed composition framebuffer."};
        p.gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, free_slot->buffer);
        p.gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
        p.gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        p.gl->glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        free_slot->fence = p.gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        p.gl->glFlush();
        p.gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        const auto code = p.gl->glGetError();
        if (!free_slot->fence || code) {
            if (free_slot->fence) p.gl->glDeleteSync(free_slot->fence);
            free_slot->fence = nullptr;
            return {OpenGlCompositionStatus::Failed, {}, {}, "submit-pbo-readback",
                "The driver could not submit the asynchronous framebuffer readback.", code};
        }
        free_slot->ticket = ++p.next_readback_ticket;
        free_slot->busy = true;
        OpenGlReadbackResult submitted{OpenGlCompositionStatus::Complete, {free_slot->ticket}};
        submitted.composition_timings = composition_timings;
        submitted.submission_nanoseconds = elapsed(submit_started);
        submitted.bytes = free_slot->bytes;
        return submitted;
    } catch (const std::exception& error) {
        p.selected_output = nullptr;
        return {OpenGlCompositionStatus::Failed, {}, {}, "submit-pbo-readback", error.what()};
    }
}

OpenGlReadbackResult OpenGlFrameCompositor::collectAsyncReadback(
    OpenGlReadbackTicket ticket, const CancellationPredicate& cancel,
    bool fail_collection_for_testing) {
    auto& p = *impl_;
    struct Guard { Impl& p; ~Guard() { if (p.context) p.context->doneCurrent(); } } guard{p};
    try {
        auto active = p.activate();
        if (active.status != OpenGlCompositionStatus::Complete)
            return {active.status, {}, {}, std::move(active.operation), std::move(active.cause), active.error_code};
        const auto slot = std::find_if(p.readback_slots.begin(), p.readback_slots.end(),
            [ticket](const auto& item) { return item.busy && item.ticket == ticket.value; });
        if (slot == p.readback_slots.end())
            return {OpenGlCompositionStatus::Unsupported, {}, {}, "collect-pbo-ticket",
                "The asynchronous readback ticket is unknown or has already been released."};
        const auto oldest = std::min_element(p.readback_slots.begin(), p.readback_slots.end(),
            [](const auto& a, const auto& b) {
                if (a.busy != b.busy) return a.busy;
                return a.busy && a.ticket < b.ticket;
            });
        if (oldest == p.readback_slots.end() || oldest != slot)
            return {OpenGlCompositionStatus::Busy, {}, {}, "collect-pbo-order",
                "Asynchronous readback tickets must be collected in submission order."};
        if (fail_collection_for_testing)
            return {OpenGlCompositionStatus::Failed, ticket, {}, "map-pbo",
                "Injected asynchronous readback collection failure for regression coverage.", -1};

        const auto wait_started = Clock::now();
        for (;;) {
            if (cancel && cancel()) return {OpenGlCompositionStatus::Cancelled};
            const auto status = p.gl->glClientWaitSync(slot->fence,
                GL_SYNC_FLUSH_COMMANDS_BIT, 1'000'000);
            if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) break;
            if (status == GL_WAIT_FAILED)
                return {OpenGlCompositionStatus::Failed, {}, {}, "wait-pbo-fence",
                    "The driver failed while waiting for the asynchronous readback fence.",
                    p.gl->glGetError()};
        }
        const auto wait_nanoseconds = elapsed(wait_started);
        if (cancel && cancel()) return {OpenGlCompositionStatus::Cancelled};
        const auto copy_started = Clock::now();
        media::RgbaFrame frame{slot->width, slot->height, slot->width * 4, {}};
        frame.rgba_pixels.resize(static_cast<std::size_t>(slot->bytes));
        p.gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, slot->buffer);
        const auto* mapped = static_cast<const std::uint8_t*>(p.gl->glMapBufferRange(
            GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(slot->bytes), GL_MAP_READ_BIT));
        if (!mapped) {
            p.gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            return {OpenGlCompositionStatus::Failed, {}, {}, "map-pbo",
                "The completed asynchronous readback buffer could not be mapped.",
                p.gl->glGetError()};
        }
        std::memcpy(frame.rgba_pixels.data(), mapped, frame.rgba_pixels.size());
        const bool unmapped = p.gl->glUnmapBuffer(GL_PIXEL_PACK_BUFFER) == GL_TRUE;
        p.gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        if (!unmapped)
            return {OpenGlCompositionStatus::Failed, {}, {}, "unmap-pbo",
                "The asynchronous readback buffer contents became invalid."};
        for (int y = 0; y < frame.height / 2; ++y) {
            auto first = frame.rgba_pixels.begin() + static_cast<std::size_t>(y) * frame.stride;
            auto last = frame.rgba_pixels.begin() + static_cast<std::size_t>(frame.height - 1 - y) * frame.stride;
            std::swap_ranges(first, first + frame.stride, last);
        }
        const auto copy_nanoseconds = elapsed(copy_started);
        p.gl->glDeleteSync(slot->fence);
        slot->fence = nullptr;
        slot->busy = false;
        slot->ticket = 0;
        OpenGlReadbackResult completed{OpenGlCompositionStatus::Complete, {}, std::move(frame)};
        completed.fence_wait_nanoseconds = wait_nanoseconds;
        completed.copy_nanoseconds = copy_nanoseconds;
        completed.bytes = slot->bytes;
        return completed;
    } catch (const std::exception& error) {
        return {OpenGlCompositionStatus::Failed, {}, {}, "collect-pbo-readback", error.what()};
    }
}

void OpenGlFrameCompositor::discardAsyncReadbacks() noexcept {
    auto& p = *impl_;
    if (!p.context || p.readback_slots.empty()) return;
    try {
        auto active = p.activate();
        if (active.status != OpenGlCompositionStatus::Complete) return;
        if (std::any_of(p.readback_slots.begin(), p.readback_slots.end(),
                [](const auto& slot) { return slot.busy; })) p.gl->glFinish();
        for (auto& slot : p.readback_slots) {
            if (slot.fence) p.gl->glDeleteSync(slot.fence);
            slot.fence = nullptr;
            slot.ticket = 0;
            slot.busy = false;
        }
        p.context->doneCurrent();
    } catch (...) {}
}

unsigned OpenGlFrameCompositor::asyncReadbackSlotCount() const noexcept {
    return static_cast<unsigned>(impl_->readback_slots.size());
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
        if (!p.image_operation && (!p.share_context || !QOpenGLContext::areSharing(p.context.get(), p.share_context)))
            return {OpenGlCompositionStatus::Unsupported, {}, "check-sharing",
                "The producer has no verified shared context."};
        if (!bounded(width, height, p.texture_limit) ||
            static_cast<std::uint64_t>(width) * height * 4 > p.budget->impl_->byte_limit)
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
        p.targets.reserve(p.budget->impl_->target_limit);
        {
            std::lock_guard lock(p.budget->impl_->mutex);
            if ((adding && p.budget->impl_->targets >= p.budget->impl_->target_limit) ||
                p.budget->impl_->bytes - old_bytes + bytes > p.budget->impl_->byte_limit)
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
    if (std::any_of(layers.begin(), layers.end(), [](const auto& layer) {
            return !layer.effect_stack.empty(); })) {
        auto prepared_layers = layers;
        for (auto& layer : prepared_layers) {
            if (layer.effect_stack.empty()) continue;
            if (!layer.gpu_color_adjustments.empty() || !layer.gpu_effects.empty())
                return result(OpenGlCompositionStatus::Unsupported, "prepare-clip-effects",
                    "A source cannot combine processed and unprocessed effect stacks.");
            auto passes = effects::colorAdjustmentPasses(layer.effect_stack);
            if (!passes) return result(OpenGlCompositionStatus::Unsupported, "prepare-clip-effects",
                "The clip effect stack is outside the shared GPU effect contract.");
            layer.gpu_color_adjustments = std::move(*passes);
            layer.effect_stack = {};
        }
        return render(width, height, prepared_layers, cancel, timings, read_output);
    }
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
        if (p.image_operation) return p.renderImage(*p.image_operation, cancel, measured);
        for (std::size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
            const auto& layer = layers[layer_index];
            if ((layer.texture_frame || layer.native_frame) && !layer.gpu_effects.empty())
                return result(OpenGlCompositionStatus::Unsupported, "check-texture-effects",
                    "Ordered blur effects on a borrowed texture require a separate image operation.");
            if (!layer.gpu_color_adjustments.empty() && !layer.gpu_effects.empty())
                return result(OpenGlCompositionStatus::Unsupported, "check-effects",
                    "A layer cannot combine legacy and ordered GPU effect lists.", 0,
                    static_cast<int>(layer_index));
            if (layer.gpu_color_adjustments.size() > 256)
                return result(OpenGlCompositionStatus::Unsupported, "check-effects",
                    "Color adjustment stack exceeds the 256-effect GPU limit.", 0,
                    static_cast<int>(layer_index));
            for (const auto& adjustment : layer.gpu_color_adjustments) {
                if (!std::isfinite(adjustment.brightness) || adjustment.brightness < -100.0 ||
                    adjustment.brightness > 100.0 ||
                    !std::isfinite(adjustment.contrast_percent) ||
                    adjustment.contrast_percent < 0.0 || adjustment.contrast_percent > 200.0 ||
                    !std::isfinite(adjustment.saturation_percent) ||
                    adjustment.saturation_percent < 0.0 || adjustment.saturation_percent > 200.0)
                    return result(OpenGlCompositionStatus::Unsupported, "check-effects",
                        "Color adjustment parameters are outside the shared effect contract.", 0,
                        static_cast<int>(layer_index));
            }
            if (layer.gpu_effects.size() > 256)
                return result(OpenGlCompositionStatus::Unsupported, "check-effects",
                    "Ordered effect stack exceeds the 256-effect GPU limit.", 0,
                    static_cast<int>(layer_index));
            bool has_gpu_effect_work = false;
            for (const auto& effect : layer.gpu_effects) {
                if (const auto* adjustment = std::get_if<effects::ColorAdjustmentParameters>(&effect)) {
                    if (!std::isfinite(adjustment->brightness) || adjustment->brightness < -100.0 ||
                        adjustment->brightness > 100.0 ||
                        !std::isfinite(adjustment->contrast_percent) ||
                        adjustment->contrast_percent < 0.0 || adjustment->contrast_percent > 200.0 ||
                        !std::isfinite(adjustment->saturation_percent) ||
                        adjustment->saturation_percent < 0.0 || adjustment->saturation_percent > 200.0)
                        return result(OpenGlCompositionStatus::Unsupported, "check-effects",
                            "Color adjustment parameters are outside the shared effect contract.", 0,
                            static_cast<int>(layer_index));
                    has_gpu_effect_work = true;
                } else {
                    const auto& blur = std::get<GpuGaussianBlurParameters>(effect);
                    if (!std::isfinite(blur.radius_pixels) || blur.radius_pixels < 0.0 ||
                        blur.radius_pixels > 100.0)
                        return result(OpenGlCompositionStatus::Unsupported, "check-effects",
                            "Gaussian Blur parameters are outside the shared effect contract.", 0,
                            static_cast<int>(layer_index));
                    has_gpu_effect_work = has_gpu_effect_work || std::lround(blur.radius_pixels) > 0;
                }
            }
            if (has_gpu_effect_work && usable(layer) &&
                static_cast<std::uint64_t>(layer.frame->width) * layer.frame->height * 4 >
                    maximum_effect_scratch_bytes)
                return result(OpenGlCompositionStatus::Unsupported, "check-effect-memory",
                    "Ordered GPU effects exceed the 64 MiB per-worker temporary texture limit.",
                    0, static_cast<int>(layer_index));
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
            struct NativeUse {
                detail::WindowsVideoInterop* adapter = nullptr;
                ~NativeUse() { if (adapter) (void)adapter->end(); }
            } native_use;
            if (layer.native_frame) {
                if (!p.video_interop) p.video_interop = std::make_unique<detail::WindowsVideoInterop>(p.context.get(), gl);
                const auto started = Clock::now();
                auto imported = p.video_interop->begin(layer.native_frame);
                if (imported.status != OpenGlCompositionStatus::Complete) return imported;
                native_use.adapter = p.video_interop.get();
                ++measured.native_video_imports;
                measured.native_video_conversion_nanoseconds += elapsed(started);
                p.rememberResourcePeak();
            }
            struct TextureUse {
                OpenGlTextureFramePtr frame; QOpenGLContext* context;
                ~TextureUse() {
                    if (frame) { std::string cause; std::int64_t code = 0; (void)frame->endUse(context, cause, code); }
                }
            } texture_use{layer.texture_frame, p.context.get()};
            if (layer.texture_frame) {
                std::string cause; std::int64_t code = 0;
                if (!layer.texture_frame->beginUse(p.context.get(), cause, code)) {
                    texture_use.frame.reset();
                    return result(OpenGlCompositionStatus::Failed, "consume-layer-texture", cause, code);
                }
            }
            p.program->setUniformValue("source_bottom_left", bool(layer.texture_frame));
            p.program->setUniformValue("color_adjustment_count",
                layer.gpu_effects.empty()
                    ? static_cast<int>(layer.gpu_color_adjustments.size()) : 0);
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
            if (!layer.texture_frame && !layer.native_frame) {
            gl->glBindTexture(GL_TEXTURE_2D, p.texture);
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
            }
            if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
            GLuint layer_texture = layer.native_frame ? p.video_interop->texture() :
                layer.texture_frame ? layer.texture_frame->texture() : p.texture;
            const bool has_gpu_effect_work = std::any_of(layer.gpu_effects.begin(),
                layer.gpu_effects.end(), [](const auto& effect) {
                    if (std::holds_alternative<effects::ColorAdjustmentParameters>(effect))
                        return true;
                    return std::lround(std::get<GpuGaussianBlurParameters>(effect).radius_pixels) > 0;
                });
            if (has_gpu_effect_work) {
                const int layer_index = static_cast<int>(&layer - layers.data());
                if (!p.effect_scratch || p.effect_scratch->width() != f.width ||
                    p.effect_scratch->height() != f.height) {
                    p.effect_scratch.reset();
                    QOpenGLFramebufferObjectFormat format;
                    format.setInternalTextureFormat(GL_RGBA8);
                    p.effect_scratch = std::make_unique<QOpenGLFramebufferObject>(
                        f.width, f.height, format);
                    if (!p.effect_scratch->isValid())
                        return result(OpenGlCompositionStatus::Failed, "allocate-effect-target",
                            "Cannot allocate the bounded RGBA8 effect target.", 0, layer_index);
                    p.rememberResourcePeak();
                }
                gl->glBindFramebuffer(GL_FRAMEBUFFER, p.effect_source_fbo);
                gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_2D, p.texture, 0);
                if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                    return result(OpenGlCompositionStatus::Failed, "attach-effect-source",
                        "Cannot attach the uploaded layer texture as an effect target.",
                        gl->glGetError(), layer_index);
                if (!p.effect_program->bind())
                    return result(OpenGlCompositionStatus::Failed, "bind-effect-shader",
                        "Cannot bind the ordered effect shader.", 0, layer_index);
                gl->glDisable(GL_BLEND);
                gl->glDisable(GL_DITHER);
                gl->glDisable(GL_FRAMEBUFFER_SRGB);
                gl->glViewport(0, 0, f.width, f.height);
                p.effect_program->setUniformValue("source_image", 0);
                const auto render_effect_pass = [&](GLuint source_texture,
                                                    GLuint destination_texture,
                                                    GLuint destination_fbo) {
                    if (cancelled()) return false;
                    gl->glBindFramebuffer(GL_FRAMEBUFFER, destination_fbo);
                    if (destination_fbo == p.effect_source_fbo) {
                        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, destination_texture, 0);
                        if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                            return false;
                    }
                    gl->glActiveTexture(GL_TEXTURE0);
                    gl->glBindTexture(GL_TEXTURE_2D, source_texture);
                    gl->glDrawArrays(GL_TRIANGLES, 0, 3);
                    return gl->glGetError() == GL_NO_ERROR;
                };
                GLuint current_texture = p.texture;
                for (const auto& effect : layer.gpu_effects) {
                    const auto effect_started = Clock::now();
                    if (const auto* adjustment =
                            std::get_if<effects::ColorAdjustmentParameters>(&effect)) {
                        p.effect_program->setUniformValue("effect_kind", 0);
                        p.effect_program->setUniformValue("color_parameters",
                            QVector3D(static_cast<float>(adjustment->brightness / 100.0),
                                static_cast<float>(adjustment->contrast_percent / 100.0),
                                static_cast<float>(adjustment->saturation_percent / 100.0)));
                        p.effect_program->setUniformValue("premultiply_alpha", false);
                        p.effect_program->setUniformValue("unpremultiply_alpha", false);
                        p.effect_program->setUniformValue("horizontal_pass", true);
                        p.effect_program->setUniformValue("blur_radius", 0);
                        const GLuint destination = current_texture == p.texture
                            ? p.effect_scratch->texture() : p.texture;
                        const GLuint framebuffer = destination == p.texture
                            ? p.effect_source_fbo : p.effect_scratch->handle();
                        if (!render_effect_pass(current_texture, destination, framebuffer)) {
                            if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
                            return result(OpenGlCompositionStatus::Failed, "apply-color-adjustment",
                                "OpenGL failed to process an ordered Color Adjustment.",
                                gl->glGetError(), layer_index);
                        }
                        current_texture = destination;
                        ++measured.color_adjustment_count;
                        measured.color_adjustment_submission_nanoseconds += elapsed(effect_started);
                    } else {
                        const auto& blur = std::get<GpuGaussianBlurParameters>(effect);
                        const int radius = static_cast<int>(std::lround(blur.radius_pixels));
                        if (radius <= 0) continue;
                        p.effect_program->setUniformValue("effect_kind", 1);
                        p.effect_program->setUniformValue("blur_radius", radius);
                        p.effect_program->setUniformValue("premultiply_alpha", true);
                        p.effect_program->setUniformValue("unpremultiply_alpha", false);
                        p.effect_program->setUniformValue("horizontal_pass", true);
                        GLuint destination = current_texture == p.texture
                            ? p.effect_scratch->texture() : p.texture;
                        GLuint framebuffer = destination == p.texture
                            ? p.effect_source_fbo : p.effect_scratch->handle();
                        if (!render_effect_pass(current_texture, destination, framebuffer)) {
                            if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
                            return result(OpenGlCompositionStatus::Failed, "premultiply-blur-input",
                                "OpenGL failed to premultiply Gaussian Blur input.",
                                gl->glGetError(), layer_index);
                        }
                        current_texture = destination;
                        p.effect_program->setUniformValue("premultiply_alpha", false);
                        for (int pass = 0; pass < 3; ++pass) {
                            for (const bool horizontal : {true, false}) {
                                p.effect_program->setUniformValue("horizontal_pass", horizontal);
                                destination = current_texture == p.texture
                                    ? p.effect_scratch->texture() : p.texture;
                                framebuffer = destination == p.texture
                                    ? p.effect_source_fbo : p.effect_scratch->handle();
                                if (!render_effect_pass(current_texture, destination, framebuffer)) {
                                    if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
                                    return result(OpenGlCompositionStatus::Failed,
                                        horizontal ? "blur-horizontal" : "blur-vertical",
                                        "OpenGL failed during a Gaussian Blur pass.",
                                        gl->glGetError(), layer_index);
                                }
                                current_texture = destination;
                            }
                        }
                        p.effect_program->setUniformValue("unpremultiply_alpha", true);
                        destination = current_texture == p.texture
                            ? p.effect_scratch->texture() : p.texture;
                        framebuffer = destination == p.texture
                            ? p.effect_source_fbo : p.effect_scratch->handle();
                        if (!render_effect_pass(current_texture, destination, framebuffer)) {
                            if (cancelled()) return result(OpenGlCompositionStatus::Cancelled);
                            return result(OpenGlCompositionStatus::Failed, "unpremultiply-blur-output",
                                "OpenGL failed to restore straight alpha after Gaussian Blur.",
                                gl->glGetError(), layer_index);
                        }
                        current_texture = destination;
                        p.effect_program->setUniformValue("unpremultiply_alpha", false);
                        ++measured.gaussian_blur_count;
                        measured.gaussian_blur_submission_nanoseconds += elapsed(effect_started);
                    }
                }
                layer_texture = current_texture;
                if (!output->bind() || !p.program->bind())
                    return result(OpenGlCompositionStatus::Failed, "restore-composition-target",
                        "Cannot restore the composition framebuffer after effects.", 0, layer_index);
                gl->glViewport(0, 0, width, height);
                gl->glEnable(GL_BLEND);
                gl->glBlendEquation(GL_FUNC_ADD);
                gl->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                    GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            }
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
            gl->glActiveTexture(GL_TEXTURE0);
            gl->glBindTexture(GL_TEXTURE_2D, layer_texture);
            gl->glDrawArrays(GL_TRIANGLES, 0, 3);
            if (native_use.adapter) {
                auto released = native_use.adapter->end(); native_use.adapter = nullptr;
                if (released.status != OpenGlCompositionStatus::Complete) return released;
            }
            if (texture_use.frame) {
                std::string cause; std::int64_t code = 0;
                if (!texture_use.frame->endUse(p.context.get(), cause, code))
                    return result(OpenGlCompositionStatus::Failed, "protect-layer-texture", cause, code);
                texture_use.frame.reset();
            }
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
