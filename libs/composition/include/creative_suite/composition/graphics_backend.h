#pragma once

#include <string_view>

namespace creative_suite::composition {
// Application preference; Automatic deliberately retains the established OpenGL policy.
enum class GraphicsBackend { Automatic, OpenGl, Vulkan, Cpu };
[[nodiscard]] constexpr std::string_view backendName(GraphicsBackend backend) noexcept {
    switch (backend) {
    case GraphicsBackend::Automatic: return "auto";
    case GraphicsBackend::OpenGl: return "opengl";
    case GraphicsBackend::Vulkan: return "vulkan";
    case GraphicsBackend::Cpu: return "cpu";
    }
    return "auto";
}
// Set once before constructing application workers. Other applications retain OpenGL.
void setSessionGraphicsBackend(GraphicsBackend) noexcept;
[[nodiscard]] GraphicsBackend sessionGraphicsBackend() noexcept;
}
