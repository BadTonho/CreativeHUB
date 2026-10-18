#include "layer_scale_drag.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace motion::model {
bool validDragScale(double v) noexcept
{
    return std::isfinite(v) && v >= minimum_drag_scale && v <= maximum_drag_scale;
}
std::array<std::array<double,2>,3> scaleDragDirections(const AffineTransform& m)
{
    if (!creative_suite::composition::validAffine(m) || m.tx!=0 || m.ty!=0)
        throw std::invalid_argument("Scale controls require finite invertible linear geometry.");
    std::array<std::array<double,2>,3> result{{{m.a,m.b},{m.c,m.d},{m.a+m.c,m.b+m.d}}};
    if(!std::isfinite(result[2][0])||!std::isfinite(result[2][1]))
        result[2]={std::midpoint(m.a,m.c),std::midpoint(m.b,m.d)};
    for(auto& v:result) {
        const auto size=std::max(std::abs(v[0]),std::abs(v[1]));
        if(!std::isfinite(size)||size==0)throw std::runtime_error("Scale control projection exceeds finite geometry limits.");
        v[0]/=size;v[1]/=size;const auto length=std::hypot(v[0],v[1]);
        v[0]/=length;v[1]/=length;
    }
    return result;
}
ScaleDragCalculation::ScaleDragCalculation(ScaleDragControl control,bool linked,
    std::array<double,2> initial,AffineTransform axes,std::array<double,2> lever)
    : control_(control),linked_(linked),initial_(initial),lever_(lever),local_lever_{},axes_(axes),
      inverse_(creative_suite::composition::requireInverseAffine(axes))
{
    if((control!=ScaleDragControl::X&&control!=ScaleDragControl::Y&&control!=ScaleDragControl::Diagonal)||
        !validDragScale(initial[0])||!validDragScale(initial[1])||axes.tx!=0||axes.ty!=0||
        !std::isfinite(lever[0])||!std::isfinite(lever[1])||!std::isfinite(std::hypot(lever[0],lever[1]))||
        std::hypot(lever[0],lever[1])==0)
        throw std::invalid_argument("Scale drag requires valid factors and control geometry.");
    if(control==ScaleDragControl::Diagonal&&!linked) {
        local_lever_=inverse_.map(lever[0],lever[1]);
        if(!std::isfinite(local_lever_[0])||!std::isfinite(local_lever_[1])||
            local_lever_[0]==0||local_lever_[1]==0)
            throw std::runtime_error("Scale drag reference exceeds finite geometry limits.");
    }
}
std::array<double,2> ScaleDragCalculation::evaluate(double dx,double dy) const
{
    if(!std::isfinite(dx)||!std::isfinite(dy))throw std::invalid_argument("Non-finite scale pointer.");
    if(dx==0&&dy==0)return initial_;
    auto result=initial_;
    const auto bounded=[](double f,double initial) {
        if(!std::isfinite(f))throw std::runtime_error("Scale factor exceeds finite limits.");
        f=std::clamp(f,minimum_drag_scale/initial,maximum_drag_scale/initial);
        return std::clamp(initial*f,minimum_drag_scale,maximum_drag_scale);
    };
    if(control_==ScaleDragControl::Diagonal&&!linked_) {
        const auto q=inverse_.map(dx,dy);
        result[0]=bounded(1+q[0]/local_lever_[0],initial_[0]);
        result[1]=bounded(1+q[1]/local_lever_[1],initial_[1]);
    } else {
        const auto length=std::hypot(lever_[0],lever_[1]);
        const auto px=(dx/length)*(lever_[0]/length),py=(dy/length)*(lever_[1]/length);
        auto projection=px+py;
        if(!std::isfinite(px)||!std::isfinite(py)||!std::isfinite(projection))
            throw std::runtime_error("Scale projection exceeds finite limits.");
        // Remove cancellation roundoff in transverse motion, without rounding
        // ordinary small projected displacements or the resulting scale values.
        if(std::abs(projection)<=8*std::numeric_limits<double>::epsilon()*(std::abs(px)+std::abs(py)))projection=0;
        auto factor=1+projection;
        if(!std::isfinite(factor))throw std::runtime_error("Scale projection exceeds finite limits.");
        if(linked_) {
            factor=std::clamp(factor,std::max(minimum_drag_scale/initial_[0],minimum_drag_scale/initial_[1]),
                std::min(maximum_drag_scale/initial_[0],maximum_drag_scale/initial_[1]));
            // Move a rounded boundary inward as a common factor, retaining the
            // proportion instead of independently clipping the two products.
            for(int attempt=0;attempt<8;++attempt) {
                result={initial_[0]*factor,initial_[1]*factor};
                if(validDragScale(result[0])&&validDragScale(result[1]))return result;
                factor=std::nextafter(factor,1.);
            }
            throw std::runtime_error("Linked scale boundary has no finite valid factors.");
        }
        const auto axis=control_==ScaleDragControl::X?0:1;
        result[axis]=bounded(factor,initial_[axis]);
    }
    return result;
}
std::array<double,2> ScaleDragCalculation::indicator(std::array<double,2> values) const
{
    if(!validDragScale(values[0])||!validDragScale(values[1]))throw std::invalid_argument("Invalid scale indicator values.");
    const auto fx=values[0]/initial_[0],fy=values[1]/initial_[1];
    auto result=control_==ScaleDragControl::Diagonal&&!linked_?axes_.map(local_lever_[0]*fx,local_lever_[1]*fy):
        std::array{lever_[0]*(control_==ScaleDragControl::Y?fy:fx),lever_[1]*(control_==ScaleDragControl::Y?fy:fx)};
    if(!std::isfinite(result[0])||!std::isfinite(result[1]))throw std::runtime_error("Scale indicator exceeds finite geometry limits.");
    return result;
}

bool CompositionDocument::setLayerScaleAtFrame(LayerId id,double x,double y,std::int64_t frame)
{
    auto* layer=findLayer(id);
    if(!layer||!validDragScale(x)||!validDragScale(y)||frame<layer->timeline_start_frame||
        frame-layer->timeline_start_frame>=layer->duration_frames)return false;
    namespace a=creative_suite::animation;
    const auto local=frame-layer->timeline_start_frame;
    const auto old=evaluateLayerScale(*layer,local);
    if(old==std::array{x,y})return false;
    auto candidate=*layer;
    if(!candidate.scale_y&&x!=y)candidate.scale_y=ScaleChannel{candidate.transform.scale,candidate.keyframes.scale};
    const auto set=[local](auto& keys,double& base,double before,double value) {
        if(before==value)return;
        if(keys.empty())base=value;
        else if(!a::setKeyframe(keys,local,value))throw std::runtime_error("Scale key insertion failed.");
    };
    set(candidate.keyframes.scale,candidate.transform.scale,old[0],x);
    if(candidate.scale_y)set(candidate.scale_y->keyframes,candidate.scale_y->value,old[1],y);
    const auto old_count=layer->keyframes.scale.size()+(layer->scale_y?layer->scale_y->keyframes.size():0);
    const auto new_count=candidate.keyframes.scale.size()+(candidate.scale_y?candidate.scale_y->keyframes.size():0);
    if(!canInsertKeyframes(new_count-old_count))return false;
    if(!a::validTransform(candidate.transform)||!a::validTransformKeyframes(candidate.keyframes)||
        (candidate.scale_y&&!validScaleChannel(*candidate.scale_y)))return false;
    (void)localLayerGeometry(candidate,canvas_size_,local);
    (void)LayerTransformHierarchy(canvas_size_,layers_).evaluate(frame,{},{},LayerScaleOverride{id,x,y});
    *layer=std::move(candidate);return true;
}
} // namespace motion::model
