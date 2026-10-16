#pragma once
#include <creative_suite/composition/gpu_frame_compositor.h>
#include <QString>
#include <array>
class QWindow;

namespace creative_suite::composition {
// Opaque allocation; no Vulkan or platform headers cross this boundary.
struct VulkanImage {
    int width = 0, height = 0;
    std::shared_ptr<class VulkanDevice> device;
    std::shared_ptr<void> allocation;
};
class VulkanDevice final : public std::enable_shared_from_this<VulkanDevice> {
public:
    // Create/destroy on the GUI thread before/after all workers and windows.
    static std::shared_ptr<VulkanDevice> create(std::string& cause);
    ~VulkanDevice();
    QString name() const;
    bool supportsPreciseRotation() const noexcept;
    bool valid() const noexcept;
    void attachWindow(QWindow*);
    // False with an empty cause means worker contention: retry on a later GUI update.
    // The application supplies normalized RGBA for the area outside the frame.
    bool present(QWindow*, const OpenGlTextureFramePtr&, bool grayscale, std::string& cause,
        std::array<float, 4> surround = {0, 0, 0, 1});
    void releaseWindow(QWindow*) noexcept;
    std::uint64_t reservedBytes() const noexcept;
    std::uint64_t peakBytes() const noexcept;
    struct Impl;
private:
    VulkanDevice();
    std::unique_ptr<Impl> impl_;
    friend class VulkanFrameCompositor;
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
};
}
