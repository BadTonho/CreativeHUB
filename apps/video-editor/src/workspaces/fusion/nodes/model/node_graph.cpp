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

bool hasTransformKeyframes(const timeline::TransformKeyframes& keyframes) noexcept {
    return !keyframes.position_x.empty() || !keyframes.position_y.empty() ||
        !keyframes.scale.empty() || !keyframes.rotation.empty() ||
        !keyframes.opacity.empty();
}

const creative_suite::effects::ParameterDefinition* findEffectParameter(
    const creative_suite::effects::EffectInstance& effect,
    std::string_view parameter_id) noexcept {
    const auto* definition = creative_suite::effects::findDefinition(effect.id);
    if (definition == nullptr) return nullptr;
    const auto found = std::find_if(definition->parameters.begin(),
        definition->parameters.end(), [parameter_id](const auto& parameter) {
            return parameter.id == parameter_id;
        });
    return found == definition->parameters.end() ? nullptr : &*found;
}

bool validEffectParameterKeyframes(const Node& node) noexcept {
    std::set<std::string> animated_parameters;
    for (const auto& animated : node.effect_parameter_keyframes) {
        const auto* parameter = findEffectParameter(node.effect, animated.parameter_id);
        if (parameter == nullptr || !animated_parameters.insert(animated.parameter_id).second ||
            !creative_suite::animation::validScalarKeyframes(animated.keyframes)) {
            return false;
        }
        for (const auto& keyframe : animated.keyframes) {
            if (keyframe.value < parameter->minimum ||
                keyframe.value > parameter->maximum) return false;
        }
    }
    return true;
}
}

bool isMaskNode(NodeType type) noexcept {
    return type == NodeType::RectangleMask || type == NodeType::EllipseMask;
}
std::optional<PortKind> outputPort(NodeType type) noexcept {
    if (type == NodeType::Output) return std::nullopt;
    return isMaskNode(type) ? PortKind::Mask : PortKind::Image;
}
std::vector<InputPort> inputPorts(NodeType type) {
    if (isMaskNode(type)) return {};
    std::vector<InputPort> ports;
    if (type == NodeType::Merge) ports = {{0, PortKind::Image, "Background"}, {1, PortKind::Image, "Foreground"}};
    else if (type != NodeType::Input && type != NodeType::Background && type != NodeType::Text)
        ports.push_back({0, PortKind::Image, "Image"});
    ports.push_back({2, PortKind::Mask, "Mask"});
    return ports;
}
double maskValue(const MaskParameters& p, std::size_t i) noexcept {
    switch (i) { case 0: return p.center_x; case 1: return p.center_y; case 2: return p.width;
        case 3: return p.height; default: return p.rotation; }
}
void setMaskValue(MaskParameters& p, std::size_t i, double v) noexcept {
    switch (i) { case 0: p.center_x=v; break; case 1: p.center_y=v; break; case 2: p.width=v; break;
        case 3: p.height=v; break; default: p.rotation=v; break; }
}
MaskParameters evaluateMask(const MaskParameters& base, const MaskKeyframes& keys, std::int64_t frame) {
    auto p = base;
    for (std::size_t i=0; i<5; ++i) setMaskValue(p, i,
        creative_suite::animation::evaluateScalar(maskValue(base,i),keys[i],frame));
    return p;
}

NodeGraph makePassthroughGraph() {
    NodeGraph graph;
    graph.nodes = {{1, NodeType::Input, 80.0, 120.0},
                   {2, NodeType::Output, 360.0, 120.0}};
    graph.connections = {{1, 2, 0}};
    graph.next_id = 3;
    return graph;
}

NodeGraph makeCompositionGraph() {
    auto graph = makePassthroughGraph();
    graph.nodes.front().type = NodeType::Background;
    return graph;
}

const Node* findNode(const NodeGraph& graph, NodeId id) noexcept { return find(graph, id); }
bool isVisualNode(NodeType type) noexcept {
    return type == NodeType::Input || type == NodeType::Transform ||
        type == NodeType::Color || type == NodeType::Merge ||
        type == NodeType::Output || type == NodeType::Effect || type == NodeType::Background || type == NodeType::Text;
}

GraphValidation validate(const NodeGraph& graph) {
    std::unordered_map<NodeId, const Node*> by_id;
    std::size_t output_count = 0;
    NodeId maximum_id = 0;
    for (const auto& node : graph.nodes) {
        if (node.presentation && !creative_suite::composition_exchange::validPresentation(*node.presentation)) return {GraphError::InvalidParameter, node.id};
        if (!isVisualNode(node.type) && !isMaskNode(node.type)) return {GraphError::InvalidParameter, node.id};
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
        if (!creative_suite::animation::validTransformKeyframes(
                node.transform_keyframes) ||
            (node.type != NodeType::Transform &&
             hasTransformKeyframes(node.transform_keyframes)) ||
            (node.type != NodeType::Effect &&
             !node.effect_parameter_keyframes.empty()))
            return {GraphError::InvalidParameter, node.id};
        for (std::size_t i=0; i<5; ++i) {
            const double lo = i<2 ? -2.0 : i<4 ? 0.0 : -3600.0;
            const double hi = i<2 ? 3.0 : i<4 ? 4.0 : 3600.0;
            const auto v = maskValue(node.mask,i);
            if (!std::isfinite(v) || v<lo || v>hi ||
                !creative_suite::animation::validScalarKeyframes(node.mask_keyframes[i]) ||
                (!isMaskNode(node.type) && !node.mask_keyframes[i].empty())) return {GraphError::InvalidParameter,node.id};
            for (const auto& key : node.mask_keyframes[i])
                if (key.value<lo || key.value>hi) return {GraphError::InvalidParameter,node.id};
        }
        if (!std::isfinite(node.mask.soft_edge) || node.mask.soft_edge<0 || node.mask.soft_edge>1)
            return {GraphError::InvalidParameter,node.id};
        if (node.type == NodeType::Text && !timeline::isValidTextStyle(node.text))
            return {GraphError::InvalidParameter, node.id};
        if (node.type == NodeType::Output) ++output_count;
        if (node.type == NodeType::Effect &&
            (!creative_suite::effects::isValid(node.effect) ||
             !validEffectParameterKeyframes(node)))
            return {GraphError::InvalidParameter, node.id};
    }
    if (graph.next_id == 0 || graph.next_id <= maximum_id ||
        graph.next_id > static_cast<NodeId>(
            std::numeric_limits<std::int64_t>::max()))
        return {GraphError::InvalidParameter, graph.next_id};
    if (output_count == 0) return {GraphError::MissingOutput, 0};
    if (output_count > 1) return {GraphError::MultipleOutputs, 0};

    std::set<std::pair<NodeId, std::uint8_t>> occupied;
    for (const auto& edge : graph.connections) {
        const auto from = by_id.find(edge.from), to = by_id.find(edge.to);
        if (from == by_id.end() || to == by_id.end() || edge.from == edge.to)
            return {GraphError::InvalidConnection, edge.to};
        if (from->second->type == NodeType::Output)
            return {GraphError::InvalidConnection, edge.from};
        const auto ports = inputPorts(to->second->type);
        const auto port = std::find_if(ports.begin(),ports.end(),[&](const auto& p){ return p.index==edge.input; });
        if (port==ports.end() || outputPort(from->second->type)!=port->kind)
            return {GraphError::IncompatibleInput,edge.to};
        if (!occupied.emplace(edge.to, edge.input).second)
            return {GraphError::InputAlreadyConnected, edge.to};
    }
    if (!evaluationOrder(graph)) return {GraphError::Cycle, 0};
    return {};
}

bool validKeyframeRange(const NodeGraph& graph,
                        std::int64_t duration_frames) noexcept {
    if (duration_frames <= 0) return false;
    const auto valid_range = [duration_frames](const auto& keyframes) {
        return std::all_of(keyframes.begin(), keyframes.end(),
            [duration_frames](const auto& keyframe) {
                return keyframe.frame >= 0 && keyframe.frame < duration_frames;
            });
    };
    for (const auto& node : graph.nodes) {
        for (const auto& keys : node.mask_keyframes) if (!valid_range(keys)) return false;
        if (node.type == NodeType::Transform) {
            for (const auto property : {timeline::TransformProperty::PositionX,
                                        timeline::TransformProperty::PositionY,
                                        timeline::TransformProperty::Scale,
                                        timeline::TransformProperty::Rotation,
                                        timeline::TransformProperty::Opacity}) {
                if (!valid_range(timeline::keyframesFor(
                        node.transform_keyframes, property))) return false;
            }
        }
        for (const auto& parameter : node.effect_parameter_keyframes) {
            if (!valid_range(parameter.keyframes)) return false;
        }
    }
    return true;
}

std::optional<NodeGraph> connect(const NodeGraph& graph, NodeId from, NodeId to,
                                 std::uint8_t input) {
    if (!find(graph, from) || !find(graph, to) || from == to) return std::nullopt;
    const auto ports = inputPorts(find(graph,to)->type);
    const auto port = std::find_if(ports.begin(),ports.end(),[&](const auto& p){return p.index==input;});
    if (port==ports.end() || outputPort(find(graph,from)->type)!=port->kind) return std::nullopt;
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
