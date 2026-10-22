#pragma once
#include <creative_suite/composition/gpu_frame_compositor.h>

namespace creative_suite::composition {
// Optional Qt/OpenGL adapter. The CPU compositor and frame contract remain Qt
// independent. Source pixels and the GUI-created surface are borrowed.
class OpenGlFrameCompositor final : public GpuFrameCompositor {
public:
    GraphicsBackend backend() const noexcept override { return GraphicsBackend::OpenGl; }
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
        OpenGlCompositionTimings* timings = nullptr,
        CompositionOutputAlpha output_alpha = CompositionOutputAlpha::Opaque) override;

    using GpuFrameCompositor::submitAsyncReadback;

    // Configure a bounded PBO pool on the compositor worker. The returned
    // slot count is at most two and accounts for the supplied staging budget.
    // Unsupported means callers should retain the synchronous GPU path.
    [[nodiscard]] OpenGlCompositionResult prepareAsyncReadback(
        int width, int height, std::uint64_t staging_budget_bytes,
        unsigned maximum_slots = 2, unsigned* prepared_slots = nullptr,
        bool fail_allocation_for_testing = false);
    [[nodiscard]] OpenGlReadbackResult submitAsyncReadback(
        int width, int height, const std::vector<CompositionLayer>& layers,
        const CancellationPredicate& cancel = {},
        CompositionOutputAlpha output_alpha = CompositionOutputAlpha::Opaque) override;
    [[nodiscard]] OpenGlReadbackResult collectAsyncReadback(
        OpenGlReadbackTicket ticket, const CancellationPredicate& cancel = {},
        bool fail_collection_for_testing = false);
    // Worker-only. Completes outstanding transfers before making their PBOs
    // reusable. Used during cancellation and GPU-to-CPU recovery.
    void discardAsyncReadbacks() noexcept;
    [[nodiscard]] unsigned asyncReadbackSlotCount() const noexcept;

    // No pixel readback. Busy means bounded backpressure, not a technical error.
    // A shared budget also includes targets from retiring compositor sessions.
    [[nodiscard]] OpenGlTextureCompositionResult composeTexture(
        int width, int height, const std::vector<CompositionLayer>& layers,
        const CancellationPredicate& cancel = {}, OpenGlCompositionTimings* timings = nullptr);
    // Transparent premultiplied output; no video fitting or readback. Source
    // texture cache and group scratch share the supplied pool's byte budget.
    [[nodiscard]] OpenGlTextureCompositionResult composeRasterTexture(
        const OpenGlRasterRequest&, const CancellationPredicate& cancel = {},
        OpenGlCompositionTimings* timings = nullptr);
    // Transparent RGBA8 intermediates for graph evaluation. Input leases are
    // synchronized, source alpha is retained, and Merge uses straight alpha.
    [[nodiscard]] OpenGlTextureCompositionResult processImage(
        const OpenGlImageOperation&, const CancellationPredicate& cancel = {},
        OpenGlCompositionTimings* timings = nullptr);
    [[nodiscard]] OpenGlTextureCompositionResult importFrame(
        const std::shared_ptr<const media::NativeVideoFrame>&,
        const CancellationPredicate& cancel = {}, OpenGlCompositionTimings* timings = nullptr);
    // Copies to a retained BGRA encoder surface entirely on the GPU. The
    // caller submits that surface only after Complete; no readback is made.
    [[nodiscard]] OpenGlCompositionResult copyToNative(const OpenGlTextureFramePtr&,
        const std::shared_ptr<const media::NativeVideoFrame>&,
        const CancellationPredicate& cancel = {});
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
        OpenGlCompositionTimings*, bool read_output,
        CompositionOutputAlpha = CompositionOutputAlpha::Opaque);
};

} // namespace creative_suite::composition
