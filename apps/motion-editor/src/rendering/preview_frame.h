#pragma once

#include <creative_suite/composition/opengl_frame_compositor.h>
#include <creative_suite/media/video_frame.h>
#include <cstdint>

namespace motion::ui {

// Transport only: exactly one representation, with request identity retained
// across the worker mailbox and GUI presentation. No GL work in destructors.
struct PreviewFrame {
    creative_suite::media::RgbaFramePtr rgba;
    creative_suite::composition::OpenGlTextureFramePtr texture;
    std::uint64_t generation = 0;
    std::uint64_t cancellation_generation = 0;
    bool playback = false;
    [[nodiscard]] bool valid() const noexcept {
        return bool(rgba) != bool(texture) && (!texture || texture->valid());
    }
};

} // namespace motion::ui
