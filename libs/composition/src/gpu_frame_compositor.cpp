#include <creative_suite/composition/opengl_frame_compositor.h>
#include <creative_suite/composition/vulkan_frame_compositor.h>
#include <atomic>
#include <mutex>

namespace creative_suite::composition {
namespace {
std::atomic<GraphicsBackend> selected{GraphicsBackend::OpenGl};
std::mutex session_mutex;
std::weak_ptr<VulkanDevice> session_device;
}
void setSessionGraphicsBackend(GraphicsBackend backend) noexcept { selected.store(backend); }
GraphicsBackend sessionGraphicsBackend() noexcept { return selected.load(); }
void setSessionVulkanDevice(const std::shared_ptr<VulkanDevice>& device) {
    std::lock_guard lock(session_mutex); session_device = device;
}
std::shared_ptr<VulkanDevice> sessionVulkanDevice() {
    std::lock_guard lock(session_mutex); return session_device.lock();
}
std::unique_ptr<GpuFrameCompositor> createGpuFrameCompositor(QOffscreenSurface* surface,
    OpenGlPrecisionPolicy precision, QOpenGLContext* share,
    std::shared_ptr<OpenGlTexturePoolBudget> budget, GraphicsBackend backend) {
    if (backend == GraphicsBackend::Vulkan) return std::make_unique<VulkanFrameCompositor>(sessionVulkanDevice(), budget ? budget->byteLimit() : 64ULL*1024*1024, budget ? budget->targetLimit() : 3, budget);
    if (backend == GraphicsBackend::Cpu) return {};
    return std::make_unique<OpenGlFrameCompositor>(surface, precision, share, std::move(budget));
}
}
