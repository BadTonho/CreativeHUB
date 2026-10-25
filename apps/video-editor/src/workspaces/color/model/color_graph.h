#pragma once

#include <creative_suite/effects/effects.h>
#include <creative_suite/composition/shape_mask.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace color {

using NodeId = std::uint64_t;
using PowerWindowId = std::uint64_t;

enum class WindowCombineMode : std::uint8_t {
    Add,
    Subtract,
    Intersect,
};

enum class NodeType {
    Input,
    Effect,
    Curves,
    ParallelMixer,
    Output,
};

enum class CurveKind : std::uint8_t {
    Master,
    Red,
    Green,
    Blue,
    HueVsHue,
    HueVsSaturation,
    HueVsLuminance,
    LuminanceVsSaturation,
    SaturationVsSaturation,
};

struct CurvesNode {
    bool enabled = true;
    creative_suite::effects::VideoCurvesParameters parameters;

    friend bool operator==(const CurvesNode&, const CurvesNode&) = default;
};

struct PowerWindow {
    PowerWindowId id = 0;
    creative_suite::composition::ShapeMaskKind kind =
        creative_suite::composition::ShapeMaskKind::Rectangle;
    creative_suite::composition::ShapeMaskParameters parameters;
    creative_suite::composition::ShapeMaskKeyframes keyframes;
    WindowCombineMode combine_mode = WindowCombineMode::Add;

    friend bool operator==(const PowerWindow&, const PowerWindow&) = default;
};

struct Node {
    NodeId id = 0;
    NodeType type = NodeType::Input;
    double x = 0.0;
    double y = 0.0;
    std::optional<creative_suite::effects::EffectInstance> effect;
    std::optional<CurvesNode> curves;
    std::vector<PowerWindow> power_windows;

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
    PowerWindowId next_window_id = 1;

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

// Adds a validated, disconnected effect node at the given graph position.
// Returns no ID and leaves the graph unchanged if the effect or graph is invalid.
[[nodiscard]] std::optional<NodeId> addDisconnectedEffectNode(
    Graph& graph, creative_suite::effects::EffectInstance effect,
    double x, double y);
[[nodiscard]] bool insertEffectAfter(
    Graph& graph, NodeId anchor, creative_suite::effects::EffectInstance effect);
[[nodiscard]] bool insertCurvesAfter(Graph& graph, NodeId anchor);
[[nodiscard]] bool addParallelBranch(
    Graph& graph, NodeId anchor, creative_suite::effects::EffectInstance effect);
[[nodiscard]] bool connect(
    Graph& graph, NodeId from, NodeId to, std::uint32_t input,
    std::optional<double> weight = {});
[[nodiscard]] bool removeEffect(Graph& graph, NodeId id);
[[nodiscard]] bool removeParallelMixer(Graph& graph, NodeId id);
[[nodiscard]] bool moveNode(Graph& graph, NodeId id, double x, double y);
[[nodiscard]] bool setEffectEnabled(Graph& graph, NodeId id, bool enabled);
[[nodiscard]] bool setCurvesEnabled(Graph& graph, NodeId id, bool enabled);
[[nodiscard]] bool setCurvePoints(Graph& graph, NodeId id, CurveKind kind,
    creative_suite::effects::CurvesPointList points);
[[nodiscard]] bool setCurvesQualifier(Graph& graph, NodeId id,
    creative_suite::effects::TonalQualifier qualifier);
[[nodiscard]] std::optional<PowerWindowId> addPowerWindow(
    Graph& graph, NodeId node_id,
    creative_suite::composition::ShapeMaskKind kind);
[[nodiscard]] bool removePowerWindow(
    Graph& graph, NodeId node_id, PowerWindowId window_id);
[[nodiscard]] bool reorderPowerWindow(
    Graph& graph, NodeId node_id, std::size_t from, std::size_t to);
[[nodiscard]] bool setPowerWindowSettings(
    Graph& graph, NodeId node_id, PowerWindowId window_id,
    double soft_edge, bool invert, WindowCombineMode combine_mode);
[[nodiscard]] bool setPowerWindowGeometry(
    Graph& graph, NodeId node_id, PowerWindowId window_id,
    std::int64_t local_frame,
    const creative_suite::composition::ShapeMaskParameters& geometry);
[[nodiscard]] bool setPowerWindowTrack(
    Graph& graph, NodeId node_id, PowerWindowId window_id,
    const creative_suite::composition::ShapeMaskParameters& base,
    creative_suite::composition::ShapeMaskKeyframes keyframes);
[[nodiscard]] bool clearPowerWindowTrack(
    Graph& graph, NodeId node_id, PowerWindowId window_id);
[[nodiscard]] bool hasPowerWindows(const Graph& graph) noexcept;
[[nodiscard]] bool setEffectParameter(
    Graph& graph, NodeId id, std::string_view parameter, double value);
[[nodiscard]] bool setMixerInputWeight(
    Graph& graph, NodeId id, std::uint32_t input, double weight);

}  // namespace color
