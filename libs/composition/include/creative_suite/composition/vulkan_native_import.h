#pragma once
#include <creative_suite/composition/vulkan_frame_compositor.h>
#include <creative_suite/media/native_video_frame.h>

namespace creative_suite::composition {
struct VulkanNativeImportTicket { std::uint64_t value=0; };
struct VulkanNativeImportResult {
    GpuCompositionStatus status=GpuCompositionStatus::Failed;
    VulkanNativeImportTicket ticket;
    GpuTextureFramePtr frame;
    std::string cause;
    std::int64_t error_code=0;
};
// Explicit experimental worker-owned bridge; not selected by application importFrame.
// prepare checks identity NV12 storage and adapter identity. A completed submission
// returns a GPU lease before physical completion; collect retires the source FIFO.
// Keep the GUI-created device alive until every worker and consumer has stopped.
class VulkanNativeImportBridge final {
public:
    explicit VulkanNativeImportBridge(std::shared_ptr<VulkanDevice>);
    ~VulkanNativeImportBridge();
    static std::optional<std::array<std::uint8_t,8>> adapterLuid(const media::NativeVideoFramePtr&,std::string& cause);
    GpuCompositionResult prepare(const media::NativeVideoFramePtr&,unsigned maximum=2,
        bool reject_format_for_testing=false);
    // Fault stages: 1 after D3D conversion while owning the mutex, 2 after
    // recording external Vulkan commands, 3 after the D3D handoff.
    VulkanNativeImportResult submit(const media::NativeVideoFramePtr&,
        const GpuFrameCompositor::CancellationPredicate& = {},unsigned fail_stage_for_testing=0);
    VulkanNativeImportResult collect(VulkanNativeImportTicket,
        const GpuFrameCompositor::CancellationPredicate& = {},bool fail_for_testing=false);
    void discard() noexcept;
    bool isRetired() const noexcept;
    unsigned slotCount() const noexcept;
    struct Metrics {
        std::uint64_t submissions=0,collections=0,submit_nanoseconds=0,wait_nanoseconds=0;
        std::uint64_t nv12_copy_bytes=0,bgra_blit_bytes=0,conversion_nanoseconds=0;
        unsigned peak_pending=0;
        std::int64_t retirement_error_code=0;
    };
    Metrics metrics() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
