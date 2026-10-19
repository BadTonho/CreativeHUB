#pragma once
#include "preview_renderer.h"
namespace motion::ui {
// GUI/export adapters provide immutable decoded stills; this builder owns no
// decoder or document. Graph construction and pixel participation are shared.
void preparePreviewLayers(PreviewRequest&,const std::vector<model::CompositionLayer>&,
    const std::function<creative_suite::media::RgbaFramePtr(const model::CompositionLayer&)>&,
    std::shared_ptr<const model::TrackMatteGraph> graph = {});
}
