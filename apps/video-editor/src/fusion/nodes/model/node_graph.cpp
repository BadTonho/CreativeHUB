#include "node_graph.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace fusion::nodes {
namespace {
const Node* find(const NodeGraph& graph, NodeId id) noexcept {
    const auto it = std::find_if(graph.nodes.begin(), graph.nodes.end(),
        [id](const Node& node) { return node.id == id; });
    return it == graph.nodes.end() ? nullptr : &*it;
}
std::uint8_t inputCount(NodeType type) noexcept {
    switch (type) {
    case NodeType::Transform:
    case NodeType::Color:
    case NodeType::Output: return 1;
    case NodeType::Merge: return 2;
    case NodeType::Input: return 0;
    }
    return 0;
}
}

NodeGraph makePassthroughGraph() {
    NodeGraph graph;
    graph.nodes = {{1, NodeType::Input, 80.0, 120.0},
                   {2, NodeType::Output, 360.0, 120.0}};
    graph.connections = {{1, 2, 0}};
    graph.next_id = 3;
    return graph;
}

const Node* findNode(const NodeGraph& graph, NodeId id) noexcept { return find(graph, id); }
bool isVisualNode(NodeType type) noexcept {
    return type == NodeType::Input || type == NodeType::Transform ||
        type == NodeType::Color || type == NodeType::Merge || type == NodeType::Output;
}

GraphValidation validate(const NodeGraph& graph) {
    std::unordered_map<NodeId, const Node*> by_id;
    std::unordered_set<NodeId> inputs;
    std::size_t output_count = 0;
    NodeId maximum_id = 0;
    for (const auto& node : graph.nodes) {
        if (node.id == 0 || node.id > static_cast<NodeId>(
                std::numeric_limits<std::int64_t>::max()) ||
            !by_id.emplace(node.id, &node).second)
            return {GraphError::DuplicateId, node.id};
        maximum_id = std::max(maximum_id, node.id);
        if (node.type != NodeType::Input && !node.source_path.empty())
            return {GraphError::InvalidParameter, node.id};
        if (!std::isfinite(node.x) || !std::isfinite(node.y) ||
            !std::isfinite(node.source_frame_rate) || node.source_frame_rate <= 0.0 ||
            node.source_frame_rate > 1000.0 ||
            node.source_frame_count < 0 ||
            !timeline::validTransform(node.transform) ||
            !std::isfinite(node.color.brightness) || node.color.brightness < -100.0 ||
            node.color.brightness > 100.0 ||
            !std::isfinite(node.color.contrast_percent) ||
            node.color.contrast_percent < 0.0 || node.color.contrast_percent > 200.0 ||
            !std::isfinite(node.color.saturation_percent) ||
            node.color.saturation_percent < 0.0 || node.color.saturation_percent > 200.0)
            return {GraphError::InvalidParameter, node.id};
        if (node.type == NodeType::Input) inputs.insert(node.id);
        if (node.type == NodeType::Output) ++output_count;
    }
    if (graph.next_id == 0 || graph.next_id <= maximum_id ||
        graph.next_id > static_cast<NodeId>(
            std::numeric_limits<std::int64_t>::max()))
        return {GraphError::InvalidParameter, graph.next_id};
    if (inputs.empty()) return {GraphError::MissingInput, 0};
    if (output_count == 0) return {GraphError::MissingOutput, 0};
    if (output_count > 1) return {GraphError::MultipleOutputs, 0};

    std::set<std::pair<NodeId, std::uint8_t>> occupied;
    for (const auto& edge : graph.connections) {
        const auto from = by_id.find(edge.from), to = by_id.find(edge.to);
        if (from == by_id.end() || to == by_id.end() || edge.from == edge.to)
            return {GraphError::InvalidConnection, edge.to};
        if (from->second->type == NodeType::Output)
            return {GraphError::InvalidConnection, edge.from};
        const auto count = inputCount(to->second->type);
        if (edge.input >= count) return {GraphError::IncompatibleInput, edge.to};
        if (!occupied.emplace(edge.to, edge.input).second)
            return {GraphError::InputAlreadyConnected, edge.to};
    }
    if (!evaluationOrder(graph)) return {GraphError::Cycle, 0};
    return {};
}

std::optional<NodeGraph> connect(const NodeGraph& graph, NodeId from, NodeId to,
                                 std::uint8_t input) {
    if (!find(graph, from) || !find(graph, to) || from == to) return std::nullopt;
    const auto count = inputCount(find(graph, to)->type);
    if (input >= count) return std::nullopt;
    NodeGraph candidate = graph;
    candidate.connections.erase(std::remove_if(candidate.connections.begin(),
        candidate.connections.end(), [to, input](const Connection& edge) {
            return edge.to == to && edge.input == input;
        }), candidate.connections.end());
    candidate.connections.push_back({from, to, input});
    if (!validate(candidate)) return std::nullopt;
    return candidate;
}

std::optional<NodeGraph> disconnect(const NodeGraph& graph, NodeId to, std::uint8_t input) {
    NodeGraph candidate = graph;
    const auto before = candidate.connections.size();
    candidate.connections.erase(std::remove_if(candidate.connections.begin(),
        candidate.connections.end(), [to, input](const Connection& edge) {
            return edge.to == to && edge.input == input;
        }), candidate.connections.end());
    if (candidate.connections.size() == before || !validate(candidate)) return std::nullopt;
    return candidate;
}

std::optional<std::vector<NodeId>> evaluationOrder(const NodeGraph& graph) {
    std::unordered_map<NodeId, std::size_t> indegree;
    for (const auto& node : graph.nodes) indegree.emplace(node.id, 0);
    for (const auto& edge : graph.connections) {
        if (!indegree.contains(edge.from) || !indegree.contains(edge.to)) return std::nullopt;
        ++indegree[edge.to];
    }
    std::vector<NodeId> ready, order;
    for (const auto& node : graph.nodes) if (indegree[node.id] == 0) ready.push_back(node.id);
    while (!ready.empty()) {
        const auto id = ready.back(); ready.pop_back(); order.push_back(id);
        for (const auto& edge : graph.connections) if (edge.from == id) {
            auto& degree = indegree[edge.to];
            if (--degree == 0) ready.push_back(edge.to);
        }
    }
    if (order.size() != graph.nodes.size()) return std::nullopt;
    return order;
}
} // namespace fusion::nodes
