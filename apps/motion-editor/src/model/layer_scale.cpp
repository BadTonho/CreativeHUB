#include "composition_document.h"
#include "layer_transform_hierarchy.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace motion::model {
namespace a=creative_suite::animation;
namespace {
bool axisValid(ScaleAxis axis){return axis==ScaleAxis::X || axis==ScaleAxis::Y;}
bool valueValid(double value){return std::isfinite(value)&&value>0;}
void materializeY(CompositionLayer& l){if(!l.scale_y)l.scale_y=ScaleChannel{l.transform.scale,l.keyframes.scale};}
a::ScalarKeyframes& keysFor(CompositionLayer& l,ScaleAxis axis){return axis==ScaleAxis::X?l.keyframes.scale:l.scale_y->keyframes;}
double& baseFor(CompositionLayer& l,ScaleAxis axis){return axis==ScaleAxis::X?l.transform.scale:l.scale_y->value;}
void validateGeometry(const CompositionLayer& l,CanvasSize canvas) {
    try {
        (void)localLayerGeometry(l,canvas,0);
        for(const auto axis:{ScaleAxis::X,ScaleAxis::Y})for(const auto& key:layerScaleKeys(l,axis))
            (void)localLayerGeometry(l,canvas,key.frame);
    }catch(const std::exception& e){throw std::runtime_error("Scale geometry for layer "+std::to_string(l.id)+": "+e.what());}
}
}
bool validScaleChannel(const ScaleChannel& v) noexcept {
    return valueValid(v.value)&&a::validScalarKeyframes(v.keyframes)&&
        std::all_of(v.keyframes.begin(),v.keyframes.end(),[](const auto& k){return valueValid(k.value);});
}
bool validParentAffine(const creative_suite::composition::AffineTransform2D& m) noexcept {
    const auto size=std::max({std::abs(m.a),std::abs(m.b),std::abs(m.c),std::abs(m.d)});
    return creative_suite::composition::validAffine(m)&&size>0&&(m.a/size)*(m.d/size)-(m.b/size)*(m.c/size)>0;
}
double layerScaleBase(const CompositionLayer& l,ScaleAxis axis) noexcept {
    return axis==ScaleAxis::Y && l.scale_y?l.scale_y->value:l.transform.scale;
}
const a::ScalarKeyframes& layerScaleKeys(const CompositionLayer& l,ScaleAxis axis) noexcept {
    return axis==ScaleAxis::Y && l.scale_y?l.scale_y->keyframes:l.keyframes.scale;
}
std::array<double,2> evaluateLayerScale(const CompositionLayer& l,std::int64_t frame) noexcept {
    return {a::evaluateScalar(l.transform.scale,l.keyframes.scale,frame),
        a::evaluateScalar(layerScaleBase(l,ScaleAxis::Y),layerScaleKeys(l,ScaleAxis::Y),frame)};
}
bool CompositionDocument::setLayerScaleLinked(LayerId id,bool linked) {
    auto* l=findLayer(id);if(!l || l->scale_linked==linked)return false;l->scale_linked=linked;return true;
}
bool CompositionDocument::setLayerScaleAxisBase(LayerId id,ScaleAxis axis,double value) {
    auto* l=findLayer(id);if(!l || !axisValid(axis)||!valueValid(value)||layerScaleBase(*l,axis)==value)return false;
    auto candidate=*l;materializeY(candidate);baseFor(candidate,axis)=value;validateGeometry(candidate,canvas_size_);
    *l=std::move(candidate);return true;
}
bool CompositionDocument::replaceLayerScaleAxisKeyframes(LayerId id,ScaleAxis axis,a::ScalarKeyframes keys) {
    auto* l=findLayer(id);if(!l || !axisValid(axis)||(!a::validScalarKeyframes(keys)||!std::all_of(keys.begin(),keys.end(),[](const auto& k){return valueValid(k.value);}))||layerScaleKeys(*l,axis)==keys)return false;
    auto candidate=*l;materializeY(candidate);keysFor(candidate,axis)=std::move(keys);validateGeometry(candidate,canvas_size_);
    *l=std::move(candidate);return true;
}
bool CompositionDocument::setLayerScaleAxisKeyframe(LayerId id,ScaleAxis axis,std::int64_t frame,double value) {
    const auto* l=findLayer(id);if(!l||!axisValid(axis)||frame<0||!valueValid(value))return false;
    auto keys=layerScaleKeys(*l,axis);if(!a::setKeyframe(keys,frame,value))return false;
    return replaceLayerScaleAxisKeyframes(id,axis,std::move(keys));
}
bool CompositionDocument::removeLayerScaleAxisKeyframe(LayerId id,ScaleAxis axis,std::int64_t frame) {
    const auto* l=findLayer(id);if(!l||!axisValid(axis))return false;
    auto keys=layerScaleKeys(*l,axis);if(!a::removeKeyframe(keys,frame))return false;
    return replaceLayerScaleAxisKeyframes(id,axis,std::move(keys));
}
bool CompositionDocument::moveLayerScaleAxisKeyframe(LayerId id,ScaleAxis axis,std::int64_t from,std::int64_t to) {
    const auto* l=findLayer(id);if(!l||!axisValid(axis)||from<0||from>=l->duration_frames||to<0||to>=l->duration_frames||from==to)return false;
    auto keys=layerScaleKeys(*l,axis);
    auto found=std::find_if(keys.begin(),keys.end(),[from](const auto& k){return k.frame==from;});
    if(found==keys.end()||std::any_of(keys.begin(),keys.end(),[to](const auto& k){return k.frame==to;}))return false;
    found->frame=to;std::sort(keys.begin(),keys.end(),[](const auto& l,const auto& r){return l.frame<r.frame;});
    return replaceLayerScaleAxisKeyframes(id,axis,std::move(keys));
}
bool CompositionDocument::setLayerScaleAxisKeyframeInterpolation(LayerId id,ScaleAxis axis,std::int64_t frame,
    a::InterpolationMode mode,const a::CubicBezierEasing& easing) {
    const auto* l=findLayer(id);if(!l||!axisValid(axis)||!a::validKeyframeInterpolation(mode,easing))return false;
    auto keys=layerScaleKeys(*l,axis);auto found=std::find_if(keys.begin(),keys.end(),[frame](const auto& k){return k.frame==frame;});
    if(found==keys.end()||(found->interpolation==mode&&found->easing==easing))return false;
    found->interpolation=mode;found->easing=easing;return replaceLayerScaleAxisKeyframes(id,axis,std::move(keys));
}
bool CompositionDocument::editLayerScaleAxis(LayerId id,ScaleAxis axis,double value,std::int64_t frame,std::optional<double> ratio) {
    auto* l=findLayer(id);if(!l||!axisValid(axis)||!valueValid(value)||frame<0)return false;
    const auto local=frame<l->timeline_start_frame?0:frame-l->timeline_start_frame;
    const auto old=evaluateLayerScale(*l,local);auto values=old;values[static_cast<std::size_t>(axis)]=value;
    if(l->scale_linked) {
        const auto proportion=ratio.value_or(old[1]/old[0]);if(!valueValid(proportion))return false;
        if(axis==ScaleAxis::X)values[1]=value*proportion;else values[0]=value/proportion;
    }
    auto candidate=*l;materializeY(candidate);bool changed=false;
    for(const auto target:{ScaleAxis::X,ScaleAxis::Y}) {
        if(!l->scale_linked&&target!=axis)continue;
        const auto index=static_cast<std::size_t>(target);if(!valueValid(values[index]))return false;
        auto& keys=keysFor(candidate,target);
        if(!keys.empty() && (frame<l->timeline_start_frame || local>=l->duration_frames ||
            std::none_of(keys.begin(),keys.end(),[local](const auto& k){return k.frame==local;})))return false;
        if(values[index]==old[index])continue;
        if(keys.empty())baseFor(candidate,target)=values[index];else (void)a::setKeyframe(keys,local,values[index]);
        changed=true;
    }
    if(!changed)return false;
    if(l->scale_linked && !l->scale_y)candidate.scale_y.reset();
    validateGeometry(candidate,canvas_size_);
    auto staged=layers_;*std::find_if(staged.begin(),staged.end(),[id](const auto& v){return v.id==id;})=candidate;
    (void)LayerTransformHierarchy(canvas_size_,staged).evaluate(frame);
    *l=std::move(candidate);return true;
}
} // namespace motion::model
