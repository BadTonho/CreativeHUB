#pragma once

#include <creative_suite/composition/frame_compositor.h>

#include <functional>
#include <memory>
#include <string>

class QOffscreenSurface;
class QOpenGLContext;

namespace creative_suite::composition {

enum class OpenGlCompositionStatus { Complete, Cancelled, Unsupported, Failed, Busy };
// CoreOnly also permits capability/fallback regression on drivers advertising
// extensions. Rotated requests require precise FP64 arithmetic in Automatic.
enum class OpenGlPrecisionPolicy { Automatic, CoreOnly };

struct OpenGlCompositionTimings {
    // CPU wall time: submission is not a GPU execution timer. Readback includes
    // the wait for submitted work and the conversion to top-down RGBA rows.
    std::uint64_t upload_nanoseconds = 0;
    std::uint64_t draw_submission_nanoseconds = 0;
    std::uint64_t readback_nanoseconds = 0;
    std::uint64_t uploaded_bytes = 0;
    std::uint64_t readback_bytes = 0;
    std::uint64_t uploaded_layers = 0;
    std::uint64_t producer_fence_submission_nanoseconds = 0;
    std::uint64_t color_adjustment_submission_nanoseconds = 0;
    std::uint64_t color_adjustment_count = 0;
    std::uint64_t gaussian_blur_submission_nanoseconds = 0;
    std::uint64_t gaussian_blur_count = 0;
};

struct OpenGlCompositionResult {
    OpenGlCompositionStatus status = OpenGlCompositionStatus::Failed;
    std::optional<media::RgbaFrame> frame;
    std::string operation;
    std::string cause;
    std::int64_t error_code = 0;
    // Layer index in the submitted composition when a layer effect failed;
    // -1 means the failure was not associated with one layer.
    int layer_index = -1;
};

// Known storage requested by this compositor, not driver-measured VRAM.
struct OpenGlResourceUsage {
    std::uint64_t texture_bytes = 0;
    std::uint64_t geometry_buffer_bytes = 0;
    std::uint64_t peak_known_bytes = 0;
};

// Shared by active and retiring compositors. Reservations include retired
// targets, and are released only by the owning worker's resource cleanup.
class OpenGlTexturePoolBudget final {
public:
    static constexpr std::uint64_t maximum_bytes = 64ULL * 1024 * 1024;
    static constexpr unsigned maximum_targets = 3;
    OpenGlTexturePoolBudget();
    ~OpenGlTexturePoolBudget();
    [[nodiscard]] std::uint64_t bytes() const noexcept;
    [[nodiscard]] unsigned targets() const noexcept;
private:
    friend class OpenGlFrameCompositor;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class OpenGlTextureFrame final {
public:
    ~OpenGlTextureFrame();
    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;
    [[nodiscard]] unsigned texture() const noexcept;
    [[nodiscard]] std::uint64_t session() const noexcept;
    [[nodiscard]] bool valid() const noexcept;
    // FBO textures have a bottom-left origin, unlike the top-down RGBA output.
    [[nodiscard]] bool bottomLeftOrigin() const noexcept { return true; }
    // Call around a draw with the consumer context current. Wait is GPU-side;
    // no CPU wait/readback occurs. Sync objects are collected by the producer.
    [[nodiscard]] bool beginUse(QOpenGLContext*, std::string& cause, std::int64_t& code) const;
    [[nodiscard]] bool endUse(QOpenGLContext*, std::string& cause, std::int64_t& code) const;
private:
    friend class OpenGlFrameCompositor;
    struct State;
    explicit OpenGlTextureFrame(std::shared_ptr<State>);
    std::shared_ptr<State> state_;
};
using OpenGlTextureFramePtr = std::shared_ptr<const OpenGlTextureFrame>;

struct OpenGlTextureCompositionResult {
    OpenGlCompositionStatus status = OpenGlCompositionStatus::Failed;
    OpenGlTextureFramePtr frame;
    std::string operation;
    std::string cause;
    std::int64_t error_code = 0;
};

// Optional Qt/OpenGL adapter. The CPU compositor and frame contract remain Qt
// independent. Source pixels and the GUI-created surface are borrowed.
class OpenGlFrameCompositor final {
public:
    using CancellationPredicate = std::function<bool()>;

    // Call and destroy the returned surface on the GUI thread. Keep it alive
    // until every compositor using it has been destroyed on its worker thread.
    [[nodiscard]] static std::unique_ptr<QOffscreenSurface> createSurface();

    // Construct, compose, and destroy on one worker thread. Context and all GL
    // resources are created lazily there. Pass Qt's stable global share context
    // for texture delivery; never operate that borrowed context on the worker.
    explicit OpenGlFrameCompositor(QOffscreenSurface* surface,
        OpenGlPrecisionPolicy precision = OpenGlPrecisionPolicy::Automatic,
        QOpenGLContext* share_context = nullptr,
        std::shared_ptr<OpenGlTexturePoolBudget> budget = {});
    ~OpenGlFrameCompositor();
    OpenGlFrameCompositor(const OpenGlFrameCompositor&) = delete;
    OpenGlFrameCompositor& operator=(const OpenGlFrameCompositor&) = delete;

    // Same raster contract as FrameCompositor. Invalid layers contribute no
    // pixels. Device/request limits return Unsupported; technical errors return
    // Failed. Neither status contains a partially composed frame.
    [[nodiscard]] OpenGlCompositionResult compose(
        int width, int height, const std::vector<CompositionLayer>& layers,
        const CancellationPredicate& cancel = {},
        OpenGlCompositionTimings* timings = nullptr);

    // No pixel readback. Busy means bounded backpressure, not a technical error.
    // A shared budget also includes targets from retiring compositor sessions.
    [[nodiscard]] OpenGlTextureCompositionResult composeTexture(
        int width, int height, const std::vector<CompositionLayer>& layers,
        const CancellationPredicate& cancel = {}, OpenGlCompositionTimings* timings = nullptr);
    // Worker-only recovery of a retained lease from this compositor session.
    [[nodiscard]] OpenGlCompositionResult readback(const OpenGlTextureFramePtr&,
        const CancellationPredicate& cancel = {}, OpenGlCompositionTimings* timings = nullptr);
    // Worker-only, nonblocking maintenance. Busy is expected backpressure.
    void retireTextureFrames() noexcept;
    [[nodiscard]] OpenGlCompositionResult collectReleasedTextureFrames();
    [[nodiscard]] bool hasPendingTextureFrames() const;
    [[nodiscard]] std::uint64_t texturePoolBytes() const noexcept;
    [[nodiscard]] unsigned texturePoolOccupancy() const;
    [[nodiscard]] OpenGlResourceUsage resourceUsage() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    [[nodiscard]] OpenGlCompositionResult render(int, int,
        const std::vector<CompositionLayer>&, const CancellationPredicate&,
        OpenGlCompositionTimings*, bool read_output);
};

} // namespace creative_suite::composition
