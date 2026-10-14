#pragma once
#include "timeline/timeline_model.h"
#include <creative_suite/composition_exchange/document.h>
namespace application {
[[nodiscard]] creative_suite::composition_exchange::Document
exportComposition(const timeline::TimelineClip &, int width, int height, timeline::FrameRate);
[[nodiscard]] fusion::nodes::NodeGraph
importComposition(const creative_suite::composition_exchange::Document &);
} // namespace application
