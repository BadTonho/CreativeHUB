#pragma once

#include "../model/color_graph.h"
#include "media/video_frame.h"

#include <functional>
#include <optional>

namespace color {

[[nodiscard]] std::optional<media::VideoFrame> evaluate(
    const Graph& graph, const media::VideoFrame& input,
    const std::function<bool()>& should_cancel = {});

}  // namespace color
