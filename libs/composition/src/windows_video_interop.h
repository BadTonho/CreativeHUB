#pragma once
#include <creative_suite/media/native_video_frame.h>
#include <creative_suite/composition/opengl_frame_compositor.h>
#include <vector>
class QOpenGLContext;
class QOpenGLFunctions_3_2_Core;
namespace creative_suite::composition::detail {
// All calls/destruction require the owning OpenGL context current on its worker.
class WindowsVideoInterop final {
public:
    WindowsVideoInterop(QOpenGLContext*, QOpenGLFunctions_3_2_Core*);
    ~WindowsVideoInterop();
    OpenGlCompositionResult begin(const media::NativeVideoFramePtr&);
    OpenGlCompositionResult end();
    OpenGlCompositionResult write(unsigned source_texture, const media::NativeVideoFramePtr& destination);
    unsigned texture() const noexcept;
    std::uint64_t reservedBytes() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    // Four decoder devices at most; each conversion bridge is limited to 64 MiB.
    std::vector<std::unique_ptr<Impl>> imports_;
    Impl* active_import_ = nullptr;
};
}
