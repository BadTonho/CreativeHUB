#ifdef CS_HAS_VULKAN
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#endif
#include <creative_suite/composition/vulkan_frame_compositor.h>
#include <creative_suite/media/native_video_frame.h>
#include <QWindow>
#include <QFile>
#include <QLibrary>
#include <QVulkanInstance>
#include <QCoreApplication>
#if !QT_CONFIG(vulkan)
#undef CS_HAS_VULKAN
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <atomic>

#ifdef CS_HAS_VULKAN
#include <vulkan/vulkan.h>
#include "composition_spirv.h"
#include "composition_precise_spirv.h"
#endif

namespace creative_suite::composition {
namespace {
using Status = OpenGlCompositionStatus;
using Clock = std::chrono::steady_clock;
std::uint64_t ns(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
}
struct Failure : std::runtime_error {
    std::int64_t code;
    Status status;
    Failure(std::string cause, std::int64_t value=0, Status state=Status::Failed)
        : std::runtime_error(std::move(cause)),code(value),status(state) {}
};
void cancelled(const GpuFrameCompositor::CancellationPredicate& cancel) {
    if(cancel && cancel())throw Failure("",0,Status::Cancelled);
}
#ifdef CS_HAS_VULKAN
void check(VkResult result,const char* operation) {
    if(result!=VK_SUCCESS)throw Failure(operation,result);
}

PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR = nullptr;
PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = nullptr;
PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets = nullptr;
PFN_vkAllocateMemory vkAllocateMemory = nullptr;
PFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
PFN_vkBindBufferMemory vkBindBufferMemory = nullptr;
PFN_vkBindImageMemory vkBindImageMemory = nullptr;
PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets = nullptr;
PFN_vkCmdBindPipeline vkCmdBindPipeline = nullptr;
PFN_vkCmdBlitImage vkCmdBlitImage = nullptr;
PFN_vkCmdClearColorImage vkCmdClearColorImage = nullptr;
PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage = nullptr;
PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer = nullptr;
PFN_vkCmdDispatch vkCmdDispatch = nullptr;
PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier = nullptr;
PFN_vkCmdPushConstants vkCmdPushConstants = nullptr;
PFN_vkCreateBuffer vkCreateBuffer = nullptr;
PFN_vkCreateCommandPool vkCreateCommandPool = nullptr;
PFN_vkCreateComputePipelines vkCreateComputePipelines = nullptr;
PFN_vkCreateDescriptorPool vkCreateDescriptorPool = nullptr;
PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout = nullptr;
PFN_vkCreateDevice vkCreateDevice = nullptr;
PFN_vkCreateFence vkCreateFence = nullptr;
PFN_vkCreateImage vkCreateImage = nullptr;
PFN_vkCreateImageView vkCreateImageView = nullptr;
PFN_vkCreatePipelineLayout vkCreatePipelineLayout = nullptr;
PFN_vkCreateSemaphore vkCreateSemaphore = nullptr;
PFN_vkCreateShaderModule vkCreateShaderModule = nullptr;
PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR = nullptr;
PFN_vkDestroyBuffer vkDestroyBuffer = nullptr;
PFN_vkDestroyCommandPool vkDestroyCommandPool = nullptr;
PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool = nullptr;
PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout = nullptr;
PFN_vkDestroyDevice vkDestroyDevice = nullptr;
PFN_vkDestroyFence vkDestroyFence = nullptr;
PFN_vkDestroyImage vkDestroyImage = nullptr;
PFN_vkDestroyImageView vkDestroyImageView = nullptr;
PFN_vkDestroyPipeline vkDestroyPipeline = nullptr;
PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout = nullptr;
PFN_vkDestroySemaphore vkDestroySemaphore = nullptr;
PFN_vkDestroyShaderModule vkDestroyShaderModule = nullptr;
PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR = nullptr;
PFN_vkDeviceWaitIdle vkDeviceWaitIdle = nullptr;
PFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;
PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;
PFN_vkFreeCommandBuffers vkFreeCommandBuffers = nullptr;
PFN_vkFreeMemory vkFreeMemory = nullptr;
PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = nullptr;
PFN_vkGetDeviceQueue vkGetDeviceQueue = nullptr;
PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements = nullptr;
PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures = nullptr;
PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;
PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = nullptr;
PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR = nullptr;
PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR = nullptr;
PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR = nullptr;
PFN_vkMapMemory vkMapMemory = nullptr;
PFN_vkQueuePresentKHR vkQueuePresentKHR = nullptr;
PFN_vkQueueSubmit vkQueueSubmit = nullptr;
PFN_vkQueueWaitIdle vkQueueWaitIdle = nullptr;
PFN_vkResetCommandPool vkResetCommandPool = nullptr;
PFN_vkResetDescriptorPool vkResetDescriptorPool = nullptr;
PFN_vkResetFences vkResetFences = nullptr;
PFN_vkUnmapMemory vkUnmapMemory = nullptr;
PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets = nullptr;
PFN_vkWaitForFences vkWaitForFences = nullptr;
void loadVulkanApi() {
    static QLibrary loader;
    static std::once_flag once;
    static std::string error;
    std::call_once(once, [] {
        try {
#ifdef Q_OS_WIN
            loader.setFileName("vulkan-1");
#elif defined(Q_OS_MACOS)
            loader.setFileName("libvulkan.1.dylib");
#else
            loader.setFileName("libvulkan.so.1");
#endif
            loader.setLoadHints(QLibrary::PreventUnloadHint);
            if(!loader.load())throw Failure("The Vulkan runtime loader is unavailable: " + loader.errorString().toStdString());
        vkAcquireNextImageKHR = reinterpret_cast<PFN_vkAcquireNextImageKHR>(loader.resolve("vkAcquireNextImageKHR"));
        if(!vkAcquireNextImageKHR)throw Failure("The Vulkan loader lacks vkAcquireNextImageKHR.");
        vkAllocateCommandBuffers = reinterpret_cast<PFN_vkAllocateCommandBuffers>(loader.resolve("vkAllocateCommandBuffers"));
        if(!vkAllocateCommandBuffers)throw Failure("The Vulkan loader lacks vkAllocateCommandBuffers.");
        vkAllocateDescriptorSets = reinterpret_cast<PFN_vkAllocateDescriptorSets>(loader.resolve("vkAllocateDescriptorSets"));
        if(!vkAllocateDescriptorSets)throw Failure("The Vulkan loader lacks vkAllocateDescriptorSets.");
        vkAllocateMemory = reinterpret_cast<PFN_vkAllocateMemory>(loader.resolve("vkAllocateMemory"));
        if(!vkAllocateMemory)throw Failure("The Vulkan loader lacks vkAllocateMemory.");
        vkBeginCommandBuffer = reinterpret_cast<PFN_vkBeginCommandBuffer>(loader.resolve("vkBeginCommandBuffer"));
        if(!vkBeginCommandBuffer)throw Failure("The Vulkan loader lacks vkBeginCommandBuffer.");
        vkBindBufferMemory = reinterpret_cast<PFN_vkBindBufferMemory>(loader.resolve("vkBindBufferMemory"));
        if(!vkBindBufferMemory)throw Failure("The Vulkan loader lacks vkBindBufferMemory.");
        vkBindImageMemory = reinterpret_cast<PFN_vkBindImageMemory>(loader.resolve("vkBindImageMemory"));
        if(!vkBindImageMemory)throw Failure("The Vulkan loader lacks vkBindImageMemory.");
        vkCmdBindDescriptorSets = reinterpret_cast<PFN_vkCmdBindDescriptorSets>(loader.resolve("vkCmdBindDescriptorSets"));
        if(!vkCmdBindDescriptorSets)throw Failure("The Vulkan loader lacks vkCmdBindDescriptorSets.");
        vkCmdBindPipeline = reinterpret_cast<PFN_vkCmdBindPipeline>(loader.resolve("vkCmdBindPipeline"));
        if(!vkCmdBindPipeline)throw Failure("The Vulkan loader lacks vkCmdBindPipeline.");
        vkCmdBlitImage = reinterpret_cast<PFN_vkCmdBlitImage>(loader.resolve("vkCmdBlitImage"));
        if(!vkCmdBlitImage)throw Failure("The Vulkan loader lacks vkCmdBlitImage.");
        vkCmdClearColorImage = reinterpret_cast<PFN_vkCmdClearColorImage>(loader.resolve("vkCmdClearColorImage"));
        if(!vkCmdClearColorImage)throw Failure("The Vulkan loader lacks vkCmdClearColorImage.");
        vkCmdCopyBufferToImage = reinterpret_cast<PFN_vkCmdCopyBufferToImage>(loader.resolve("vkCmdCopyBufferToImage"));
        if(!vkCmdCopyBufferToImage)throw Failure("The Vulkan loader lacks vkCmdCopyBufferToImage.");
        vkCmdCopyImageToBuffer = reinterpret_cast<PFN_vkCmdCopyImageToBuffer>(loader.resolve("vkCmdCopyImageToBuffer"));
        if(!vkCmdCopyImageToBuffer)throw Failure("The Vulkan loader lacks vkCmdCopyImageToBuffer.");
        vkCmdDispatch = reinterpret_cast<PFN_vkCmdDispatch>(loader.resolve("vkCmdDispatch"));
        if(!vkCmdDispatch)throw Failure("The Vulkan loader lacks vkCmdDispatch.");
        vkCmdPipelineBarrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(loader.resolve("vkCmdPipelineBarrier"));
        if(!vkCmdPipelineBarrier)throw Failure("The Vulkan loader lacks vkCmdPipelineBarrier.");
        vkCmdPushConstants = reinterpret_cast<PFN_vkCmdPushConstants>(loader.resolve("vkCmdPushConstants"));
        if(!vkCmdPushConstants)throw Failure("The Vulkan loader lacks vkCmdPushConstants.");
        vkCreateBuffer = reinterpret_cast<PFN_vkCreateBuffer>(loader.resolve("vkCreateBuffer"));
        if(!vkCreateBuffer)throw Failure("The Vulkan loader lacks vkCreateBuffer.");
        vkCreateCommandPool = reinterpret_cast<PFN_vkCreateCommandPool>(loader.resolve("vkCreateCommandPool"));
        if(!vkCreateCommandPool)throw Failure("The Vulkan loader lacks vkCreateCommandPool.");
        vkCreateComputePipelines = reinterpret_cast<PFN_vkCreateComputePipelines>(loader.resolve("vkCreateComputePipelines"));
        if(!vkCreateComputePipelines)throw Failure("The Vulkan loader lacks vkCreateComputePipelines.");
        vkCreateDescriptorPool = reinterpret_cast<PFN_vkCreateDescriptorPool>(loader.resolve("vkCreateDescriptorPool"));
        if(!vkCreateDescriptorPool)throw Failure("The Vulkan loader lacks vkCreateDescriptorPool.");
        vkCreateDescriptorSetLayout = reinterpret_cast<PFN_vkCreateDescriptorSetLayout>(loader.resolve("vkCreateDescriptorSetLayout"));
        if(!vkCreateDescriptorSetLayout)throw Failure("The Vulkan loader lacks vkCreateDescriptorSetLayout.");
        vkCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(loader.resolve("vkCreateDevice"));
        if(!vkCreateDevice)throw Failure("The Vulkan loader lacks vkCreateDevice.");
        vkCreateFence = reinterpret_cast<PFN_vkCreateFence>(loader.resolve("vkCreateFence"));
        if(!vkCreateFence)throw Failure("The Vulkan loader lacks vkCreateFence.");
        vkCreateImage = reinterpret_cast<PFN_vkCreateImage>(loader.resolve("vkCreateImage"));
        if(!vkCreateImage)throw Failure("The Vulkan loader lacks vkCreateImage.");
        vkCreateImageView = reinterpret_cast<PFN_vkCreateImageView>(loader.resolve("vkCreateImageView"));
        if(!vkCreateImageView)throw Failure("The Vulkan loader lacks vkCreateImageView.");
        vkCreatePipelineLayout = reinterpret_cast<PFN_vkCreatePipelineLayout>(loader.resolve("vkCreatePipelineLayout"));
        if(!vkCreatePipelineLayout)throw Failure("The Vulkan loader lacks vkCreatePipelineLayout.");
        vkCreateSemaphore = reinterpret_cast<PFN_vkCreateSemaphore>(loader.resolve("vkCreateSemaphore"));
        if(!vkCreateSemaphore)throw Failure("The Vulkan loader lacks vkCreateSemaphore.");
        vkCreateShaderModule = reinterpret_cast<PFN_vkCreateShaderModule>(loader.resolve("vkCreateShaderModule"));
        if(!vkCreateShaderModule)throw Failure("The Vulkan loader lacks vkCreateShaderModule.");
        vkCreateSwapchainKHR = reinterpret_cast<PFN_vkCreateSwapchainKHR>(loader.resolve("vkCreateSwapchainKHR"));
        if(!vkCreateSwapchainKHR)throw Failure("The Vulkan loader lacks vkCreateSwapchainKHR.");
        vkDestroyBuffer = reinterpret_cast<PFN_vkDestroyBuffer>(loader.resolve("vkDestroyBuffer"));
        if(!vkDestroyBuffer)throw Failure("The Vulkan loader lacks vkDestroyBuffer.");
        vkDestroyCommandPool = reinterpret_cast<PFN_vkDestroyCommandPool>(loader.resolve("vkDestroyCommandPool"));
        if(!vkDestroyCommandPool)throw Failure("The Vulkan loader lacks vkDestroyCommandPool.");
        vkDestroyDescriptorPool = reinterpret_cast<PFN_vkDestroyDescriptorPool>(loader.resolve("vkDestroyDescriptorPool"));
        if(!vkDestroyDescriptorPool)throw Failure("The Vulkan loader lacks vkDestroyDescriptorPool.");
        vkDestroyDescriptorSetLayout = reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(loader.resolve("vkDestroyDescriptorSetLayout"));
        if(!vkDestroyDescriptorSetLayout)throw Failure("The Vulkan loader lacks vkDestroyDescriptorSetLayout.");
        vkDestroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(loader.resolve("vkDestroyDevice"));
        if(!vkDestroyDevice)throw Failure("The Vulkan loader lacks vkDestroyDevice.");
        vkDestroyFence = reinterpret_cast<PFN_vkDestroyFence>(loader.resolve("vkDestroyFence"));
        if(!vkDestroyFence)throw Failure("The Vulkan loader lacks vkDestroyFence.");
        vkDestroyImage = reinterpret_cast<PFN_vkDestroyImage>(loader.resolve("vkDestroyImage"));
        if(!vkDestroyImage)throw Failure("The Vulkan loader lacks vkDestroyImage.");
        vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(loader.resolve("vkDestroyImageView"));
        if(!vkDestroyImageView)throw Failure("The Vulkan loader lacks vkDestroyImageView.");
        vkDestroyPipeline = reinterpret_cast<PFN_vkDestroyPipeline>(loader.resolve("vkDestroyPipeline"));
        if(!vkDestroyPipeline)throw Failure("The Vulkan loader lacks vkDestroyPipeline.");
        vkDestroyPipelineLayout = reinterpret_cast<PFN_vkDestroyPipelineLayout>(loader.resolve("vkDestroyPipelineLayout"));
        if(!vkDestroyPipelineLayout)throw Failure("The Vulkan loader lacks vkDestroyPipelineLayout.");
        vkDestroySemaphore = reinterpret_cast<PFN_vkDestroySemaphore>(loader.resolve("vkDestroySemaphore"));
        if(!vkDestroySemaphore)throw Failure("The Vulkan loader lacks vkDestroySemaphore.");
        vkDestroyShaderModule = reinterpret_cast<PFN_vkDestroyShaderModule>(loader.resolve("vkDestroyShaderModule"));
        if(!vkDestroyShaderModule)throw Failure("The Vulkan loader lacks vkDestroyShaderModule.");
        vkDestroySwapchainKHR = reinterpret_cast<PFN_vkDestroySwapchainKHR>(loader.resolve("vkDestroySwapchainKHR"));
        if(!vkDestroySwapchainKHR)throw Failure("The Vulkan loader lacks vkDestroySwapchainKHR.");
        vkDeviceWaitIdle = reinterpret_cast<PFN_vkDeviceWaitIdle>(loader.resolve("vkDeviceWaitIdle"));
        if(!vkDeviceWaitIdle)throw Failure("The Vulkan loader lacks vkDeviceWaitIdle.");
        vkEndCommandBuffer = reinterpret_cast<PFN_vkEndCommandBuffer>(loader.resolve("vkEndCommandBuffer"));
        if(!vkEndCommandBuffer)throw Failure("The Vulkan loader lacks vkEndCommandBuffer.");
        vkEnumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(loader.resolve("vkEnumeratePhysicalDevices"));
        if(!vkEnumeratePhysicalDevices)throw Failure("The Vulkan loader lacks vkEnumeratePhysicalDevices.");
        vkFreeCommandBuffers = reinterpret_cast<PFN_vkFreeCommandBuffers>(loader.resolve("vkFreeCommandBuffers"));
        if(!vkFreeCommandBuffers)throw Failure("The Vulkan loader lacks vkFreeCommandBuffers.");
        vkFreeMemory = reinterpret_cast<PFN_vkFreeMemory>(loader.resolve("vkFreeMemory"));
        if(!vkFreeMemory)throw Failure("The Vulkan loader lacks vkFreeMemory.");
        vkGetBufferMemoryRequirements = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(loader.resolve("vkGetBufferMemoryRequirements"));
        if(!vkGetBufferMemoryRequirements)throw Failure("The Vulkan loader lacks vkGetBufferMemoryRequirements.");
        vkGetDeviceQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(loader.resolve("vkGetDeviceQueue"));
        if(!vkGetDeviceQueue)throw Failure("The Vulkan loader lacks vkGetDeviceQueue.");
        vkGetImageMemoryRequirements = reinterpret_cast<PFN_vkGetImageMemoryRequirements>(loader.resolve("vkGetImageMemoryRequirements"));
        if(!vkGetImageMemoryRequirements)throw Failure("The Vulkan loader lacks vkGetImageMemoryRequirements.");
        vkGetPhysicalDeviceFeatures = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(loader.resolve("vkGetPhysicalDeviceFeatures"));
        if(!vkGetPhysicalDeviceFeatures)throw Failure("The Vulkan loader lacks vkGetPhysicalDeviceFeatures.");
        vkGetPhysicalDeviceMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(loader.resolve("vkGetPhysicalDeviceMemoryProperties"));
        if(!vkGetPhysicalDeviceMemoryProperties)throw Failure("The Vulkan loader lacks vkGetPhysicalDeviceMemoryProperties.");
        vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(loader.resolve("vkGetPhysicalDeviceProperties"));
        if(!vkGetPhysicalDeviceProperties)throw Failure("The Vulkan loader lacks vkGetPhysicalDeviceProperties.");
        vkGetPhysicalDeviceQueueFamilyProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(loader.resolve("vkGetPhysicalDeviceQueueFamilyProperties"));
        if(!vkGetPhysicalDeviceQueueFamilyProperties)throw Failure("The Vulkan loader lacks vkGetPhysicalDeviceQueueFamilyProperties.");
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(loader.resolve("vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
        if(!vkGetPhysicalDeviceSurfaceCapabilitiesKHR)throw Failure("The Vulkan loader lacks vkGetPhysicalDeviceSurfaceCapabilitiesKHR.");
        vkGetPhysicalDeviceSurfaceFormatsKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(loader.resolve("vkGetPhysicalDeviceSurfaceFormatsKHR"));
        if(!vkGetPhysicalDeviceSurfaceFormatsKHR)throw Failure("The Vulkan loader lacks vkGetPhysicalDeviceSurfaceFormatsKHR.");
        vkGetPhysicalDeviceSurfaceSupportKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(loader.resolve("vkGetPhysicalDeviceSurfaceSupportKHR"));
        if(!vkGetPhysicalDeviceSurfaceSupportKHR)throw Failure("The Vulkan loader lacks vkGetPhysicalDeviceSurfaceSupportKHR.");
        vkGetSwapchainImagesKHR = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(loader.resolve("vkGetSwapchainImagesKHR"));
        if(!vkGetSwapchainImagesKHR)throw Failure("The Vulkan loader lacks vkGetSwapchainImagesKHR.");
        vkMapMemory = reinterpret_cast<PFN_vkMapMemory>(loader.resolve("vkMapMemory"));
        if(!vkMapMemory)throw Failure("The Vulkan loader lacks vkMapMemory.");
        vkQueuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR>(loader.resolve("vkQueuePresentKHR"));
        if(!vkQueuePresentKHR)throw Failure("The Vulkan loader lacks vkQueuePresentKHR.");
        vkQueueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(loader.resolve("vkQueueSubmit"));
        if(!vkQueueSubmit)throw Failure("The Vulkan loader lacks vkQueueSubmit.");
        vkQueueWaitIdle = reinterpret_cast<PFN_vkQueueWaitIdle>(loader.resolve("vkQueueWaitIdle"));
        if(!vkQueueWaitIdle)throw Failure("The Vulkan loader lacks vkQueueWaitIdle.");
        vkResetCommandPool = reinterpret_cast<PFN_vkResetCommandPool>(loader.resolve("vkResetCommandPool"));
        if(!vkResetCommandPool)throw Failure("The Vulkan loader lacks vkResetCommandPool.");
        vkResetDescriptorPool = reinterpret_cast<PFN_vkResetDescriptorPool>(loader.resolve("vkResetDescriptorPool"));
        if(!vkResetDescriptorPool)throw Failure("The Vulkan loader lacks vkResetDescriptorPool.");
        vkResetFences = reinterpret_cast<PFN_vkResetFences>(loader.resolve("vkResetFences"));
        if(!vkResetFences)throw Failure("The Vulkan loader lacks vkResetFences.");
        vkUnmapMemory = reinterpret_cast<PFN_vkUnmapMemory>(loader.resolve("vkUnmapMemory"));
        if(!vkUnmapMemory)throw Failure("The Vulkan loader lacks vkUnmapMemory.");
        vkUpdateDescriptorSets = reinterpret_cast<PFN_vkUpdateDescriptorSets>(loader.resolve("vkUpdateDescriptorSets"));
        if(!vkUpdateDescriptorSets)throw Failure("The Vulkan loader lacks vkUpdateDescriptorSets.");
        vkWaitForFences = reinterpret_cast<PFN_vkWaitForFences>(loader.resolve("vkWaitForFences"));
        if(!vkWaitForFences)throw Failure("The Vulkan loader lacks vkWaitForFences.");
        } catch(const std::exception& failure) { error=failure.what(); }
    });
    if(!error.empty())throw Failure(error,0,Status::Unsupported);
}

struct Parameters {
    std::array<int,4> control{};
    std::array<double,4> geometry{};
    std::array<double,4> rotation{1,0,1,1};
    std::array<float,4> color{};
};
struct ImageAllocation {
    VkImage image=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    VkImageView view=VK_NULL_HANDLE;
    int width=0,height=0;
    VkFormat format=VK_FORMAT_R8G8B8A8_UINT;
    VkDeviceSize bytes=0;
};
#endif
}

#ifdef CS_HAS_VULKAN
struct VulkanDevice::Impl {
    QVulkanInstance instance;
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    VkDevice device=VK_NULL_HANDLE;
    VkQueue queue=VK_NULL_HANDLE;
    uint32_t family=0;
    VkCommandPool commands=VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptors=VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    VkFence fence=VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory_properties{};
    bool precise=false;
    std::atomic_bool lost{false};
    mutable std::recursive_mutex mutex;
    std::vector<std::unique_ptr<ImageAllocation>> pool;
    std::uint64_t bytes=0,peak=0;
    static constexpr std::uint64_t limit=256ULL*1024*1024;
    struct Swapchain {
        VkSwapchainKHR handle=VK_NULL_HANDLE;
        VkSurfaceKHR surface=VK_NULL_HANDLE;
        VkExtent2D size{};
        VkSemaphore acquire=VK_NULL_HANDLE,complete=VK_NULL_HANDLE;
        std::vector<VkImage> images;
    };
    std::map<QWindow*,Swapchain> windows;
    ~Impl() {
        if(!device)return;
        vkDeviceWaitIdle(device);
        for(auto& [window,swap]:windows){Q_UNUSED(window);destroy(swap);}
        for(auto& allocation:pool)destroy(*allocation);
        if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
        if(layout)vkDestroyPipelineLayout(device,layout,nullptr);
        if(descriptor_pool)vkDestroyDescriptorPool(device,descriptor_pool,nullptr);
        if(descriptors)vkDestroyDescriptorSetLayout(device,descriptors,nullptr);
        if(fence)vkDestroyFence(device,fence,nullptr);
        if(commands)vkDestroyCommandPool(device,commands,nullptr);
        vkDestroyDevice(device,nullptr);
    }
    void destroy(ImageAllocation& image) {
        if(image.view)vkDestroyImageView(device,image.view,nullptr);
        if(image.image)vkDestroyImage(device,image.image,nullptr);
        if(image.memory)vkFreeMemory(device,image.memory,nullptr);
        bytes-=image.bytes;
    }
    void destroy(Swapchain& swap) {
        if(swap.handle)vkDestroySwapchainKHR(device,swap.handle,nullptr);
        if(swap.acquire)vkDestroySemaphore(device,swap.acquire,nullptr);
        if(swap.complete)vkDestroySemaphore(device,swap.complete,nullptr);
        swap={};
    }
    uint32_t memoryType(uint32_t bits,VkMemoryPropertyFlags flags) {
        for(uint32_t i=0;i<memory_properties.memoryTypeCount;++i)
            if((bits&(1u<<i)) && (memory_properties.memoryTypes[i].propertyFlags&flags)==flags)return i;
        throw Failure("No compatible Vulkan memory type.",0,Status::Unsupported);
    }
    VkCommandBuffer begin() {
        if(lost)throw Failure("The Vulkan device was lost.",VK_ERROR_DEVICE_LOST);
        check(vkResetCommandPool(device,commands,0),"Reset Vulkan command pool.");
        VkCommandBufferAllocateInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        info.commandPool=commands;info.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;info.commandBufferCount=1;
        VkCommandBuffer cmd{};check(vkAllocateCommandBuffers(device,&info,&cmd),"Allocate Vulkan commands.");
        VkCommandBufferBeginInfo start{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        start.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(cmd,&start),"Begin Vulkan commands.");return cmd;
    }
    void submit(VkCommandBuffer cmd,VkSemaphore wait=VK_NULL_HANDLE,VkSemaphore signal=VK_NULL_HANDLE) {
        check(vkEndCommandBuffer(cmd),"End Vulkan commands.");
        check(vkResetFences(device,1,&fence),"Reset Vulkan completion fence.");
        VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
        if(wait){submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&wait;submit.pWaitDstStageMask=&stage;}
        if(signal){submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&signal;}
        auto result=vkQueueSubmit(queue,1,&submit,fence);
        if(result==VK_ERROR_DEVICE_LOST)lost=true;
        check(result,"Submit Vulkan commands.");
        result=vkWaitForFences(device,1,&fence,VK_TRUE,10'000'000'000ULL);
        if(result!=VK_SUCCESS)lost=true;
        check(result,"Wait for Vulkan completion.");
        vkFreeCommandBuffers(device,commands,1,&cmd);
    }
    void barrier(VkCommandBuffer cmd,VkImage image,VkImageLayout old_layout,VkImageLayout new_layout,
        VkAccessFlags source=VK_ACCESS_MEMORY_WRITE_BIT,VkAccessFlags target=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask=old_layout==VK_IMAGE_LAYOUT_UNDEFINED?0:source;b.dstAccessMask=target;
        b.oldLayout=old_layout;b.newLayout=new_layout;b.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image;
        b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
    }
    std::shared_ptr<VulkanImage> image(std::shared_ptr<VulkanDevice> owner,int width,int height,
        VkFormat format=VK_FORMAT_R8G8B8A8_UINT) {
        if(width<=0 || height<=0 || width>int(properties.limits.maxImageDimension2D) ||
            height>int(properties.limits.maxImageDimension2D) || uint64_t(width)*height*4>limit)
            throw Failure("Vulkan image dimensions exceed the resource limit.",0,Status::Unsupported);
        auto found=std::find_if(pool.begin(),pool.end(),[&](const auto& a){return a->width==width && a->height==height && a->format==format;});
        std::unique_ptr<ImageAllocation> allocation;
        if(found!=pool.end()){allocation=std::move(*found);pool.erase(found);}
        else {
            allocation=std::make_unique<ImageAllocation>();allocation->width=width;allocation->height=height;allocation->format=format;
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            info.imageType=VK_IMAGE_TYPE_2D;info.format=format;info.extent={uint32_t(width),uint32_t(height),1};
            info.mipLevels=1;info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;
            info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            check(vkCreateImage(device,&info,nullptr,&allocation->image),"Create Vulkan image.");
            VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,allocation->image,&req);
            while(bytes+req.size>limit && !pool.empty()){destroy(*pool.back());pool.pop_back();}
            if(bytes+req.size>limit){vkDestroyImage(device,allocation->image,nullptr);throw Failure("The bounded Vulkan image pool is occupied.",0,Status::Busy);}
            VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memory.allocationSize=req.size;
            memory.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            auto result=vkAllocateMemory(device,&memory,nullptr,&allocation->memory);
            if(result!=VK_SUCCESS){vkDestroyImage(device,allocation->image,nullptr);check(result,"Allocate Vulkan image memory.");}
            allocation->bytes=req.size;bytes+=req.size;peak=std::max(peak,bytes);
            try {
                check(vkBindImageMemory(device,allocation->image,allocation->memory,0),"Bind Vulkan image memory.");
                VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=allocation->image;
                view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
                check(vkCreateImageView(device,&view,nullptr,&allocation->view),"Create Vulkan image view.");
                auto cmd=begin();barrier(cmd,allocation->image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0);submit(cmd);
            }catch(...){destroy(*allocation);throw;}
        }
        auto result=std::make_shared<VulkanImage>();result->width=width;result->height=height;result->device=std::move(owner);
        result->allocation=std::shared_ptr<void>(allocation.release(),[this](void* value){
            std::lock_guard lock(mutex);pool.emplace_back(static_cast<ImageAllocation*>(value));
        });return result;
    }
    struct Buffer {
        Impl& p;VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;void* data=nullptr;VkDeviceSize reserved=0;
        Buffer(Impl& owner,VkDeviceSize size,bool cpu_read=false):p(owner) {
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=size;
            info.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            check(vkCreateBuffer(p.device,&info,nullptr,&buffer),"Create Vulkan transfer buffer.");
            try {
                VkMemoryRequirements req{};vkGetBufferMemoryRequirements(p.device,buffer,&req);
                while(p.bytes+req.size>limit && !p.pool.empty()){p.destroy(*p.pool.back());p.pool.pop_back();}
                if(p.bytes+req.size>limit)throw Failure("The bounded Vulkan transfer budget is occupied.",0,Status::Busy);
                VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=req.size;
                allocation.memoryTypeIndex=p.memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                // Write-combined coherent heaps are suitable for uploads, but CPU
                // readback should prefer a cached heap when the device offers one.
                if(cpu_read)for(uint32_t i=0;i<p.memory_properties.memoryTypeCount;++i) {
                    const auto flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
                    if((req.memoryTypeBits&(1u<<i)) && (p.memory_properties.memoryTypes[i].propertyFlags&flags)==flags){allocation.memoryTypeIndex=i;break;}
                }
                check(vkAllocateMemory(p.device,&allocation,nullptr,&memory),"Allocate Vulkan transfer memory.");
                reserved=req.size;p.bytes+=reserved;p.peak=std::max(p.peak,p.bytes);
                check(vkBindBufferMemory(p.device,buffer,memory,0),"Bind Vulkan transfer memory.");
                check(vkMapMemory(p.device,memory,0,size,0,&data),"Map Vulkan transfer memory.");
            }catch(...){p.bytes-=reserved;if(memory)vkFreeMemory(p.device,memory,nullptr);vkDestroyBuffer(p.device,buffer,nullptr);throw;}
        }
        ~Buffer(){p.bytes-=reserved;if(data)vkUnmapMemory(p.device,memory);vkDestroyBuffer(p.device,buffer,nullptr);vkFreeMemory(p.device,memory,nullptr);}
    };
    static ImageAllocation& allocation(const std::shared_ptr<VulkanImage>& image) {
        return *static_cast<ImageAllocation*>(image->allocation.get());
    }
    std::shared_ptr<VulkanImage> input(const OpenGlImageInput& input,std::shared_ptr<VulkanDevice> owner,OpenGlCompositionTimings* timings) {
        if(input.texture) {
            const auto image=input.texture->vulkanImage();
            if(!image || image->device!=owner)throw Failure("The image belongs to another graphics device.",0,Status::Unsupported);
            return image;
        }
        media::RgbaFramePtr recovered;
        const media::RgbaFrame* frame=input.rgba;
        if(input.native){
#ifdef CS_NATIVE_VIDEO_INTEROP
            recovered=input.native->download_rgba();frame=recovered.get();
#else
            throw Failure("Native video is not enabled in this build.",0,Status::Unsupported);
#endif
        }
        if(!frame || frame->width<=0 || frame->height<=0 || frame->stride<int64_t(frame->width)*4 ||
            uint64_t(frame->stride)*frame->height>frame->rgba_pixels.size())
            throw Failure("Invalid Vulkan RGBA input.",0,Status::Unsupported);
        const auto start=Clock::now();auto image=this->image(owner,frame->width,frame->height);
        Buffer staging(*this,uint64_t(frame->width)*frame->height*4);
        for(int y=0;y<frame->height;++y)std::memcpy(static_cast<char*>(staging.data)+size_t(y)*frame->width*4,
            frame->rgba_pixels.data()+size_t(y)*frame->stride,size_t(frame->width)*4);
        auto cmd=begin();auto& a=allocation(image);barrier(cmd,a.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={uint32_t(frame->width),uint32_t(frame->height),1};
        vkCmdCopyBufferToImage(cmd,staging.buffer,a.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
        barrier(cmd,a.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);submit(cmd);
        if(timings){timings->upload_nanoseconds+=ns(start);timings->uploaded_bytes+=uint64_t(frame->width)*frame->height*4;++timings->uploaded_layers;}
        return image;
    }
    void dispatch(const std::shared_ptr<VulkanImage>& bg,const std::shared_ptr<VulkanImage>& fg,
        const std::shared_ptr<VulkanImage>& mask,const std::shared_ptr<VulkanImage>& output,
        const Parameters& parameters,const std::shared_ptr<VulkanImage>& display={}) {
        check(vkResetDescriptorPool(device,descriptor_pool,0),"Reset Vulkan descriptors.");
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool=descriptor_pool;allocate.descriptorSetCount=1;allocate.pSetLayouts=&descriptors;
        VkDescriptorSet set{};check(vkAllocateDescriptorSets(device,&allocate,&set),"Allocate Vulkan descriptors.");
        // All descriptors are valid even for shader branches that do not access them.
        auto dummy=display ? display : image(output->device,1,1,VK_FORMAT_R8G8B8A8_UNORM);
        const std::array<std::shared_ptr<VulkanImage>,5> images{bg,fg?fg:bg,mask?mask:bg,output,dummy};
        std::array<VkDescriptorImageInfo,5> image_info{};std::array<VkWriteDescriptorSet,5> writes{};
        for(unsigned i=0;i<5;++i){image_info[i].imageView=allocation(images[i]).view;image_info[i].imageLayout=VK_IMAGE_LAYOUT_GENERAL;
            writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=set;writes[i].dstBinding=i;
            writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;writes[i].pImageInfo=&image_info[i];}
        vkUpdateDescriptorSets(device,5,writes.data(),0,nullptr);
        auto cmd=begin();vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);
        if(precise)vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(parameters),&parameters);
        else {
            struct Small {std::array<int,4> control;std::array<float,4> geometry,rotation,color;} small;
            small.control=parameters.control;small.color=parameters.color;
            for(unsigned i=0;i<4;++i){small.geometry[i]=float(parameters.geometry[i]);small.rotation[i]=float(parameters.rotation[i]);}
            vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(small),&small);
        }
        vkCmdDispatch(cmd,(output->width+15)/16,(output->height+15)/16,1);
        barrier(cmd,allocation(output).image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL);
        if(display)barrier(cmd,allocation(display).image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL);
        submit(cmd);
    }
};
#else
struct VulkanDevice::Impl {};
#endif

VulkanDevice::VulkanDevice():impl_(std::make_unique<Impl>()){}
VulkanDevice::~VulkanDevice()=default;
std::shared_ptr<VulkanDevice> VulkanDevice::create(std::string& cause) {
    if(qEnvironmentVariableIntValue("CREATIVE_SUITE_DISABLE_VULKAN")==1){cause="Vulkan was disabled by the diagnostic environment setting.";return {};}
#ifdef CS_HAS_VULKAN
    try {
        loadVulkanApi();
        auto device=std::shared_ptr<VulkanDevice>(new VulkanDevice);auto& p=*device->impl_;
        p.instance.setApiVersion(QVersionNumber(1,1));
        if(qEnvironmentVariableIntValue("CREATIVE_SUITE_VULKAN_VALIDATION")==1)
            p.instance.setLayers({"VK_LAYER_KHRONOS_validation"});
        if(!p.instance.create())throw Failure("Cannot initialize the Vulkan loader or instance.",p.instance.errorCode());
        uint32_t count=0;check(vkEnumeratePhysicalDevices(p.instance.vkInstance(),&count,nullptr),"Enumerate Vulkan devices.");
        std::vector<VkPhysicalDevice> devices(count);check(vkEnumeratePhysicalDevices(p.instance.vkInstance(),&count,devices.data()),"Enumerate Vulkan devices.");
        for(auto candidate:devices){
            uint32_t families=0;vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,nullptr);
            std::vector<VkQueueFamilyProperties> queues(families);vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,queues.data());
            for(uint32_t i=0;i<families;++i)if((queues[i].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)){
                p.physical=candidate;p.family=i;break;
            }
            if(p.physical)break;
        }
        if(!p.physical)throw Failure("No Vulkan graphics/compute device is available.",0,Status::Unsupported);
        vkGetPhysicalDeviceProperties(p.physical,&p.properties);vkGetPhysicalDeviceMemoryProperties(p.physical,&p.memory_properties);
        VkPhysicalDeviceFeatures available{};vkGetPhysicalDeviceFeatures(p.physical,&available);
        p.precise=available.shaderFloat64;VkPhysicalDeviceFeatures enabled{};enabled.shaderFloat64=p.precise;
        const float priority=1;VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex=p.family;queue.queueCount=1;queue.pQueuePriorities=&priority;
        const char* extension=VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};info.queueCreateInfoCount=1;info.pQueueCreateInfos=&queue;
        info.enabledExtensionCount=1;info.ppEnabledExtensionNames=&extension;info.pEnabledFeatures=&enabled;
        check(vkCreateDevice(p.physical,&info,nullptr,&p.device),"Create Vulkan device.");
        vkGetDeviceQueue(p.device,p.family,0,&p.queue);
        VkCommandPoolCreateInfo commands{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};commands.queueFamilyIndex=p.family;
        commands.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check(vkCreateCommandPool(p.device,&commands,nullptr,&p.commands),"Create Vulkan command pool.");
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(p.device,&fence,nullptr,&p.fence),"Create Vulkan completion fence.");
        std::array<VkDescriptorSetLayoutBinding,5> bindings{};
        for(unsigned i=0;i<5;++i){bindings[i].binding=i;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
        VkDescriptorSetLayoutCreateInfo descriptor{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};descriptor.bindingCount=5;descriptor.pBindings=bindings.data();
        check(vkCreateDescriptorSetLayout(p.device,&descriptor,nullptr,&p.descriptors),"Create Vulkan descriptor layout.");
        VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,5};VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets=1;pool.poolSizeCount=1;pool.pPoolSizes=&pool_size;check(vkCreateDescriptorPool(p.device,&pool,nullptr,&p.descriptor_pool),"Create Vulkan descriptor pool.");
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,uint32_t(p.precise?sizeof(Parameters):64)};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&p.descriptors;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&range;
        check(vkCreatePipelineLayout(p.device,&layout,nullptr,&p.layout),"Create Vulkan pipeline layout.");
        VkShaderModuleCreateInfo shader_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shader_info.codeSize=p.precise?sizeof(composition_precise_spirv):sizeof(composition_spirv);
        shader_info.pCode=p.precise?composition_precise_spirv:composition_spirv;
        VkShaderModule shader{};check(vkCreateShaderModule(p.device,&shader_info,nullptr,&shader),"Load Vulkan composition shader.");
        VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipeline.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline.stage.module=shader;pipeline.stage.pName="main";pipeline.layout=p.layout;
        auto result=vkCreateComputePipelines(p.device,VK_NULL_HANDLE,1,&pipeline,nullptr,&p.pipeline);
        vkDestroyShaderModule(p.device,shader,nullptr);check(result,"Create Vulkan composition pipeline.");
        return device;
    }catch(const Failure& error){cause=error.what();if(error.code)cause+=" (Vulkan error "+std::to_string(error.code)+")";return {};}
    catch(const std::exception& error){cause=error.what();return {};}
#else
    cause="This build does not include Vulkan support.";return {};
#endif
}
QString VulkanDevice::name() const {
#ifdef CS_HAS_VULKAN
    return QString::fromUtf8(impl_->properties.deviceName);
#else
    return {};
#endif
}
bool VulkanDevice::supportsPreciseRotation() const noexcept {
#ifdef CS_HAS_VULKAN
    return impl_->precise;
#else
    return false;
#endif
}
bool VulkanDevice::valid() const noexcept {
#ifdef CS_HAS_VULKAN
    return impl_->device && !impl_->lost;
#else
    return false;
#endif
}
std::uint64_t VulkanDevice::reservedBytes() const noexcept {
#ifdef CS_HAS_VULKAN
    std::lock_guard lock(impl_->mutex);return impl_->bytes;
#else
    return 0;
#endif
}
std::uint64_t VulkanDevice::peakBytes() const noexcept {
#ifdef CS_HAS_VULKAN
    std::lock_guard lock(impl_->mutex);return impl_->peak;
#else
    return 0;
#endif
}

VulkanFrameCompositor::VulkanFrameCompositor(std::shared_ptr<VulkanDevice> device,std::uint64_t bytes,unsigned targets,
    std::shared_ptr<OpenGlTexturePoolBudget> budget)
    :device_(std::move(device)),byte_limit_(bytes),target_limit_(targets),
     budget_(budget ? std::move(budget) : std::make_shared<OpenGlTexturePoolBudget>(bytes, targets)) {}
OpenGlTextureFramePtr VulkanFrameCompositor::publish(std::shared_ptr<VulkanImage> image) {
    const auto bytes=std::uint64_t(image->width)*image->height*4;
    struct Reservation {
        std::shared_ptr<OpenGlTexturePoolBudget> budget;std::uint64_t bytes;
        Reservation(std::shared_ptr<OpenGlTexturePoolBudget> value,std::uint64_t size):budget(std::move(value)),bytes(size) {
            if(!budget->tryReserve(bytes))throw Failure("The shared Vulkan output budget is occupied.",0,Status::Busy);
        }
        ~Reservation(){budget->releaseReservation(bytes);}
    };
    auto reservation=std::make_shared<Reservation>(budget_,bytes);
    auto frame=std::shared_ptr<OpenGlTextureFrame>(new OpenGlTextureFrame(std::move(image)));
    frame->reservation_=std::move(reservation);outputs_.push_back(frame);return frame;
}
OpenGlCompositionResult VulkanFrameCompositor::collectReleasedTextureFrames() {
    if(device_ && !device_->valid())return {Status::Failed,{},"vulkan-device-lost","The Vulkan device is no longer available."};
    return {Status::Complete};
}
void VulkanFrameCompositor::checkCapacity(int width,int height) {
    outputs_.erase(std::remove_if(outputs_.begin(),outputs_.end(),[](const auto& item){return item.expired();}),outputs_.end());
    std::uint64_t live_bytes=0;
    for(const auto& entry:outputs_)if(auto frame=entry.lock())live_bytes+=uint64_t(frame->width())*frame->height()*4;
    if(width<=0 || height<=0 || uint64_t(width)*height*4>byte_limit_)
        throw Failure("The Vulkan output exceeds this compositor's image budget.",0,Status::Unsupported);
    if(outputs_.size()>=target_limit_ || live_bytes+uint64_t(width)*height*4>byte_limit_ || budget_->targets()>=target_limit_ || budget_->bytes()+uint64_t(width)*height*4>byte_limit_)
        throw Failure("The bounded Vulkan output pool is occupied.",0,Status::Busy);
}
VulkanFrameCompositor::~VulkanFrameCompositor()=default;
unsigned VulkanFrameCompositor::texturePoolOccupancy() const {
    return unsigned(std::count_if(outputs_.begin(),outputs_.end(),[](const auto& output){return !output.expired();}));
}
std::uint64_t VulkanFrameCompositor::texturePoolBytes() const noexcept {return budget_->bytes();}
OpenGlResourceUsage VulkanFrameCompositor::resourceUsage() const noexcept {
    return {device_?device_->reservedBytes():0,0,device_?device_->peakBytes():0};
}
OpenGlTextureCompositionResult VulkanFrameCompositor::processImage(const OpenGlImageOperation& operation,
    const CancellationPredicate& cancel,OpenGlCompositionTimings* timings) {
    try {
        cancelled(cancel);
#ifdef CS_HAS_VULKAN
        if(!device_ || !device_->valid())throw Failure("The Vulkan device is unavailable.");
        auto& p=*device_->impl_;std::lock_guard lock(p.mutex);
        if(!animation::validTransform(operation.transform) || !std::isfinite(operation.blur_radius_pixels) || operation.blur_radius_pixels<0 || operation.blur_radius_pixels>100)
            throw Failure("Invalid Vulkan image operation.",0,Status::Unsupported);
        if(operation.transform.rotation_degrees!=0 && !p.precise)throw Failure("Precise rotation requires shaderFloat64.",0,Status::Unsupported);
        for(const auto& color:operation.colors)
            if(!std::isfinite(color.brightness) || !std::isfinite(color.contrast_percent) || !std::isfinite(color.saturation_percent) ||
                color.brightness < -100 || color.brightness > 100 || color.contrast_percent < 0 || color.contrast_percent > 200 ||
                color.saturation_percent < 0 || color.saturation_percent > 200)
                throw Failure("Invalid Vulkan color parameters.",0,Status::Unsupported);
        auto bg=p.input(operation.background,device_,timings);
        auto fg=operation.foreground.width()>0?p.input(operation.foreground,device_,timings):std::shared_ptr<VulkanImage>{};
        auto mask=operation.mask.width()>0?p.input(operation.mask,device_,timings):std::shared_ptr<VulkanImage>{};
        int width=operation.output_width?operation.output_width:bg->width,height=operation.output_height?operation.output_height:bg->height;
        checkCapacity(width,height);
        if(operation.kind!=OpenGlImageOperationKind::Transform && (width!=bg->width || height!=bg->height))
            throw Failure("Only Transform accepts an alternate Vulkan canvas.",0,Status::Unsupported);
        if((operation.kind==OpenGlImageOperationKind::Merge || operation.kind==OpenGlImageOperationKind::MaskedBlend) && !fg)
            throw Failure("The Vulkan foreground is missing.",0,Status::Unsupported);
        if(operation.kind==OpenGlImageOperationKind::ApplyMask || operation.kind==OpenGlImageOperationKind::MaskedBlend){
            if(!mask || mask->width!=width || mask->height!=height || (fg && (fg->width!=width || fg->height!=height)))
                throw Failure("Vulkan mask dimensions do not match.",0,Status::Unsupported);
        }
        Parameters params;params.control[0]=int(operation.kind);
        const auto& t=operation.transform;const double angle=t.rotation_degrees*std::numbers::pi/180;
        params.geometry={width*t.position_x,height*t.position_y,bg->width*t.scale,bg->height*t.scale};
        params.rotation={std::cos(angle),std::sin(angle),t.scale,t.opacity};
        const auto started=Clock::now();
        const auto execute=[&]{cancelled(cancel);auto out=p.image(device_,width,height);p.dispatch(bg,fg,mask,out,params);bg=std::move(out);};
        if(operation.kind==OpenGlImageOperationKind::Color){
            for(const auto& color:operation.colors){
                params.color={float(color.brightness/100),float(color.contrast_percent/100),float(color.saturation_percent/100),0};execute();
                if(timings)++timings->color_adjustment_count;
            }
        }else if(operation.kind==OpenGlImageOperationKind::GaussianBlur){
            params.control[1]=int(std::lround(operation.blur_radius_pixels));
            if(params.control[1])for(int pass=0;pass<8;++pass){params.control[3]=pass;params.control[2]=pass%2;execute();}
            if(timings)++timings->gaussian_blur_count;
        }else execute();
        cancelled(cancel);if(timings)timings->draw_submission_nanoseconds+=ns(started);
        auto frame=publish(std::move(bg));
        return {Status::Complete,std::move(frame)};
#else
        Q_UNUSED(operation);Q_UNUSED(timings);throw Failure("Vulkan was not compiled into this build.",0,Status::Unsupported);
#endif
    }catch(const Failure& error){
#ifdef CS_HAS_VULKAN
        if(error.status==Status::Failed && device_)device_->impl_->lost=true;
#endif
        return {error.status,{},"vulkan-process-image",error.what(),error.code};}
    catch(const std::exception& error){return {Status::Failed,{},"vulkan-process-image",error.what()};}
}
OpenGlTextureCompositionResult VulkanFrameCompositor::importFrame(const std::shared_ptr<const media::NativeVideoFrame>& frame,
    const CancellationPredicate& cancel,OpenGlCompositionTimings* timings) {
    OpenGlImageOperation operation;operation.background.native=frame;return processImage(operation,cancel,timings);
}
OpenGlTextureCompositionResult VulkanFrameCompositor::composeTexture(int width,int height,
    const std::vector<CompositionLayer>& layers,const CancellationPredicate& cancel,OpenGlCompositionTimings* timings) {
    try {
        cancelled(cancel);
#ifdef CS_HAS_VULKAN
        if(!device_ || !device_->valid())throw Failure("The Vulkan device is unavailable.");
        checkCapacity(width,height);
        auto& p=*device_->impl_;std::lock_guard lock(p.mutex);
        auto canvas=p.image(device_,width,height);Parameters params;params.control[0]=8;p.dispatch(canvas,{},{},canvas,params);
        for(const auto& layer:layers){
            cancelled(cancel);if(!layer.frame || !animation::validTransform(layer.transform))continue;
            if(layer.transform.rotation_degrees!=0 && !p.precise)throw Failure("Precise rotation requires shaderFloat64.",0,Status::Unsupported);
            OpenGlImageInput source{layer.frame,layer.texture_frame,layer.native_frame};
            OpenGlTextureFramePtr processed;
            const auto apply=[&](OpenGlImageOperation operation){operation.background=source;auto result=processImage(operation,cancel,timings);
                if(result.status!=Status::Complete)throw Failure(result.cause,result.error_code,result.status);
                processed=std::move(result.frame);source={nullptr,processed};};
            if(!layer.effect_stack.empty()){
                auto passes=effects::colorAdjustmentPasses(layer.effect_stack);
                if(!passes)throw Failure("Unsupported Vulkan effect stack.",0,Status::Unsupported);
                OpenGlImageOperation operation;operation.kind=OpenGlImageOperationKind::Color;operation.colors=*passes;apply(operation);
            }
            for(const auto& color:layer.gpu_color_adjustments){OpenGlImageOperation op;op.kind=OpenGlImageOperationKind::Color;op.colors={color};apply(op);}
            for(const auto& effect:layer.gpu_effects)std::visit([&](const auto& value){
                OpenGlImageOperation op;using E=std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<E,effects::ColorAdjustmentParameters>){op.kind=OpenGlImageOperationKind::Color;op.colors={value};}
                else {op.kind=OpenGlImageOperationKind::GaussianBlur;op.blur_radius_pixels=value.radius_pixels;}
                apply(op);
            },effect);
            auto fg=p.input(source,device_,timings);auto out=p.image(device_,width,height);
            const auto& t=layer.transform;const double scale=std::min(double(width)/fg->width,double(height)/fg->height)*t.scale;
            const double angle=t.rotation_degrees*std::numbers::pi/180;
            params.control[0]=7;params.geometry={width*t.position_x,height*t.position_y,fg->width*scale,fg->height*scale};
            params.rotation={std::cos(angle),std::sin(angle),scale,t.opacity};
            const auto started=Clock::now();p.dispatch(canvas,fg,{},out,params);canvas=std::move(out);
            if(timings)timings->draw_submission_nanoseconds+=ns(started);
        }
        cancelled(cancel);auto frame=publish(std::move(canvas));
        return {Status::Complete,std::move(frame)};
#else
        Q_UNUSED(width);Q_UNUSED(height);Q_UNUSED(layers);Q_UNUSED(timings);throw Failure("Vulkan was not compiled into this build.",0,Status::Unsupported);
#endif
    }catch(const Failure& error){return {error.status,{},"vulkan-compose",error.what(),error.code};}
    catch(const std::exception& error){return {Status::Failed,{},"vulkan-compose",error.what()};}
}
OpenGlCompositionResult VulkanFrameCompositor::readback(const OpenGlTextureFramePtr& frame,
    const CancellationPredicate& cancel,OpenGlCompositionTimings* timings) {
    try {
        cancelled(cancel);
#ifdef CS_HAS_VULKAN
        if(!device_ || !device_->valid() || !frame || !frame->vulkanImage() || frame->vulkanImage()->device!=device_)
            throw Failure("Invalid Vulkan readback lease.",0,Status::Unsupported);
        auto& p=*device_->impl_;std::lock_guard lock(p.mutex);const auto start=Clock::now();
        media::RgbaFrame rgba;rgba.width=frame->width();rgba.height=frame->height();rgba.stride=rgba.width*4;
        const uint64_t size=uint64_t(rgba.stride)*rgba.height;rgba.rgba_pixels.resize(size);
        VulkanDevice::Impl::Buffer staging(p,size,true);auto& a=p.allocation(frame->vulkanImage());auto cmd=p.begin();
        p.barrier(cmd,a.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={uint32_t(rgba.width),uint32_t(rgba.height),1};
        vkCmdCopyImageToBuffer(cmd,a.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,staging.buffer,1,&region);
        p.barrier(cmd,a.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);p.submit(cmd);
        cancelled(cancel);std::memcpy(rgba.rgba_pixels.data(),staging.data,size);
        if(timings){timings->readback_bytes+=size;timings->readback_nanoseconds+=ns(start);}
        return {Status::Complete,std::move(rgba)};
#else
        Q_UNUSED(frame);Q_UNUSED(timings);throw Failure("Vulkan was not compiled into this build.",0,Status::Unsupported);
#endif
    }catch(const Failure& error){return {error.status,{},"vulkan-readback",error.what(),error.code};}
    catch(const std::exception& error){return {Status::Failed,{},"vulkan-readback",error.what()};}
}
OpenGlCompositionResult VulkanFrameCompositor::compose(int width,int height,const std::vector<CompositionLayer>& layers,
    const CancellationPredicate& cancel,OpenGlCompositionTimings* timings) {
    auto result=composeTexture(width,height,layers,cancel,timings);
    if(result.status!=Status::Complete)return {result.status,{},result.operation,result.cause,result.error_code};
    return readback(result.frame,cancel,timings);
}
OpenGlCompositionResult VulkanFrameCompositor::copyToNative(const OpenGlTextureFramePtr&,
    const std::shared_ptr<const media::NativeVideoFrame>&,const CancellationPredicate&) {
    return {Status::Unsupported,{},"vulkan-native-output","Vulkan export uses RGBA encoder input."};
}

void VulkanDevice::attachWindow(QWindow* window) {
#ifdef CS_HAS_VULKAN
    window->setSurfaceType(QSurface::VulkanSurface);window->setVulkanInstance(&impl_->instance);
#else
    Q_UNUSED(window);
#endif
}
void VulkanDevice::releaseWindow(QWindow* window) noexcept {
#ifdef CS_HAS_VULKAN
    std::lock_guard lock(impl_->mutex);const auto found=impl_->windows.find(window);
    if(found==impl_->windows.end())return;
    vkDeviceWaitIdle(impl_->device);impl_->destroy(found->second);impl_->windows.erase(found);
#else
    Q_UNUSED(window);
#endif
}
bool VulkanDevice::present(QWindow* window,const OpenGlTextureFramePtr& frame,bool grayscale,std::string& cause,
    std::array<float, 4> surround) {
#ifdef CS_HAS_VULKAN
    try {
        if(!valid() || !frame || !frame->vulkanImage() || frame->vulkanImage()->device.get()!=this)
            throw Failure("The Vulkan presentation image or device is unavailable.");
        auto& p=*impl_;std::unique_lock lock(p.mutex,std::try_to_lock);
        if(!lock.owns_lock()){cause.clear();return false;}
        auto& swap=p.windows[window];const auto surface=QVulkanInstance::surfaceForWindow(window);
        if(!surface)throw Failure("Cannot create the Vulkan window surface.");
        VkBool32 supported=VK_FALSE;check(vkGetPhysicalDeviceSurfaceSupportKHR(p.physical,p.family,surface,&supported),"Check Vulkan presentation support.");
        if(!supported)throw Failure("The Vulkan queue cannot present to this window.");
        VkSurfaceCapabilitiesKHR capabilities{};check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(p.physical,surface,&capabilities),"Query Vulkan surface capabilities.");
        VkExtent2D size=capabilities.currentExtent;
        if(size.width==UINT32_MAX){size.width=uint32_t(window->width()*window->devicePixelRatio());size.height=uint32_t(window->height()*window->devicePixelRatio());
            size.width=std::clamp(size.width,capabilities.minImageExtent.width,capabilities.maxImageExtent.width);
            size.height=std::clamp(size.height,capabilities.minImageExtent.height,capabilities.maxImageExtent.height);}
        if(!size.width || !size.height)return true;
        if(!swap.handle || swap.size.width!=size.width || swap.size.height!=size.height || swap.surface!=surface){
            check(vkDeviceWaitIdle(p.device),"Wait before Vulkan resize.");p.destroy(swap);swap.surface=surface;swap.size=size;
            if(!(capabilities.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT))throw Failure("The Vulkan surface cannot receive transfer images.");
            uint32_t count=0;check(vkGetPhysicalDeviceSurfaceFormatsKHR(p.physical,surface,&count,nullptr),"Query Vulkan surface formats.");
            std::vector<VkSurfaceFormatKHR> formats(count);check(vkGetPhysicalDeviceSurfaceFormatsKHR(p.physical,surface,&count,formats.data()),"Query Vulkan surface formats.");
            const auto format=std::find_if(formats.begin(),formats.end(),[](auto value){return value.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
                (value.format==VK_FORMAT_B8G8R8A8_UNORM || value.format==VK_FORMAT_R8G8B8A8_UNORM);});
            if(format==formats.end())throw Failure("No compatible Vulkan SDR presentation format.");
            VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};info.surface=surface;
            info.minImageCount=std::max(2u,capabilities.minImageCount);if(capabilities.maxImageCount)info.minImageCount=std::min(info.minImageCount,capabilities.maxImageCount);
            info.imageFormat=format->format;info.imageColorSpace=format->colorSpace;info.imageExtent=size;info.imageArrayLayers=1;
            info.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT;info.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;info.preTransform=capabilities.currentTransform;
            info.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            if(!(capabilities.supportedCompositeAlpha&info.compositeAlpha))info.compositeAlpha=VkCompositeAlphaFlagBitsKHR(capabilities.supportedCompositeAlpha&(~capabilities.supportedCompositeAlpha+1));
            info.presentMode=VK_PRESENT_MODE_FIFO_KHR;info.clipped=VK_TRUE;
            check(vkCreateSwapchainKHR(p.device,&info,nullptr,&swap.handle),"Create Vulkan swapchain.");
            check(vkGetSwapchainImagesKHR(p.device,swap.handle,&count,nullptr),"Query Vulkan swapchain images.");swap.images.resize(count);
            check(vkGetSwapchainImagesKHR(p.device,swap.handle,&count,swap.images.data()),"Query Vulkan swapchain images.");
            VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            check(vkCreateSemaphore(p.device,&semaphore,nullptr,&swap.acquire),"Create Vulkan acquire semaphore.");
            check(vkCreateSemaphore(p.device,&semaphore,nullptr,&swap.complete),"Create Vulkan present semaphore.");
        }
        auto bg=frame->vulkanImage();auto scratch=p.image(shared_from_this(),bg->width,bg->height);
        auto display=p.image(shared_from_this(),bg->width,bg->height,VK_FORMAT_R8G8B8A8_UNORM);
        Parameters params;params.control={9,0,0,int(grayscale)};p.dispatch(bg,{},{},scratch,params,display);
        uint32_t index=0;auto acquired=vkAcquireNextImageKHR(p.device,swap.handle,1'000'000'000ULL,swap.acquire,VK_NULL_HANDLE,&index);
        if(acquired==VK_ERROR_OUT_OF_DATE_KHR){p.destroy(swap);return true;}
        if(acquired!=VK_SUBOPTIMAL_KHR)check(acquired,"Acquire Vulkan presentation image.");
        auto cmd=p.begin();p.barrier(cmd,swap.images[index],VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0);
        VkClearColorValue clear{};std::copy(surround.begin(),surround.end(),clear.float32);
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdClearColorImage(cmd,swap.images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);
        p.barrier(cmd,swap.images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        auto& image=p.allocation(display);p.barrier(cmd,image.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        const double scale=std::min(double(size.width)/bg->width,double(size.height)/bg->height);
        const int w=std::max(1,int(std::lround(bg->width*scale))),h=std::max(1,int(std::lround(bg->height*scale)));
        const int x=(int(size.width)-w)/2,y=(int(size.height)-h)/2;
        VkImageBlit region{};region.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.srcOffsets[1]={bg->width,bg->height,1};
        region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.dstOffsets[0]={x,y,0};region.dstOffsets[1]={x+w,y+h,1};
        vkCmdBlitImage(cmd,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,swap.images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region,VK_FILTER_LINEAR);
        p.barrier(cmd,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
        p.barrier(cmd,swap.images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        p.submit(cmd,swap.acquire,swap.complete);
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount=1;present.pWaitSemaphores=&swap.complete;
        present.swapchainCount=1;present.pSwapchains=&swap.handle;present.pImageIndices=&index;
        p.instance.presentAboutToBeQueued(window);const auto result=vkQueuePresentKHR(p.queue,&present);p.instance.presentQueued(window);
        check(vkQueueWaitIdle(p.queue),"Wait for Vulkan presentation.");
        if(result==VK_ERROR_OUT_OF_DATE_KHR || result==VK_SUBOPTIMAL_KHR)p.destroy(swap);
        else check(result,"Present Vulkan image.");
        return true;
    }catch(const Failure& error){cause=error.what();if(error.code)cause+=" (Vulkan error "+std::to_string(error.code)+")";
        if(error.code==VK_ERROR_DEVICE_LOST)impl_->lost=true;
        return false;}
    catch(const std::exception& error){cause=error.what();return false;}
#else
    Q_UNUSED(window);Q_UNUSED(frame);Q_UNUSED(grayscale);Q_UNUSED(surround);cause="Vulkan was not compiled into this build.";return false;
#endif
}
}
