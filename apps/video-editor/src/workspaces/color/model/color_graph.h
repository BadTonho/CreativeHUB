#pragma once

#include <creative_suite/effects/effects.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace color {

using NodeId = std::uint64_t;

enum class NodeType {
    Input,
    Effect,
    ParallelMixer,
    Output,
};

struct Node {
    NodeId id = 0;
    NodeType type = NodeType::Input;
    double x = 0.0;
    double y = 0.0;
    std::optional<creative_suite::effects::EffectInstance> effect;

    friend bool operator==(const Node&, const Node&) = default;
};

struct Connection {
    NodeId from = 0;
    NodeId to = 0;
    std::uint32_t input = 0;
    double weight = 1.0;

    friend bool operator==(const Connection&, const Connection&) = default;
};

struct Graph {
    NodeId next_id = 1;
    std::vector<Node> nodes;
    std::vector<Connection> connections;

    friend bool operator==(const Graph&, const Graph&) = default;
};

[[nodiscard]] Graph makePassthroughGraph();
[[nodiscard]] Graph fromEffectStack(
    std::span<const creative_suite::effects::EffectInstance> effects);
[[nodiscard]] bool validate(const Graph& graph) noexcept;
[[nodiscard]] std::optional<std::vector<NodeId>> evaluationOrder(
    const Graph& graph);
[[nodiscard]] std::optional<std::vector<creative_suite::effects::EffectInstance>>
linearEffectStack(const Graph& graph);
[[nodiscard]] std::vector<NodeId> effectNodesInOrder(const Graph& graph);
[[nodiscard]] std::vector<creative_suite::effects::EffectInstance>
effectInstancesInOrder(const Graph& graph);
[[nodiscard]] const Node* findNode(const Graph& graph, NodeId id) noexcept;
[[nodiscard]] Node* findNode(Graph& graph, NodeId id) noexcept;

[[nodiscard]] bool insertEffectAfter(
    Graph& graph, NodeId anchor, creative_suite::effects::EffectInstance effect);
[[nodiscard]] bool addParallelBranch(
    Graph& graph, NodeId anchor, creative_suite::effects::EffectInstance effect);
[[nodiscard]] bool connect(
    Graph& graph, NodeId from, NodeId to, std::uint32_t input,
    std::optional<double> weight = {});
[[nodiscard]] bool removeEffect(Graph& graph, NodeId id);
[[nodiscard]] bool removeParallelMixer(Graph& graph, NodeId id);
[[nodiscard]] bool moveNode(Graph& graph, NodeId id, double x, double y);
[[nodiscard]] bool setEffectEnabled(Graph& graph, NodeId id, bool enabled);
[[nodiscard]] bool setEffectParameter(
    Graph& graph, NodeId id, std::string_view parameter, double value);
[[nodiscard]] bool setMixerInputWeight(
    Graph& graph, NodeId id, std::uint32_t input, double weight);

}  // namespace color
