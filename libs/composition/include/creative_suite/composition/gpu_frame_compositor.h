#pragma once

#include <creative_suite/composition/frame_compositor.h>
#include <creative_suite/composition/graphics_backend.h>
#include <creative_suite/effects/drop_shadow.h>
#include <creative_suite/effects/gradient_map.h>

#include <functional>
#include <memory>
#include <string>
#include <QImage>
#include <QRect>
#include <QTransform>

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
    std::uint64_t native_video_imports = 0;
    std::uint64_t native_video_conversion_nanoseconds = 0;
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
    explicit OpenGlTexturePoolBudget(std::uint64_t byte_limit = maximum_bytes,
        unsigned target_limit = maximum_targets);
    ~OpenGlTexturePoolBudget();
    [[nodiscard]] std::uint64_t bytes() const noexcept;
    [[nodiscard]] unsigned targets() const noexcept;
    [[nodiscard]] std::uint64_t byteLimit() const noexcept;
    [[nodiscard]] unsigned targetLimit() const noexcept;
    // Shared reservations include leases retained after their compositor retires.
    [[nodiscard]] bool tryReserve(std::uint64_t bytes);
    void releaseReservation(std::uint64_t bytes) noexcept;
private:
    friend class OpenGlFrameCompositor;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class VulkanFrameCompositor;
class VulkanDevice;
struct VulkanImage;

class OpenGlTextureFrame final {
public:
    ~OpenGlTextureFrame();
    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;
    [[nodiscard]] unsigned texture() const noexcept;
    [[nodiscard]] std::uint64_t session() const noexcept;
    [[nodiscard]] bool valid() const noexcept;
    // FBO textures have a bottom-left origin, unlike the top-down RGBA output.
    [[nodiscard]] bool bottomLeftOrigin() const noexcept;
    [[nodiscard]] GraphicsBackend backend() const noexcept;
    [[nodiscard]] std::shared_ptr<VulkanImage> vulkanImage() const noexcept { return vulkan_; }
    // Call around a draw with the consumer context current. Wait is GPU-side;
    // no CPU wait/readback occurs. Sync objects are collected by the producer.
    [[nodiscard]] bool beginUse(QOpenGLContext*, std::string& cause, std::int64_t& code) const;
    [[nodiscard]] bool endUse(QOpenGLContext*, std::string& cause, std::int64_t& code) const;
private:
    friend class OpenGlFrameCompositor;
    friend class VulkanFrameCompositor;
    friend class VulkanNativeImportBridge;
    struct State;
    explicit OpenGlTextureFrame(std::shared_ptr<State>);
    std::shared_ptr<State> state_;
    explicit OpenGlTextureFrame(std::shared_ptr<VulkanImage> image);
    std::shared_ptr<VulkanImage> vulkan_;
    std::shared_ptr<void> reservation_;
};
using OpenGlTextureFramePtr = std::shared_ptr<const OpenGlTextureFrame>;

struct OpenGlTextureCompositionResult {
    OpenGlCompositionStatus status = OpenGlCompositionStatus::Failed;
    OpenGlTextureFramePtr frame;
    std::string operation;
    std::string cause;
    std::int64_t error_code = 0;
};

struct OpenGlImageInput {
    const media::RgbaFrame* rgba = nullptr;
    OpenGlTextureFramePtr texture;
    std::shared_ptr<const media::NativeVideoFrame> native;
    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;
};
enum class OpenGlImageOperationKind { Copy, Transform, Color, Merge, ApplyMask, MaskedBlend, GaussianBlur, ApplyTrackMatte, DropShadow };
struct OpenGlImageOperation {
    OpenGlImageOperationKind kind = OpenGlImageOperationKind::Copy;
    OpenGlImageInput background;
    OpenGlImageInput foreground;
    animation::Transform2D transform;
    std::vector<effects::ColorAdjustmentParameters> colors;
    OpenGlImageInput mask;
    // Optional canvas for an intrinsic source transformed into a composition.
    // Zero preserves the source-sized legacy image-operation contract.
    int output_width = 0, output_height = 0;
    double blur_radius_pixels = 0;
    // Only Transform accepts this source-pixel to output-pixel override.
    std::optional<AffineTransform2D> source_to_canvas;
    // ApplyTrackMatte uses background as target and mask as straight RGBA source.
    // An empty mask means transparent; dimensions otherwise match the target.
    TrackMatteMode track_matte_mode = TrackMatteMode::Alpha;
    effects::DropShadowParameters drop_shadow;
};

// Optional raster contract: top-down ARGB32 premultiplied content and ARGB32
// premultiplied or Grayscale8 coverage masks in document pixel coordinates.
// QImage supplies row stride and native channel layout; no aspect fit is applied.
// Images are implicitly shared immutable inputs for this call. Empty masks reveal
// all pixels. Gray coverage multiplies every premultiplied channel before opacity.
// Group children compose first, then ordered transforms and group opacity once.
// A partial upload is valid only when previous_key matches the resident image.
struct OpenGlRasterInput {
    std::string identity;
    QImage pixels;
    qint64 previous_key = 0;
    QRect dirty;
    // Stable semantic revision survives CPU pixel eviction. Zero uses QImage cacheKey.
    qint64 revision = 0;
};
struct OpenGlRasterTransform {
    QTransform matrix;
    QRect clip;
};
struct OpenGlRasterLayer {
    OpenGlRasterInput content;
    OpenGlRasterInput mask;
    int opacity = 100;
    BlendMode blend_mode = BlendMode::Normal;
    std::optional<effects::GradientMapParameters> adjustment;
};
struct OpenGlRasterNode {
    std::vector<OpenGlRasterLayer> layers;
    std::vector<OpenGlRasterTransform> transforms;
    int opacity = 100;
    bool group = false;
    BlendMode blend_mode = BlendMode::Normal;
};
struct OpenGlRasterRequest {
    QSize size;
    std::vector<OpenGlRasterNode> nodes;
};

// Opaque identity for a submitted asynchronous RGBA readback. Tickets are
// valid only for the compositor that created them and must be collected FIFO.
struct OpenGlReadbackTicket {
    std::uint64_t value = 0;
    friend bool operator==(const OpenGlReadbackTicket&, const OpenGlReadbackTicket&) = default;
};

struct OpenGlReadbackResult {
    OpenGlCompositionStatus status = OpenGlCompositionStatus::Failed;
    OpenGlReadbackTicket ticket;
    std::optional<media::RgbaFrame> frame;
    std::string operation;
    std::string cause;
    std::int64_t error_code = 0;
    OpenGlCompositionTimings composition_timings;
    std::uint64_t submission_nanoseconds = 0;
    std::uint64_t fence_wait_nanoseconds = 0;
    std::uint64_t copy_nanoseconds = 0;
    std::uint64_t bytes = 0;
};

// Neutral names coexist with legacy OpenGl names for existing consumers.
using GpuTextureFramePtr = OpenGlTextureFramePtr;
using GpuCompositionResult = OpenGlCompositionResult;
using GpuCompositionStatus = OpenGlCompositionStatus;
using GpuCompositionTimings = OpenGlCompositionTimings;
using GpuTextureCompositionResult = OpenGlTextureCompositionResult;
using GpuImageOperation = OpenGlImageOperation;
using GpuImageInput = OpenGlImageInput;
using GpuReadbackTicket = OpenGlReadbackTicket;
using GpuReadbackResult = OpenGlReadbackResult;

// Worker-owned optional recording scope. finish() submits work, not a CPU wait.
// Destruction retires any recorded work safely, including cancelled requests.
class GpuCommandBatch {
public:
    virtual ~GpuCommandBatch() = default;
    virtual GpuCompositionResult finish() = 0;
};

// Worker-owned processing interface. Inputs are borrowed during calls; output leases
// are immutable and retain their device. Cancellation never publishes partial output.
class GpuFrameCompositor {
public:
    using CancellationPredicate = std::function<bool()>;
    virtual ~GpuFrameCompositor() = default;
    virtual GraphicsBackend backend() const noexcept = 0;
    virtual std::unique_ptr<GpuCommandBatch> beginCommandBatch(const CancellationPredicate& = {}) { return {}; }
    virtual OpenGlCompositionResult prepareAsyncReadback(int, int, std::uint64_t,
        unsigned = 2, unsigned* prepared_slots = nullptr, bool = false) {
        if (prepared_slots) *prepared_slots = 0;
        return {OpenGlCompositionStatus::Unsupported};
    }
    virtual OpenGlReadbackResult submitAsyncReadback(int, int, const std::vector<CompositionLayer>&,
        const CancellationPredicate& = {}) { return {OpenGlCompositionStatus::Unsupported}; }
    virtual OpenGlReadbackResult submitAsyncReadback(const OpenGlTextureFramePtr&,
        const CancellationPredicate& = {}) { return {OpenGlCompositionStatus::Unsupported}; }
    virtual OpenGlReadbackResult collectAsyncReadback(OpenGlReadbackTicket,
        const CancellationPredicate& = {}, bool = false) { return {OpenGlCompositionStatus::Unsupported}; }
    virtual void discardAsyncReadbacks() noexcept {}
    virtual unsigned asyncReadbackSlotCount() const noexcept { return 0; }
    virtual OpenGlCompositionResult compose(int, int, const std::vector<CompositionLayer>&,
        const CancellationPredicate& = {}, OpenGlCompositionTimings* = nullptr) = 0;
    virtual OpenGlTextureCompositionResult composeTexture(int, int, const std::vector<CompositionLayer>&,
        const CancellationPredicate& = {}, OpenGlCompositionTimings* = nullptr) = 0;
    virtual OpenGlTextureCompositionResult processImage(const OpenGlImageOperation&,
        const CancellationPredicate& = {}, OpenGlCompositionTimings* = nullptr) = 0;
    virtual OpenGlTextureCompositionResult importFrame(const std::shared_ptr<const media::NativeVideoFrame>&,
        const CancellationPredicate& = {}, OpenGlCompositionTimings* = nullptr) = 0;
    virtual OpenGlCompositionResult readback(const OpenGlTextureFramePtr&,
        const CancellationPredicate& = {}, OpenGlCompositionTimings* = nullptr) = 0;
    virtual OpenGlCompositionResult copyToNative(const OpenGlTextureFramePtr&,
        const std::shared_ptr<const media::NativeVideoFrame>&, const CancellationPredicate& = {}) = 0;
    virtual void retireTextureFrames() noexcept = 0;
    virtual OpenGlCompositionResult collectReleasedTextureFrames() = 0;
    virtual bool hasPendingTextureFrames() const = 0;
    virtual std::uint64_t texturePoolBytes() const noexcept = 0;
    virtual unsigned texturePoolOccupancy() const = 0;
    virtual OpenGlResourceUsage resourceUsage() const noexcept = 0;
};
std::unique_ptr<GpuFrameCompositor> createGpuFrameCompositor(QOffscreenSurface*,
    OpenGlPrecisionPolicy = OpenGlPrecisionPolicy::Automatic, QOpenGLContext* = nullptr,
    std::shared_ptr<OpenGlTexturePoolBudget> = {},
    GraphicsBackend = sessionGraphicsBackend());
} // namespace creative_suite::composition
