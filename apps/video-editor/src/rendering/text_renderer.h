#pragma once

#include "../media/video_frame.h"
#include "../timeline/timeline_model.h"

#include <optional>

namespace rendering {

[[nodiscard]] std::optional<media::VideoFrame> renderText(
    const timeline::TextStyle& text_style,
    int canvas_width = 1920,
    int canvas_height = 1080);

} // namespace rendering
