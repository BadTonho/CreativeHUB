#pragma once

#include "../media/video_frame.h"
#include "../timeline/timeline_model.h"

#include <optional>

namespace rendering {

[[nodiscard]] std::optional<media::VideoFrame> renderText(
    const timeline::TextStyle& text_style);

} // namespace rendering
