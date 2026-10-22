#include "masked_layer_renderer.h"
#include "layer_effect_processor.h"
#include "model/layer_transform_hierarchy.h"
#include <creative_suite/composition/frame_compositor.h>
#include <creative_suite/composition_exchange/raster.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace motion::ui {
namespace c=creative_suite::composition;
namespace x=creative_suite::composition_exchange;
namespace a=creative_suite::animation;
namespace m=creative_suite::media;
bool hasLayerMasks(const PreviewLayerSnapshot& layer) noexcept {
    return layer.source_mask || layer.transform_mask || std::any_of(layer.effects.begin(),layer.effects.end(),[](const auto& e){return std::visit([](const auto& v){return v.mask!=0;},e);});
}
MaskedLayerFrame renderMaskedLayer(const PreviewLayerSnapshot& layer,m::RgbaFramePtr source,
    const PreviewRequest& request,MotionMaskRasterCache& cache,c::OpenGlFrameCompositor* gpu,
    const std::function<bool()>& cancel, const a::Transform2D* resolved_transform,const c::AffineTransform2D* resolved_world,const PathRaster* path_raster,std::uint64_t preparation_budget_bytes,bool record_metrics) {
    c::OpenGlCompositionTimings timings;
    const auto canceled=[&]{return cancel && cancel();};
    const auto coverage=[&](model::MaskId id,int width,int height,bool source_space=true) {
        if(!request.sceneMasks())throw std::runtime_error("Missing mask definitions.");
        const auto it=std::find_if(request.sceneMasks()->begin(),request.sceneMasks()->end(),[id](const auto& m){return m.id==id;});
        if(it==request.sceneMasks()->end())throw std::runtime_error("Unknown mask reference for layer "+std::to_string(layer.id));
        const auto override_path=request.path_override&&request.path_override->target==model::PathTarget{id,true}?request.path_override->geometry.get():nullptr;
        const auto space=path_raster&&source_space?path_raster->space:c::MaskRasterSpace{};
        auto frame=cache.coverage(*it,request.composition_frame,width,height,cancel,space,override_path);
        if(!frame && !canceled())throw std::runtime_error("Mask coverage exceeds its bounded frame budget.");
        return frame;
    };
    const bool intrinsic=layer.kind==model::LayerKind::Text || layer.kind==model::LayerKind::Shape;
    const auto profile=layer.presentation?layer.presentation->profile:x::Profile::MotionLayer;
    auto transform=resolved_transform?*resolved_transform:a::evaluateTransform(layer.transform,layer.keyframes,layer.local_frame);
    std::optional<c::AffineTransform2D> local_world;
    if (!resolved_transform) {
        const model::AnchorPoint anchor{
            a::evaluateScalar(layer.anchor_point.x_pixels,layer.anchor_keyframes[0],layer.local_frame),
            a::evaluateScalar(layer.anchor_point.y_pixels,layer.anchor_keyframes[1],layer.local_frame)};
        const auto sy=layer.scale_y?a::evaluateScalar(layer.scale_y->value,layer.scale_y->keyframes,layer.local_frame):transform.scale;
        if(sy!=transform.scale) {
            model::CompositionLayer local{layer.id,model::LayerKind::Null,""};local.transform=transform;
            local.scale_y=model::ScaleChannel{sy,{}};local.anchor_point=anchor;
            local_world=model::localLayerGeometry(local,request.canvas_size,0);resolved_world=&*local_world;
        } else if (anchor != model::AnchorPoint{}) {
            const auto geometry=model::geometryOffset(transform,request.canvas_size,anchor);
            transform.position_x=geometry.translation_x_pixels/request.canvas_size.width;
            transform.position_y=geometry.translation_y_pixels/request.canvas_size.height;
        }
    }
    if(request.mask_view_layer==layer.id) {
        auto id=request.mask_view_target==-2?layer.source_mask:request.mask_view_target==-1?layer.transform_mask:
            request.mask_view_target>=0 && std::size_t(request.mask_view_target)<layer.effects.size()?std::visit([](const auto& e){return e.mask;},layer.effects[request.mask_view_target]):0;
        if(id)return {coverage(id,request.mask_view_target==-1?request.canvas_size.width:source->width,
            request.mask_view_target==-1?request.canvas_size.height:source->height,request.mask_view_target!=-1),{}};
    }
    const auto evaluated_effects=model::validLayerEffects(layer.effects)
        ? model::evaluateLayerEffects(layer.effects,layer.local_frame)
        : layer.effects;
    if(hasActiveExpandedEffect(layer))return renderExpandedLayer(layer,source,request,cache,gpu,cancel,resolved_world,path_raster,preparation_budget_bytes,record_metrics);
    if(gpu) {
        c::OpenGlImageInput current{source.get()};
        const auto process=[&](c::OpenGlImageOperation operation) {
            if(canceled())return c::OpenGlTextureFramePtr{};
            c::OpenGlCompositionTimings measured;
            auto result=gpu->processImage(operation,cancel,&measured);addMaskTimings(timings,measured);
            if(result.status==c::OpenGlCompositionStatus::Cancelled)return c::OpenGlTextureFramePtr{};
            if(result.status!=c::OpenGlCompositionStatus::Complete || !result.frame)
                throw std::runtime_error(result.operation+": "+(result.cause.empty()?"The bounded image target pool is occupied.":result.cause));
            return result.frame;
        };
        const auto apply=[&](model::MaskId id,c::OpenGlImageInput original,bool blend,bool source_space=true) {
            auto mask=coverage(id,current.width(),current.height(),source_space);if(!mask)return c::OpenGlTextureFramePtr{};
            c::OpenGlImageOperation operation;operation.kind=blend?c::OpenGlImageOperationKind::MaskedBlend:c::OpenGlImageOperationKind::ApplyMask;
            operation.background=blend?original:current;if(blend)operation.foreground=current;operation.mask={mask.get()};return process(std::move(operation));
        };
        if(layer.source_mask){current={nullptr,apply(layer.source_mask,{},false)};if(canceled())return {};}
        for(const auto& effect:evaluated_effects) {
            if(canceled())return {};
            if(!std::visit([](const auto& e){return e.enabled;},effect))continue;
            if(const auto* shadow=std::get_if<model::DropShadowEffect>(&effect);shadow&&shadow->parameters.opacity_percent==0)continue;
            if(const auto* glow=std::get_if<model::GlowEffect>(&effect);glow&&(glow->parameters.opacity_percent==0||std::lround(glow->parameters.radius_pixels)==0))continue;
            auto original=current;
            c::OpenGlImageOperation op;op.background=current;
            std::visit([&](const auto& e){using E=std::decay_t<decltype(e)>;
                if constexpr(std::is_same_v<E,model::ColorAdjustmentEffect>) {op.kind=c::OpenGlImageOperationKind::Color;op.colors={{e.brightness,e.contrast_percent,e.saturation_percent}};}
                else if constexpr(std::is_same_v<E,model::GaussianBlurEffect>) {op.kind=c::OpenGlImageOperationKind::GaussianBlur;op.blur_radius_pixels=e.radius_pixels;}
                else {op.kind=c::OpenGlImageOperationKind::Copy;}
            },effect);
            auto processed=process(std::move(op));current={nullptr,std::move(processed)};
            if(canceled())return {};
            const auto id=std::visit([](const auto& e){return e.mask;},effect);
            if(id)current={nullptr,apply(id,original,true)};
        }
        const auto transformOp=[&](a::Transform2D value,bool use_affine=false) {
            c::OpenGlImageOperation op;op.kind=c::OpenGlImageOperationKind::Transform;op.background=current;
            op.output_width=request.canvas_size.width;op.output_height=request.canvas_size.height;
            if(profile==x::Profile::MotionLayer && !intrinsic)value.scale*=std::min(double(op.output_width)/current.width(),double(op.output_height)/current.height());
            op.transform=value;
            if(use_affine && resolved_world) {
                const auto fit=profile==x::Profile::MotionLayer && !intrinsic?std::min(double(op.output_width)/current.width(),double(op.output_height)/current.height()):1.;
                op.source_to_canvas=path_raster?c::composeAffine(*resolved_world,{1,0,0,1,path_raster->origin_x,path_raster->origin_y}):model::sourcePixelGeometry(*resolved_world,current.width(),current.height(),fit);
            }
            return op;
        };
        c::OpenGlImageInput original;
        if(layer.transform_mask){auto op=transformOp({});if(path_raster)op.source_to_canvas=c::AffineTransform2D{1,0,0,1,request.canvas_size.width*.5+path_raster->origin_x,request.canvas_size.height*.5+path_raster->origin_y};original={nullptr,process(std::move(op))};}
        auto transformed=process(transformOp(transform,true));current={nullptr,std::move(transformed)};
        if(canceled())return {};
        if(layer.transform_mask)current={nullptr,apply(layer.transform_mask,original,true,false)};
        return {{},current.texture,timings};
    }
    auto frame=std::make_shared<m::RgbaFrame>(*source);
    if(layer.source_mask) {auto mask=coverage(layer.source_mask,frame->width,frame->height);if(!mask||!c::applyMask(*frame,*mask,cancel))return {};}
    for(const auto& effect:evaluated_effects) {
        if(canceled())return {};
        if(!std::visit([](const auto& e){return e.enabled;},effect))continue;
        if(const auto* shadow=std::get_if<model::DropShadowEffect>(&effect);shadow&&shadow->parameters.opacity_percent==0)continue;
        if(const auto* glow=std::get_if<model::GlowEffect>(&effect);glow&&(glow->parameters.opacity_percent==0||std::lround(glow->parameters.radius_pixels)==0))continue;
        const auto id=std::visit([](const auto& e){return e.mask;},effect);
        auto original=id?std::make_shared<m::RgbaFrame>(*frame):nullptr;
        if(!applyLayerEffects(*frame,{effect},cancel))return {};
        if(id){auto mask=coverage(id,frame->width,frame->height);if(!mask||!c::maskedBlend(*original,*frame,*mask,cancel))return {};}
    }
    std::optional<m::RgbaFrame> affine_frame;
    if(resolved_world) {
        const auto fit=profile==x::Profile::MotionLayer && !intrinsic?std::min(double(request.canvas_size.width)/frame->width,double(request.canvas_size.height)/frame->height):1.;
        affine_frame=c::transformAffineFrame(*frame,request.canvas_size.width,request.canvas_size.height,
            (path_raster?c::composeAffine(*resolved_world,{1,0,0,1,path_raster->origin_x,path_raster->origin_y}):model::sourcePixelGeometry(*resolved_world,frame->width,frame->height,fit)),transform.opacity,cancel);
        if(!affine_frame)return {};
    }
    auto processed=resolved_world?std::move(*affine_frame):x::transformFrame(*frame,request.canvas_size.width,request.canvas_size.height,transform,profile,intrinsic,cancel);
    if(layer.transform_mask) {
        auto original=x::transformFrame(*frame,request.canvas_size.width,request.canvas_size.height,{},profile,intrinsic,cancel);
        if(path_raster){auto identity=c::transformAffineFrame(*frame,request.canvas_size.width,request.canvas_size.height,{1,0,0,1,request.canvas_size.width*.5+path_raster->origin_x,request.canvas_size.height*.5+path_raster->origin_y},1,cancel);if(!identity)return {};original=std::move(*identity);}
        auto mask=coverage(layer.transform_mask,processed.width,processed.height,false);
        if(!mask||!c::maskedBlend(original,processed,*mask,cancel))return {};
    }
    if(canceled())return {};
    return {std::make_shared<const m::RgbaFrame>(std::move(processed)),{}};
}
}
