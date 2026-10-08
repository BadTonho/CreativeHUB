#include "node_graph_evaluator.h"

#include <creative_suite/effects/effects.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace fusion::nodes {
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
}

std::optional<media::VideoFrame> evaluate(const NodeGraph& graph, const InputFrames& inputs) {
    const auto output = std::find_if(graph.nodes.begin(), graph.nodes.end(),
        [](const Node& node) { return node.type == NodeType::Output; });
    if (output == graph.nodes.end()) return std::nullopt;
    return evaluate(graph, inputs, output->id, EvaluationContext{});
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
    int canvas_width = 0, canvas_height = 0;
    const auto selectCanvas = [&](bool selected_clip_only, bool required_only) {
        for (const auto& node : graph.nodes) {
            if (node.type != NodeType::Input ||
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
    if (!selectCanvas(true, true) && !selectCanvas(true, false) &&
        !selectCanvas(false, true)) {
        static_cast<void>(selectCanvas(false, false));
    }
    if (canvas_width <= 0 || canvas_height <= 0) return std::nullopt;
    const auto transparent = [&] {
        return media::VideoFrame{canvas_width, canvas_height, canvas_width * 4,
            std::vector<std::uint8_t>(static_cast<std::size_t>(canvas_width) * canvas_height * 4, 0)};
    };
    std::unordered_map<NodeId, media::VideoFrame> values;
    for (const auto id : *order) {
        if (!required.contains(id)) continue;
        const auto* node = findNode(graph, id);
        if (!node) return std::nullopt;
        if (node->type == NodeType::Input) {
            const auto found = inputs.find(id);
            if (found != inputs.end() && usable(found->second)) values.emplace(id, *found->second);
            else values.emplace(id, transparent()); // Missing / ended sources are transparent.
            continue;
        }
        const auto* first = incoming(graph, id, 0);
        const auto input = first != nullptr ? values.find(first->from) : values.end();
        const media::VideoFrame empty_input = input == values.end()
            ? transparent() : media::VideoFrame{};
        const auto& background = input != values.end() ? input->second : empty_input;
        if (node->type == NodeType::Transform) {
            values.emplace(id, transformFrame(background,
                timeline::evaluateTransform(node->transform,
                    node->transform_keyframes, context.local_frame)));
        } else if (node->type == NodeType::Color) {
            auto frame = background;
            if (creative_suite::effects::applyColorAdjustment(frame,
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
    }
    const auto found = values.find(target_node);
    return found == values.end() ? std::nullopt : std::optional<media::VideoFrame>{found->second};
}
} // namespace fusion::nodes
