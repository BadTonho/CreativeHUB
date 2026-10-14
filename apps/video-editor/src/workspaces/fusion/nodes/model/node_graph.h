#pragma once

#include "timeline/timeline_transform.h"
#include "timeline/text_style.h"
#include <creative_suite/effects/effects.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fusion::nodes {

using NodeId = std::uint64_t;

enum class NodeType { Input, Transform, Color, Merge, Output, Effect, Background, Text, RectangleMask, EllipseMask };

struct ColorParameters {
    double brightness = 0.0;
    double contrast_percent = 100.0;
    double saturation_percent = 100.0;
    friend bool operator==(const ColorParameters&, const ColorParameters&) = default;
};

struct EffectParameterKeyframes {
    std::string parameter_id;
    creative_suite::animation::ScalarKeyframes keyframes;
    friend bool operator==(const EffectParameterKeyframes&,
                           const EffectParameterKeyframes&) = default;
};

enum class PortKind { Image, Mask };
struct InputPort { std::uint8_t index; PortKind kind; const char* label; };
[[nodiscard]] std::vector<InputPort> inputPorts(NodeType);
[[nodiscard]] std::optional<PortKind> outputPort(NodeType) noexcept;
[[nodiscard]] bool isMaskNode(NodeType) noexcept;

// Geometry is normalized to the receiving image. Curves use clip-local frames.
struct MaskParameters {
    double center_x = 0.5, center_y = 0.5, width = 0.5, height = 0.5;
    double rotation = 0.0, soft_edge = 0.0;
    bool invert = false;
    friend bool operator==(const MaskParameters&, const MaskParameters&) = default;
};
using MaskKeyframes = std::array<creative_suite::animation::ScalarKeyframes, 5>;
[[nodiscard]] double maskValue(const MaskParameters&, std::size_t) noexcept;
void setMaskValue(MaskParameters&, std::size_t, double) noexcept;
[[nodiscard]] MaskParameters evaluateMask(const MaskParameters&, const MaskKeyframes&, std::int64_t);

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
    timeline::TransformKeyframes transform_keyframes;
    ColorParameters color;
    creative_suite::effects::EffectInstance effect;
    std::vector<EffectParameterKeyframes> effect_parameter_keyframes;
    std::array<std::uint8_t, 4> background_color{0, 0, 0, 0};
    timeline::TextStyle text;
    MaskParameters mask;
    MaskKeyframes mask_keyframes;
    friend bool operator==(const Node&, const Node&) = default;
};

struct Connection {
    NodeId from = 0;
    NodeId to = 0;
    // Merge input 0 is background; input 1 is foreground. Image input is 0; the optional typed Mask input is 2.
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
[[nodiscard]] NodeGraph makeCompositionGraph();
[[nodiscard]] GraphValidation validate(const NodeGraph& graph);
[[nodiscard]] bool validKeyframeRange(
    const NodeGraph& graph, std::int64_t duration_frames) noexcept;
[[nodiscard]] std::optional<NodeGraph> connect(
    const NodeGraph& graph, NodeId from, NodeId to, std::uint8_t input);
[[nodiscard]] std::optional<NodeGraph> disconnect(
    const NodeGraph& graph, NodeId to, std::uint8_t input);
[[nodiscard]] std::optional<std::vector<NodeId>> evaluationOrder(
    const NodeGraph& graph);
[[nodiscard]] const Node* findNode(const NodeGraph& graph, NodeId id) noexcept;
[[nodiscard]] bool isVisualNode(NodeType type) noexcept;

} // namespace fusion::nodes
