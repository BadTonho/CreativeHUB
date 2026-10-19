#if defined(_WIN32) && defined(CS_HAS_VULKAN) && defined(CS_NATIVE_VIDEO_INTEROP)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#define CS_VULKAN_NATIVE_EXPORT 1
#endif
#ifdef CS_HAS_VULKAN
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#endif
#include <creative_suite/composition/vulkan_frame_compositor.h>
#include <creative_suite/composition/vulkan_native_export.h>
#include <creative_suite/composition/vulkan_native_import.h>
#include "windows_nv12_converter.h"
#include <creative_suite/media/native_video_frame.h>
#include <creative_suite/diagnostics/logger.h>
#include <QWindow>
#include <QFile>
#include <QLibrary>
#include <QVulkanInstance>
#include <QCoreApplication>
#if !QT_CONFIG(vulkan)
#undef CS_HAS_VULKAN
#undef CS_VULKAN_NATIVE_EXPORT
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
#include <thread>
#include <deque>
#include <utility>
#include <new>

#ifdef CS_HAS_VULKAN
#include <vulkan/vulkan.h>
#include "composition_spirv.h"
#include "composition_precise_spirv.h"
#include "interop_import_spirv.h"
#include "interop_export_spirv.h"
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
PFN_vkCreateSampler vkCreateSampler = nullptr;
PFN_vkDestroySampler vkDestroySampler = nullptr;
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
PFN_vkGetFenceStatus vkGetFenceStatus = nullptr;
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
PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2 = nullptr;
PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties = nullptr;
PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties = nullptr;
PFN_vkGetPhysicalDeviceImageFormatProperties2 vkGetPhysicalDeviceImageFormatProperties2 = nullptr;
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
        vkCreateSampler=reinterpret_cast<PFN_vkCreateSampler>(loader.resolve("vkCreateSampler"));
        if(!vkCreateSampler)throw Failure("The Vulkan loader lacks vkCreateSampler.");
        vkDestroySampler=reinterpret_cast<PFN_vkDestroySampler>(loader.resolve("vkDestroySampler"));
        if(!vkDestroySampler)throw Failure("The Vulkan loader lacks vkDestroySampler.");
        vkGetPhysicalDeviceProperties2=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(loader.resolve("vkGetPhysicalDeviceProperties2"));
        if(!vkGetPhysicalDeviceProperties2)throw Failure("The Vulkan loader lacks vkGetPhysicalDeviceProperties2.");
        vkEnumerateDeviceExtensionProperties=reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(loader.resolve("vkEnumerateDeviceExtensionProperties"));
        if(!vkEnumerateDeviceExtensionProperties)throw Failure("The Vulkan loader lacks vkEnumerateDeviceExtensionProperties.");
        vkGetDeviceProcAddr=reinterpret_cast<PFN_vkGetDeviceProcAddr>(loader.resolve("vkGetDeviceProcAddr"));
        if(!vkGetDeviceProcAddr)throw Failure("The Vulkan loader lacks vkGetDeviceProcAddr.");
        vkGetPhysicalDeviceFormatProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(loader.resolve("vkGetPhysicalDeviceFormatProperties"));
        if(!vkGetPhysicalDeviceFormatProperties)throw Failure("The Vulkan loader lacks vkGetPhysicalDeviceFormatProperties.");
        vkGetPhysicalDeviceImageFormatProperties2=reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2>(loader.resolve("vkGetPhysicalDeviceImageFormatProperties2"));
        if(!vkGetPhysicalDeviceImageFormatProperties2)throw Failure("The Vulkan loader lacks vkGetPhysicalDeviceImageFormatProperties2.");
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
        vkGetFenceStatus = reinterpret_cast<PFN_vkGetFenceStatus>(loader.resolve("vkGetFenceStatus"));
        if(!vkGetFenceStatus)throw Failure("The Vulkan loader lacks vkGetFenceStatus.");
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
    VkDescriptorSetLayout descriptors=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    struct BoundaryPipeline {
        VkDevice device=VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptors=VK_NULL_HANDLE;
        VkPipelineLayout layout=VK_NULL_HANDLE;
        VkPipeline pipeline=VK_NULL_HANDLE;
        VkSampler sampler=VK_NULL_HANDLE;
        ~BoundaryPipeline(){
            if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
            if(layout)vkDestroyPipelineLayout(device,layout,nullptr);
            if(descriptors)vkDestroyDescriptorSetLayout(device,descriptors,nullptr);
            if(sampler)vkDestroySampler(device,sampler,nullptr);
        }
    };
    std::unique_ptr<BoundaryPipeline> import_boundary,export_boundary;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory_properties{};
    bool precise=false;
    std::atomic_bool lost{false};
    mutable std::recursive_mutex mutex;
    std::vector<std::unique_ptr<ImageAllocation>> pool;
    std::map<const void*,std::uint64_t> allocation_bytes;
    std::uint64_t bytes=0,peak=0,known_peak=0;
    static constexpr std::uint64_t limit=256ULL*1024*1024;
    std::uint64_t capacity=limit,external_reserved=0;
    bool native_export=false;
    struct Completion { std::atomic_bool done{false}; };
    struct Context {
        VkDevice device=VK_NULL_HANDLE;
        ~Context(){
            if(fence)vkDestroyFence(device,fence,nullptr);
            if(descriptors)vkDestroyDescriptorPool(device,descriptors,nullptr);
            if(pool)vkDestroyCommandPool(device,pool,nullptr);
        }
        VkCommandPool pool=VK_NULL_HANDLE;
        VkCommandBuffer command=VK_NULL_HANDLE;
        VkDescriptorPool descriptors=VK_NULL_HANDLE;
        VkFence fence=VK_NULL_HANDLE;
        std::thread::id owner;
        unsigned sets=0;
        bool pending=false,recording=false;
        unsigned waiters=0;
        std::shared_ptr<Completion> completion;
        std::vector<std::shared_ptr<void>> resources;
        std::vector<std::shared_ptr<void>> images;
    };
    std::vector<std::unique_ptr<Context>> contexts;
    std::map<std::thread::id,Context*> recording;
    Diagnostics counters;
    const bool serial=qEnvironmentVariableIntValue("CREATIVE_SUITE_VULKAN_SERIAL")==1;
    struct Buffer;
    std::vector<std::unique_ptr<Buffer>> buffer_pool;
    struct Swapchain {
        VkSwapchainKHR handle=VK_NULL_HANDLE;
        VkSurfaceKHR surface=VK_NULL_HANDLE;
        VkExtent2D size{};
        std::vector<VkSemaphore> acquire,complete;
        std::vector<std::shared_ptr<Completion>> acquired;
        unsigned next=0;
        std::vector<VkImage> images;
    };
    std::map<QWindow*,Swapchain> windows;
    ~Impl() {
        if(!device)return;
        vkDeviceWaitIdle(device);
        for(auto& c:contexts){c->resources.clear();c->images.clear();}
        buffer_pool.clear();
        contexts.clear();
        for(auto& [window,swap]:windows){Q_UNUSED(window);destroy(swap);}
        for(auto& allocation:pool)destroy(*allocation);
        import_boundary.reset();export_boundary.reset();
        if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
        if(layout)vkDestroyPipelineLayout(device,layout,nullptr);
        if(descriptors)vkDestroyDescriptorSetLayout(device,descriptors,nullptr);
        vkDestroyDevice(device,nullptr);
    }
    void destroy(ImageAllocation& image) {
        if(image.view)vkDestroyImageView(device,image.view,nullptr);
        if(image.image)vkDestroyImage(device,image.image,nullptr);
        if(image.memory)vkFreeMemory(device,image.memory,nullptr);
        allocation_bytes.erase(&image);bytes-=image.bytes;
    }
    void destroy(Swapchain& swap) {
        if(swap.handle)vkDestroySwapchainKHR(device,swap.handle,nullptr);
        for(auto semaphore:swap.acquire)vkDestroySemaphore(device,semaphore,nullptr);
        for(auto semaphore:swap.complete)vkDestroySemaphore(device,semaphore,nullptr);
        swap={};
    }
    uint32_t memoryType(uint32_t bits,VkMemoryPropertyFlags flags) {
        for(uint32_t i=0;i<memory_properties.memoryTypeCount;++i)
            if((bits&(1u<<i)) && (memory_properties.memoryTypes[i].propertyFlags&flags)==flags)return i;
        throw Failure("No compatible Vulkan memory type.",0,Status::Unsupported);
    }
    void reap() {
        for(auto& c:contexts)if(c->pending){
            const auto status=vkGetFenceStatus(device,c->fence);
            if(status==VK_NOT_READY)continue;
            if(status!=VK_SUCCESS){lost=true;check(status,"Collect Vulkan completion.");}
            c->pending=false;c->completion->done=true;--counters.pending;
            c->resources.clear();c->images.clear();
        }
    }
    void wait(Context& c,std::unique_lock<std::recursive_mutex>& lock,
        const GpuFrameCompositor::CancellationPredicate& cancel={}) {
        const auto start=Clock::now();++counters.waits;
        // The context cannot be recycled while its caller owns its completion.
        auto completion=c.completion;++c.waiters;
        struct Unpin {Context& context;~Unpin(){--context.waiters;}} unpin{c};
        while(!completion->done){
            cancelled(cancel);lock.unlock();
            const auto result=vkWaitForFences(device,1,&c.fence,VK_TRUE,1'000'000);lock.lock();
            if(result!=VK_SUCCESS && result!=VK_TIMEOUT){lost=true;check(result,"Wait for Vulkan completion.");}
            reap();if(lost)throw Failure("The Vulkan device was lost.",VK_ERROR_DEVICE_LOST);
            if(ns(start)>10'000'000'000ULL){lost=true;throw Failure("Vulkan completion timed out.",VK_TIMEOUT);}
        }
        counters.wait_nanoseconds+=ns(start);
    }
    Context* current() {const auto i=recording.find(std::this_thread::get_id());return i==recording.end()?nullptr:i->second;}
    Context* start(bool nonblocking,const GpuFrameCompositor::CancellationPredicate& cancel={}) {
        std::unique_lock lock(mutex);if(current())return nullptr;
        if(lost)throw Failure("The Vulkan device was lost.",VK_ERROR_DEVICE_LOST);
        for(;;){
            reap();const auto owner=std::this_thread::get_id();Context* available=nullptr;Context* oldest=nullptr;unsigned count=0;
            for(auto& c:contexts){
                if(!c->pending && !c->recording && !c->waiters)available=c.get();
                if(c->owner==owner && c->pending){++count;if(!oldest)oldest=c.get();}
            }
            if(count>=2){if(nonblocking)return nullptr;wait(*oldest,lock,cancel);continue;}
            if(!available && contexts.size()<6){
                auto c=std::make_unique<Context>();c->device=device;
                VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool_info.queueFamilyIndex=family;
                check(vkCreateCommandPool(device,&pool_info,nullptr,&c->pool),"Create Vulkan command context.");
                VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};command.commandPool=c->pool;
                command.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command.commandBufferCount=1;
                check(vkAllocateCommandBuffers(device,&command,&c->command),"Allocate Vulkan command context.");
                VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(device,&fence,nullptr,&c->fence),"Create Vulkan context fence.");
                std::array<VkDescriptorPoolSize,2> sizes{{{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,5*256},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,256}}};
                VkDescriptorPoolCreateInfo descriptor_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};descriptor_info.maxSets=256;descriptor_info.poolSizeCount=uint32_t(sizes.size());descriptor_info.pPoolSizes=sizes.data();
                check(vkCreateDescriptorPool(device,&descriptor_info,nullptr,&c->descriptors),"Create Vulkan context descriptors.");
                available=c.get();contexts.push_back(std::move(c));
            }
            if(!available){if(nonblocking)return nullptr;
                for(auto& c:contexts)if(c->pending){oldest=c.get();break;}
                if(!oldest)throw Failure("All Vulkan recording contexts are occupied.",0,Status::Busy);
                wait(*oldest,lock,cancel);continue;}
            check(vkResetCommandPool(device,available->pool,0),"Reset Vulkan context.");
            check(vkResetDescriptorPool(device,available->descriptors,0),"Reset Vulkan context descriptors.");
            VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};info.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            check(vkBeginCommandBuffer(available->command,&info),"Begin Vulkan context.");
            available->owner=owner;available->recording=true;available->sets=0;available->completion=std::make_shared<Completion>();
            recording[owner]=available;return available;
        }
    }
    void retain(const std::shared_ptr<VulkanImage>& image) {
        if(!image)return;auto* c=current();if(!c)throw Failure("Missing Vulkan recording context.");
        const auto& allocation=image->allocation;
        if(std::find(c->images.begin(),c->images.end(),allocation)==c->images.end())c->images.push_back(allocation);
    }
    VkCommandBuffer begin() {auto* c=current();if(!c)throw Failure("Missing Vulkan recording context.");return c->command;}
    // Individual operations append to the current batch. Only finish() submits.
    void submit(VkCommandBuffer) {}
    std::shared_ptr<Completion> finish(Context* c,VkSemaphore wait_semaphore=VK_NULL_HANDLE,VkSemaphore signal=VK_NULL_HANDLE,const void* submit_extension=nullptr) {
        if(!c)return {};std::unique_lock lock(mutex);
        if(!c->recording)return c->completion;
        check(vkEndCommandBuffer(c->command),"End Vulkan command batch.");
        check(vkResetFences(device,1,&c->fence),"Reset Vulkan context fence.");
        VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};info.pNext=submit_extension;info.commandBufferCount=1;info.pCommandBuffers=&c->command;
        if(wait_semaphore){info.waitSemaphoreCount=1;info.pWaitSemaphores=&wait_semaphore;info.pWaitDstStageMask=&stage;}
        if(signal){info.signalSemaphoreCount=1;info.pSignalSemaphores=&signal;}
        const auto result=vkQueueSubmit(queue,1,&info,c->fence);
        c->recording=false;recording.erase(c->owner);
        if(result!=VK_SUCCESS){lost=true;check(result,"Submit Vulkan command batch.");}
        c->pending=true;++counters.submissions;++counters.pending;counters.peak_pending=std::max(counters.peak_pending,counters.pending);
        auto completion=c->completion;if(serial)this->wait(*c,lock);return completion;
    }
    // Only valid before recording image work or acquiring a signaled semaphore.
    // Reset the unsubmitted reservation; no fence or queue submission is needed.
    void abandonEmpty(Context* c) {
        if(!c || !c->recording)return;
        if(!c->resources.empty() || !c->images.empty() || c->sets)
            throw Failure("Cannot abandon a nonempty Vulkan command context.");
        check(vkResetCommandPool(device,c->pool,0),"Abandon Vulkan presentation reservation.");
        recording.erase(c->owner);c->recording=false;c->completion->done=true;
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
        reap();
        if(auto* c=current())for(const auto& resident:c->images){
            auto* a=static_cast<ImageAllocation*>(resident.get());
            if(resident.use_count()==1 && a->width==width && a->height==height && a->format==format){
                auto image=std::make_shared<VulkanImage>();image->width=width;image->height=height;
                image->device=std::move(owner);image->allocation=resident;image->completion=c->completion;return image;
            }
        }
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
            trim(req.size);
            if(bytes+req.size>capacity){vkDestroyImage(device,allocation->image,nullptr);throw Failure("The bounded Vulkan image pool is occupied.",0,Status::Busy);}
            VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memory.allocationSize=req.size;
            memory.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            auto result=vkAllocateMemory(device,&memory,nullptr,&allocation->memory);
            if(result!=VK_SUCCESS){vkDestroyImage(device,allocation->image,nullptr);check(result,"Allocate Vulkan image memory.");}
            allocation->bytes=req.size;bytes+=req.size;peak=std::max(peak,bytes);known_peak=std::max(known_peak,bytes+external_reserved);
            try {
                // Charge first: bookkeeping allocation can fail, and destroy()
                // must release exactly the backing already charged here.
                registerAllocation(allocation.get(),req.size,"image");
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
        });result->completion=current()->completion;retain(result);return result;
    }
    struct Buffer {
        VkDeviceSize capacity=0;bool readable=false;
        Impl& p;VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;void* data=nullptr;VkDeviceSize reserved=0;
        Buffer(Impl& owner,VkDeviceSize size,bool cpu_read=false):capacity(size),readable(cpu_read),p(owner) {
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=size;
            info.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            check(vkCreateBuffer(p.device,&info,nullptr,&buffer),"Create Vulkan transfer buffer.");
            try {
                VkMemoryRequirements req{};vkGetBufferMemoryRequirements(p.device,buffer,&req);
                p.trim(req.size);
                if(p.bytes+req.size>p.capacity)throw Failure("The bounded Vulkan transfer budget is occupied.",0,Status::Busy);
                VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=req.size;
                allocation.memoryTypeIndex=p.memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                // Write-combined coherent heaps are suitable for uploads, but CPU
                // readback should prefer a cached heap when the device offers one.
                if(cpu_read)for(uint32_t i=0;i<p.memory_properties.memoryTypeCount;++i) {
                    const auto flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
                    if((req.memoryTypeBits&(1u<<i)) && (p.memory_properties.memoryTypes[i].propertyFlags&flags)==flags){allocation.memoryTypeIndex=i;break;}
                }
                check(vkAllocateMemory(p.device,&allocation,nullptr,&memory),"Allocate Vulkan transfer memory.");
                reserved=req.size;p.bytes+=reserved;p.peak=std::max(p.peak,p.bytes);p.known_peak=std::max(p.known_peak,p.bytes+p.external_reserved);
                p.registerAllocation(this,reserved,"transfer");
                check(vkBindBufferMemory(p.device,buffer,memory,0),"Bind Vulkan transfer memory.");
                check(vkMapMemory(p.device,memory,0,size,0,&data),"Map Vulkan transfer memory.");
            }catch(...){p.allocation_bytes.erase(this);p.bytes-=reserved;if(memory)vkFreeMemory(p.device,memory,nullptr);vkDestroyBuffer(p.device,buffer,nullptr);throw;}
        }
        ~Buffer(){p.allocation_bytes.erase(this);p.bytes-=reserved;if(data)vkUnmapMemory(p.device,memory);vkDestroyBuffer(p.device,buffer,nullptr);vkFreeMemory(p.device,memory,nullptr);}
    };
    void registerAllocation(const void* key,std::uint64_t size,const char* kind) {
        // Private regression injection: fail metadata only, after backing is charged.
        if(qgetenv("CREATIVE_SUITE_VULKAN_FAIL_METADATA")==kind)throw std::bad_alloc();
        allocation_bytes[key]=size;
    }
    Diagnostics memoryDiagnostics() const {
        auto result=counters;
        // Snapshot without allocating: diagnostics() is a nonthrowing boundary.
        for(const auto& [key,size]:allocation_bytes){
            bool pending=false;
            for(const auto& c:contexts)if(c->pending){
                const auto matches=[&](const auto& value){return value.get()==key;};
                if(std::any_of(c->images.begin(),c->images.end(),matches) || std::any_of(c->resources.begin(),c->resources.end(),matches)){pending=true;break;}
            }
            if(pending)result.pending_bytes+=size;
        }
        for(const auto& image:pool)result.cached_bytes+=image->bytes;
        for(const auto& buffer:buffer_pool)result.cached_bytes+=buffer->reserved;
        result.active_bytes=bytes-result.pending_bytes-result.cached_bytes;
        result.external_bytes=external_reserved;result.known_bytes=bytes+external_reserved;return result;
    }
    void traceMemory(const char* stage) const {
        if(qEnvironmentVariableIntValue("CREATIVE_SUITE_VULKAN_MEMORY_TRACE")!=1)return;
        const auto m=memoryDiagnostics();
        creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Info,"vulkan-memory",stage,"Known allocation snapshot; decoder excluded.",
            {{"active_bytes",std::to_string(m.active_bytes)},{"pending_bytes",std::to_string(m.pending_bytes)},
             {"cached_bytes",std::to_string(m.cached_bytes)},{"external_bytes",std::to_string(m.external_bytes)},{"known_bytes",std::to_string(m.known_bytes)}});
    }
    void trim(VkDeviceSize needed) {
        while(bytes+needed>capacity && !buffer_pool.empty())buffer_pool.pop_back();
        while(bytes+needed>capacity && !pool.empty()){destroy(*pool.back());pool.pop_back();}
    }
    std::shared_ptr<Buffer> buffer(VkDeviceSize size,bool cpu_read=false) {
        auto found=std::find_if(buffer_pool.begin(),buffer_pool.end(),[&](const auto& b){return b->capacity==size && b->readable==cpu_read;});
        std::unique_ptr<Buffer> value;
        if(found!=buffer_pool.end()){value=std::move(*found);buffer_pool.erase(found);}
        else value=std::make_unique<Buffer>(*this,size,cpu_read);
        return std::shared_ptr<Buffer>(value.release(),[this](Buffer* b){std::lock_guard lock(mutex);buffer_pool.emplace_back(b);});
    }
    static ImageAllocation& allocation(const std::shared_ptr<VulkanImage>& image) {
        return *static_cast<ImageAllocation*>(image->allocation.get());
    }
    std::shared_ptr<VulkanImage> input(const OpenGlImageInput& input,std::shared_ptr<VulkanDevice> owner,OpenGlCompositionTimings* timings) {
        if(input.texture) {
            const auto image=input.texture->vulkanImage();
            if(!image || image->device!=owner)throw Failure("The image belongs to another graphics device.",0,Status::Unsupported);
            retain(image);return image;
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
        auto staging=buffer(uint64_t(frame->width)*frame->height*4);current()->resources.push_back(staging);
        for(int y=0;y<frame->height;++y)std::memcpy(static_cast<char*>(staging->data)+size_t(y)*frame->width*4,
            frame->rgba_pixels.data()+size_t(y)*frame->stride,size_t(frame->width)*4);
        auto cmd=begin();auto& a=allocation(image);barrier(cmd,a.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={uint32_t(frame->width),uint32_t(frame->height),1};
        vkCmdCopyBufferToImage(cmd,staging->buffer,a.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
        barrier(cmd,a.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);submit(cmd);
        if(timings){timings->upload_nanoseconds+=ns(start);timings->uploaded_bytes+=uint64_t(frame->width)*frame->height*4;++timings->uploaded_layers;}
        return image;
    }
    BoundaryPipeline& boundary(bool importing) {
        auto& cached=importing?import_boundary:export_boundary;if(cached)return *cached;
        auto value=std::make_unique<BoundaryPipeline>();value->device=device;
        std::array<VkDescriptorSetLayoutBinding,2> bindings{};
        for(unsigned i=0;i<2;++i){bindings[i].binding=i;bindings[i].descriptorType=i==0 && importing?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
        VkDescriptorSetLayoutCreateInfo descriptor{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};descriptor.bindingCount=2;descriptor.pBindings=bindings.data();
        check(vkCreateDescriptorSetLayout(device,&descriptor,nullptr,&value->descriptors),"Create Vulkan boundary descriptor layout.");
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout_info.setLayoutCount=1;layout_info.pSetLayouts=&value->descriptors;
        check(vkCreatePipelineLayout(device,&layout_info,nullptr,&value->layout),"Create Vulkan boundary pipeline layout.");
        if(importing){VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            sampler.magFilter=sampler.minFilter=VK_FILTER_NEAREST;sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            check(vkCreateSampler(device,&sampler,nullptr,&value->sampler),"Create Vulkan boundary sampler.");}
        VkShaderModuleCreateInfo shader_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shader_info.codeSize=importing?sizeof(interop_import_spirv):sizeof(interop_export_spirv);
        shader_info.pCode=importing?interop_import_spirv:interop_export_spirv;
        VkShaderModule shader{};check(vkCreateShaderModule(device,&shader_info,nullptr,&shader),"Load Vulkan boundary shader.");
        VkComputePipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipeline_info.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};pipeline_info.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline_info.stage.module=shader;pipeline_info.stage.pName="main";pipeline_info.layout=value->layout;
        const auto result=vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pipeline_info,nullptr,&value->pipeline);
        vkDestroyShaderModule(device,shader,nullptr);check(result,"Create Vulkan boundary pipeline.");cached=std::move(value);return *cached;
    }
    // The caller retains an external source and records ownership/layout barriers.
    void dispatchBoundary(bool importing,VkImageView source,const std::shared_ptr<VulkanImage>& output) {
        auto& conversion=boundary(importing);if(current()->sets++>=256)throw Failure("The bounded Vulkan descriptor batch is full.",0,Status::Busy);
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocate.descriptorPool=current()->descriptors;
        allocate.descriptorSetCount=1;allocate.pSetLayouts=&conversion.descriptors;VkDescriptorSet set{};
        check(vkAllocateDescriptorSets(device,&allocate,&set),"Allocate Vulkan boundary descriptors.");
        std::array<VkDescriptorImageInfo,2> images{{{conversion.sampler,source,VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,allocation(output).view,VK_IMAGE_LAYOUT_GENERAL}}};
        std::array<VkWriteDescriptorSet,2> writes{};
        for(unsigned i=0;i<2;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;
            writes[i].descriptorType=i==0 && importing?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;writes[i].pImageInfo=&images[i];}
        vkUpdateDescriptorSets(device,2,writes.data(),0,nullptr);retain(output);auto cmd=begin();
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,conversion.pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,conversion.layout,0,1,&set,0,nullptr);
        vkCmdDispatch(cmd,(output->width+15)/16,(output->height+15)/16,1);
        barrier(cmd,allocation(output).image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL);
    }
    void dispatch(const std::shared_ptr<VulkanImage>& bg,const std::shared_ptr<VulkanImage>& fg,
        const std::shared_ptr<VulkanImage>& mask,const std::shared_ptr<VulkanImage>& output,
        const Parameters& parameters,const std::shared_ptr<VulkanImage>& display={}) {
        if(current()->sets++>=256)throw Failure("The bounded Vulkan descriptor batch is full.",0,Status::Busy);
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool=current()->descriptors;allocate.descriptorSetCount=1;allocate.pSetLayouts=&descriptors;
        VkDescriptorSet set{};check(vkAllocateDescriptorSets(device,&allocate,&set),"Allocate Vulkan descriptors.");
        // All descriptors are valid even for shader branches that do not access them.
        auto dummy=display ? display : image(output->device,1,1,VK_FORMAT_R8G8B8A8_UNORM);
        const std::array<std::shared_ptr<VulkanImage>,5> images{bg,fg?fg:bg,mask?mask:bg,output,dummy};
        std::array<VkDescriptorImageInfo,5> image_info{};std::array<VkWriteDescriptorSet,5> writes{};
        for(unsigned i=0;i<5;++i){retain(images[i]);image_info[i].imageView=allocation(images[i]).view;image_info[i].imageLayout=VK_IMAGE_LAYOUT_GENERAL;
            writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=set;writes[i].dstBinding=i;
            writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;writes[i].pImageInfo=&image_info[i];}
        vkUpdateDescriptorSets(device,5,writes.data(),0,nullptr);
        auto cmd=begin();vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);
        if(precise)vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(parameters),&parameters);
        else {
            struct Small {std::array<int,4> control;std::array<float,4> geometry,rotation,color;} compact_parameters;
            compact_parameters.control=parameters.control;compact_parameters.color=parameters.color;
            for(unsigned i=0;i<4;++i){compact_parameters.geometry[i]=float(parameters.geometry[i]);compact_parameters.rotation[i]=float(parameters.rotation[i]);}
            vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(compact_parameters),&compact_parameters);
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
VulkanDevice::~VulkanDevice(){
#ifdef CS_HAS_VULKAN
    if(impl_->device)try {
        const auto stats=diagnostics();
        creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Info,"vulkan","session_metrics","Vulkan session resource and synchronization summary.",
            {{"submissions",std::to_string(stats.submissions)},{"worker_waits",std::to_string(stats.waits)},
             {"worker_wait_ns",std::to_string(stats.wait_nanoseconds)},{"pending_contexts",std::to_string(stats.pending)},
             {"peak_pending_contexts",std::to_string(stats.peak_pending)},{"reserved_bytes",std::to_string(impl_->bytes)},
             {"peak_reserved_bytes",std::to_string(impl_->peak)},{"serial_diagnostic",impl_->serial?"true":"false"}});
    }catch(...){}
#endif
}
std::shared_ptr<VulkanDevice> VulkanDevice::create(std::string& cause) {return create(cause,CreateOptions{});}
std::shared_ptr<VulkanDevice> VulkanDevice::create(std::string& cause,const CreateOptions& options) {
    if(qEnvironmentVariableIntValue("CREATIVE_SUITE_DISABLE_VULKAN")==1){cause="Vulkan was disabled by the diagnostic environment setting.";return {};}
#ifdef CS_HAS_VULKAN
    try {
        if(!options.known_byte_limit || options.known_byte_limit>Impl::limit || options.external_reserved_bytes>=options.known_byte_limit)
            throw Failure("Invalid bounded Vulkan/native reservation.",0,Status::Unsupported);
#ifndef CS_VULKAN_NATIVE_EXPORT
        if(options.experimental_native_export || options.adapter_luid)throw Failure("Native Vulkan export is unavailable in this build.",0,Status::Unsupported);
#endif
        loadVulkanApi();
        auto device=std::shared_ptr<VulkanDevice>(new VulkanDevice);auto& p=*device->impl_;
        p.capacity=options.known_byte_limit-options.external_reserved_bytes;p.external_reserved=options.external_reserved_bytes;p.known_peak=p.external_reserved;
        p.instance.setApiVersion(QVersionNumber(1,1));
        if(qEnvironmentVariableIntValue("CREATIVE_SUITE_VULKAN_VALIDATION")==1)
            p.instance.setLayers({"VK_LAYER_KHRONOS_validation"});
        if(!p.instance.create())throw Failure("Cannot initialize the Vulkan loader or instance.",p.instance.errorCode());
        uint32_t count=0;check(vkEnumeratePhysicalDevices(p.instance.vkInstance(),&count,nullptr),"Enumerate Vulkan devices.");
        std::vector<VkPhysicalDevice> devices(count);check(vkEnumeratePhysicalDevices(p.instance.vkInstance(),&count,devices.data()),"Enumerate Vulkan devices.");
        for(auto candidate:devices){
            if(options.adapter_luid){
                VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
                VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};properties.pNext=&id;
                vkGetPhysicalDeviceProperties2(candidate,&properties);
                if(!id.deviceLUIDValid || std::memcmp(id.deviceLUID,options.adapter_luid->data(),VK_LUID_SIZE))continue;
            }
            uint32_t families=0;vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,nullptr);
            std::vector<VkQueueFamilyProperties> queues(families);vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,queues.data());
            for(uint32_t i=0;i<families;++i)if((queues[i].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)){
                p.physical=candidate;p.family=i;break;
            }
            if(p.physical)break;
        }
        if(!p.physical)throw Failure("No matching Vulkan graphics/compute adapter is available.",0,Status::Unsupported);
        vkGetPhysicalDeviceProperties(p.physical,&p.properties);vkGetPhysicalDeviceMemoryProperties(p.physical,&p.memory_properties);
        VkPhysicalDeviceFeatures available{};vkGetPhysicalDeviceFeatures(p.physical,&available);
        p.precise=available.shaderFloat64;VkPhysicalDeviceFeatures enabled{};enabled.shaderFloat64=p.precise;
        const float priority=1;VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex=p.family;queue.queueCount=1;queue.pQueuePriorities=&priority;
        std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
#ifdef CS_VULKAN_NATIVE_EXPORT
        if(options.experimental_native_export || options.try_native_export){
            uint32_t extension_count=0;check(vkEnumerateDeviceExtensionProperties(p.physical,nullptr,&extension_count,nullptr),"Enumerate Vulkan device extensions.");
            std::vector<VkExtensionProperties> device_extensions(extension_count);
            check(vkEnumerateDeviceExtensionProperties(p.physical,nullptr,&extension_count,device_extensions.data()),"Enumerate Vulkan device extensions.");
            bool supported=true;
            for(const auto* required:{VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME}){
                if(std::none_of(device_extensions.begin(),device_extensions.end(),[&](const auto& value){return std::strcmp(value.extensionName,required)==0;}))
                    supported=false;
            }
            if(!supported && options.experimental_native_export)
                throw Failure("Missing Vulkan external-memory/keyed-mutex extensions.",0,Status::Unsupported);
            if(supported){extensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);extensions.push_back(VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME);p.native_export=true;}
        }
#endif
        VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};info.queueCreateInfoCount=1;info.pQueueCreateInfos=&queue;
        info.enabledExtensionCount=uint32_t(extensions.size());info.ppEnabledExtensionNames=extensions.data();info.pEnabledFeatures=&enabled;
        check(vkCreateDevice(p.physical,&info,nullptr,&p.device),"Create Vulkan device.");
        vkGetDeviceQueue(p.device,p.family,0,&p.queue);
        std::array<VkDescriptorSetLayoutBinding,5> bindings{};
        for(unsigned i=0;i<5;++i){bindings[i].binding=i;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
        VkDescriptorSetLayoutCreateInfo descriptor{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};descriptor.bindingCount=5;descriptor.pBindings=bindings.data();
        check(vkCreateDescriptorSetLayout(p.device,&descriptor,nullptr,&p.descriptors),"Create Vulkan descriptor layout.");
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
    Q_UNUSED(options);cause="This build does not include Vulkan support.";return {};
#endif
}
bool VulkanDevice::supportsNativeExport() const noexcept {
#ifdef CS_HAS_VULKAN
    return impl_->native_export && impl_->properties.vendorID==0x10DE;
#else
    return false;
#endif
}
std::shared_ptr<void> VulkanDevice::tryReserveExternal(std::uint64_t bytes) {
#ifdef CS_HAS_VULKAN
    auto& p=*impl_;std::lock_guard lock(p.mutex);
    if(!bytes || p.lost || bytes>=p.capacity)return {};
    p.reap();p.trim(bytes);if(p.bytes+bytes>p.capacity)return {};
    // Construct the control block before changing accounting (allocation may throw).
    auto armed=std::make_shared<bool>(false);
    auto lease=std::shared_ptr<void>(new std::uint8_t{},[owner=shared_from_this(),bytes,armed](void* value){
        delete static_cast<std::uint8_t*>(value);if(!*armed)return;
        auto& state=*owner->impl_;std::lock_guard guard(state.mutex);
        state.capacity+=bytes;state.external_reserved-=bytes;
    });
    p.capacity-=bytes;p.external_reserved+=bytes;*armed=true;
    p.known_peak=std::max(p.known_peak,p.bytes+p.external_reserved);return lease;
#else
    Q_UNUSED(bytes);return {};
#endif
}
std::uint64_t VulkanDevice::totalReservedBytes() const noexcept {
#ifdef CS_HAS_VULKAN
    std::lock_guard lock(impl_->mutex);return impl_->bytes+impl_->external_reserved;
#else
    return 0;
#endif
}
std::uint64_t VulkanDevice::totalPeakBytes() const noexcept {
#ifdef CS_HAS_VULKAN
    std::lock_guard lock(impl_->mutex);return impl_->known_peak;
#else
    return 0;
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
namespace {
class VulkanCommandBatch final : public GpuCommandBatch {
    std::function<GpuCompositionResult()> submit_;
public:
    explicit VulkanCommandBatch(std::function<GpuCompositionResult()> submit):submit_(std::move(submit)){}
    ~VulkanCommandBatch() override {if(submit_)static_cast<void>(finish());}
    GpuCompositionResult finish() override {
        if(!submit_)return {Status::Complete};auto submit=std::exchange(submit_,{});return submit();
    }
};
}
std::unique_ptr<GpuCommandBatch> VulkanFrameCompositor::beginCommandBatch(const CancellationPredicate& cancel) {
#ifdef CS_HAS_VULKAN
    cancelled(cancel);if(!device_ || !device_->valid())throw Failure("The Vulkan device is unavailable.");
    auto* context=device_->impl_->start(false,cancel);if(!context)return {};
    auto token=context->completion;
    return std::make_unique<VulkanCommandBatch>([device=device_,context,token]{
        try {std::lock_guard lock(device->impl_->mutex);
            if(context->completion!=token)return GpuCompositionResult{Status::Complete};
            device->impl_->finish(context);return GpuCompositionResult{Status::Complete};}
        catch(const Failure& error){device->impl_->lost=true;
            creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,"vulkan","submit_batch",error.what(),{{"error_code",std::to_string(error.code)}});
            return GpuCompositionResult{error.status,{},"vulkan-submit-batch",error.what(),error.code};}
        catch(const std::exception& error){device->impl_->lost=true;return GpuCompositionResult{Status::Failed,{},"vulkan-submit-batch",error.what()};}
    });
#else
    Q_UNUSED(cancel);return {};
#endif
}
VulkanDevice::Diagnostics VulkanDevice::diagnostics() const noexcept {
#ifdef CS_HAS_VULKAN
    std::lock_guard lock(impl_->mutex);return impl_->memoryDiagnostics();
#else
    return {};
#endif
}
struct VulkanFrameCompositor::Readbacks {
#ifdef CS_HAS_VULKAN
    struct Slot {
        std::shared_ptr<VulkanDevice::Impl::Buffer> buffer;
        std::shared_ptr<VulkanDevice::Impl::Completion> completion;
        VulkanDevice::Impl::Context* context=nullptr;
        std::uint64_t ticket=0;
    };
    std::vector<Slot> readback_slots;
    std::deque<std::uint64_t> order;
#endif
    int width=0,height=0;
};
VulkanFrameCompositor::VulkanFrameCompositor(std::shared_ptr<VulkanDevice> device,std::uint64_t bytes,unsigned targets,
    std::shared_ptr<OpenGlTexturePoolBudget> budget)
    :device_(std::move(device)),byte_limit_(bytes),target_limit_(targets),
     budget_(budget ? std::move(budget) : std::make_shared<OpenGlTexturePoolBudget>(bytes, targets)) {}
OpenGlCompositionResult VulkanFrameCompositor::prepareAsyncReadback(int width,int height,std::uint64_t budget,
    unsigned maximum,unsigned* prepared,bool fail) {
    if(prepared)*prepared=0;
    try {
#ifdef CS_HAS_VULKAN
        discardAsyncReadbacks();readbacks_.reset();
        if(!device_ || !device_->valid())throw Failure("The Vulkan device is unavailable.");
        if(width<=0 || height<=0 || fail)throw Failure("Vulkan asynchronous readback is unavailable.",0,Status::Unsupported);
        auto value=std::make_unique<Readbacks>();value->width=width;value->height=height;
        auto& p=*device_->impl_;std::lock_guard lock(p.mutex);p.reap();
        const auto size=uint64_t(width)*height*4;
        if(width>int(p.properties.limits.maxImageDimension2D) || height>int(p.properties.limits.maxImageDimension2D) || size>VulkanDevice::Impl::limit)
            throw Failure("The Vulkan readback canvas exceeds device or memory limits.",0,Status::Unsupported);
        maximum=unsigned(std::min<std::uint64_t>({maximum,2,budget/size}));
        for(unsigned i=0;i<maximum;++i){
            try {Readbacks::Slot slot;slot.buffer=p.buffer(size,true);value->readback_slots.push_back(std::move(slot));}
            catch(const Failure& error){if(error.status!=Status::Busy)throw;break;}
        }
        if(value->readback_slots.empty())throw Failure("No bounded Vulkan readback slot is available.",0,Status::Unsupported);
        if(prepared)*prepared=unsigned(value->readback_slots.size());readbacks_=std::move(value);return {Status::Complete};
#else
        Q_UNUSED(width);Q_UNUSED(height);Q_UNUSED(budget);Q_UNUSED(maximum);Q_UNUSED(fail);
        return {Status::Unsupported};
#endif
    }catch(const Failure& error){return {error.status,{},"vulkan-prepare-readback",error.what(),error.code};}
}
unsigned VulkanFrameCompositor::asyncReadbackSlotCount() const noexcept {
#ifdef CS_HAS_VULKAN
    return readbacks_?unsigned(readbacks_->readback_slots.size()):0;
#else
    return 0;
#endif
}
OpenGlReadbackResult VulkanFrameCompositor::submitAsyncReadback(int width,int height,
    const std::vector<CompositionLayer>& layers,const CancellationPredicate& cancel) {
#ifdef CS_HAS_VULKAN
    if(!readbacks_ || std::none_of(readbacks_->readback_slots.begin(),readbacks_->readback_slots.end(),[](const auto& slot){return !slot.ticket;}))
        return {readbacks_?Status::Busy:Status::Unsupported};
#endif
    std::unique_ptr<GpuCommandBatch> batch;
    try {batch=beginCommandBatch(cancel);}catch(const Failure& error){return {error.status,{}, {},"vulkan-begin-readback",error.what(),error.code};}
    OpenGlCompositionTimings timings;
    auto frame=composeTexture(width,height,layers,cancel,&timings);
    OpenGlReadbackResult result{frame.status,{}, {},frame.operation,frame.cause,frame.error_code};
    if(frame.status==Status::Complete)result=submitAsyncReadback(frame.frame,cancel);
    result.composition_timings=timings;return result;
}
OpenGlReadbackResult VulkanFrameCompositor::submitAsyncReadback(const OpenGlTextureFramePtr& frame,
    const CancellationPredicate& cancel) {
    try {
        cancelled(cancel);
#ifdef CS_HAS_VULKAN
        if(!device_ || !device_->valid() || !frame || !frame->vulkanImage() || frame->vulkanImage()->device!=device_)
            throw Failure("Invalid Vulkan readback lease.",0,Status::Unsupported);
        if(!readbacks_ || frame->width()!=readbacks_->width || frame->height()!=readbacks_->height)
            throw Failure("The Vulkan readback pool was not prepared for this canvas.",0,Status::Unsupported);
        auto& p=*device_->impl_;
        auto batch=beginCommandBatch(cancel);std::lock_guard lock(p.mutex);
        auto slot=std::find_if(readbacks_->readback_slots.begin(),readbacks_->readback_slots.end(),[](const auto& item){return item.ticket==0;});
        if(slot==readbacks_->readback_slots.end())throw Failure("All bounded Vulkan readback readback_slots are occupied.",0,Status::Busy);
        const auto start=Clock::now();auto image=frame->vulkanImage();p.retain(image);
        auto* c=p.current();c->resources.push_back(slot->buffer);
        auto cmd=p.begin();auto& a=p.allocation(image);
        p.barrier(cmd,a.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        region.imageExtent={uint32_t(frame->width()),uint32_t(frame->height()),1};
        vkCmdCopyImageToBuffer(cmd,a.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,slot->buffer->buffer,1,&region);
        p.barrier(cmd,a.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
        slot->completion=c->completion;slot->context=c;
        p.finish(c);
        static std::atomic<std::uint64_t> sequence{0};slot->ticket=++sequence;readbacks_->order.push_back(slot->ticket);
        OpenGlReadbackResult submitted{Status::Complete,{slot->ticket}};submitted.submission_nanoseconds=ns(start);
        submitted.bytes=uint64_t(frame->width())*frame->height()*4;return submitted;
#else
        Q_UNUSED(frame);throw Failure("Vulkan was not compiled into this build.",0,Status::Unsupported);
#endif
    }catch(const Failure& error){return {error.status,{}, {},"vulkan-submit-readback",error.what(),error.code};}
    catch(const std::exception& error){return {Status::Failed,{}, {},"vulkan-submit-readback",error.what()};}
}
OpenGlReadbackResult VulkanFrameCompositor::collectAsyncReadback(OpenGlReadbackTicket ticket,
    const CancellationPredicate& cancel,bool fail) {
    try {
        cancelled(cancel);
#ifdef CS_HAS_VULKAN
        if(!readbacks_)throw Failure("No Vulkan readbacks are prepared.",0,Status::Unsupported);
        auto slot=std::find_if(readbacks_->readback_slots.begin(),readbacks_->readback_slots.end(),[&](const auto& item){return item.ticket==ticket.value && item.ticket;});
        if(slot==readbacks_->readback_slots.end())throw Failure("Unknown or released Vulkan readback ticket.",0,Status::Unsupported);
        if(readbacks_->order.front()!=ticket.value)throw Failure("Vulkan readback tickets must be collected FIFO.",0,Status::Busy);
        if(fail)throw Failure("Injected Vulkan readback collection failure.",-1);
        auto& p=*device_->impl_;std::unique_lock lock(p.mutex);const auto start=Clock::now();p.reap();
        if(!slot->completion->done)p.wait(*slot->context,lock,cancel);
        cancelled(cancel);const auto wait_ns=ns(start);const auto copy=Clock::now();
        media::RgbaFrame rgba{readbacks_->width,readbacks_->height,readbacks_->width*4,{}};
        const auto bytes=uint64_t(rgba.stride)*rgba.height;rgba.rgba_pixels.resize(bytes);
        std::memcpy(rgba.rgba_pixels.data(),slot->buffer->data,bytes);
        slot->ticket=0;slot->completion.reset();readbacks_->order.pop_front();
        OpenGlReadbackResult result{Status::Complete,ticket,std::move(rgba)};
        result.fence_wait_nanoseconds=wait_ns;result.copy_nanoseconds=ns(copy);result.bytes=bytes;return result;
#else
        Q_UNUSED(ticket);Q_UNUSED(fail);return {Status::Unsupported};
#endif
    }catch(const Failure& error){return {error.status,ticket,{},"vulkan-collect-readback",error.what(),error.code};}
    catch(const std::exception& error){return {Status::Failed,ticket,{},"vulkan-collect-readback",error.what()};}
}
void VulkanFrameCompositor::discardAsyncReadbacks() noexcept {
#ifdef CS_HAS_VULKAN
    if(!readbacks_ || !device_)return;
    try {auto& p=*device_->impl_;std::unique_lock lock(p.mutex);
        for(auto& slot:readbacks_->readback_slots)if(slot.ticket){if(!slot.completion->done)p.wait(*slot.context,lock);slot.ticket=0;slot.completion.reset();}
        readbacks_->order.clear();
    }catch(...){device_->impl_->lost=true;}
#endif
}
OpenGlTextureCompositionResult VulkanFrameCompositor::tryUpload(const media::RgbaFrame& rgba) {
#ifdef CS_HAS_VULKAN
    if(!device_ || !device_->valid())return {Status::Failed,{},"vulkan-preview-upload","The Vulkan device is unavailable."};
    auto& p=*device_->impl_;std::unique_lock lock(p.mutex,std::try_to_lock);
    if(!lock.owns_lock())return {Status::Busy};
    VulkanDevice::Impl::Context* context=nullptr;
    try {
        context=p.start(true);if(!context)return {Status::Busy};
        OpenGlImageOperation operation;operation.background.rgba=&rgba;
        auto result=processImage(operation);p.finish(context);return result;
    }catch(const Failure& error){
        if(context)try{p.finish(context);}catch(...){p.lost=true;}
        return {error.status,{},"vulkan-preview-upload",error.what(),error.code};
    }catch(const std::exception& error){
        if(context)try{p.finish(context);}catch(...){p.lost=true;}
        return {Status::Failed,{},"vulkan-preview-upload",error.what()};
    }
#else
    Q_UNUSED(rgba);return {Status::Unsupported};
#endif
}
OpenGlCompositionResult VulkanFrameCompositor::collectReleasedTextureFrames() {
    if(device_ && !device_->valid())return {Status::Failed,{},"vulkan-device-lost","The Vulkan device is no longer available."};
#ifdef CS_HAS_VULKAN
    if(device_){std::lock_guard lock(device_->impl_->mutex);try {device_->impl_->reap();}catch(const Failure& error){return {Status::Failed,{},"vulkan-collect",error.what(),error.code};}}
#endif
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
VulkanFrameCompositor::~VulkanFrameCompositor(){discardAsyncReadbacks();}
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
        auto batch=beginCommandBatch(cancel);
        auto& p=*device_->impl_;std::lock_guard lock(p.mutex);
        if(operation.source_to_canvas)throw Failure("Affine image Transform is not implemented by Vulkan.",0,Status::Unsupported);
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
        const auto execute=[&]{cancelled(cancel);auto out=p.image(device_,width,height);p.dispatch(bg,fg,mask,out,params);bg=std::move(out);p.traceMemory("effects");};
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
        if(batch){auto result=batch->finish();if(result.status!=Status::Complete)throw Failure(result.cause,result.error_code,result.status);}
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
        if(std::any_of(layers.begin(),layers.end(),[](const auto& layer){return !validBlendMode(layer.blend_mode)||layer.blend_mode!=BlendMode::Normal;}))
            throw Failure("Non-Normal blend modes are not implemented by Vulkan.",0,Status::Unsupported);
#ifdef CS_HAS_VULKAN
        if(!device_ || !device_->valid())throw Failure("The Vulkan device is unavailable.");
        checkCapacity(width,height);
        if(std::any_of(layers.begin(),layers.end(),[](const auto& layer){return layer.source_to_canvas.has_value();}))
            throw Failure("Affine layer geometry is not implemented by Vulkan.",0,Status::Unsupported);
        auto batch=beginCommandBatch(cancel);
        auto& p=*device_->impl_;std::lock_guard lock(p.mutex);
        std::shared_ptr<VulkanImage> canvas;Parameters params;
        // First-layer effects can retire their temporary images before allocating
        // the canvas. The existing batch allocator reuses only unleased images.
        const auto initializeCanvas=[&]{if(!canvas){canvas=p.image(device_,width,height);Parameters clear;clear.control[0]=8;p.dispatch(canvas,{},{},canvas,clear);}};
        for(const auto& layer:layers){
            cancelled(cancel);
            if(!layer.frame || !animation::validTransform(layer.transform))continue;
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
            auto fg=p.input(source,device_,timings);initializeCanvas();auto out=p.image(device_,width,height);
            const auto& t=layer.transform;const double scale=std::min(double(width)/fg->width,double(height)/fg->height)*t.scale;
            const double angle=t.rotation_degrees*std::numbers::pi/180;
            params.control[0]=7;params.geometry={width*t.position_x,height*t.position_y,fg->width*scale,fg->height*scale};
            params.rotation={std::cos(angle),std::sin(angle),scale,t.opacity};
            const auto started=Clock::now();p.dispatch(canvas,fg,{},out,params);canvas=std::move(out);p.traceMemory("composition");
            if(timings)timings->draw_submission_nanoseconds+=ns(started);
        }
        cancelled(cancel);initializeCanvas();auto frame=publish(std::move(canvas));
        if(batch){auto result=batch->finish();if(result.status!=Status::Complete)throw Failure(result.cause,result.error_code,result.status);}
        return {Status::Complete,std::move(frame)};
#else
        Q_UNUSED(width);Q_UNUSED(height);Q_UNUSED(layers);Q_UNUSED(timings);throw Failure("Vulkan was not compiled into this build.",0,Status::Unsupported);
#endif
    }catch(const Failure& error){return {error.status,{},"vulkan-compose",error.what(),error.code};}
    catch(const std::exception& error){return {Status::Failed,{},"vulkan-compose",error.what()};}
}
OpenGlCompositionResult VulkanFrameCompositor::readback(const OpenGlTextureFramePtr& frame,
    const CancellationPredicate& cancel,OpenGlCompositionTimings* timings) {
    const auto start=Clock::now();
    if(!frame)return {Status::Unsupported,{},"vulkan-readback","Missing Vulkan image."};
    unsigned readback_slots=0;auto prepared=prepareAsyncReadback(frame->width(),frame->height(),uint64_t(frame->width())*frame->height()*4,1,&readback_slots);
    if(prepared.status!=Status::Complete)return prepared;
    const auto submitted=submitAsyncReadback(frame,cancel);
    if(submitted.status!=Status::Complete)return {submitted.status,{},submitted.operation,submitted.cause,submitted.error_code};
    auto result=collectAsyncReadback(submitted.ticket,cancel);
    if(timings){timings->readback_bytes+=result.bytes;timings->readback_nanoseconds+=ns(start);}
    return {result.status,std::move(result.frame),result.operation,result.cause,result.error_code};
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
VulkanDevice::PresentationResult VulkanDevice::presentFrame(QWindow* window,const OpenGlTextureFramePtr& frame,bool grayscale,
    std::array<float, 4> surround,bool defer_acquisition_for_testing) {
#ifdef CS_HAS_VULKAN
    std::string cause;
    Impl::Context* context=nullptr;
    VkSemaphore acquired_wait=VK_NULL_HANDLE;
    const auto retire_failed_acquisition=[&]{
        std::lock_guard lock(impl_->mutex);
        if(context){
            if(acquired_wait)impl_->finish(context,acquired_wait);
            else impl_->abandonEmpty(context);
            context=nullptr;
        }
        // An acquired image must be released by destroying this failed swapchain
        // after its semaphore consumer, even if recording failed before present.
        if(acquired_wait){
            check(vkDeviceWaitIdle(impl_->device),"Retire failed Vulkan presentation.");
            auto found=impl_->windows.find(window);if(found!=impl_->windows.end())impl_->destroy(found->second);
        }
    };
    try {
        if(!window || !valid() || !frame || !frame->vulkanImage() || frame->vulkanImage()->device.get()!=this)
            throw Failure("The Vulkan presentation image or device is unavailable.");
        auto& p=*impl_;std::unique_lock lock(p.mutex,std::try_to_lock);
        if(!lock.owns_lock()){return {PresentStatus::Deferred,PresentDeferral::DeviceBusy};}
        auto& swap=p.windows[window];const auto surface=QVulkanInstance::surfaceForWindow(window);
        if(!surface)throw Failure("Cannot create the Vulkan window surface.");
        VkBool32 supported=VK_FALSE;check(vkGetPhysicalDeviceSurfaceSupportKHR(p.physical,p.family,surface,&supported),"Check Vulkan presentation support.");
        if(!supported)throw Failure("The Vulkan queue cannot present to this window.");
        VkSurfaceCapabilitiesKHR capabilities{};check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(p.physical,surface,&capabilities),"Query Vulkan surface capabilities.");
        VkExtent2D size=capabilities.currentExtent;
        if(size.width==UINT32_MAX){size.width=uint32_t(window->width()*window->devicePixelRatio());size.height=uint32_t(window->height()*window->devicePixelRatio());
            size.width=std::clamp(size.width,capabilities.minImageExtent.width,capabilities.maxImageExtent.width);
            size.height=std::clamp(size.height,capabilities.minImageExtent.height,capabilities.maxImageExtent.height);}
        if(!size.width || !size.height)return {PresentStatus::Deferred,PresentDeferral::ZeroExtent};
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
            swap.acquire.resize(2);swap.acquired.resize(2);swap.complete.resize(count);
            for(auto& value:swap.acquire)check(vkCreateSemaphore(p.device,&semaphore,nullptr,&value),"Create Vulkan acquire semaphore.");
            for(auto& value:swap.complete)check(vkCreateSemaphore(p.device,&semaphore,nullptr,&value),"Create Vulkan present semaphore.");
        }
        p.reap();const auto acquire_slot=swap.next%2;
        if(swap.acquired[acquire_slot] && !swap.acquired[acquire_slot]->done){return {PresentStatus::Deferred,PresentDeferral::ContextBusy};}
        context=p.start(true);if(!context){return {PresentStatus::Deferred,PresentDeferral::ContextBusy};}
        uint32_t index=0;const auto acquired=defer_acquisition_for_testing ? VK_NOT_READY : vkAcquireNextImageKHR(p.device,swap.handle,0,swap.acquire[acquire_slot],VK_NULL_HANDLE,&index);
        if(acquired==VK_NOT_READY || acquired==VK_TIMEOUT){p.abandonEmpty(context);context=nullptr;return {PresentStatus::Deferred,PresentDeferral::AcquireBusy};}
        if(acquired==VK_ERROR_OUT_OF_DATE_KHR){p.abandonEmpty(context);context=nullptr;check(vkDeviceWaitIdle(p.device),"Wait before Vulkan surface recreation.");p.destroy(swap);return {PresentStatus::Deferred,PresentDeferral::Recreate};}
        if(acquired!=VK_SUBOPTIMAL_KHR)check(acquired,"Acquire Vulkan presentation image.");
        acquired_wait=swap.acquire[acquire_slot];
        auto bg=frame->vulkanImage();auto scratch=p.image(shared_from_this(),bg->width,bg->height);
        auto display=p.image(shared_from_this(),bg->width,bg->height,VK_FORMAT_R8G8B8A8_UNORM);
        Parameters params;params.control={9,0,0,int(grayscale)};p.dispatch(bg,{},{},scratch,params,display);
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
        swap.acquired[acquire_slot]=p.finish(context,swap.acquire[acquire_slot],swap.complete[index]);context=nullptr;++swap.next;
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount=1;present.pWaitSemaphores=&swap.complete[index];
        present.swapchainCount=1;present.pSwapchains=&swap.handle;present.pImageIndices=&index;
        p.instance.presentAboutToBeQueued(window);const auto result=vkQueuePresentKHR(p.queue,&present);p.instance.presentQueued(window);
        if(p.serial)check(vkQueueWaitIdle(p.queue),"Wait for serial Vulkan presentation.");
        PresentationReceipt receipt;receipt.owner_=weak_from_this();receipt.token_=swap.acquired[acquire_slot];
        if(result==VK_ERROR_OUT_OF_DATE_KHR || result==VK_SUBOPTIMAL_KHR){check(vkDeviceWaitIdle(p.device),"Wait before Vulkan surface recreation.");p.destroy(swap);}
        else check(result,"Present Vulkan image.");
        return {PresentStatus::Submitted,PresentDeferral::None,std::move(receipt)};
    }catch(const Failure& error){
        try{retire_failed_acquisition();}catch(...){impl_->lost=true;}
        if(error.status==Status::Busy && !impl_->lost)return {PresentStatus::Deferred,PresentDeferral::ContextBusy};
        cause=error.what();if(error.code)cause+=" (Vulkan error "+std::to_string(error.code)+")";
        if(error.code==VK_ERROR_DEVICE_LOST)impl_->lost=true;
        return {PresentStatus::Failed,PresentDeferral::None,{},std::move(cause)};}
    catch(const std::exception& error){
        try{retire_failed_acquisition();}catch(...){impl_->lost=true;}
        cause=error.what();return {PresentStatus::Failed,PresentDeferral::None,{},std::move(cause)};}
#else
    Q_UNUSED(window);Q_UNUSED(frame);Q_UNUSED(grayscale);Q_UNUSED(surround);Q_UNUSED(defer_acquisition_for_testing);return {PresentStatus::Failed,PresentDeferral::None,{},"Vulkan was not compiled into this build."};
#endif
}
bool VulkanDevice::present(QWindow* window,const OpenGlTextureFramePtr& frame,bool grayscale,
    std::string& cause,std::array<float,4> surround) {
    auto result=presentFrame(window,frame,grayscale,surround);cause=std::move(result.cause);
    return result.status==PresentStatus::Submitted;
}
VulkanDevice::CompletionStatus VulkanDevice::pollPresentation(const PresentationReceipt& receipt,std::string& cause) noexcept {
    cause.clear();
#ifdef CS_HAS_VULKAN
    if(receipt.owner_.lock().get()!=this || !receipt.token_){cause="Invalid Vulkan presentation receipt.";return CompletionStatus::Failed;}
    const auto token=std::static_pointer_cast<Impl::Completion>(receipt.token_);
    if(token->done.load())return CompletionStatus::Complete;
    try {
        std::unique_lock lock(impl_->mutex,std::try_to_lock);
        if(!lock.owns_lock())return CompletionStatus::Pending;
        // Do not reap unrelated work to decide whether this frame completed.
        if(token->done)return CompletionStatus::Complete;
        if(impl_->lost)throw Failure("The Vulkan device was lost.");
        for(auto& context:impl_->contexts)if(context->completion==token && context->pending){
            const auto status=vkGetFenceStatus(impl_->device,context->fence);
            if(status==VK_NOT_READY)return CompletionStatus::Pending;
            check(status,"Poll Vulkan presentation completion.");
            context->pending=false;token->done=true;--impl_->counters.pending;
            context->resources.clear();context->images.clear();return CompletionStatus::Complete;
        }
        throw Failure("The Vulkan presentation completion is unavailable.");
    }catch(const Failure& error){
        impl_->lost=true;cause=error.what();
        if(error.code)cause+=" (Vulkan error "+std::to_string(error.code)+")";
        return CompletionStatus::Failed;
    }catch(const std::exception& error){impl_->lost=true;cause=error.what();return CompletionStatus::Failed;}
#else
    Q_UNUSED(receipt);cause="Vulkan was not compiled into this build.";return CompletionStatus::Failed;
#endif
}

}

#include "vulkan_native_export.inc"
#include "vulkan_native_import.inc"
