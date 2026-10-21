#include "node_graph_evaluator.h"
#include <creative_suite/effects/gaussian_blur.h>
#include <creative_suite/composition_exchange/raster.h>
#include "mask_rasterizer.h"

#include <creative_suite/effects/effects.h>
#include <creative_suite/diagnostics/logger.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace fusion::nodes {

bool hasFramePayload(const EvaluatedGraphFrame& frame) noexcept {
    return frame.rgba.has_value() || static_cast<bool>(frame.texture);
}
namespace {
const Connection* incoming(const NodeGraph& graph, NodeId id, std::uint8_t input) {
    const auto it = std::find_if(graph.connections.begin(), graph.connections.end(),
        [id, input](const Connection& edge) { return edge.to == id && edge.input == input; });
    return it == graph.connections.end() ? nullptr : &*it;
}

bool usable(const media::VideoFramePtr& frame) {
    return frame && frame->width > 0 && frame->height > 0 &&
        frame->stride >= frame->width * 4 &&
        frame->rgba_pixels.size() >= static_cast<std::size_t>(frame->stride) * frame->height;
}

media::VideoFrame transformFrame(const media::VideoFrame& source,
                                 const timeline::Transform2D& transform) {
    media::VideoFrame result{source.width, source.height, source.width * 4,
        std::vector<std::uint8_t>(static_cast<std::size_t>(source.width) * source.height * 4, 0)};
    const double cx = transform.position_x * source.width;
    const double cy = transform.position_y * source.height;
    const double c = std::cos(-transform.rotation_degrees * 3.14159265358979323846 / 180.0);
    const double s = std::sin(-transform.rotation_degrees * 3.14159265358979323846 / 180.0);
    for (int y = 0; y < result.height; ++y) {
        for (int x = 0; x < result.width; ++x) {
            const double dx = static_cast<double>(x) + 0.5 - cx;
            const double dy = static_cast<double>(y) + 0.5 - cy;
            const auto sx = static_cast<int>(std::floor((c * dx - s * dy) / transform.scale + source.width * 0.5));
            const auto sy = static_cast<int>(std::floor((s * dx + c * dy) / transform.scale + source.height * 0.5));
            if (sx < 0 || sy < 0 || sx >= source.width || sy >= source.height) continue;
            const auto source_offset = static_cast<std::size_t>(sy) * source.stride + static_cast<std::size_t>(sx) * 4;
            const auto target_offset = static_cast<std::size_t>(y) * result.stride + static_cast<std::size_t>(x) * 4;
            std::copy_n(source.rgba_pixels.begin() + static_cast<std::ptrdiff_t>(source_offset), 4,
                result.rgba_pixels.begin() + static_cast<std::ptrdiff_t>(target_offset));
            result.rgba_pixels[target_offset + 3] = static_cast<std::uint8_t>(std::lround(
                result.rgba_pixels[target_offset + 3] * std::clamp(transform.opacity, 0.0, 1.0)));
        }
    }
    return result;
}

media::VideoFrame mergeFrames(const media::VideoFrame& background,
                              const media::VideoFrame& foreground) {
    // Merge is an intermediate graph operation, so its straight alpha must
    // survive until the shared Timeline compositor blends the clip with tracks
    // beneath it. The Timeline compositor itself starts on opaque black.
    media::VideoFrame result = background;
    const double scale = std::min(
        static_cast<double>(background.width) / foreground.width,
        static_cast<double>(background.height) / foreground.height);
    const int fitted_width = std::max(1, static_cast<int>(std::lround(foreground.width * scale)));
    const int fitted_height = std::max(1, static_cast<int>(std::lround(foreground.height * scale)));
    const int left = (background.width - fitted_width) / 2;
    const int top = (background.height - fitted_height) / 2;
    for (int y = std::max(0, top); y < std::min(background.height, top + fitted_height); ++y)
    for (int x = std::max(0, left); x < std::min(background.width, left + fitted_width); ++x) {
        const int sx = std::clamp(static_cast<int>((x - left) / scale), 0, foreground.width - 1);
        const int sy = std::clamp(static_cast<int>((y - top) / scale), 0, foreground.height - 1);
        const auto b = static_cast<std::size_t>(y) * background.stride + static_cast<std::size_t>(x) * 4;
        const auto f = static_cast<std::size_t>(sy) * foreground.stride + static_cast<std::size_t>(sx) * 4;
        const double fa = foreground.rgba_pixels[f + 3] / 255.0;
        const double ba = background.rgba_pixels[b + 3] / 255.0;
        const double out_a = fa + ba * (1.0 - fa);
        for (int channel = 0; channel < 3; ++channel) {
            const double premultiplied = foreground.rgba_pixels[f + channel] * fa +
                background.rgba_pixels[b + channel] * ba * (1.0 - fa);
            result.rgba_pixels[b + channel] = out_a > 0.0
                ? static_cast<std::uint8_t>(std::clamp(std::lround(premultiplied / out_a), 0L, 255L)) : 0;
        }
        result.rgba_pixels[b + 3] = static_cast<std::uint8_t>(std::clamp(std::lround(out_a * 255.0), 0L, 255L));
    }
    return result;
}

media::VideoFramePtr maskCoverage(MaskRasterCache& cache, const Node& node,
    int width, int height, const EvaluationContext& context) {
    auto frame=cache.coverage(node.type,evaluateMask(node.mask,node.mask_keyframes,context.local_frame),
        width,height,context.should_cancel);
    if (!frame && !(context.should_cancel && context.should_cancel()))
        creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,
            "fusion-mask","raster-coverage","Mask coverage exceeds the supported geometry or 64 MiB buffer limit.",
            {{"node_id",std::to_string(node.id)},{"width",std::to_string(width)},
             {"height",std::to_string(height)},{"local_frame",std::to_string(context.local_frame)},
             {"error_code","resource-limit"}});
    return frame;
}

creative_suite::composition::OpenGlTextureFramePtr evaluateGpu(const NodeGraph& graph, const InputFrames& inputs,
    NodeId target, EvaluationContext context) {
    using namespace creative_suite::composition;
    const auto order = evaluationOrder(graph);
    if (!order || !context.gpu) return {};
    try {
    auto batch=context.gpu->beginCommandBatch(context.should_cancel);
    std::unordered_set<NodeId> required{target};
    std::vector<NodeId> pending{target};
    while (!pending.empty()) {
        const auto id = pending.back(); pending.pop_back();
        for (const auto& edge : graph.connections)
            if (edge.to == id && required.insert(edge.from).second) pending.push_back(edge.from);
    }
    int width = context.canvas_width, height = context.canvas_height;
    const auto select = [&](bool selected, bool only_required) {
        for (const auto& node : graph.nodes) {
            if ((node.type != NodeType::Input && node.type != NodeType::Background && node.type != NodeType::Text) || (only_required && !required.contains(node.id)) ||
                (selected && !node.source_path.empty())) continue;
            const auto input = inputs.find(node.id);
            const auto native = context.native_inputs ? context.native_inputs->find(node.id) : NativeInputFrames::const_iterator{};
            if (context.native_inputs && native != context.native_inputs->end() && native->second) {
                width = native->second->width(); height = native->second->height(); return true;
            }
            if (input != inputs.end() && usable(input->second)) {
                width = input->second->width; height = input->second->height; return true;
            }
        }
        return false;
    };
    if ((width <= 0 || height <= 0) && !select(true, true) && !select(true, false) && !select(false, true)) (void)select(false, false);
    if (width <= 0 || height <= 0) return {};
    const media::VideoFrame transparent{width, height, width * 4,
        std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)};
    MaskRasterCache mask_cache;
    std::unordered_map<NodeId, OpenGlTextureFramePtr> values;
    std::unordered_map<NodeId, unsigned> remaining;
    for (const auto& edge : graph.connections) if (required.contains(edge.to)) ++remaining[edge.from];
    for (const auto id : *order) {
        if (!required.contains(id)) continue;
        if (context.should_cancel && context.should_cancel()) return {};
        const auto* node = findNode(graph, id);
        if (!node) return {};
        if (isMaskNode(node->type) && id!=target) continue;
        const auto input = [&](std::uint8_t port) {
            const auto* edge = incoming(graph, id, port);
            const auto found = edge ? values.find(edge->from) : values.end();
            return found == values.end() ? OpenGlImageInput{&transparent} : OpenGlImageInput{nullptr, found->second};
        };
        OpenGlImageOperation operation;
        operation.background = input(0);
        media::VideoFramePtr mask_view;
        if (isMaskNode(node->type)) {
            mask_view=maskCoverage(mask_cache,*node,width,height,context);
            if (!mask_view) return {};
            operation.background={mask_view.get()};
        }
        if ((node->type == NodeType::Input || node->type == NodeType::Background || node->type == NodeType::Text)) {
            const auto found = inputs.find(id);
            operation.background = {found != inputs.end() && usable(found->second) ? found->second.get() : &transparent};
            if (context.native_inputs) {
                const auto native = context.native_inputs->find(id);
                if (native != context.native_inputs->end()) operation.background.native = native->second;
            }
        } else if (node->type == NodeType::Transform) {
            operation.kind = OpenGlImageOperationKind::Transform;
            operation.transform = timeline::evaluateTransform(node->transform, node->transform_keyframes, context.local_frame);
            if (node->presentation && node->presentation->profile == creative_suite::composition_exchange::Profile::MotionLayer) {
                operation.output_width = width; operation.output_height = height;
                if (node->presentation->text_width == 0)
                    operation.transform.scale *= std::min(double(width)/operation.background.width(), double(height)/operation.background.height());
            }
        } else if (node->type == NodeType::Color) {
            operation.kind = OpenGlImageOperationKind::Color;
            if (node->color.enabled) operation.colors.push_back({node->color.brightness, node->color.contrast_percent, node->color.saturation_percent});
        } else if (node->type == NodeType::Effect) {
            auto effect = node->effect;
            for (const auto& parameter : node->effect_parameter_keyframes) {
                const auto base = creative_suite::effects::parameterValue(effect, parameter.parameter_id);
                const auto value = creative_suite::animation::evaluateScalar(base, parameter.keyframes, context.local_frame);
                if (!creative_suite::effects::setParameterValue(effect, parameter.parameter_id, value)) return {};
            }
            auto passes = creative_suite::effects::colorAdjustmentPasses(std::span(&effect, 1));
            if (!passes) return {};
            operation.kind = OpenGlImageOperationKind::Color;
            operation.colors = std::move(*passes);
        } else if (node->type == NodeType::GaussianBlur) {
            if (node->gaussian_blur.enabled) {
                operation.kind=OpenGlImageOperationKind::GaussianBlur;
                operation.blur_radius_pixels=node->gaussian_blur.radius_pixels;
            }
        } else if (node->type == NodeType::Merge) {
            operation.kind = OpenGlImageOperationKind::Merge; operation.foreground = input(1);
        } else if (node->type == NodeType::Output && operation.background.texture) {
            values.emplace(id, operation.background.texture);
        }
        if (!values.contains(id)) {
            OpenGlCompositionTimings measured;
            auto rendered = context.gpu->processImage(operation, context.should_cancel, &measured);
            if (context.gpu_timings) {
                auto& total = *context.gpu_timings;
                total.upload_nanoseconds += measured.upload_nanoseconds;
                total.uploaded_bytes += measured.uploaded_bytes;
                total.uploaded_layers += measured.uploaded_layers;
                total.draw_submission_nanoseconds += measured.draw_submission_nanoseconds;
                total.gaussian_blur_count += measured.gaussian_blur_count;
                total.gaussian_blur_submission_nanoseconds += measured.gaussian_blur_submission_nanoseconds;
                total.native_video_imports += measured.native_video_imports;
                total.native_video_conversion_nanoseconds += measured.native_video_conversion_nanoseconds;
            }
            if (rendered.status != OpenGlCompositionStatus::Complete || !rendered.frame) {
                if (rendered.status != OpenGlCompositionStatus::Cancelled) {
                    creative_suite::diagnostics::Logger::instance().log(
                        rendered.status == OpenGlCompositionStatus::Failed ? creative_suite::diagnostics::Level::Error : creative_suite::diagnostics::Level::Warning,
                        "fusion-gpu", rendered.operation.empty() ? "graph-resources" : rendered.operation,
                        rendered.cause.empty() ? "The bounded GPU graph resources are occupied." : rendered.cause,
                        {{"node_id", std::to_string(id)}, {"local_frame", std::to_string(context.local_frame)},
                         {"error_code", std::to_string(rendered.error_code)}, {"fallback", "complete-cpu-graph"}});
                }
                return {};
            }
            values.emplace(id, std::move(rendered.frame));
        }
        if (const auto* edge=incoming(graph,id,2)) {
            const auto* mask_node=findNode(graph,edge->from);
            auto original=operation.background;
            const auto& processed=values.at(id);
            auto coverage=maskCoverage(mask_cache,*mask_node,processed->width(),processed->height(),context);
            if (!coverage) return {};
            // Foreground has already been consumed by Merge; release its last
            // lease before reserving the masked result in the bounded pool.
            if (const auto* foreground=incoming(graph,id,1); foreground && remaining[foreground->from]==1 && foreground->from!=target)
                values.erase(foreground->from);
            operation.foreground={};
            OpenGlImageOperation masked;
            masked.mask={coverage.get()};
            const bool blend=node->type==NodeType::Transform || node->type==NodeType::Color ||
                node->type==NodeType::Effect || node->type==NodeType::GaussianBlur || node->type==NodeType::Merge;
            masked.kind=blend ? OpenGlImageOperationKind::MaskedBlend : OpenGlImageOperationKind::ApplyMask;
            OpenGlTextureFramePtr normalized_original;
            if(blend && (original.width()!=processed->width() || original.height()!=processed->height())) {
                OpenGlImageOperation align;align.kind=OpenGlImageOperationKind::Transform;align.background=original;
                align.output_width=processed->width();align.output_height=processed->height();
                if(node->presentation && node->presentation->profile==creative_suite::composition_exchange::Profile::MotionLayer && node->presentation->text_width==0)
                    align.transform.scale*=std::min(double(align.output_width)/original.width(),double(align.output_height)/original.height());
                auto aligned=context.gpu->processImage(align,context.should_cancel);
                if(aligned.status!=OpenGlCompositionStatus::Complete || !aligned.frame)return {};
                normalized_original=std::move(aligned.frame);original={nullptr,normalized_original};
            }
            masked.background=blend ? original : OpenGlImageInput{nullptr,processed};
            if (blend) masked.foreground={nullptr,processed};
            OpenGlCompositionTimings measured;
            auto result=context.gpu->processImage(masked,context.should_cancel,&measured);
            if (context.gpu_timings) {
                context.gpu_timings->uploaded_bytes+=measured.uploaded_bytes;
                context.gpu_timings->uploaded_layers+=measured.uploaded_layers;
                context.gpu_timings->upload_nanoseconds+=measured.upload_nanoseconds;
                context.gpu_timings->draw_submission_nanoseconds+=measured.draw_submission_nanoseconds;
            }
            if (result.status!=OpenGlCompositionStatus::Complete || !result.frame) {
                if (result.status!=OpenGlCompositionStatus::Cancelled)
                    creative_suite::diagnostics::Logger::instance().log(result.status==OpenGlCompositionStatus::Failed ? creative_suite::diagnostics::Level::Error : creative_suite::diagnostics::Level::Warning,
                        "fusion-gpu",result.operation,result.cause,{{"node_id",std::to_string(id)},
                        {"error_code",std::to_string(result.error_code)},{"fallback","complete-cpu-graph"}});
                return {};
            }
            values[id]=std::move(result.frame);
        }
        // Release intermediates after their last required edge, including
        // branched inputs and Output aliases, before allocating the next node.
        for (const auto& edge : graph.connections)
            if (edge.to == id && --remaining[edge.from] == 0 && edge.from != target) values.erase(edge.from);
    }
    const auto final = values.find(target);
    if (final == values.end()) return {};
    if (context.gpu_used) *context.gpu_used = true;
    if(batch){
        auto result=batch->finish();
        if(result.status!=OpenGlCompositionStatus::Complete){
            creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,
                "fusion-gpu",result.operation,result.cause,{{"error_code",std::to_string(result.error_code)},{"fallback","complete-cpu-graph"}});
            if(context.gpu_used)*context.gpu_used=false;return {};
        }
    }
    return final->second;
    }catch(const std::exception& error){
        if(!(context.should_cancel && context.should_cancel()))
            creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,
                "fusion-gpu","record-graph",error.what(),{{"fallback","complete-cpu-graph"}});
        if(context.gpu_used)*context.gpu_used=false;return {};
    }
}
}

std::optional<media::VideoFrame> evaluate(const NodeGraph& graph, const InputFrames& inputs) {
    const auto output = std::find_if(graph.nodes.begin(), graph.nodes.end(),
        [](const Node& node) { return node.type == NodeType::Output; });
    if (output == graph.nodes.end()) return std::nullopt;
    return evaluate(graph, inputs, output->id, EvaluationContext{});
}

std::optional<EvaluatedGraphFrame> evaluateFrame(const NodeGraph& graph, const InputFrames& inputs,
    EvaluationContext context) {
    const auto output = std::find_if(graph.nodes.begin(), graph.nodes.end(),
        [](const Node& node) { return node.type == NodeType::Output; });
    if (output == graph.nodes.end()) return std::nullopt;
    return evaluateFrame(graph, inputs, output->id, context);
}
std::optional<EvaluatedGraphFrame> evaluateFrame(const NodeGraph& graph, const InputFrames& inputs,
    NodeId target, EvaluationContext context) {
    if (context.gpu_used) *context.gpu_used = false;
    if (!validate(graph) || !findNode(graph, target) || (context.should_cancel && context.should_cancel())) return std::nullopt;
    if (context.gpu) {
        if (auto output = evaluateGpu(graph, inputs, target, context))
            return EvaluatedGraphFrame{{}, std::move(output)};
        if (context.should_cancel && context.should_cancel()) return std::nullopt;
    }
    context.gpu = nullptr;
    auto cpu = evaluate(graph, inputs, target, context);
    if (!cpu) return std::nullopt;
    return EvaluatedGraphFrame{std::move(cpu), {}};
}

std::optional<media::VideoFrame> evaluate(const NodeGraph& graph,
                                          const InputFrames& inputs,
                                          NodeId target_node) {
    return evaluate(graph, inputs, target_node, EvaluationContext{});
}

std::optional<media::VideoFrame> evaluate(
    const NodeGraph& graph, const InputFrames& inputs,
    EvaluationContext context) {
    const auto output = std::find_if(graph.nodes.begin(), graph.nodes.end(),
        [](const Node& node) { return node.type == NodeType::Output; });
    if (output == graph.nodes.end()) return std::nullopt;
    return evaluate(graph, inputs, output->id, context);
}

std::optional<media::VideoFrame> evaluate(const NodeGraph& graph,
                                          const InputFrames& inputs,
                                          NodeId target_node,
                                          EvaluationContext context) {
    if (!validate(graph)) return std::nullopt;
    if (findNode(graph, target_node) == nullptr) return std::nullopt;
    if (context.gpu_used) *context.gpu_used = false;
    if (context.gpu) {
        if (auto output = evaluateGpu(graph, inputs, target_node, context)) {
            auto recovered = context.gpu->readback(output, context.should_cancel);
            if (recovered.status == creative_suite::composition::OpenGlCompositionStatus::Complete && recovered.frame)
                return std::move(recovered.frame);
            if (recovered.status != creative_suite::composition::OpenGlCompositionStatus::Cancelled)
                creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error, "fusion-gpu", recovered.operation, recovered.cause,
                    {{"error_code", std::to_string(recovered.error_code)}, {"fallback", "complete-cpu-graph"}});
            if (context.gpu_used) *context.gpu_used = false;
        }
        if (context.should_cancel && context.should_cancel()) return std::nullopt;
    }
    if (context.native_inputs && !context.native_inputs->empty()) {
        auto recovered = inputs;
        for (const auto& [id, native] : *context.native_inputs) {
            if (context.should_cancel && context.should_cancel()) return std::nullopt;
            if (native) recovered[id] = context.recover_native_input
                ? context.recover_native_input(id) : native->download_rgba();
        }
        context.gpu = nullptr; context.native_inputs = nullptr;
        return evaluate(graph, recovered, target_node, context);
    }
    const auto order = evaluationOrder(graph);
    if (!order) return std::nullopt;
    std::unordered_set<NodeId> required{target_node};
    std::vector<NodeId> pending{target_node};
    while (!pending.empty()) {
        const auto downstream = pending.back();
        pending.pop_back();
        for (const auto& edge : graph.connections) {
            if (edge.to == downstream && required.insert(edge.from).second)
                pending.push_back(edge.from);
        }
    }
    int canvas_width = context.canvas_width, canvas_height = context.canvas_height;
    const auto selectCanvas = [&](bool selected_clip_only, bool required_only) {
        for (const auto& node : graph.nodes) {
            if ((node.type != NodeType::Input && node.type != NodeType::Background && node.type != NodeType::Text) ||
                (required_only && !required.contains(node.id)) ||
                (selected_clip_only && !node.source_path.empty())) continue;
            const auto found = inputs.find(node.id);
            if (found == inputs.end() || !usable(found->second)) continue;
            canvas_width = found->second->width;
            canvas_height = found->second->height;
            return true;
        }
        return false;
    };
    if ((canvas_width <= 0 || canvas_height <= 0) && !selectCanvas(true, true) && !selectCanvas(true, false) &&
        !selectCanvas(false, true)) {
        static_cast<void>(selectCanvas(false, false));
    }
    if (canvas_width <= 0 || canvas_height <= 0) return std::nullopt;
    const auto transparent = [&] {
        return media::VideoFrame{canvas_width, canvas_height, canvas_width * 4,
            std::vector<std::uint8_t>(static_cast<std::size_t>(canvas_width) * canvas_height * 4, 0)};
    };
    MaskRasterCache mask_cache;
    std::unordered_map<NodeId, media::VideoFrame> values;
    std::unordered_map<NodeId,unsigned> remaining;
    for (const auto& edge : graph.connections) if (required.contains(edge.to)) ++remaining[edge.from];
    for (const auto id : *order) {
        if (context.should_cancel && context.should_cancel()) return std::nullopt;
        if (!required.contains(id)) continue;
        const auto* node = findNode(graph, id);
        if (!node) return std::nullopt;
        if (isMaskNode(node->type)) {
            if (id==target_node) {
                auto mask=maskCoverage(mask_cache,*node,canvas_width,canvas_height,context);
                if (!mask) return std::nullopt;
                values.emplace(id,*mask);
            }
            continue;
        }
        if ((node->type == NodeType::Input || node->type == NodeType::Background || node->type == NodeType::Text)) {
            const auto found = inputs.find(id);
            if (found != inputs.end() && usable(found->second)) values.emplace(id, *found->second);
            else values.emplace(id, transparent()); // Missing / ended sources are transparent.
            if (const auto* edge=incoming(graph,id,2)) {
                const auto* mask=findNode(graph,edge->from);
                auto& frame=values.at(id);
                auto coverage=maskCoverage(mask_cache,*mask,frame.width,frame.height,context);
                if (!coverage || !creative_suite::composition::applyMask(frame,*coverage,context.should_cancel)) return std::nullopt;
            }
            continue;
        }
        const auto* first = incoming(graph, id, 0);
        const auto input = first != nullptr ? values.find(first->from) : values.end();
        const media::VideoFrame empty_input = input == values.end()
            ? transparent() : media::VideoFrame{};
        const auto& background = input != values.end() ? input->second : empty_input;
        if (node->type == NodeType::Transform) {
            const auto transform = timeline::evaluateTransform(node->transform, node->transform_keyframes, context.local_frame);
            if (node->presentation && node->presentation->profile == creative_suite::composition_exchange::Profile::MotionLayer)
                values.emplace(id, creative_suite::composition_exchange::transformFrame(background, canvas_width, canvas_height, transform, node->presentation->profile, node->presentation->text_width > 0, context.should_cancel));
            else values.emplace(id, transformFrame(background, transform));
        } else if (node->type == NodeType::Color) {
            auto frame = background;
            if (node->color.enabled && creative_suite::effects::applyColorAdjustment(frame,
                {node->color.brightness, node->color.contrast_percent,
                 node->color.saturation_percent}) !=
                    creative_suite::effects::ProcessingResult::Completed)
                return std::nullopt;
            values.emplace(id, std::move(frame));
        } else if (node->type == NodeType::Effect) {
            auto frame = background;
            auto effect = node->effect;
            for (const auto& parameter : node->effect_parameter_keyframes) {
                const auto base_value = creative_suite::effects::parameterValue(
                    effect, parameter.parameter_id);
                const auto animated_value = creative_suite::animation::evaluateScalar(
                    base_value, parameter.keyframes, context.local_frame);
                if (!creative_suite::effects::setParameterValue(
                        effect, parameter.parameter_id, animated_value))
                    return std::nullopt;
            }
            if (!creative_suite::effects::applyStack(frame,
                    std::span<const creative_suite::effects::EffectInstance>(
                        &effect, 1)))
                return std::nullopt;
            values.emplace(id, std::move(frame));
        } else if (node->type == NodeType::GaussianBlur) {
            auto frame=background;
            if (node->gaussian_blur.enabled && creative_suite::effects::applyGaussianBlur(frame,
                    node->gaussian_blur.radius_pixels, context.should_cancel) != creative_suite::effects::ProcessingResult::Completed)
                return std::nullopt;
            values.emplace(id,std::move(frame));
        } else if (node->type == NodeType::Merge) {
            const auto* second = incoming(graph, id, 1);
            const auto foreground = second != nullptr
                ? values.find(second->from) : values.end();
            const media::VideoFrame empty_foreground = foreground == values.end()
                ? transparent() : media::VideoFrame{};
            const auto& foreground_frame = foreground != values.end()
                ? foreground->second : empty_foreground;
            values.emplace(id, mergeFrames(background, foreground_frame));
        } else if (node->type == NodeType::Output) {
            values.emplace(id, background);
        }
        if (const auto* edge=incoming(graph,id,2)) {
            const auto* mask=findNode(graph,edge->from);
            auto& frame=values.at(id);
            auto coverage=maskCoverage(mask_cache,*mask,frame.width,frame.height,context);
            if (!coverage) return std::nullopt;
            const bool blend=node->type!=NodeType::Output;
            std::optional<media::VideoFrame> aligned;
            if(blend && (background.width!=frame.width || background.height!=frame.height))
                aligned=creative_suite::composition_exchange::transformFrame(background,frame.width,frame.height,{},
                    creative_suite::composition_exchange::Profile::MotionLayer,node->presentation && node->presentation->text_width>0,context.should_cancel);
            if (!(blend ? creative_suite::composition::maskedBlend(aligned?*aligned:background,frame,*coverage,context.should_cancel)
                : creative_suite::composition::applyMask(frame,*coverage,context.should_cancel))) return std::nullopt;
        }
        for (const auto& edge : graph.connections)
            if (edge.to==id && --remaining[edge.from]==0 && edge.from!=target_node) values.erase(edge.from);
    }
    const auto found = values.find(target_node);
    return found == values.end() ? std::nullopt : std::optional<media::VideoFrame>{found->second};
}
} // namespace fusion::nodes
