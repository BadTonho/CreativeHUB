#pragma once
#include <creative_suite/composition/gpu_frame_compositor.h>
#include <QString>
#include <array>
#include <optional>
class QWindow;

namespace creative_suite::composition {
// Opaque allocation; no Vulkan or platform headers cross this boundary.
struct VulkanImage {
    int width = 0, height = 0;
    std::shared_ptr<class VulkanDevice> device;
    std::shared_ptr<void> allocation;
    // Backend-private completion token; same-queue consumers synchronize on GPU.
    std::shared_ptr<void> completion;
};
class VulkanDevice final : public std::enable_shared_from_this<VulkanDevice> {
public:
    // Create/destroy on the GUI thread before/after all workers and windows.
    static std::shared_ptr<VulkanDevice> create(std::string& cause);
    // Opt-in research configuration. Normal editor creation keeps these defaults.
    struct CreateOptions {
        bool experimental_native_export = false;
        // Opportunistic editor request: missing interop must not disable Vulkan.
        bool try_native_export = false;
        std::optional<std::array<std::uint8_t, 8>> adapter_luid;
        std::uint64_t known_byte_limit = 256ULL * 1024 * 1024;
        std::uint64_t external_reserved_bytes = 0;
    };
    static std::shared_ptr<VulkanDevice> create(std::string& cause, const CreateOptions&);
    ~VulkanDevice();
    QString name() const;
    bool supportsPreciseRotation() const noexcept;
    bool valid() const noexcept;
    bool supportsNativeExport() const noexcept;
    // Nonblocking admission; empty means occupied/unsupported. The opaque lease
    // retains this device and must outlive every external consumer/allocation.
    std::shared_ptr<void> tryReserveExternal(std::uint64_t bytes);
    std::uint64_t totalReservedBytes() const noexcept;
    std::uint64_t totalPeakBytes() const noexcept;
    void attachWindow(QWindow*);
    // False with an empty cause means a deferred presentation; retry on a later
    // GUI update (zero extent instead waits for expose/resize).
    // The application supplies normalized RGBA for the area outside the frame.
    bool present(QWindow*, const OpenGlTextureFramePtr&, bool grayscale, std::string& cause,
        std::array<float, 4> surround = {0, 0, 0, 1});
    enum class PresentStatus { Submitted, Deferred, Failed };
    enum class PresentDeferral { None, DeviceBusy, ContextBusy, AcquireBusy, ZeroExtent, Recreate };
    enum class CompletionStatus { Pending, Complete, Failed };
    // Receipts own only a completion token and a weak device identity. GUI-thread
    // polling never waits; completion means GPU commands, not physical display.
    class PresentationReceipt {
        std::weak_ptr<VulkanDevice> owner_;
        std::shared_ptr<void> token_;
        friend class VulkanDevice;
    };
    struct PresentationResult {
        PresentStatus status = PresentStatus::Deferred;
        PresentDeferral deferral = PresentDeferral::None;
        PresentationReceipt receipt;
        std::string cause;
    };
    // The final argument simulates an unsignaled, unavailable acquisition in
    // native regression tests; production callers retain its default.
    PresentationResult presentFrame(QWindow*, const OpenGlTextureFramePtr&, bool grayscale,
        std::array<float, 4> surround = {0, 0, 0, 1}, bool defer_acquisition_for_testing = false);
    CompletionStatus pollPresentation(const PresentationReceipt&, std::string& cause) noexcept;
    void releaseWindow(QWindow*) noexcept;
    std::uint64_t reservedBytes() const noexcept;
    std::uint64_t peakBytes() const noexcept;
    struct Impl;
    struct Diagnostics {
        std::uint64_t submissions = 0, waits = 0, wait_nanoseconds = 0;
        unsigned pending = 0, peak_pending = 0;
        // Disjoint known allocation classes; pending takes priority over leases.
        std::uint64_t active_bytes = 0, pending_bytes = 0, cached_bytes = 0;
        std::uint64_t external_bytes = 0, known_bytes = 0;
    };
    Diagnostics diagnostics() const noexcept;
private:
    VulkanDevice();
    std::unique_ptr<Impl> impl_;
    friend class VulkanFrameCompositor;
    friend class VulkanNativeExportBridge;
    friend class VulkanNativeImportBridge;
};
void setSessionVulkanDevice(const std::shared_ptr<VulkanDevice>&);
std::shared_ptr<VulkanDevice> sessionVulkanDevice();

class VulkanFrameCompositor final : public GpuFrameCompositor {
public:
    explicit VulkanFrameCompositor(std::shared_ptr<VulkanDevice> = sessionVulkanDevice(),
        std::uint64_t byte_limit = 64ULL*1024*1024, unsigned target_limit = 3,
        std::shared_ptr<OpenGlTexturePoolBudget> budget = {});
    ~VulkanFrameCompositor() override;
    GraphicsBackend backend() const noexcept override { return GraphicsBackend::Vulkan; }
    // GUI upload adapter: never waits for a recording context or device lock.
    OpenGlTextureCompositionResult tryUpload(const media::RgbaFrame&);
    std::unique_ptr<GpuCommandBatch> beginCommandBatch(const CancellationPredicate& = {}) override;
    OpenGlCompositionResult prepareAsyncReadback(int, int, std::uint64_t, unsigned = 2,
        unsigned* = nullptr, bool = false) override;
    OpenGlReadbackResult submitAsyncReadback(int, int, const std::vector<CompositionLayer>&,
        const CancellationPredicate& = {}) override;
    OpenGlReadbackResult submitAsyncReadback(const OpenGlTextureFramePtr&,
        const CancellationPredicate& = {}) override;
    OpenGlReadbackResult collectAsyncReadback(OpenGlReadbackTicket,
        const CancellationPredicate& = {}, bool = false) override;
    void discardAsyncReadbacks() noexcept override;
    unsigned asyncReadbackSlotCount() const noexcept override;
    OpenGlCompositionResult compose(int,int,const std::vector<CompositionLayer>&,
        const CancellationPredicate& = {}, OpenGlCompositionTimings* = nullptr) override;
    OpenGlTextureCompositionResult composeTexture(int,int,const std::vector<CompositionLayer>&,
        const CancellationPredicate& = {}, OpenGlCompositionTimings* = nullptr) override;
    OpenGlTextureCompositionResult processImage(const OpenGlImageOperation&,
        const CancellationPredicate& = {}, OpenGlCompositionTimings* = nullptr) override;
    OpenGlTextureCompositionResult importFrame(const std::shared_ptr<const media::NativeVideoFrame>&,
        const CancellationPredicate& = {}, OpenGlCompositionTimings* = nullptr) override;
    OpenGlCompositionResult readback(const OpenGlTextureFramePtr&,
        const CancellationPredicate& = {}, OpenGlCompositionTimings* = nullptr) override;
    OpenGlCompositionResult copyToNative(const OpenGlTextureFramePtr&,
        const std::shared_ptr<const media::NativeVideoFrame>&, const CancellationPredicate& = {}) override;
    void retireTextureFrames() noexcept override {}
    OpenGlCompositionResult collectReleasedTextureFrames() override;
    bool hasPendingTextureFrames() const override { return false; }
    std::uint64_t texturePoolBytes() const noexcept override;
    unsigned texturePoolOccupancy() const override;
    OpenGlResourceUsage resourceUsage() const noexcept override;
private:
    std::shared_ptr<VulkanDevice> device_;
    std::uint64_t byte_limit_;
    unsigned target_limit_;
    std::shared_ptr<OpenGlTexturePoolBudget> budget_;
    OpenGlTextureFramePtr publish(std::shared_ptr<VulkanImage>);
    void checkCapacity(int width, int height);
    std::vector<std::weak_ptr<const OpenGlTextureFrame>> outputs_;
    struct Readbacks;
    std::unique_ptr<Readbacks> readbacks_;
};
}
