#include "track_matte_graph.h"
#include <stdexcept>
#include <algorithm>
namespace motion::model {
TrackMatteGraph::TrackMatteGraph(const std::vector<CompositionLayer>& layers) {
    nodes_.reserve(layers.size());index_.reserve(layers.size());order_.reserve(layers.size());
    for(const auto& layer:layers) {
        if(!layer.id || !index_.emplace(layer.id,nodes_.size()).second)
            throw std::invalid_argument("Duplicate or zero track matte layer identifier.");
        nodes_.push_back({layer.id,layer.kind,layer.track_matte});
    }
    std::vector<unsigned> dependencies(nodes_.size());
    for(std::size_t i=0;i<nodes_.size();++i) {
        const auto& node=nodes_[i];
        if(!node.matte) {order_.push_back(node.id);continue;}
        const auto source=index_.find(node.matte->source_id);
        if(node.kind==LayerKind::Null || source==index_.end() || source->second==i ||
            nodes_[source->second].kind==LayerKind::Null ||
            !creative_suite::composition::validTrackMatteMode(node.matte->mode))
            throw std::invalid_argument("Invalid track matte relation on layer "+std::to_string(node.id));
        dependencies[i]=1;consumers_[node.matte->source_id].push_back(node.id);
        participants_.insert(node.id);participants_.insert(node.matte->source_id);
    }
    for(std::size_t i=0;i<order_.size();++i) {
        const auto found=consumers_.find(order_[i]);if(found==consumers_.end())continue;
        for(auto id:found->second)if(--dependencies[index_.at(id)]==0)order_.push_back(id);
    }
    if(order_.size()!=nodes_.size())throw std::invalid_argument("Cyclic track matte relations.");
}
std::vector<LayerId> TrackMatteGraph::eligibleSources(LayerId target) const {
    const auto node=index_.find(target);
    if(node==index_.end() || nodes_[node->second].kind==LayerKind::Null)return {};
    std::unordered_set<LayerId> excluded{target};std::vector<LayerId> pending{target};
    for(std::size_t i=0;i<pending.size();++i) {
        const auto found=consumers_.find(pending[i]);if(found==consumers_.end())continue;
        for(auto id:found->second)if(excluded.insert(id).second)pending.push_back(id);
    }
    std::vector<LayerId> result;
    for(const auto& n:nodes_)if(n.kind!=LayerKind::Null && !excluded.contains(n.id))result.push_back(n.id);
    return result;
}
std::vector<TrackMatteGraph::Participation> TrackMatteGraph::plan(
    const std::vector<CompositionLayer>& layers,std::int64_t frame,LayerId mask_view) const {
    if(frame<0 || layers.size()!=nodes_.size())throw std::invalid_argument("Invalid track matte scene context.");
    std::vector<Participation> result(layers.size());std::vector<std::size_t> pending;
    const auto active=[&](std::size_t i){const auto& l=layers[i];return l.kind!=LayerKind::Null && l.duration_frames>0 && frame>=l.timeline_start_frame && frame-l.timeline_start_frame<l.duration_frames;};
    for(std::size_t i=0;i<layers.size();++i) {
        if(layers[i].id!=nodes_[i].id || layers[i].track_matte!=nodes_[i].matte || layers[i].kind!=nodes_[i].kind)
            throw std::invalid_argument("Stale track matte scene snapshot.");
        if((layers[i].visible && active(i)) || (layers[i].id==mask_view && layers[i].kind!=LayerKind::Null)) {
            result[i]={true,true};pending.push_back(i);
        }
    }
    if(mask_view)return result; // Isolated shape-mask View does not consume mattes.
    for(std::size_t i=0;i<pending.size();++i) {
        const auto& n=nodes_[pending[i]];if(!n.matte)continue;
        const auto source=index_.at(n.matte->source_id);
        if(active(source) && !result[source].needed){result[source].needed=true;pending.push_back(source);}
    }
    return result;
}
std::vector<LayerId> CompositionDocument::eligibleTrackMatteSources(LayerId target) const {
    return TrackMatteGraph(layers_).eligibleSources(target);
}
bool CompositionDocument::setLayerTrackMatte(LayerId id,std::optional<TrackMatteBinding> matte) {
    auto* target=findLayer(id);
    if(!target || target->kind==LayerKind::Null || target->track_matte==matte)return false;
    CompositionLayer* source=nullptr;
    if(matte) {
        if(!creative_suite::composition::validTrackMatteMode(matte->mode))return false;
        const auto eligible=eligibleTrackMatteSources(id);
        if(std::find(eligible.begin(),eligible.end(),matte->source_id)==eligible.end())return false;
        source=findLayer(matte->source_id);
    }
    const bool new_source=matte && (!target->track_matte || target->track_matte->source_id!=matte->source_id);
    target->track_matte=matte;
    if(new_source)source->visible=false;
    return true;
}
}
