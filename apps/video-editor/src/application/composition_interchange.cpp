#include "composition_interchange.h"
#include "timeline/timeline_curve_slice.h"
#include <algorithm>
#include <set>
#include <sstream>
#include <map>
namespace application {
namespace x = creative_suite::composition_exchange;
using namespace fusion::nodes;
namespace {
std::string mediaName(const std::filesystem::path &path) {
    const auto name = path.filename().u8string();
    return std::string(name.begin(), name.end());
}
const Node *input(const NodeGraph &g, NodeId id, int port) {
    for (auto e : g.connections)
        if (e.to == id && e.input == port)
            return findNode(g, e.from);
    return nullptr;
}
void shift(timeline::TransformKeyframes &keys, std::int64_t delta) {
    for (auto *list :
         {&keys.position_x, &keys.position_y, &keys.scale, &keys.rotation, &keys.opacity})
        for (auto &k : *list)
            k.frame += delta;
}
} // namespace
x::Document exportComposition(const timeline::TimelineClip &clip, int w, int h,
                              timeline::FrameRate fps) {
    if (clip.kind != timeline::ClipKind::FusionComposition || !clip.node_graph)
        throw x::Unsupported("Select a Fusion Composition to export.");
    if (clip.transform != timeline::Transform2D{} || !clip.effects.empty() ||
        clip.keyframes != timeline::TransformKeyframes{})
        throw x::Unsupported("Composition clip: Edit Inspector effects and transforms require "
                             "composition-wide processing and cannot be transferred yet.");
    constexpr auto limit = std::int64_t(1) << 50;
    if (clip.timeline_duration_frames < 1 || clip.timeline_duration_frames >= limit ||
        clip.composition_input_offset_frames <= -limit ||
        clip.composition_input_offset_frames >= limit)
        throw x::Unsupported(
            "Composition duration or source offset exceeds the interchange time range.");
    const auto &g = *clip.node_graph;
    if (!validate(g))
        throw x::Error("The Fusion graph is invalid.");
    std::vector<x::Issue> issues;
    std::set<NodeId> used;
    for (const auto &n : g.nodes) {
        if ((n.type==NodeType::Merge || n.type==NodeType::Output) && input(g,n.id,2))
            issues.push_back({n.id,"Masks on Merge or Output are not supported by interchange."});
        if (!n.effect_parameter_keyframes.empty())
            issues.push_back({n.id, "Animated effects are not supported."});
        unsigned count = 0;
        for (auto e : g.connections)
            if (e.from == n.id)
                ++count;
        if (count > 1 && !isMaskNode(n.type))
            issues.push_back({n.id, "Shared branches are not supported."});
    }
    x::Document d;
    d.name = clip.display_name;
    d.width = w;
    d.height = h;
    d.fps_numerator = fps.numerator;
    d.fps_denominator = fps.denominator;
    d.duration = clip.timeline_duration_frames;
    const auto maskFor = [&](const Node& receiver) -> x::MaskId {
        const auto* mask=input(g,receiver.id,2);
        if(!mask)return 0;
        if(!isMaskNode(mask->type)) {issues.push_back({receiver.id,"Invalid mask input."});return 0;}
        if(used.insert(mask->id).second) {
            x::Mask definition{mask->id, mask->type==NodeType::RectangleMask ? creative_suite::composition::ShapeMaskKind::Rectangle : creative_suite::composition::ShapeMaskKind::Ellipse,mask->mask,mask->mask_keyframes};
            d.masks.push_back(std::move(definition));
        }
        return mask->id;
    };
    const auto branch = [&](const Node *end) {
        if (!end)
            return;
        x::Layer layer;
        const Node *n = end;
        bool transformed = false;
        if (n->type == NodeType::Transform) {
            transformed = true;
            layer.transform_mask=maskFor(*n);
            layer.transform = n->transform;
            layer.curves = n->transform_keyframes;
            used.insert(n->id);
            n = input(g, n->id, 0);
        }
        std::vector<std::vector<x::Effect>> stages;
        while (n && (n->type==NodeType::Color || n->type==NodeType::Effect || n->type==NodeType::GaussianBlur)) {
            used.insert(n->id);
            const auto mask=maskFor(*n);
            std::vector<x::Effect> group;
            if(n->type==NodeType::GaussianBlur)
                group.emplace_back(x::BlurEffect{n->gaussian_blur.radius_pixels,n->gaussian_blur.enabled,mask});
            else if(n->type==NodeType::Color)
                group.emplace_back(x::ColorEffect{{n->color.brightness,n->color.contrast_percent,n->color.saturation_percent},n->color.enabled,mask});
            else {
                auto effect=n->effect;effect.enabled=true;
                const auto passes=creative_suite::effects::colorAdjustmentPasses(std::span(&effect,1));
                if(!passes)issues.push_back({n->id,"Unsupported effect."});
                else {
                    if(mask && passes->size()>1)issues.push_back({n->id,"A masked multi-pass effect cannot be split into independent stages."});
                    for(const auto& pass:*passes)group.emplace_back(x::ColorEffect{pass,n->effect.enabled,mask});
                }
            }
            stages.push_back(std::move(group));
            n=input(g,n->id,0);
        }
        if (!n || (n->type != NodeType::Input && n->type != NodeType::Background &&
                   n->type != NodeType::Text)) {
            issues.push_back({n ? n->id : end->id, "Expected a source, ordered static effects "
                                                   "and at most one Transform before Merge."});
            return;
        }
        used.insert(n->id);
        layer.source_mask=maskFor(*n);
        layer.id = n->id;
        layer.presentation = n->presentation.value_or(x::Presentation{});
        if (layer.presentation.name.empty())
            layer.presentation.name = n->type == NodeType::Input  ? mediaName(n->source_path)
                                      : n->type == NodeType::Text ? "Text"
                                                                  : "Background";
        if (n->presentation)
            layer.presentation.start -= clip.composition_input_offset_frames;
        else
            layer.presentation.offset = clip.composition_input_offset_frames;
        if (layer.presentation.duration == 0)
            layer.presentation.duration = d.duration;
        const auto old_start = layer.presentation.start;
        const auto start = std::max<std::int64_t>(0, old_start);
        const auto end_frame = std::min(d.duration, old_start + layer.presentation.duration);
        if (end_frame <= start) {
            issues.push_back({n->id, "A source lies entirely outside the composition."});
            return;
        }
        layer.presentation.offset += start - old_start;
        layer.presentation.start = start;
        layer.presentation.duration = end_frame - start;
        if (transformed) {
            timeline::Transform2D base;
            layer.curves = timeline::sliceTransformCurves(layer.transform, layer.curves, start,
                                                          layer.presentation.duration, base);
            layer.transform = base;
        }
        layer.source = n->source_path;
        layer.source_rate = n->source_frame_rate;
        layer.source_count = n->source_frame_count;
        layer.kind = n->type == NodeType::Input
                         ? (n->source_is_still ? x::Kind::Image : x::Kind::Video)
                     : n->type == NodeType::Text ? x::Kind::Text
                                                 : x::Kind::Background;
        layer.color = n->background_color;
        layer.text = {n->text.content, n->text.font_family, n->text.font_size_pixels,
                      int(n->text.alignment), n->text.color};
        for(auto group=stages.rbegin();group!=stages.rend();++group)
            layer.effects.insert(layer.effects.end(),group->begin(),group->end());
        d.layers.push_back(std::move(layer));
    };
    const auto out = std::find_if(g.nodes.begin(), g.nodes.end(),
                                  [](const auto &n) { return n.type == NodeType::Output; });
    used.insert(out->id);
    std::vector<const Node*> sources;
    const Node* root=input(g,out->id,0);
    while(root && root->type==NodeType::Merge) {
        used.insert(root->id);
        const auto* foreground=input(g,root->id,1);
        if(foreground && foreground->type==NodeType::Merge)
            issues.push_back({foreground->id,"Nested foreground compositions are not supported."});
        else sources.push_back(foreground);
        root=input(g,root->id,0);
    }
    sources.push_back(root);
    for(auto source=sources.rbegin();source!=sources.rend();++source)branch(*source);
    for (const auto &n : g.nodes)
        if (!used.contains(n.id))
            issues.push_back({n.id, "Disconnected or unsupported node."});
    auto invalid = x::validate(d);
    issues.insert(issues.end(), invalid.begin(), invalid.end());
    if (!issues.empty()) {
        std::ostringstream message;
        for (auto i : issues)
            message << "Node " << i.id << ": " << i.reason << '\n';
        throw x::Unsupported(message.str());
    }
    return d;
}
NodeGraph importComposition(const x::Document &d) {
    auto issues = x::validate(d);
    if (!issues.empty())
        throw x::Error(issues.front().reason);
    NodeGraph g;
    NodeId background = 0;
    double row = 0;
    std::vector<std::pair<x::MaskId,NodeId>> bindings;
    std::size_t column = 0;
    const auto add = [&](Node n) -> NodeId {
        n.id = g.next_id++;
        n.x = double(column++) * 180;
        n.y = row;
        g.nodes.push_back(n);
        return n.id;
    };
    for (const auto &layer : d.layers) {
        column = 0;
        Node source;
        source.type = layer.kind == x::Kind::Text         ? NodeType::Text
                      : layer.kind == x::Kind::Background ? NodeType::Background
                                                          : NodeType::Input;
        source.source_path = layer.source;
        source.source_frame_rate = layer.source_rate > 0 ? layer.source_rate : 30;
        source.source_frame_count = layer.source_count;
        source.source_is_still = layer.kind == x::Kind::Image;
        source.background_color = layer.color;
        source.text = {layer.text.content, layer.text.font, layer.text.size, layer.text.color,
                       static_cast<timeline::TextAlignment>(layer.text.alignment)};
        source.presentation = layer.presentation;
        NodeId previous = add(source);
        if(layer.source_mask)bindings.emplace_back(layer.source_mask,previous);
        for(const auto& stage:layer.effects) {
            Node effect;
            if(const auto* blur=std::get_if<x::BlurEffect>(&stage)) {
                effect.type=NodeType::GaussianBlur;effect.gaussian_blur={blur->radius_pixels,blur->enabled};
            } else {
                const auto& c=std::get<x::ColorEffect>(stage);
                effect.type=NodeType::Color;effect.color={c.parameters.brightness,c.parameters.contrast_percent,c.parameters.saturation_percent,c.enabled};
            }
            const auto id=add(effect);
            const auto mask=std::visit([](const auto& e){return e.mask;},stage);
            if(mask)bindings.emplace_back(mask,id);
            g.connections.push_back({previous,id,0});previous=id;
        }
        Node transform;
        transform.type = NodeType::Transform;
        transform.transform = layer.transform;
        transform.transform_keyframes = layer.curves;
        shift(transform.transform_keyframes, layer.presentation.start);
        timeline::Transform2D bounded_base;
        transform.transform_keyframes = timeline::sliceTransformCurves(
            transform.transform, transform.transform_keyframes, 0, d.duration, bounded_base);
        transform.transform = bounded_base;
        transform.presentation = layer.presentation;
        auto id = add(transform);
        if(layer.transform_mask)bindings.emplace_back(layer.transform_mask,id);
        g.connections.push_back({previous, id, 0});
        previous = id;
        if (background) {
            Node merge;
            merge.type = NodeType::Merge;
            auto mid = add(merge);
            g.connections.push_back({background, mid, 0});
            g.connections.push_back({previous, mid, 1});
            background = mid;
        } else
            background = previous;
        row += 160;
    }
    Node output;
    output.type = NodeType::Output;
    auto id = add(output);
    if (background)
        g.connections.push_back({background, id, 0});
    row += 160;
    std::map<x::MaskId,NodeId> mapped_masks;
    for(const auto& m:d.masks) {
        Node mask;mask.type=m.kind==creative_suite::composition::ShapeMaskKind::Rectangle?NodeType::RectangleMask:NodeType::EllipseMask;
        mask.mask=m.parameters;mask.mask_keyframes=m.keyframes;
        for(std::size_t i=0;i<5;++i)if(!mask.mask_keyframes[i].empty() && mask.mask_keyframes[i].back().frame>=d.duration) {
            const auto base=maskValue(mask.mask,i);
            mask.mask_keyframes[i]=creative_suite::animation::sliceScalarCurve(mask.mask_keyframes[i],0,d.duration,base);
            setMaskValue(mask.mask,i,creative_suite::animation::evaluateScalar(base,m.keyframes[i],0));
        }
        column=0; mapped_masks.emplace(m.id,add(mask));row+=160;
    }
    for(const auto& [mask,receiver]:bindings)g.connections.push_back({mapped_masks.at(mask),receiver,2});
    if (!validate(g))
        throw x::Error("Converted Fusion graph is invalid.");
    return g;
}
} // namespace application
