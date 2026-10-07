#pragma once

#include "timeline/timeline_transform.h"
#include <creative_suite/effects/effects.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fusion::nodes {

using NodeId = std::uint64_t;

enum class NodeType { Input, Transform, Color, Merge, Output };

struct ColorParameters {
    double brightness = 0.0;
    double contrast_percent = 100.0;
    double saturation_percent = 100.0;
    friend bool operator==(const ColorParameters&, const ColorParameters&) = default;
};

struct Node {
    NodeId id = 0;
    NodeType type = NodeType::Input;
    double x = 0.0;
    double y = 0.0;
    // Empty Input path means the selected Timeline clip. Other paths must be
    // project media and are resolved by the playback/export boundary.
    std::filesystem::path source_path;
    double source_frame_rate = 30.0;
    std::int64_t source_frame_count = 0;
    bool source_is_still = false;
    timeline::Transform2D transform;
    ColorParameters color;
    friend bool operator==(const Node&, const Node&) = default;
};

struct Connection {
    NodeId from = 0;
    NodeId to = 0;
    // Merge input 0 is background; input 1 is foreground. All other nodes use 0.
    std::uint8_t input = 0;
    friend bool operator==(const Connection&, const Connection&) = default;
};

struct NodeGraph {
    std::vector<Node> nodes;
    std::vector<Connection> connections;
    NodeId next_id = 1;
    friend bool operator==(const NodeGraph&, const NodeGraph&) = default;
};

enum class GraphError {
    None, MissingInput, MissingOutput, DuplicateId, InvalidParameter,
    InvalidConnection, IncompatibleInput, InputAlreadyConnected, Cycle,
    MultipleOutputs
};

struct GraphValidation {
    GraphError error = GraphError::None;
    NodeId node_id = 0;
    [[nodiscard]] explicit operator bool() const noexcept {
        return error == GraphError::None;
    }
};

[[nodiscard]] NodeGraph makePassthroughGraph();
[[nodiscard]] GraphValidation validate(const NodeGraph& graph);
[[nodiscard]] std::optional<NodeGraph> connect(
    const NodeGraph& graph, NodeId from, NodeId to, std::uint8_t input);
[[nodiscard]] std::optional<NodeGraph> disconnect(
    const NodeGraph& graph, NodeId to, std::uint8_t input);
[[nodiscard]] std::optional<std::vector<NodeId>> evaluationOrder(
    const NodeGraph& graph);
[[nodiscard]] const Node* findNode(const NodeGraph& graph, NodeId id) noexcept;
[[nodiscard]] bool isVisualNode(NodeType type) noexcept;

} // namespace fusion::nodes
