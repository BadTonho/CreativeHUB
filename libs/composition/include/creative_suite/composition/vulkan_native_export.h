#pragma once

#include <creative_suite/composition/vulkan_frame_compositor.h>
#include <creative_suite/media/native_video_frame.h>

namespace creative_suite::composition {

// Experimental worker-owned Windows bridge. No application selects it implicitly.
// Tickets retain their source/destination through GPU completion. The caller keeps
// the GUI-created device alive until the worker, bridge and encoder have stopped.
struct VulkanNativeExportTicket { std::uint64_t value=0; };
struct VulkanNativeExportResult {
    GpuCompositionStatus status=GpuCompositionStatus::Failed;
    VulkanNativeExportTicket ticket;
    media::NativeVideoFramePtr frame;
    std::string cause;
    std::int64_t error_code=0;
};
class VulkanNativeExportBridge final {
public:
    explicit VulkanNativeExportBridge(std::shared_ptr<VulkanDevice>);
    ~VulkanNativeExportBridge();
    static std::optional<std::array<std::uint8_t,8>> adapterLuid(
        const media::NativeVideoFramePtr&,std::string& cause);
    GpuCompositionResult prepare(const media::NativeVideoFramePtr&,unsigned maximum=2,
        bool reject_format_for_testing=false);
    VulkanNativeExportResult submit(const GpuTextureFramePtr&,
        const media::NativeVideoFramePtr&,const GpuFrameCompositor::CancellationPredicate& = {});
    VulkanNativeExportResult collect(VulkanNativeExportTicket,
        const GpuFrameCompositor::CancellationPredicate& = {});
    void discard() noexcept;
    unsigned slotCount() const noexcept;
    struct Metrics {
        std::uint64_t submissions=0,collections=0,submit_nanoseconds=0,wait_nanoseconds=0;
        std::uint64_t vulkan_blit_bytes=0,d3d_copy_bytes=0;
        unsigned peak_pending=0;
    };
    Metrics metrics() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace creative_suite::composition
