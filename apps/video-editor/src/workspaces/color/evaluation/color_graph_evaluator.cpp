#include "color_graph_evaluator.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace color {
namespace {

bool usable(const media::VideoFrame& frame) {
    return frame.width > 0 && frame.height > 0 && frame.stride >= frame.width * 4 &&
        frame.rgba_pixels.size() >= static_cast<std::size_t>(frame.stride) * frame.height;
}

const Connection* incoming(const Graph& graph, NodeId id, std::uint32_t input) {
    const auto found = std::find_if(graph.connections.begin(), graph.connections.end(),
        [id, input](const Connection& edge) { return edge.to == id && edge.input == input; });
    return found == graph.connections.end() ? nullptr : &*found;
}

std::vector<const Connection*> inputs(const Graph& graph, NodeId id) {
    std::vector<const Connection*> result;
    for (const auto& edge : graph.connections)
        if (edge.to == id) result.push_back(&edge);
    std::sort(result.begin(), result.end(), [](const auto* left, const auto* right) {
        return left->input < right->input;
    });
    return result;
}

std::optional<media::VideoFrame> mix(
    const std::vector<std::pair<const media::VideoFrame*, double>>& branches) {
    if (branches.empty()) return std::nullopt;
    if (branches.size() == 1) return *branches.front().first;
    double weight_total = 0.0;
    for (const auto& [frame, weight] : branches) {
        if (!frame || !usable(*frame)) return std::nullopt;
        weight_total += weight;
    }
    if (weight_total <= 0.0) return std::nullopt;
    const auto& first = *branches.front().first;
    for (const auto& [frame, unused_weight] : branches) {
        static_cast<void>(unused_weight);
        if (frame->width != first.width || frame->height != first.height ||
            frame->stride != first.stride) return std::nullopt;
    }
    media::VideoFrame result = first;
    for (int y = 0; y < result.height; ++y) {
        auto* target = result.rgba_pixels.data() +
            static_cast<std::size_t>(y) * static_cast<std::size_t>(result.stride);
        for (int x = 0; x < result.width; ++x) {
            for (int channel = 0; channel < 4; ++channel) {
                double value = 0.0;
                for (const auto& [frame, weight] : branches) {
                    const auto offset = static_cast<std::size_t>(y) * frame->stride +
                        static_cast<std::size_t>(x) * 4 + static_cast<std::size_t>(channel);
                    value += frame->rgba_pixels[offset] * weight;
                }
                target[static_cast<std::size_t>(x) * 4 + static_cast<std::size_t>(channel)] =
                    static_cast<std::uint8_t>(std::clamp(
                        std::lround(value / weight_total), 0L, 255L));
            }
        }
    }
    return result;
}

}  // namespace

namespace {

std::optional<media::VideoFrame> evaluateFrom(
    const Graph& graph, const media::VideoFrame& input, NodeId root,
    std::optional<NodeId> stop_before,
    const std::function<bool()>& should_cancel) {
    if (!validate(graph) || !usable(input) ||
        (should_cancel && should_cancel())) return std::nullopt;
    const auto order = evaluationOrder(graph);
    if (!order) return std::nullopt;

    std::unordered_set<NodeId> required{root};
    std::vector<NodeId> pending{root};
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        for (const auto* edge : inputs(graph, id))
            if (required.insert(edge->from).second) pending.push_back(edge->from);
    }

    std::unordered_map<NodeId, media::VideoFrame> values;
    std::unordered_map<NodeId, std::size_t> remaining_consumers;
    for (const auto& edge : graph.connections)
        if (required.contains(edge.to) && required.contains(edge.from))
            ++remaining_consumers[edge.from];

    for (const auto id : *order) {
        if (!required.contains(id)) continue;
        if (should_cancel && should_cancel()) return std::nullopt;
        const auto* node = findNode(graph, id);
        if (!node) return std::nullopt;
        if (stop_before && id == *stop_before) {
            const auto* edge = incoming(graph, id, 0);
            const auto source = edge ? values.find(edge->from) : values.end();
            return source == values.end()
                ? std::nullopt
                : std::optional<media::VideoFrame>{source->second};
        }
        if (node->type == NodeType::Input) {
            values.emplace(id, input);
        } else if (node->type == NodeType::Effect || node->type == NodeType::Curves) {
            const auto* edge = incoming(graph, id, 0);
            const auto source = edge ? values.find(edge->from) : values.end();
            if (source == values.end()) return std::nullopt;
            auto frame = source->second;
            auto effect = node->effect;
            if (node->type == NodeType::Curves) {
                effect = creative_suite::effects::makeDefaultInstance("video.curves");
                effect->enabled = node->curves->enabled;
                effect->video_curves = node->curves->parameters;
            }
            if (!effect) return std::nullopt;
            if (effect->enabled) {
                if (node->type == NodeType::Curves) {
                    creative_suite::effects::ColorAdjustmentParameters adjustment;
                    adjustment.video_curves = node->curves->parameters;
                    if (creative_suite::effects::applyColorAdjustment(
                            frame, adjustment, should_cancel) !=
                        creative_suite::effects::ProcessingResult::Completed)
                        return std::nullopt;
                } else if (!creative_suite::effects::applyStack(
                        frame, std::span<const creative_suite::effects::EffectInstance>(
                            &*effect, 1))) {
                    return std::nullopt;
                }
            }
            values.emplace(id, std::move(frame));
        } else if (node->type == NodeType::ParallelMixer) {
            std::vector<std::pair<const media::VideoFrame*, double>> branches;
            for (const auto* edge : inputs(graph, id)) {
                const auto source = values.find(edge->from);
                if (source != values.end()) branches.emplace_back(&source->second, edge->weight);
            }
            auto frame = mix(branches);
            if (!frame) return std::nullopt;
            values.emplace(id, std::move(*frame));
        } else if (node->type == NodeType::Output) {
            const auto* edge = incoming(graph, id, 0);
            const auto source = edge ? values.find(edge->from) : values.end();
            if (source == values.end()) return std::nullopt;
            values.emplace(id, source->second);
        }

        for (const auto* edge : inputs(graph, id)) {
            auto remaining = remaining_consumers.find(edge->from);
            if (remaining != remaining_consumers.end() && --remaining->second == 0 &&
                edge->from != root) values.erase(edge->from);
        }
    }
    const auto result = values.find(root);
    return result == values.end()
        ? std::nullopt
        : std::optional<media::VideoFrame>{std::move(result->second)};
}

}  // namespace

std::optional<media::VideoFrame> evaluate(
    const Graph& graph, const media::VideoFrame& input,
    const std::function<bool()>& should_cancel) {
    const auto output_node = std::find_if(graph.nodes.begin(), graph.nodes.end(),
        [](const Node& node) { return node.type == NodeType::Output; });
    if (output_node == graph.nodes.end()) return std::nullopt;
    return evaluateFrom(graph, input, output_node->id, {}, should_cancel);
}

std::optional<media::VideoFrame> evaluateInput(
    const Graph& graph, NodeId curves_node, const media::VideoFrame& input,
    const std::function<bool()>& should_cancel) {
    const auto* node = findNode(graph, curves_node);
    if (!node || node->type != NodeType::Curves || !incoming(graph, curves_node, 0))
        return std::nullopt;
    return evaluateFrom(graph, input, curves_node, curves_node, should_cancel);
}

std::optional<std::vector<creative_suite::effects::EffectInstance>>
linearEffectStackBefore(const Graph& graph, NodeId curves_node) {
    if (!validate(graph)) return std::nullopt;
    const auto* selected = findNode(graph, curves_node);
    if (!selected || selected->type != NodeType::Curves || !selected->curves)
        return std::nullopt;
    std::vector<creative_suite::effects::EffectInstance> reversed;
    std::unordered_set<NodeId> visited;
    NodeId current = curves_node;
    while (visited.insert(current).second) {
        const auto* edge = incoming(graph, current, 0);
        if (!edge) return std::nullopt;
        const auto* previous = findNode(graph, edge->from);
        if (!previous) return std::nullopt;
        if (previous->type == NodeType::Input) {
            std::reverse(reversed.begin(), reversed.end());
            return reversed;
        }
        if (previous->type == NodeType::Effect && previous->effect) {
            reversed.push_back(*previous->effect);
        } else if (previous->type == NodeType::Curves && previous->curves) {
            auto effect = creative_suite::effects::makeDefaultInstance("video.curves");
            effect.enabled = previous->curves->enabled;
            effect.video_curves = previous->curves->parameters;
            reversed.push_back(std::move(effect));
        } else {
            return std::nullopt;
        }
        current = previous->id;
    }
    return std::nullopt;
}

}  // namespace color
