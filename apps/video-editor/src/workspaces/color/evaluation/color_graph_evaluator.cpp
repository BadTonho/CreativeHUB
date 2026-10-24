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

std::optional<media::VideoFrame> evaluate(
    const Graph& graph, const media::VideoFrame& input,
    const std::function<bool()>& should_cancel) {
    if (!validate(graph) || !usable(input) ||
        (should_cancel && should_cancel())) return std::nullopt;
    const auto order = evaluationOrder(graph);
    if (!order) return std::nullopt;

    const auto output_node = std::find_if(graph.nodes.begin(), graph.nodes.end(),
        [](const Node& node) { return node.type == NodeType::Output; });
    if (output_node == graph.nodes.end()) return std::nullopt;
    std::unordered_set<NodeId> required{output_node->id};
    std::vector<NodeId> pending{output_node->id};
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
            if (!effect || !creative_suite::effects::applyStack(
                    frame, std::span<const creative_suite::effects::EffectInstance>(
                        &*effect, 1))) return std::nullopt;
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
                edge->from != output_node->id) values.erase(edge->from);
        }
    }
    const auto result = values.find(output_node->id);
    return result == values.end()
        ? std::nullopt
        : std::optional<media::VideoFrame>{std::move(result->second)};
}

}  // namespace color
