#pragma once
#include <memory>
#include "renderer_preferences.h"
namespace creative_suite::composition { class VulkanDevice; }
namespace settings {
struct RendererStartup {
    bool proceed = true;
    std::shared_ptr<creative_suite::composition::VulkanDevice> vulkan;
};
RendererStartup initializeRenderer();
RendererStartup initializeRenderer(const RendererLaunchOptions&);
}
