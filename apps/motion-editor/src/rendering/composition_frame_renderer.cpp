#include <creative_suite/media/video_probe.h>
#include <creative_suite/composition_exchange/raster.h>
#include "composition_frame_renderer.h"
#include "layer_content_renderer.h"
#include "layer_effect_processor.h"
#include "masked_layer_renderer.h"
#include <unordered_map>
#include "../diagnostics/performance_metrics.h"

#include <creative_suite/composition/frame_compositor.h>
#include <creative_suite/composition/opengl_frame_compositor.h>
#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/animation/animation.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace motion::ui {
namespace {

class StageTimer final {
public:
    StageTimer(bool enabled, diagnostics::PreviewTimingStage stage) noexcept
        : enabled_(enabled), stage_(stage), started_(std::chrono::steady_clock::now()) {}
    ~StageTimer()
    {
        if (!enabled_) return;
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started_).count();
        if (elapsed >= 0) {
            diagnostics::PerformanceMetrics::instance().recordTiming(
                stage_, static_cast<std::uint64_t>(elapsed));
        }
    }

private:
    bool enabled_;
    diagnostics::PreviewTimingStage stage_;
    std::chrono::steady_clock::time_point started_;
};

std::string pathForLog(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

class MotionDecodeObserver final : public creative_suite::media::DecodeObserver {
public:
    [[nodiscard]] bool is_enabled() const noexcept override
    {
        return diagnostics::PerformanceMetrics::instance().enabled();
    }

    void record_timing(
        creative_suite::media::DecodeTimingStage,
        std::uint64_t) noexcept override
    {
        // Motion records aggregate decode, timestamp-seek, and forward-decode
        // timings at the operation boundary rather than duplicating FFmpeg
        // substages in its one-second preview samples.
    }

    void record_discarded_frame() noexcept override
    {
        diagnostics::PerformanceMetrics::instance().recordDiscardedIntermediateFrame();
    }

    void record_timestamp_seek(
        creative_suite::media::DecodeSeekResult result,
        std::uint64_t nanoseconds) noexcept override
    {
        diagnostics::PerformanceMetrics::instance().recordTimestampSeek(
            result == creative_suite::media::DecodeSeekResult::Succeeded,
            nanoseconds);
    }
};

MotionDecodeObserver& motionDecodeObserver() noexcept
{
    static MotionDecodeObserver observer;
    return observer;
}

} // namespace

void CompositionGpuMetrics::record(
    const creative_suite::composition::OpenGlCompositionTimings& timings) noexcept
{
    uploaded_bytes += timings.uploaded_bytes;
    readback_bytes += timings.readback_bytes;
    uploaded_layers += timings.uploaded_layers;
    upload_nanoseconds += timings.upload_nanoseconds;
    draw_submission_nanoseconds += timings.draw_submission_nanoseconds;
    readback_nanoseconds += timings.readback_nanoseconds;
    color_adjustment_count += timings.color_adjustment_count;
    gaussian_blur_count += timings.gaussian_blur_count;
}

void CompositionFrameRenderer::configureTextureDelivery(QOpenGLContext* share_context) {
    texture_share_context_ = share_context;
    texture_budget_ = std::make_shared<creative_suite::composition::OpenGlTexturePoolBudget>();
}
void CompositionFrameRenderer::disableTextureDelivery(bool use_cpu) {
    texture_delivery_disabled_ = true;
    if (use_cpu) gpu_composition_disabled_after_failure_ = true;
}
std::uint64_t CompositionFrameRenderer::texturePoolBytes() const noexcept {
    return texture_budget_ ? texture_budget_->bytes() : 0;
}
unsigned CompositionFrameRenderer::texturePoolOccupancy() const {
    return gpu_compositor_ ? gpu_compositor_->texturePoolOccupancy() : 0;
}
void CompositionFrameRenderer::collectTextureFrames() {
    if (!gpu_compositor_ || !texture_budget_) return;
    const auto result = gpu_compositor_->collectReleasedTextureFrames();
    if (result.status == creative_suite::composition::OpenGlCompositionStatus::Failed &&
        !texture_delivery_disabled_) {
        disableTextureDelivery(true);
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_preview", "collect_texture_frames", result.cause,
            {{"operation", result.operation}, {"error_code", std::to_string(result.error_code)}});
    }
}

void CompositionFrameRenderer::reset()
{
    video_sessions_.clear();
    content_frames_.clear();
}

void CompositionFrameRenderer::shutdown()
{
    reset();
    // OpenGlFrameCompositor must be destroyed on the same worker that created it.
    gpu_compositor_.reset();
}
void CompositionFrameRenderer::recoverAsyncReadbackFailure()
{
    if (gpu_compositor_) gpu_compositor_->discardAsyncReadbacks();
    gpu_composition_disabled_after_failure_ = true;
}
creative_suite::composition::OpenGlReadbackResult
CompositionFrameRenderer::collectAsyncReadback(
    creative_suite::composition::OpenGlReadbackTicket ticket,
    const CancellationPredicate& should_cancel)
{
    if (!gpu_compositor_) return {
        creative_suite::composition::OpenGlCompositionStatus::Failed, {}, {},
        "collect-pbo-ticket", "The export OpenGL compositor is unavailable."};
    auto collected = gpu_compositor_->collectAsyncReadback(ticket, should_cancel);
    if (gpu_metrics_ != nullptr &&
        collected.status == creative_suite::composition::OpenGlCompositionStatus::Complete) {
        ++gpu_metrics_->readback_frames_collected;
        gpu_metrics_->readback_nanoseconds += collected.fence_wait_nanoseconds +
            collected.copy_nanoseconds;
        gpu_metrics_->readback_wait_nanoseconds += collected.fence_wait_nanoseconds;
        gpu_metrics_->readback_copy_nanoseconds += collected.copy_nanoseconds;
        if (collected.fence_wait_nanoseconds > 0) ++gpu_metrics_->readback_fence_waits;
    }
    return collected;
}
creative_suite::media::RgbaFramePtr CompositionFrameRenderer::render(
    const PreviewRequest& request,
    const CancellationPredicate& should_cancel,
    bool fail_on_media_error,
    PreviewRequestMode mode,
    std::optional<creative_suite::composition::OpenGlReadbackTicket>* async_ticket,
    bool* async_failure,
    creative_suite::composition::OpenGlTextureFramePtr* texture,
    bool* texture_busy)
{
    using creative_suite::composition::CompositionLayer;
    using creative_suite::media::VideoPlaybackSession;
    if (texture) texture->reset();
    if (texture_busy) *texture_busy = false;
    if (async_ticket) async_ticket->reset();
    if (async_failure) *async_failure = false;
    if (!request.validSceneStorage() || ((request.position_override || request.rotation_override || request.scale_override) && !request.sceneHierarchy()))
        throw std::invalid_argument("Transform preview requires an exclusive scene and a hierarchy snapshot.");
    if(request.path_override){
        const auto& value=*request.path_override;bool found=false;
        if(value.target.mask){if(request.sceneMasks())for(const auto& mask:*request.sceneMasks())if(mask.id==value.target.id&&mask.path){found=true;break;}}
        else for(const auto& layer:request.sceneLayers())if(layer.id==value.target.id){const auto* shape=std::get_if<model::ShapeLayerContent>(&layer.content);found=shape&&shape->path;break;}
        if(!found||!value.geometry||!model::validPathGeometry(*value.geometry,value.target.mask)){
            creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,"motion_path","validate_path_override","Invalid path preview target or geometry",{{"path_id",std::to_string(value.target.id)},{"mask",value.target.mask?"true":"false"},{"frame",std::to_string(request.composition_frame)}});
            throw std::invalid_argument("Path preview requires an existing path and valid evaluated geometry.");
        }
    }


    if (request.sceneLayers().empty()) {
        content_frames_.clear();
        if (!request.black_canvas_when_empty) return {};
    }
    if (request.frame_rate.numerator <= 0 || request.frame_rate.denominator <= 0) {
        if (fail_on_media_error) {
            throw std::runtime_error("The Motion Studio composition frame rate is invalid.");
        }
        return {};
    }

    if(request.mask_view_layer && request.sceneMasks()) {
        const auto layer=std::find_if(request.sceneLayers().begin(),request.sceneLayers().end(),[&](const auto& l){return l.id==request.mask_view_layer;});
        if(layer!=request.sceneLayers().end()) {
            const auto target=request.mask_view_target;
            const auto id=target==-2?layer->source_mask:target==-1?layer->transform_mask:
                target>=0 && std::size_t(target)<layer->effects.size()?std::visit([](const auto& e){return e.mask;},layer->effects[target]):0;
            const auto mask=std::find_if(request.sceneMasks()->begin(),request.sceneMasks()->end(),[id](const auto& m){return m.id==id;});
            if(mask!=request.sceneMasks()->end())try {
                int width=request.canvas_size.width,height=request.canvas_size.height;
                if(target!=-1 && (!layer->presentation || layer->presentation->profile==creative_suite::composition_exchange::Profile::MotionLayer)) {
                    if(const auto* text=std::get_if<model::TextLayerContent>(&layer->content)){width=text->box_width;height=text->box_height;}
                    else if(const auto* shape=std::get_if<model::ShapeLayerContent>(&layer->content)){width=shape->width;height=shape->height;}
                    else if(layer->still_frame){width=layer->still_frame->width;height=layer->still_frame->height;}
                    else {const auto metadata=creative_suite::media::VideoProbe{}.probe(layer->source_path);width=metadata.width;height=metadata.height;}
                }
                MotionMaskRasterCache cache;
                auto frame=cache.coverage(*mask,request.composition_frame,width,height,should_cancel,{},request.path_override&&request.path_override->target==model::PathTarget{mask->id,true}?request.path_override->geometry.get():nullptr);
                if(!frame){if(should_cancel&&should_cancel())return {};throw std::runtime_error("Mask VIEW exceeds the coverage budget.");}
                auto result=creative_suite::composition::FrameCompositor::compose(request.canvas_size.width,request.canvas_size.height,{{frame.get(),{}}});
                return result?std::make_shared<const creative_suite::media::RgbaFrame>(std::move(*result)):nullptr;
            } catch(const std::exception& e){
                if(should_cancel&&should_cancel())return {};
                creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,"motion_masks","view_mask",e.what(),{{"mask_id",std::to_string(id)}});
                if(fail_on_media_error)throw;return {};
            }
        }
    }
    std::vector<creative_suite::composition::CompositionLayer> composition_layers;
    std::vector<creative_suite::media::RgbaFramePtr> owned_frames;
    struct PendingGpuEffects {
        std::size_t composition_index = 0;
        model::LayerId layer_id = 0;
        creative_suite::media::RgbaFramePtr source;
        std::vector<model::LayerEffect> effects;
    };
    std::vector<PendingGpuEffects> pending_gpu_effects;
    std::vector<model::LayerId> composition_layer_ids;
    std::uint64_t cpu_color_adjustment_fallbacks = 0;
    std::uint64_t gpu_color_adjustment_count = 0;
    std::uint64_t cpu_gaussian_blur_fallbacks = 0;
    std::uint64_t gpu_gaussian_blur_count = 0;
    bool gpu_color_adjustment_failed = false;
    std::string gpu_failure_operation;
    std::int64_t gpu_failure_code = 0;
    const bool try_gpu_effects = gpu_composition_enabled_ &&
        !gpu_composition_disabled_after_failure_;
    std::set<model::LayerId> active_content_ids;
    std::uint64_t portable_raster_bytes = 0;
    MotionMaskRasterCache mask_cache;
    bool mask_gpu_in_use=false;
    creative_suite::composition::OpenGlCompositionTimings mask_timings;
    const auto* matte_graph=request.mask_view_layer?nullptr:request.sceneMattes().get();
    std::unordered_map<model::LayerId,MaskedLayerFrame> matte_outputs;
    std::unordered_map<model::LayerId,std::size_t> matte_consumers;
    std::unordered_map<model::LayerId,const PreviewLayerSnapshot*> scene_by_id;
    std::vector<const PreviewLayerSnapshot*> processing_layers;
    if(matte_graph) {
        for(const auto& layer:request.sceneLayers()) {
            if(!scene_by_id.emplace(layer.id,&layer).second)throw std::invalid_argument("Duplicate render layer.");
            if(layer.track_matte)++matte_consumers[layer.track_matte->source_id];
        }
        for(auto id:matte_graph->order())if(const auto found=scene_by_id.find(id);found!=scene_by_id.end()) {
            if(found->second->track_matte!=matte_graph->binding(id))throw std::invalid_argument("Stale track matte relation.");
            processing_layers.push_back(found->second);
        }
        if(processing_layers.size()!=request.sceneLayers().size())throw std::invalid_argument("Unknown track matte scene layer.");
    } else if(!request.mask_view_layer)for(const auto& layer:request.sceneLayers())
        if(layer.track_matte)throw std::invalid_argument("Missing track matte graph.");
    const auto matte_relations=[&](model::LayerId affected=0) {
        std::string result;
        if(matte_graph)for(const auto& node:request.sceneLayers())if(node.track_matte &&
            (!affected || affected==node.id || affected==node.track_matte->source_id)) {
            if(!result.empty())result+=",";
            result+=std::to_string(node.id)+"->"+std::to_string(node.track_matte->source_id)+":"+
                std::string(creative_suite::composition::trackMatteModeIdentifier(node.track_matte->mode));
        }
        return result;
    };
    const auto release_matte_source=[&](model::LayerId source) {
        if(--matte_consumers.at(source)!=0)return;
        const auto found=matte_outputs.find(source);if(found==matte_outputs.end())return;
        if(!scene_by_id.at(source)->composite)portable_raster_bytes-=std::uint64_t(request.canvas_size.width)*request.canvas_size.height*4;
        matte_outputs.erase(found);
    };
    const auto cpu_mask_recovery=[&]() {
        composition_layers.clear();owned_frames.clear();pending_gpu_effects.clear();mask_cache={};matte_outputs.clear();
        const auto previous=gpu_composition_disabled_after_failure_;
        struct Restore {bool& field;bool value;~Restore(){field=value;}} restore{gpu_composition_disabled_after_failure_,previous};
        gpu_composition_disabled_after_failure_=true;
        return render(request,should_cancel,fail_on_media_error,mode,async_ticket,async_failure,texture,texture_busy);
    };
    composition_layers.reserve(request.sceneLayers().size());
    owned_frames.reserve(request.sceneLayers().size());
    composition_layer_ids.reserve(request.sceneLayers().size());
    std::unordered_map<model::LayerId, model::ResolvedLayerTransform> resolved;
    try {
        if (request.sceneHierarchy()) resolved = request.sceneHierarchy()->evaluate(request.composition_frame,request.position_override,request.rotation_override,request.scale_override);
    } catch (const std::exception& error) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error, "motion_preview", "evaluate_hierarchy", error.what());
        if (fail_on_media_error || mode==PreviewRequestMode::Gesture) throw;
        return {};
    }
    for(std::size_t layer_index=0;layer_index<request.sceneLayers().size();++layer_index) {
        const auto& layer=matte_graph?*processing_layers[layer_index]:request.sceneLayers()[layer_index];
        struct OnExit {decltype(release_matte_source)& callback;model::LayerId source;~OnExit(){if(source)callback(source);}}
            release_dependency{release_matte_source,matte_graph&&layer.track_matte?layer.track_matte->source_id:0};
        if ((should_cancel && should_cancel())) return {};
        if(!creative_suite::composition::validBlendMode(layer.blend_mode)) {
            creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,
                "motion_preview","validate_blend_mode","Invalid scene blend mode.",{{"layer_id",std::to_string(layer.id)},{"blend_mode",std::to_string(static_cast<unsigned>(layer.blend_mode))}});
            throw std::invalid_argument("Invalid scene blend mode.");
        }
        if (layer.kind == model::LayerKind::Null) continue;
        const auto resolved_entry = resolved.find(layer.id);
        if (request.sceneHierarchy() && resolved_entry == resolved.end()) {
            creative_suite::diagnostics::Logger::instance().log(
                creative_suite::diagnostics::Level::Error, "motion_preview", "evaluate_hierarchy",
                "The render layer is absent from its hierarchy snapshot.", {{"layer_id", std::to_string(layer.id)}});
            if (fail_on_media_error || mode==PreviewRequestMode::Gesture) throw std::runtime_error("Missing layer hierarchy snapshot node.");
            return {};
        }
        auto effective_transform = request.sceneHierarchy() ? static_cast<const creative_suite::animation::Transform2D&>(resolved_entry->second) :
            creative_suite::animation::evaluateTransform(layer.transform, layer.keyframes, layer.local_frame);
        std::optional<model::AffineTransform> effective_world;
        if(request.sceneHierarchy() && resolved_entry->second.affine)effective_world=resolved_entry->second.world;
        if (!request.sceneHierarchy()) {
            const model::AnchorPoint anchor{
                creative_suite::animation::evaluateScalar(layer.anchor_point.x_pixels,layer.anchor_keyframes[0],layer.local_frame),
                creative_suite::animation::evaluateScalar(layer.anchor_point.y_pixels,layer.anchor_keyframes[1],layer.local_frame)};
            const auto sy=layer.scale_y?creative_suite::animation::evaluateScalar(layer.scale_y->value,layer.scale_y->keyframes,layer.local_frame):effective_transform.scale;
            if(sy!=effective_transform.scale) {
                model::CompositionLayer local{layer.id,model::LayerKind::Null,""};local.transform=effective_transform;
                local.scale_y=model::ScaleChannel{sy,{}};local.anchor_point=anchor;
                effective_world=model::localLayerGeometry(local,request.canvas_size,0);
            } else if (anchor != model::AnchorPoint{}) {
                const auto geometry = model::geometryOffset(effective_transform,request.canvas_size,anchor);
                effective_transform.position_x=geometry.translation_x_pixels/request.canvas_size.width;
                effective_transform.position_y=geometry.translation_y_pixels/request.canvas_size.height;
            }
        }
        creative_suite::media::RgbaFramePtr frame;
        std::optional<PathRaster> path_raster;
        if (layer.kind == model::LayerKind::Image) {
            frame = layer.still_frame;
            if (frame == nullptr) {
                if (fail_on_media_error) {
                    throw std::runtime_error("The still-image layer has no cached decoded frame: " +
                                             pathForLog(layer.source_path));
                }
                creative_suite::diagnostics::Logger::instance().log(
                    creative_suite::diagnostics::Level::Error,
                    "motion_preview", "load_still_frame",
                    "The still-image layer has no cached decoded frame",
                    {{"path", pathForLog(layer.source_path)}});
                continue;
            }
        } else if (layer.kind == model::LayerKind::Video) {
            if (layer.source_path.empty() || !std::isfinite(layer.source_frame_rate) ||
                layer.source_frame_rate <= 0.0 || layer.local_frame < 0) {
                if (fail_on_media_error) {
                    throw std::runtime_error("Video layer has invalid source timing metadata: " +
                                             pathForLog(layer.source_path));
                }
                reportDecodeError(layer.source_path, -1,
                                  "Video layer has invalid source timing metadata");
                continue;
            }
            auto presentation = layer.presentation.value_or(creative_suite::composition_exchange::Presentation{});
            presentation.visible = true; // Active layer visibility belongs to the document snapshot.
            presentation.start = 0; presentation.duration = 0; presentation.source_start = layer.source_start_frame;
            const auto mapped_source_frame = layer.presentation ? creative_suite::composition_exchange::sourceFrame(presentation, layer.local_frame, layer.source_frame_rate, layer.source_frame_count, request.frame_rate.asDouble()) : model::sourceFrameForTimelineFrame(
                layer.local_frame, layer.source_start_frame, layer.source_frame_rate, request.frame_rate, layer.source_frame_count);
            if (layer.presentation && !mapped_source_frame) continue;
            if (!mapped_source_frame.has_value()) {
                if (fail_on_media_error) {
                    throw std::runtime_error("Timeline position cannot be represented as a source frame: " +
                                             pathForLog(layer.source_path));
                }
                reportDecodeError(layer.source_path, -1,
                                  "Timeline position cannot be represented as a source frame");
                continue;
            }
            const auto source_frame = *mapped_source_frame;
            try {
                StageTimer decode_timer(
                    record_preview_metrics_, diagnostics::PreviewTimingStage::Decode);
                auto session = video_sessions_.find(layer.source_path);
                if (session == video_sessions_.end()) {
                    auto opened = VideoPlaybackSession::open(
                        layer.source_path,
                        record_preview_metrics_ ? &motionDecodeObserver() : nullptr);
                    session = video_sessions_.emplace(layer.source_path, std::move(opened)).first;
                }
                const auto cancelled = [&should_cancel] {
                    return should_cancel && should_cancel();
                };
                std::optional<creative_suite::media::VideoFramePtr> decoded;
                const bool collect_forward_metrics =
                    record_preview_metrics_ &&
                    diagnostics::PerformanceMetrics::instance().enabled();
                if (shouldUseSequentialPlaybackDecode(
                        mode,
                        session->second->current_frame_index(),
                        source_frame)) {
                    creative_suite::media::ForwardDecodeDiagnostics forward_diagnostics;
                    try {
                        decoded = session->second->decode_forward_to(
                            source_frame,
                            cancelled,
                            collect_forward_metrics ? &forward_diagnostics : nullptr);
                    } catch (const std::exception&) {
                        // The shared decoder logs operational exceptions. If
                        // they were not caused by cancellation, try the normal
                        // timestamp-seek path below to recover this preview.
                        if (cancelled()) return {};
                    }
                    if (collect_forward_metrics && forward_diagnostics.attempted) {
                        diagnostics::PerformanceMetrics::instance().recordForwardDecode(
                            forward_diagnostics.completed,
                            forward_diagnostics.elapsed_nanoseconds);
                    }
                    if (!decoded.has_value()) {
                        if (!shouldFallbackToTimestampSeek(cancelled())) return {};
                        if (collect_forward_metrics) {
                            diagnostics::PerformanceMetrics::instance()
                                .recordForwardDecodeFallback();
                        }
                        decoded = session->second->decode_frame_at(source_frame, cancelled);
                    }
                } else {
                    decoded = session->second->decode_frame_at(source_frame, cancelled);
                }
                if (!decoded.has_value()) {
                    if ((should_cancel && should_cancel())) return {};
                    if (fail_on_media_error) {
                        throw std::runtime_error("The video decoder returned no frame: " +
                                                 pathForLog(layer.source_path));
                    }
                    reportDecodeError(layer.source_path, source_frame,
                                      "The video decoder returned no frame");
                    video_sessions_.erase(session);
                    continue;
                }
                frame = *decoded;
            } catch (const creative_suite::media::MediaError& error) {
                if (should_cancel && should_cancel()) return {};
                if (fail_on_media_error) {
                    throw creative_suite::media::MediaError(
                        "Video decode failed for " + pathForLog(layer.source_path) +
                            ": " + error.what(),
                        error.error_code());
                }
                reportDecodeError(layer.source_path, source_frame, error.what(),
                                  error.error_code());
                video_sessions_.erase(layer.source_path);
                continue;
            } catch (const std::exception& error) {
                if (should_cancel && should_cancel()) return {};
                if (fail_on_media_error) {
                    throw std::runtime_error("Video decode failed for " +
                        pathForLog(layer.source_path) + ": " + error.what());
                }
                reportDecodeError(layer.source_path, source_frame, error.what());
                video_sessions_.erase(layer.source_path);
                continue;
            }
        } else if (layer.kind == model::LayerKind::Text ||
                   layer.kind == model::LayerKind::Shape) {
            active_content_ids.insert(layer.id);
            auto cached = content_frames_.find(layer.id);
            const auto* shape=std::get_if<model::ShapeLayerContent>(&layer.content);
            std::optional<model::PathGeometry> path_geometry;double path_margin=0;
            model::TrimPathRange trim_range;
            std::shared_ptr<const model::PathLengthTable> path_length;
            if(shape&&shape->path){
                trim_range=model::trimPathRange(model::evaluateTrimPaths(shape->trim_paths,layer.local_frame));
                path_geometry=request.path_override&&request.path_override->target==model::PathTarget{layer.id,false}?*request.path_override->geometry:model::evaluatePath(*shape->path,layer.local_frame);
                for(const auto& effect:layer.effects)if(const auto* blur=std::get_if<model::GaussianBlurEffect>(&effect);blur&&blur->enabled)path_margin+=std::ceil(3*model::evaluateGaussianBlurEffect(*blur,layer.local_frame).radius_pixels);
            }
            if(path_geometry && cached!=content_frames_.end() && cached->second.path_geometry==path_geometry)path_length=cached->second.path_length;
            if(path_geometry && trim_range.span>0 && trim_range.span<1 && shape->stroke_width_pixels>0 && !path_length){
                try{path_length=model::measureTrimPath(*path_geometry,should_cancel);if(!path_length)return {};}
                catch(const std::exception& e){throw std::runtime_error("Trim Paths layer "+std::to_string(layer.id)+", frame "+std::to_string(request.composition_frame)+": "+e.what());}
            }
            if (cached == content_frames_.end() || cached->second.content != layer.content || cached->second.presentation != layer.presentation || cached->second.canvas != request.canvas_size || cached->second.path_geometry!=path_geometry || cached->second.path_margin!=path_margin || cached->second.trim_range!=trim_range) {
                StageTimer raster_timer(
                    record_preview_metrics_,
                    diagnostics::PreviewTimingStage::TextShapeRasterization);
                std::optional<creative_suite::media::RgbaFrame> rasterized;
                std::optional<PathRaster> new_path_raster;
                if(path_geometry){try{new_path_raster=rasterizePath(*shape,*path_geometry,path_margin,should_cancel,trim_range,path_length.get());}
                    catch(const std::exception& e){throw std::runtime_error("Path raster/Trim Paths layer "+std::to_string(layer.id)+", frame "+std::to_string(request.composition_frame)+": "+e.what());}}
                else rasterized=rasterizeLayerContent(layer.content);
                if (layer.presentation && layer.presentation->profile == creative_suite::composition_exchange::Profile::FusionCanvas && layer.kind == model::LayerKind::Text) {
                    const auto& t = std::get<model::TextLayerContent>(layer.content);
                    rasterized = creative_suite::composition_exchange::textFrame({t.text,t.font_family,t.font_size_pixels,int(t.alignment),t.color}, *layer.presentation, request.canvas_size.width, request.canvas_size.height);
                }
                if (!rasterized.has_value() && !new_path_raster) {
                    if(should_cancel && should_cancel())return {};
                    if (fail_on_media_error || (path_geometry && mode==PreviewRequestMode::Gesture)) {
                        throw std::runtime_error("Text or shape content could not be rasterized for layer " +
                                                 std::to_string(layer.id));
                    }
                    creative_suite::diagnostics::Logger::instance().log(
                        creative_suite::diagnostics::Level::Warning,
                        "motion_preview", "rasterize_layer_content",
                        "Text or shape content could not be rasterized within the supported limits",
                        {{"layer_id", std::to_string(layer.id)}});
                    content_frames_.erase(layer.id);
                    continue;
                }
                auto raster_frame=new_path_raster?new_path_raster->frame:std::make_shared<const creative_suite::media::RgbaFrame>(std::move(*rasterized));
                if(cached!=content_frames_.end()&&cached->second.content==layer.content&&cached->second.presentation==layer.presentation&&cached->second.canvas==request.canvas_size){
                    cached->second.frame=std::move(raster_frame);cached->second.path_raster=std::move(new_path_raster);cached->second.path_geometry=std::move(path_geometry);cached->second.path_margin=path_margin;
                }else cached=content_frames_.insert_or_assign(layer.id,CachedContentFrame{layer.content,layer.presentation,request.canvas_size,std::move(raster_frame),std::move(new_path_raster),std::move(path_geometry),path_margin}).first;
            }
            cached->second.trim_range=trim_range;cached->second.path_length=std::move(path_length);
            frame = cached->second.frame;path_raster=cached->second.path_raster;
            if(path_raster){if(request.sceneHierarchy())effective_world=resolved_entry->second.world;else {model::CompositionLayer local{layer.id,model::LayerKind::Shape,""};local.transform=layer.transform;local.keyframes=layer.keyframes;local.anchor_point=layer.anchor_point;local.anchor_keyframes=layer.anchor_keyframes;local.scale_y=layer.scale_y;effective_world=model::localLayerGeometry(local,request.canvas_size,layer.local_frame);}}
        } else {
            continue;
        }

        if (frame == nullptr) continue;
        const bool fusion_presentation = layer.presentation && layer.presentation->profile == creative_suite::composition_exchange::Profile::FusionCanvas;
        if (fusion_presentation && !path_raster) {
            portable_raster_bytes += std::uint64_t(request.canvas_size.width)*request.canvas_size.height*4;
            if (portable_raster_bytes > 256ULL*1024*1024)
                throw creative_suite::composition_exchange::Error("Interchange source preparation exceeds the 256 MiB frame budget.");
            frame = std::make_shared<const creative_suite::media::RgbaFrame>(creative_suite::composition_exchange::fit(*frame,request.canvas_size.width,request.canvas_size.height, layer.kind == model::LayerKind::Text, should_cancel));
        }
        const auto consumers=matte_consumers.find(layer.id);
        const bool needs_matte_pixels=matte_graph && (layer.track_matte ||
            (consumers!=matte_consumers.end() && consumers->second>0));
        if(hasLayerMasks(layer) || needs_matte_pixels || path_raster) {
            if(try_gpu_effects && !gpu_compositor_)gpu_compositor_=std::make_unique<creative_suite::composition::OpenGlFrameCompositor>(
                gpu_surface_,creative_suite::composition::OpenGlPrecisionPolicy::Automatic,texture_share_context_,texture_budget_);
            MaskedLayerFrame processed;
            try {
                const auto bytes=std::uint64_t(request.canvas_size.width)*request.canvas_size.height*4;
                if(matte_graph && portable_raster_bytes+bytes*(layer.track_matte?2:1)>256ULL*1024*1024)
                    throw std::runtime_error("Track matte intermediates exceed the 256 MiB preparation budget.");
                processed=renderMaskedLayer(layer,frame,request,mask_cache,try_gpu_effects?gpu_compositor_.get():nullptr,should_cancel,&effective_transform,effective_world?&*effective_world:nullptr,path_raster?&*path_raster:nullptr);
                if(matte_graph && layer.track_matte && (processed.rgba || processed.texture)) {
                    const auto source=matte_outputs.find(layer.track_matte->source_id);
                    if(try_gpu_effects) {
                        creative_suite::composition::OpenGlImageOperation op;
                        op.kind=creative_suite::composition::OpenGlImageOperationKind::ApplyTrackMatte;
                        op.background={processed.rgba.get(),processed.texture};op.track_matte_mode=layer.track_matte->mode;
                        if(source!=matte_outputs.end())op.mask={source->second.rgba.get(),source->second.texture};
                        creative_suite::composition::OpenGlCompositionTimings measured;
                        auto result=gpu_compositor_->processImage(op,should_cancel,&measured);
                        addMaskTimings(processed.timings,measured);
                        if(result.status==creative_suite::composition::OpenGlCompositionStatus::Cancelled)return {};
                        if(result.status!=creative_suite::composition::OpenGlCompositionStatus::Complete || !result.frame)
                            throw std::runtime_error(result.operation+": "+(result.cause.empty()?"Track matte image pool is occupied.":result.cause));
                        processed.rgba.reset();processed.texture=std::move(result.frame);
                    } else {
                        auto result=std::make_shared<creative_suite::media::RgbaFrame>(*processed.rgba);
                        if(!creative_suite::composition::applyTrackMatte(*result,source==matte_outputs.end()?nullptr:source->second.rgba.get(),layer.track_matte->mode,should_cancel)) {
                            if(should_cancel && should_cancel())return {};
                            throw std::runtime_error("The CPU track matte operation rejected its inputs.");
                        }
                        processed.rgba=std::move(result);
                    }
                }
            }
            catch(const std::exception& e) {
                if(should_cancel && should_cancel())return {};
                creative_suite::diagnostics::Logger::instance().log(try_gpu_effects?creative_suite::diagnostics::Level::Warning:creative_suite::diagnostics::Level::Error,
                    layer.track_matte?"motion_track_matte":"motion_masks",try_gpu_effects?"gpu_cpu_recovery":"render_layer",e.what(),
                    {{"layer_id",std::to_string(layer.id)},
                     {"source_id",layer.track_matte?std::to_string(layer.track_matte->source_id):std::to_string(layer.id)},
                     {"track_mattes",matte_relations(layer.id)},
                     {"mode",layer.track_matte?std::string(creative_suite::composition::trackMatteModeIdentifier(layer.track_matte->mode)):"none"},
                     {"frame",std::to_string(request.composition_frame)},{"backend",try_gpu_effects?"opengl":"cpu"}});
                if(try_gpu_effects)return cpu_mask_recovery();
                if(fail_on_media_error)throw;
                return {};
            }
            if(!processed.rgba && !processed.texture)return {};
            addMaskTimings(mask_timings,processed.timings);
            if(request.mask_view_layer==layer.id) {
                auto composed=creative_suite::composition::FrameCompositor::compose(request.canvas_size.width,request.canvas_size.height,{{processed.rgba.get(),{}}});
                return composed?std::make_shared<const creative_suite::media::RgbaFrame>(std::move(*composed)):nullptr;
            }
            portable_raster_bytes+=std::uint64_t(request.canvas_size.width)*request.canvas_size.height*4;
            if(portable_raster_bytes>256ULL*1024*1024)throw std::runtime_error("Masked layer outputs exceed the 256 MiB preparation budget.");
            if(matte_graph && matte_consumers[layer.id]>0)matte_outputs.emplace(layer.id,processed);
            if(!layer.composite)continue;
            if(processed.rgba){owned_frames.push_back(processed.rgba);composition_layers.push_back({processed.rgba.get(),{}});}
            else {
                auto geometry=std::make_shared<const creative_suite::media::RgbaFrame>(creative_suite::media::RgbaFrame{request.canvas_size.width,request.canvas_size.height,request.canvas_size.width*4,{}});
                owned_frames.push_back(geometry);CompositionLayer entry{geometry.get(),{}};entry.texture_frame=processed.texture;
                composition_layers.push_back(std::move(entry));mask_gpu_in_use=true;
            }
            composition_layers.back().blend_mode=layer.blend_mode;
            composition_layer_ids.push_back(layer.id);continue;
        }
        const bool effects_valid = model::validLayerEffects(layer.effects);
        const auto evaluated_effects = effects_valid
            ? model::evaluateLayerEffects(layer.effects, layer.local_frame)
            : layer.effects;
        std::vector<creative_suite::effects::ColorAdjustmentParameters> gpu_adjustments;
        std::vector<creative_suite::composition::GpuCompositionEffect> gpu_effects;
        const bool has_enabled_effects = hasEnabledLayerEffects(evaluated_effects);
        const bool gpu_color_only_stack = try_gpu_effects && has_enabled_effects &&
            effects_valid &&
            std::all_of(evaluated_effects.begin(), evaluated_effects.end(), [](const auto& effect) {
                return std::visit([](const auto& value) {
                    using Effect = std::decay_t<decltype(value)>;
                    return !value.enabled ||
                        std::is_same_v<Effect, model::ColorAdjustmentEffect>;
                }, effect);
            });
        const bool gpu_ordered_stack = try_gpu_effects && has_enabled_effects &&
            effects_valid &&
            std::any_of(evaluated_effects.begin(), evaluated_effects.end(), [](const auto& effect) {
                const auto* blur = std::get_if<model::GaussianBlurEffect>(&effect);
                return blur && blur->enabled;
            });
        if (gpu_color_only_stack) {
            gpu_adjustments.reserve(evaluated_effects.size());
            for (const auto& effect : evaluated_effects) {
                if (const auto* color = std::get_if<model::ColorAdjustmentEffect>(&effect);
                    color && color->enabled) {
                    gpu_adjustments.push_back({color->brightness,
                        color->contrast_percent, color->saturation_percent});
                }
            }
        } else if (gpu_ordered_stack) {
            gpu_effects.reserve(evaluated_effects.size());
            for (const auto& effect : evaluated_effects) {
                std::visit([&](const auto& value) {
                    using Effect = std::decay_t<decltype(value)>;
                    if (!value.enabled) return;
                    if constexpr (std::is_same_v<Effect, model::GaussianBlurEffect>) {
                        gpu_effects.emplace_back(
                            creative_suite::composition::GpuGaussianBlurParameters{
                                value.radius_pixels});
                    } else {
                        gpu_effects.emplace_back(
                            creative_suite::effects::ColorAdjustmentParameters{
                                value.brightness, value.contrast_percent,
                                value.saturation_percent});
                    }
                }, effect);
            }
        } else if (gpu_composition_enabled_ && has_enabled_effects) {
            cpu_color_adjustment_fallbacks += static_cast<std::uint64_t>(
                std::count_if(evaluated_effects.begin(), evaluated_effects.end(), [](const auto& effect) {
                    return std::holds_alternative<model::ColorAdjustmentEffect>(effect) &&
                        std::get<model::ColorAdjustmentEffect>(effect).enabled;
                }));
            cpu_gaussian_blur_fallbacks += static_cast<std::uint64_t>(
                std::count_if(evaluated_effects.begin(), evaluated_effects.end(), [](const auto& effect) {
                    return std::holds_alternative<model::GaussianBlurEffect>(effect) &&
                        std::get<model::GaussianBlurEffect>(effect).enabled;
                }));
        }
        const bool gpu_stack = gpu_color_only_stack || gpu_ordered_stack;
        if (has_enabled_effects && !gpu_stack) {
            try {
                StageTimer effects_timer(
                    record_preview_metrics_, diagnostics::PreviewTimingStage::Effects);
                auto processed = std::make_shared<creative_suite::media::RgbaFrame>(*frame);
                const EffectTimingRecorder effect_timing_recorder =
                    [record = record_preview_metrics_](
                        LayerEffectKind effect, std::uint64_t duration_nanoseconds) {
                        if (!record) return;
                        const auto kind = effect == LayerEffectKind::GaussianBlur
                            ? diagnostics::PreviewEffectKind::GaussianBlur
                            : diagnostics::PreviewEffectKind::ColorAdjustment;
                        diagnostics::PerformanceMetrics::instance().recordEffectTiming(
                            kind, duration_nanoseconds);
                    };
                if (!applyLayerEffects(*processed, evaluated_effects, should_cancel,
                                       effect_timing_recorder)) {
                    return {};
                }
                frame = std::move(processed);
            } catch (const std::exception& error) {
                if (should_cancel && should_cancel()) return {};
                if (fail_on_media_error) {
                    throw std::runtime_error("Effect processing failed for layer " +
                        std::to_string(layer.id) + ": " + error.what());
                }
                creative_suite::diagnostics::Logger::instance().log(
                    creative_suite::diagnostics::Level::Error,
                    "motion_preview", "apply_layer_effects", error.what(),
                    {{"layer_id", std::to_string(layer.id)}});
                continue;
            }
        }
        owned_frames.push_back(frame);
        auto transform = effective_transform;
        if (!effective_world && (layer.kind == model::LayerKind::Text ||
            layer.kind == model::LayerKind::Shape)) {
            const double fit_scale = std::min(
                static_cast<double>(request.canvas_size.width) / frame->width,
                static_cast<double>(request.canvas_size.height) / frame->height);
            if (!std::isfinite(fit_scale) || fit_scale <= 0.0) continue;
            // The shared compositor aspect-fits every source frame to the
            // canvas. Compensate here so intrinsic pixel dimensions on native
            // text/shape layers remain pixel-sized at transform scale 1.
            transform.scale /= fit_scale;
        }
        const auto composition_index = composition_layers.size();
        composition_layers.push_back(CompositionLayer{frame.get(), transform, {}, {},
            std::move(gpu_adjustments), std::move(gpu_effects)});
        composition_layers.back().blend_mode=layer.blend_mode;
        if(effective_world) {
            const bool intrinsic=layer.kind==model::LayerKind::Text || layer.kind==model::LayerKind::Shape;
            const auto fit=intrinsic || fusion_presentation?1.:std::min(double(request.canvas_size.width)/frame->width,double(request.canvas_size.height)/frame->height);
            composition_layers.back().source_to_canvas=model::sourcePixelGeometry(*effective_world,frame->width,frame->height,fit);
        }
        composition_layer_ids.push_back(layer.id);
        if (!composition_layers.back().gpu_color_adjustments.empty() ||
            !composition_layers.back().gpu_effects.empty()) {
            gpu_color_adjustment_count +=
                composition_layers.back().gpu_color_adjustments.size();
            for (const auto& effect : composition_layers.back().gpu_effects) {
                if (std::holds_alternative<creative_suite::effects::ColorAdjustmentParameters>(effect))
                    ++gpu_color_adjustment_count;
                else if (std::lround(std::get<
                             creative_suite::composition::GpuGaussianBlurParameters>(effect)
                                 .radius_pixels) > 0)
                    ++gpu_gaussian_blur_count;
            }
            pending_gpu_effects.push_back({composition_index, layer.id, frame,
                evaluated_effects});
        }
    }

    if(matte_graph) {
        // Pixel dependencies execute source-first; stack order remains document order.
        std::unordered_map<model::LayerId,std::size_t> previous;
        for(std::size_t i=0;i<composition_layer_ids.size();++i)previous.emplace(composition_layer_ids[i],i);
        std::vector<std::size_t> remap(composition_layers.size());
        std::vector<CompositionLayer> ordered;std::vector<model::LayerId> ids;
        ordered.reserve(composition_layers.size());ids.reserve(composition_layers.size());
        for(const auto& layer:request.sceneLayers())if(const auto found=previous.find(layer.id);found!=previous.end()) {
            remap[found->second]=ordered.size();ordered.push_back(std::move(composition_layers[found->second]));ids.push_back(layer.id);
        }
        composition_layers=std::move(ordered);composition_layer_ids=std::move(ids);
        for(auto& pending:pending_gpu_effects)pending.composition_index=remap[pending.composition_index];
    }
    for (auto cached = content_frames_.begin(); cached != content_frames_.end();) {
        if (!active_content_ids.contains(cached->first)) {
            cached = content_frames_.erase(cached);
        } else {
            ++cached;
        }
    }

    if ((should_cancel && should_cancel()) ||
        (composition_layers.empty() && !request.black_canvas_when_empty)) {
        return {};
    }
    StageTimer composition_timer(
        record_preview_metrics_, diagnostics::PreviewTimingStage::Composition);
    if (gpu_composition_enabled_ && !gpu_composition_disabled_after_failure_) {
        using creative_suite::composition::OpenGlCompositionStatus;
        creative_suite::composition::OpenGlCompositionTimings gpu_timings;
        creative_suite::composition::OpenGlCompositionResult gpu_result;
        creative_suite::composition::OpenGlTextureFramePtr gpu_texture;
        if (gpu_metrics_ != nullptr) ++gpu_metrics_->composition_attempts;
        if (gpu_surface_ == nullptr) {
            gpu_result.status = OpenGlCompositionStatus::Failed;
            gpu_result.operation = "create_surface";
            gpu_result.cause = "The OpenGL offscreen surface is unavailable.";
        } else {
            try {
                if (!gpu_compositor_) {
                    gpu_compositor_ = std::make_unique<
                        creative_suite::composition::OpenGlFrameCompositor>(
                            gpu_surface_, creative_suite::composition::OpenGlPrecisionPolicy::Automatic,
                            texture_share_context_, texture_budget_);
                }
                bool use_synchronous_gpu = true;
                if (texture && texture_share_context_ && !texture_delivery_disabled_) {
                    const auto direct = gpu_compositor_->composeTexture(
                        request.canvas_size.width, request.canvas_size.height,
                        composition_layers, should_cancel, &gpu_timings);
                    if (record_preview_metrics_) {
                        diagnostics::PerformanceMetrics::instance().recordTexturePool(
                            texturePoolBytes(), texturePoolOccupancy());
                    }
                    if (direct.status == OpenGlCompositionStatus::Busy) {
                        if(matte_graph) {
                            creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Warning,
                                "motion_track_matte","gpu_pool_recovery","Track matte leases prevent final texture allocation; using CPU.",
                                {{"frame",std::to_string(request.composition_frame)},{"backend","opengl"},{"track_mattes",matte_relations()}});
                            return cpu_mask_recovery();
                        }
                        if (texture_busy) *texture_busy = true;
                        return {};
                    }
                    if (direct.status == OpenGlCompositionStatus::Complete && direct.frame) {
                        gpu_texture = direct.frame;
                        gpu_result.status = direct.status;
                        use_synchronous_gpu = false;
                    } else if (direct.status == OpenGlCompositionStatus::Unsupported) {
                        // A target beyond the bounded pool uses the legacy RGBA route.
                        texture_delivery_disabled_ = true;
                        creative_suite::diagnostics::Logger::instance().log(
                            creative_suite::diagnostics::Level::Warning,
                            "motion_preview", "texture_delivery_unavailable", direct.cause,
                            {{"operation", direct.operation}, {"error_code", std::to_string(direct.error_code)}});
                    } else {
                        gpu_result.status = direct.status;
                        gpu_result.operation = direct.operation;
                        gpu_result.cause = direct.cause;
                        gpu_result.error_code = direct.error_code;
                        use_synchronous_gpu = false;
                    }
                }
                if (async_ticket != nullptr) {
                    if (!async_readback_prepared_) {
                        async_readback_prepared_ = true;
                        const auto frame_bytes = static_cast<std::uint64_t>(
                            request.canvas_size.width) * request.canvas_size.height * 4;
                        const auto pbo_budget =
                            frame_bytes < async_readback_staging_budget_bytes_
                                ? async_readback_staging_budget_bytes_ - frame_bytes : 0;
                        const auto prepared = gpu_compositor_->prepareAsyncReadback(
                            request.canvas_size.width, request.canvas_size.height,
                            pbo_budget, 2, &async_readback_slots_);
                        if (prepared.status == OpenGlCompositionStatus::Complete &&
                            async_readback_slots_ > 0) {
                            use_synchronous_gpu = false;
                            if (gpu_metrics_ != nullptr)
                                gpu_metrics_->readback_slots = async_readback_slots_;
                        } else {
                            async_readback_slots_ = 0;
                            creative_suite::diagnostics::Logger::instance().log(
                                creative_suite::diagnostics::Level::Warning,
                                "motion_export", "async_readback_unavailable",
                                prepared.cause.empty()
                                    ? "Asynchronous GPU readback is unavailable; using synchronous GPU readback."
                                    : prepared.cause,
                                {{"gpu_operation", prepared.operation},
                                 {"error_code", std::to_string(prepared.error_code)},
                                 {"canvas_width", std::to_string(request.canvas_size.width)},
                                 {"canvas_height", std::to_string(request.canvas_size.height)}});
                        }
                    } else {
                        use_synchronous_gpu = async_readback_slots_ == 0;
                    }
                    if (!use_synchronous_gpu) {
                        const auto submitted = gpu_compositor_->submitAsyncReadback(
                            request.canvas_size.width, request.canvas_size.height,
                            composition_layers, should_cancel);
                        gpu_timings = submitted.composition_timings;
                        if (submitted.status == OpenGlCompositionStatus::Complete) {
                            if (gpu_metrics_ != nullptr) {
                                gpu_metrics_->record(submitted.composition_timings);
                                gpu_metrics_->readback_bytes += submitted.bytes;
                                gpu_metrics_->readback_submit_nanoseconds +=
                                    submitted.submission_nanoseconds;
                                ++gpu_metrics_->readback_frames_submitted;
                                ++gpu_metrics_->composition_frames;
                            }
                            if (async_ticket) *async_ticket = submitted.ticket;
                            return {};
                        }
                        if (submitted.status == OpenGlCompositionStatus::Cancelled ||
                            (should_cancel && should_cancel())) return {};
                        if (async_failure) *async_failure = true;
                        gpu_result.status = submitted.status;
                        gpu_result.operation = submitted.operation;
                        gpu_result.cause = submitted.cause;
                        gpu_result.error_code = submitted.error_code;
                        use_synchronous_gpu = false;
                    }
                }
                if (use_synchronous_gpu) {
                    gpu_result = gpu_compositor_->compose(
                        request.canvas_size.width,
                        request.canvas_size.height,
                        composition_layers,
                        should_cancel,
                        &gpu_timings);
                }
            } catch (const std::exception& error) {
                gpu_result.status = OpenGlCompositionStatus::Failed;
                gpu_result.operation = "compose";
                gpu_result.cause = error.what();
            } catch (...) {
                gpu_result.status = OpenGlCompositionStatus::Failed;
                gpu_result.operation = "compose";
                gpu_result.cause = "Unknown OpenGL composition failure";
            }
        }
        addMaskTimings(gpu_timings,mask_timings);
        if (gpu_metrics_ != nullptr) gpu_metrics_->record(gpu_timings);
        if (gpu_result.status == OpenGlCompositionStatus::Cancelled ||
            (should_cancel && should_cancel())) {
            return {};
        }
        if (gpu_result.status == OpenGlCompositionStatus::Complete &&
            (gpu_result.frame.has_value() || gpu_texture)) {
            if (gpu_metrics_ != nullptr) ++gpu_metrics_->composition_frames;
            if (record_preview_metrics_ &&
                (gpu_timings.color_adjustment_count != 0 ||
                 cpu_color_adjustment_fallbacks != 0)) {
                diagnostics::PerformanceMetrics::instance().recordGpuColorAdjustment(
                    gpu_timings.color_adjustment_count, cpu_color_adjustment_fallbacks, false,
                    gpu_timings.color_adjustment_submission_nanoseconds);
            }
            if (record_preview_metrics_ &&
                (gpu_timings.gaussian_blur_count != 0 || cpu_gaussian_blur_fallbacks != 0)) {
                diagnostics::PerformanceMetrics::instance().recordGpuGaussianBlur(
                    gpu_timings.gaussian_blur_count, cpu_gaussian_blur_fallbacks, false,
                    gpu_timings.gaussian_blur_submission_nanoseconds);
                if (gpu_timings.gaussian_blur_count != 0)
                    diagnostics::PerformanceMetrics::instance().recordEffectTiming(
                        diagnostics::PreviewEffectKind::GaussianBlur,
                        gpu_timings.gaussian_blur_submission_nanoseconds);
            }
            if (record_preview_metrics_) {
                diagnostics::PerformanceMetrics::instance().recordGpuComposition(
                    true, false, gpu_timings.uploaded_bytes, gpu_timings.readback_bytes,
                    gpu_timings.upload_nanoseconds, gpu_timings.draw_submission_nanoseconds,
                    gpu_timings.readback_nanoseconds);
            }
            if (gpu_texture && texture) {
                if (record_preview_metrics_) {
                    diagnostics::PerformanceMetrics::instance().recordTiming(
                        diagnostics::PreviewTimingStage::GpuProducerFenceSubmission,
                        gpu_timings.producer_fence_submission_nanoseconds);
                }
                *texture = std::move(gpu_texture);
                return {};
            }
            return std::make_shared<const creative_suite::media::RgbaFrame>(
                std::move(*gpu_result.frame));
        }

        const bool gpu_failed = gpu_result.status == OpenGlCompositionStatus::Failed ||
            gpu_result.status == OpenGlCompositionStatus::Complete;
        if (gpu_metrics_ != nullptr && gpu_failed) ++gpu_metrics_->failures;
        gpu_color_adjustment_failed = gpu_failed;
        gpu_failure_operation = gpu_result.operation;
        gpu_failure_code = gpu_result.error_code;
        const std::string failing_layer_id = gpu_result.layer_index >= 0 &&
            static_cast<std::size_t>(gpu_result.layer_index) < composition_layer_ids.size()
            ? std::to_string(composition_layer_ids[static_cast<std::size_t>(gpu_result.layer_index)])
            : std::string("unknown");
        if(!mask_gpu_in_use || gpu_failed)gpu_composition_disabled_after_failure_ = true;
        std::string blend_context;
        for(std::size_t i=0;i<composition_layers.size();++i)if(composition_layers[i].blend_mode!=creative_suite::composition::BlendMode::Normal) {
            if(!blend_context.empty())blend_context+=",";
            blend_context+=std::to_string(composition_layer_ids[i])+":"+std::string(creative_suite::composition::blendModeIdentifier(composition_layers[i].blend_mode));
        }
        const char* const gpu_subsystem = record_preview_metrics_
            ? "motion_preview" : "motion_export";
        creative_suite::diagnostics::Logger::instance().log(
            gpu_failed ? creative_suite::diagnostics::Level::Error
                       : creative_suite::diagnostics::Level::Warning,
            gpu_subsystem, "gpu_composition_fallback",
            gpu_result.cause.empty() ? "OpenGL composition is unavailable; using CPU composition"
                                     : gpu_result.cause,
            {{"gpu_operation", gpu_result.operation},
             {"error_code", std::to_string(gpu_result.error_code)},
             {"canvas_width", std::to_string(request.canvas_size.width)},
             {"canvas_height", std::to_string(request.canvas_size.height)},
             {"layer_id", failing_layer_id},
             {"blend_modes",blend_context.empty()?"normal":blend_context},
             {"track_mattes",matte_relations()},{"frame",std::to_string(request.composition_frame)},
             {"layer_index", std::to_string(gpu_result.layer_index)},
             {"status", gpu_result.status == OpenGlCompositionStatus::Unsupported
                    ? "unsupported" : "failed"}});
        if (record_preview_metrics_) {
            diagnostics::PerformanceMetrics::instance().recordGpuComposition(
                false, gpu_failed, gpu_timings.uploaded_bytes, gpu_timings.readback_bytes,
                gpu_timings.upload_nanoseconds, gpu_timings.draw_submission_nanoseconds,
                gpu_timings.readback_nanoseconds);
            diagnostics::PerformanceMetrics::instance().recordGpuColorAdjustment(
                0, cpu_color_adjustment_fallbacks + gpu_color_adjustment_count,
                gpu_color_adjustment_failed && gpu_color_adjustment_count != 0,
                gpu_timings.color_adjustment_submission_nanoseconds);
            diagnostics::PerformanceMetrics::instance().recordGpuGaussianBlur(
                0, cpu_gaussian_blur_fallbacks + gpu_gaussian_blur_count,
                gpu_failed && gpu_gaussian_blur_count != 0,
                gpu_timings.gaussian_blur_submission_nanoseconds);
        }
    }
    if (gpu_composition_enabled_ && !try_gpu_effects &&
        cpu_color_adjustment_fallbacks != 0 && record_preview_metrics_) {
        diagnostics::PerformanceMetrics::instance().recordGpuColorAdjustment(
            0, cpu_color_adjustment_fallbacks, false, 0);
    }
    if (gpu_composition_enabled_ && !try_gpu_effects &&
        cpu_gaussian_blur_fallbacks != 0 && record_preview_metrics_) {
        diagnostics::PerformanceMetrics::instance().recordGpuGaussianBlur(
            0, cpu_gaussian_blur_fallbacks, false, 0);
    }
    if(mask_gpu_in_use)return cpu_mask_recovery();
    for (const auto& pending : pending_gpu_effects) {
        if (should_cancel && should_cancel()) return {};
        try {
            auto processed = std::make_shared<creative_suite::media::RgbaFrame>(
                *pending.source);
            const EffectTimingRecorder effect_timing_recorder =
                [record = record_preview_metrics_](
                    LayerEffectKind effect, std::uint64_t duration_nanoseconds) {
                    if (!record) return;
                    const auto kind = effect == LayerEffectKind::GaussianBlur
                        ? diagnostics::PreviewEffectKind::GaussianBlur
                        : diagnostics::PreviewEffectKind::ColorAdjustment;
                    diagnostics::PerformanceMetrics::instance().recordEffectTiming(
                        kind, duration_nanoseconds);
                };
            if (!applyLayerEffects(*processed, pending.effects, should_cancel,
                                   effect_timing_recorder)) return {};
            owned_frames.push_back(processed);
            composition_layers[pending.composition_index].frame = processed.get();
            composition_layers[pending.composition_index].gpu_color_adjustments.clear();
            composition_layers[pending.composition_index].gpu_effects.clear();
        } catch (const std::exception& error) {
            if (should_cancel && should_cancel()) return {};
            if (fail_on_media_error) {
                throw std::runtime_error("GPU effect fallback failed for layer " +
                    std::to_string(pending.layer_id) + ": " + error.what());
            }
            creative_suite::diagnostics::Logger::instance().log(
                creative_suite::diagnostics::Level::Error,
                record_preview_metrics_ ? "motion_preview" : "motion_export",
                "gpu_effect_cpu_fallback", error.what(),
                {{"layer_id", std::to_string(pending.layer_id)},
                 {"gpu_operation", gpu_failure_operation},
                 {"error_code", std::to_string(gpu_failure_code)}});
            return {};
        }
    }
    auto composed = creative_suite::composition::FrameCompositor::compose(
        request.canvas_size.width,
        request.canvas_size.height,
        composition_layers);
    if (!composed.has_value()) {
        if (fail_on_media_error) {
            throw std::runtime_error("The shared compositor rejected the Motion Studio canvas.");
        }
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_preview", "compose_frame",
            "The shared compositor rejected the Motion Studio canvas",
            {{"width", std::to_string(request.canvas_size.width)},
             {"height", std::to_string(request.canvas_size.height)}});
        return {};
    }
    if (gpu_composition_enabled_ && gpu_metrics_ != nullptr)
        ++gpu_metrics_->fallback_frames;
    return std::make_shared<const creative_suite::media::RgbaFrame>(std::move(*composed));
}

void CompositionFrameRenderer::reportDecodeError(
    const std::filesystem::path& path,
    std::int64_t source_frame,
    std::string cause,
    std::optional<int> error_code)
{
    creative_suite::diagnostics::Context context{
        {"path", pathForLog(path)},
        {"source_frame", source_frame >= 0 ? std::to_string(source_frame) : std::string{}}};
    if (error_code.has_value()) context.emplace_back("error_code", std::to_string(*error_code));
    creative_suite::diagnostics::Logger::instance().log(
        creative_suite::diagnostics::Level::Error,
        "motion_preview", "decode_video_frame", cause, context);
}
} // namespace motion::ui
