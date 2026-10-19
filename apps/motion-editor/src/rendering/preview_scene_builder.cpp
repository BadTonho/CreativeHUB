#include "preview_scene_builder.h"
#include <algorithm>
namespace motion::ui {
void preparePreviewLayers(PreviewRequest& request,const std::vector<model::CompositionLayer>& layers,
    const std::function<creative_suite::media::RgbaFramePtr(const model::CompositionLayer&)>& still,
    std::shared_ptr<const model::TrackMatteGraph> graph) {
    if(request.scene)throw std::invalid_argument("Cannot rebuild a frozen preview scene.");
    if(!graph)graph=std::make_shared<const model::TrackMatteGraph>(layers);
    const auto plan=graph->plan(layers,request.composition_frame,request.mask_view_layer);
    request.track_mattes=graph->empty()?nullptr:std::move(graph);
    request.layers.clear();request.layers.reserve(std::count_if(plan.begin(),plan.end(),[](const auto& p){return p.needed;}));
    for(std::size_t i=0;i<layers.size();++i) {
        if(!plan[i].needed)continue;
        const auto& layer=layers[i];PreviewLayerSnapshot active;
        active.id=layer.id;active.kind=layer.kind;active.source_path=layer.source_path;
        active.local_frame=request.composition_frame-layer.timeline_start_frame;
        active.source_frame_count=layer.source_frame_count;active.source_start_frame=layer.source_start_frame;
        active.source_frame_rate=layer.source_frame_rate;active.presentation=layer.presentation;
        active.source_mask=layer.source_mask;active.transform_mask=layer.transform_mask;
        active.transform=layer.transform;active.keyframes=layer.keyframes;
        active.anchor_point=layer.anchor_point;active.anchor_keyframes=layer.anchor_keyframes;
        active.scale_y=layer.scale_y;active.blend_mode=layer.blend_mode;
        active.content=layer.content;active.effects=layer.effects;
        active.track_matte=layer.track_matte;active.composite=plan[i].composite;
        if(layer.kind==model::LayerKind::Image && still)active.still_frame=still(layer);
        request.layers.push_back(std::move(active));
    }
}
}
