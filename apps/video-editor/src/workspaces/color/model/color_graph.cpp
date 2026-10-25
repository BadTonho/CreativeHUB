#include "color_graph.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace color {
namespace {

const Connection* incoming(const Graph& graph, NodeId id, std::uint32_t input) {
    const auto found = std::find_if(graph.connections.begin(), graph.connections.end(),
        [id, input](const Connection& edge) { return edge.to == id && edge.input == input; });
    return found == graph.connections.end() ? nullptr : &*found;
}

std::vector<const Connection*> incoming(const Graph& graph, NodeId id) {
    std::vector<const Connection*> result;
    for (const auto& edge : graph.connections)
        if (edge.to == id) result.push_back(&edge);
    std::sort(result.begin(), result.end(), [](const auto* left, const auto* right) {
        return left->input < right->input;
    });
    return result;
}

std::vector<const Connection*> outgoing(const Graph& graph, NodeId id) {
    std::vector<const Connection*> result;
    for (const auto& edge : graph.connections)
        if (edge.from == id) result.push_back(&edge);
    return result;
}

NodeId allocateId(Graph& graph) {
    if (graph.next_id == 0 || graph.next_id == std::numeric_limits<NodeId>::max())
        return 0;
    return graph.next_id++;
}

void eraseConnection(Graph& graph, NodeId from, NodeId to, std::uint32_t input) {
    std::erase_if(graph.connections, [=](const Connection& edge) {
        return edge.from == from && edge.to == to && edge.input == input;
    });
}

bool hasOutputPathFromInput(const Graph& graph, NodeId output) {
    std::unordered_set<NodeId> visited;
    std::vector<NodeId> pending{output};
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (!visited.insert(current).second) continue;
        const auto* node = findNode(graph, current);
        if (node && node->type == NodeType::Input) return true;
        for (const auto* edge : incoming(graph, current)) pending.push_back(edge->from);
    }
    return false;
}

}  // namespace

Graph makePassthroughGraph() {
    Graph graph;
    graph.next_id = 3;
    graph.nodes = {{1, NodeType::Input, 80.0, 120.0, std::nullopt},
                   {2, NodeType::Output, 420.0, 120.0, std::nullopt}};
    graph.connections = {{1, 2, 0, 1.0}};
    return graph;
}

Graph fromEffectStack(
    std::span<const creative_suite::effects::EffectInstance> effects) {
    Graph graph = makePassthroughGraph();
    graph.nodes.pop_back();
    graph.connections.clear();
    NodeId previous = graph.nodes.front().id;
    double x = 250.0;
    for (const auto& effect : effects) {
        const auto id = allocateId(graph);
        if (id == 0 || !creative_suite::effects::isValid(effect))
            return makePassthroughGraph();
        graph.nodes.push_back({id, NodeType::Effect, x, 120.0, effect});
        graph.connections.push_back({previous, id, 0, 1.0});
        previous = id;
        x += 170.0;
    }
    const auto output = allocateId(graph);
    if (output == 0) return makePassthroughGraph();
    graph.nodes.push_back({output, NodeType::Output, x, 120.0, std::nullopt});
    graph.connections.push_back({previous, output, 0, 1.0});
    return graph;
}

const Node* findNode(const Graph& graph, NodeId id) noexcept {
    const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(),
        [id](const Node& node) { return node.id == id; });
    return found == graph.nodes.end() ? nullptr : &*found;
}

Node* findNode(Graph& graph, NodeId id) noexcept {
    const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(),
        [id](const Node& node) { return node.id == id; });
    return found == graph.nodes.end() ? nullptr : &*found;
}

std::optional<NodeId> addDisconnectedEffectNode(
    Graph& graph, creative_suite::effects::EffectInstance effect,
    double x, double y) {
    if (!validate(graph) || !std::isfinite(x) || !std::isfinite(y) ||
        !creative_suite::effects::isValid(effect)) {
        return std::nullopt;
    }

    auto candidate = graph;
    const auto id = allocateId(candidate);
    if (id == 0) return std::nullopt;
    candidate.nodes.push_back({id, NodeType::Effect, x, y, std::move(effect)});
    if (!validate(candidate)) return std::nullopt;
    graph = std::move(candidate);
    return id;
}

std::optional<std::vector<NodeId>> evaluationOrder(const Graph& graph) {
    if (graph.nodes.empty()) return std::nullopt;
    std::unordered_map<NodeId, std::size_t> indegree;
    std::unordered_map<NodeId, std::vector<NodeId>> successors;
    for (const auto& node : graph.nodes) indegree.emplace(node.id, 0);
    for (const auto& edge : graph.connections) {
        if (!indegree.contains(edge.from) || !indegree.contains(edge.to)) return std::nullopt;
        ++indegree[edge.to];
        successors[edge.from].push_back(edge.to);
    }
    std::priority_queue<NodeId, std::vector<NodeId>, std::greater<>> ready;
    for (const auto& [id, count] : indegree)
        if (count == 0) ready.push(id);
    std::vector<NodeId> result;
    result.reserve(graph.nodes.size());
    while (!ready.empty()) {
        const auto id = ready.top();
        ready.pop();
        result.push_back(id);
        for (const auto next : successors[id])
            if (--indegree[next] == 0) ready.push(next);
    }
    if (result.size() != graph.nodes.size()) return std::nullopt;
    return result;
}

bool validate(const Graph& graph) noexcept {
    try {
        if (graph.nodes.size() < 2 || graph.next_id == 0) return false;
        std::unordered_set<NodeId> ids;
        NodeId maximum_id = 0;
        std::size_t input_count = 0;
        std::size_t output_count = 0;
        for (const auto& node : graph.nodes) {
            if (node.id == 0 || !ids.insert(node.id).second ||
                !std::isfinite(node.x) || !std::isfinite(node.y)) return false;
            maximum_id = std::max(maximum_id, node.id);
            if (node.type == NodeType::Input) {
                ++input_count;
                if (node.effect || node.curves) return false;
            } else if (node.type == NodeType::Output) {
                ++output_count;
                if (node.effect || node.curves) return false;
            } else if (node.type == NodeType::ParallelMixer) {
                if (node.effect || node.curves) return false;
            } else if (node.type == NodeType::Effect) {
                if (!node.effect || node.curves || !creative_suite::effects::isValid(*node.effect))
                    return false;
            } else if (node.type == NodeType::Curves) {
                if (node.effect || !node.curves ||
                    !creative_suite::effects::validVideoCurves(node.curves->parameters))
                    return false;
            } else {
                return false;
            }
        }
        if (input_count != 1 || output_count != 1 || graph.next_id <= maximum_id)
            return false;

        std::set<std::pair<NodeId, std::uint32_t>> input_slots;
        std::unordered_map<NodeId, std::vector<const Connection*>> node_inputs;
        for (const auto& edge : graph.connections) {
            const auto* from = findNode(graph, edge.from);
            const auto* to = findNode(graph, edge.to);
            if (!from || !to || edge.from == edge.to ||
                from->type == NodeType::Output || to->type == NodeType::Input ||
                !std::isfinite(edge.weight) || edge.weight < 0.0 || edge.weight > 1.0)
                return false;
            if (!input_slots.emplace(edge.to, edge.input).second) return false;
            if ((to->type == NodeType::Effect || to->type == NodeType::Curves ||
                 to->type == NodeType::Output) &&
                edge.input != 0) return false;
            if (to->type != NodeType::ParallelMixer && edge.weight != 1.0) return false;
            node_inputs[edge.to].push_back(&edge);
        }
        for (const auto& node : graph.nodes) {
            const auto found = node_inputs.find(node.id);
            const auto count = found == node_inputs.end() ? 0U : found->second.size();
            if ((node.type == NodeType::Input && count != 0) ||
                ((node.type == NodeType::Effect || node.type == NodeType::Curves ||
                  node.type == NodeType::Output) && count > 1))
                return false;
            if (node.type == NodeType::ParallelMixer && found != node_inputs.end()) {
                double total_weight = 0.0;
                std::vector<std::uint32_t> ports;
                for (const auto* edge : found->second) {
                    total_weight += edge->weight;
                    ports.push_back(edge->input);
                }
                if (!ports.empty()) {
                    std::sort(ports.begin(), ports.end());
                    for (std::size_t index = 0; index < ports.size(); ++index)
                        if (ports[index] != index) return false;
                    if (total_weight <= 0.0) return false;
                }
            }
        }
        const auto order = evaluationOrder(graph);
        if (!order) return false;
        const auto output = std::find_if(graph.nodes.begin(), graph.nodes.end(),
            [](const Node& node) { return node.type == NodeType::Output; });
        const auto input = std::find_if(graph.nodes.begin(), graph.nodes.end(),
            [](const Node& node) { return node.type == NodeType::Input; });
        if (output == graph.nodes.end() || input == graph.nodes.end()) return false;
        return incoming(graph, output->id, 0) != nullptr &&
            hasOutputPathFromInput(graph, output->id);
    } catch (...) {
        return false;
    }
}

std::optional<std::vector<creative_suite::effects::EffectInstance>>
linearEffectStack(const Graph& graph) {
    if (!validate(graph)) return std::nullopt;
    const auto input = std::find_if(graph.nodes.begin(), graph.nodes.end(),
        [](const Node& node) { return node.type == NodeType::Input; });
    const auto output = std::find_if(graph.nodes.begin(), graph.nodes.end(),
        [](const Node& node) { return node.type == NodeType::Output; });
    if (input == graph.nodes.end() || output == graph.nodes.end()) return std::nullopt;
    std::vector<creative_suite::effects::EffectInstance> result;
    std::unordered_set<NodeId> visited;
    NodeId current = input->id;
    while (true) {
        if (!visited.insert(current).second) return std::nullopt;
        const auto* node = findNode(graph, current);
        if (!node) return std::nullopt;
        if (node->type == NodeType::ParallelMixer) return std::nullopt;
        if (node->type == NodeType::Effect) result.push_back(*node->effect);
        else if (node->type == NodeType::Curves) {
            auto effect = creative_suite::effects::makeDefaultInstance("video.curves");
            effect.enabled = node->curves->enabled;
            effect.video_curves = node->curves->parameters;
            result.push_back(std::move(effect));
        }
        if (node->type == NodeType::Output) {
            if (node->id != output->id || !outgoing(graph, node->id).empty())
                return std::nullopt;
            break;
        }
        const auto next = outgoing(graph, current);
        if (next.size() != 1) return std::nullopt;
        current = next.front()->to;
    }
    return result;
}

std::vector<NodeId> effectNodesInOrder(const Graph& graph) {
    std::vector<NodeId> result;
    const auto order = evaluationOrder(graph);
    if (!order) return result;
    for (const auto id : *order) {
        const auto* node = findNode(graph, id);
        if (node && (node->type == NodeType::Effect || node->type == NodeType::Curves))
            result.push_back(id);
    }
    return result;
}

std::vector<creative_suite::effects::EffectInstance> effectInstancesInOrder(
    const Graph& graph) {
    std::vector<creative_suite::effects::EffectInstance> result;
    for (const auto id : effectNodesInOrder(graph)) {
        const auto* node = findNode(graph, id);
        if (node && node->effect) result.push_back(*node->effect);
        else if (node && node->curves) {
            auto effect = creative_suite::effects::makeDefaultInstance("video.curves");
            effect.enabled = node->curves->enabled;
            effect.video_curves = node->curves->parameters;
            result.push_back(std::move(effect));
        }
    }
    return result;
}

bool insertEffectAfter(
    Graph& graph, NodeId anchor, creative_suite::effects::EffectInstance effect) {
    auto candidate = graph;
    auto* anchor_node = findNode(candidate, anchor);
    if (!anchor_node || anchor_node->type == NodeType::Output ||
        !creative_suite::effects::isValid(effect)) return false;
    std::vector<Connection> next;
    for (const auto* edge : outgoing(candidate, anchor)) next.push_back(*edge);
    const auto anchor_x = anchor_node->x;
    const auto anchor_y = anchor_node->y;
    const auto id = allocateId(candidate);
    if (id == 0) return false;
    candidate.nodes.push_back({id, NodeType::Effect,
        anchor_x + 170.0, anchor_y, std::move(effect)});
    for (const auto& edge : next)
        eraseConnection(candidate, edge.from, edge.to, edge.input);
    candidate.connections.push_back({anchor, id, 0, 1.0});
    for (const auto& edge : next)
        candidate.connections.push_back({id, edge.to, edge.input, edge.weight});
    if (!validate(candidate)) return false;
    graph = std::move(candidate);
    return true;
}

bool insertCurvesAfter(Graph& graph, NodeId anchor) {
    auto candidate = graph;
    const auto* anchor_node = findNode(candidate, anchor);
    if (!anchor_node || anchor_node->type == NodeType::Output || !validate(candidate))
        return false;
    std::vector<Connection> next;
    for (const auto* edge : outgoing(candidate, anchor)) next.push_back(*edge);
    const auto anchor_x = anchor_node->x;
    const auto anchor_y = anchor_node->y;
    const auto id = allocateId(candidate);
    if (id == 0) return false;
    candidate.nodes.push_back({id, NodeType::Curves, anchor_x + 170.0, anchor_y,
        std::nullopt, CurvesNode{}});
    for (const auto& edge : next)
        eraseConnection(candidate, edge.from, edge.to, edge.input);
    candidate.connections.push_back({anchor, id, 0, 1.0});
    for (const auto& edge : next)
        candidate.connections.push_back({id, edge.to, edge.input, edge.weight});
    if (!validate(candidate)) return false;
    graph = std::move(candidate);
    return true;
}

bool addParallelBranch(
    Graph& graph, NodeId anchor, creative_suite::effects::EffectInstance effect) {
    if (!validate(graph)) return false;
    auto candidate = graph;
    const auto* anchor_node = findNode(candidate, anchor);
    if (!anchor_node || (anchor_node->type != NodeType::Input &&
                         anchor_node->type != NodeType::Effect &&
                         anchor_node->type != NodeType::Curves) ||
        !creative_suite::effects::isValid(effect)) return false;
    const auto anchor_x = anchor_node->x;
    const auto anchor_y = anchor_node->y;
    const auto branches = outgoing(candidate, anchor);
    const auto existing_mixer = std::find_if(branches.begin(), branches.end(),
        [&candidate](const Connection* edge) {
            const auto* target = findNode(candidate, edge->to);
            return target && target->type == NodeType::ParallelMixer;
        });
    if (existing_mixer != branches.end()) {
        const auto mixer_id = (*existing_mixer)->to;
        const auto mixer_inputs = incoming(candidate, mixer_id);
        if (mixer_inputs.size() >= std::numeric_limits<std::uint32_t>::max()) return false;
        const auto effect_id = allocateId(candidate);
        if (effect_id == 0) return false;
        const auto port = static_cast<std::uint32_t>(mixer_inputs.size());
        candidate.nodes.push_back({effect_id, NodeType::Effect,
            anchor_x + 170.0, anchor_y + 150.0 + 120.0 * port, std::move(effect)});
        candidate.connections.push_back({anchor, effect_id, 0, 1.0});
        candidate.connections.push_back({effect_id, mixer_id, port, 1.0});
    } else {
        if (branches.size() != 1) return false;
        const auto old = *branches.front();
        const auto effect_id = allocateId(candidate);
        const auto mixer_id = allocateId(candidate);
        if (effect_id == 0 || mixer_id == 0) return false;
        eraseConnection(candidate, old.from, old.to, old.input);
        candidate.nodes.push_back({effect_id, NodeType::Effect,
            anchor_x + 170.0, anchor_y + 150.0, std::move(effect)});
        candidate.nodes.push_back({mixer_id, NodeType::ParallelMixer,
            anchor_x + 340.0, anchor_y + 70.0, std::nullopt});
        candidate.connections.push_back({anchor, mixer_id, 0, 1.0});
        candidate.connections.push_back({anchor, effect_id, 0, 1.0});
        candidate.connections.push_back({effect_id, mixer_id, 1, 1.0});
        candidate.connections.push_back({mixer_id, old.to, old.input, old.weight});
    }
    if (!validate(candidate)) return false;
    graph = std::move(candidate);
    return true;
}

bool connect(Graph& graph, NodeId from, NodeId to, std::uint32_t input,
             std::optional<double> requested_weight) {
    auto candidate = graph;
    const auto* source = findNode(candidate, from);
    const auto* target = findNode(candidate, to);
    if (!source || !target || source->type == NodeType::Output ||
        target->type == NodeType::Input ||
        (target->type != NodeType::ParallelMixer && input != 0)) return false;
    const auto* existing = incoming(candidate, to, input);
    const bool replacing_input = existing != nullptr;
    const auto weight = requested_weight.value_or(existing ? existing->weight : 1.0);
    if (!std::isfinite(weight) || weight < 0.0 || weight > 1.0) return false;
    if (existing) eraseConnection(candidate, existing->from, existing->to, existing->input);
    if (target->type == NodeType::ParallelMixer) {
        const auto mixer_inputs = incoming(candidate, to);
        if (!replacing_input) {
            if (input != mixer_inputs.size()) return false;
        }
    }
    candidate.connections.push_back({from, to, input, weight});
    if (!validate(candidate)) return false;
    graph = std::move(candidate);
    return true;
}

bool removeEffect(Graph& graph, NodeId id) {
    auto candidate = graph;
    const auto* node = findNode(candidate, id);
    if (!node || (node->type != NodeType::Effect && node->type != NodeType::Curves)) return false;
    std::vector<Connection> previous;
    std::vector<Connection> next;
    for (const auto* edge : incoming(candidate, id)) previous.push_back(*edge);
    for (const auto* edge : outgoing(candidate, id)) next.push_back(*edge);
    candidate.nodes.erase(std::remove_if(candidate.nodes.begin(), candidate.nodes.end(),
        [id](const Node& item) { return item.id == id; }), candidate.nodes.end());
    std::erase_if(candidate.connections, [id](const Connection& edge) {
        return edge.from == id || edge.to == id;
    });
    if (previous.size() == 1) {
        for (const auto& edge : next)
            candidate.connections.push_back({previous.front().from, edge.to,
                                              edge.input, edge.weight});
    }
    const auto output = std::find_if(candidate.nodes.begin(), candidate.nodes.end(),
        [](const Node& item) { return item.type == NodeType::Output; });
    if (output != candidate.nodes.end() && incoming(candidate, output->id, 0) == nullptr) {
        const auto input = std::find_if(candidate.nodes.begin(), candidate.nodes.end(),
            [](const Node& item) { return item.type == NodeType::Input; });
        if (input != candidate.nodes.end())
            candidate.connections.push_back({input->id, output->id, 0, 1.0});
    }
    if (!validate(candidate)) return false;
    graph = std::move(candidate);
    return true;
}

bool removeParallelMixer(Graph& graph, NodeId id) {
    auto candidate = graph;
    const auto* node = findNode(candidate, id);
    if (!node || node->type != NodeType::ParallelMixer) return false;
    std::vector<Connection> previous;
    std::vector<Connection> next;
    for (const auto* edge : incoming(candidate, id)) previous.push_back(*edge);
    for (const auto* edge : outgoing(candidate, id)) next.push_back(*edge);
    if (previous.empty()) return false;
    const auto retained = std::min_element(previous.begin(), previous.end(),
        [](const Connection& left, const Connection& right) {
            return left.input < right.input;
        });
    const auto retained_source = retained->from;
    candidate.nodes.erase(std::remove_if(candidate.nodes.begin(), candidate.nodes.end(),
        [id](const Node& item) { return item.id == id; }), candidate.nodes.end());
    std::erase_if(candidate.connections, [id](const Connection& edge) {
        return edge.from == id || edge.to == id;
    });
    for (const auto& edge : next)
        candidate.connections.push_back({retained_source, edge.to,
                                          edge.input, edge.weight});

    std::vector<NodeId> prune;
    for (const auto& edge : previous)
        if (edge.from != retained_source) prune.push_back(edge.from);
    while (!prune.empty()) {
        const auto branch_id = prune.back();
        prune.pop_back();
        if (branch_id == retained_source) continue;
        const auto* branch_node = findNode(candidate, branch_id);
        if (!branch_node || branch_node->type == NodeType::Input ||
            branch_node->type == NodeType::Output || !outgoing(candidate, branch_id).empty())
            continue;
        std::vector<NodeId> parents;
        for (const auto* edge : incoming(candidate, branch_id)) parents.push_back(edge->from);
        candidate.nodes.erase(std::remove_if(candidate.nodes.begin(), candidate.nodes.end(),
            [branch_id](const Node& item) { return item.id == branch_id; }), candidate.nodes.end());
        std::erase_if(candidate.connections, [branch_id](const Connection& edge) {
            return edge.from == branch_id || edge.to == branch_id;
        });
        prune.insert(prune.end(), parents.begin(), parents.end());
    }
    if (!validate(candidate)) return false;
    graph = std::move(candidate);
    return true;
}

bool moveNode(Graph& graph, NodeId id, double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    auto* node = findNode(graph, id);
    if (!node) return false;
    if (node->x == x && node->y == y) return false;
    node->x = x;
    node->y = y;
    return true;
}

bool setEffectEnabled(Graph& graph, NodeId id, bool enabled) {
    auto* node = findNode(graph, id);
    if (!node || node->type != NodeType::Effect || !node->effect ||
        node->effect->enabled == enabled) return false;
    node->effect->enabled = enabled;
    return true;
}

bool setCurvesEnabled(Graph& graph, NodeId id, bool enabled) {
    auto* node = findNode(graph, id);
    if (!node || node->type != NodeType::Curves || !node->curves ||
        node->curves->enabled == enabled) return false;
    node->curves->enabled = enabled;
    return true;
}

bool setCurvePoints(Graph& graph, NodeId id, CurveKind kind,
                    creative_suite::effects::CurvesPointList points) {
    auto candidate = graph;
    auto* node = findNode(candidate, id);
    if (!node || node->type != NodeType::Curves || !node->curves ||
        !creative_suite::effects::validCurvePoints(points)) return false;
    auto& parameters = node->curves->parameters;
    switch (kind) {
    case CurveKind::Master: parameters.rgb.points = std::move(points); break;
    case CurveKind::Red: parameters.rgb.channels[0] = std::move(points); break;
    case CurveKind::Green: parameters.rgb.channels[1] = std::move(points); break;
    case CurveKind::Blue: parameters.rgb.channels[2] = std::move(points); break;
    case CurveKind::HueVsHue: parameters.targeted[0] = std::move(points); break;
    case CurveKind::HueVsSaturation: parameters.targeted[1] = std::move(points); break;
    case CurveKind::HueVsLuminance: parameters.targeted[2] = std::move(points); break;
    case CurveKind::LuminanceVsSaturation: parameters.targeted[3] = std::move(points); break;
    case CurveKind::SaturationVsSaturation: parameters.targeted[4] = std::move(points); break;
    }
    if (!creative_suite::effects::validVideoCurves(parameters)) return false;
    graph = std::move(candidate);
    return true;
}

bool setCurvesQualifier(Graph& graph, NodeId id,
                        creative_suite::effects::TonalQualifier qualifier) {
    auto candidate = graph;
    auto* node = findNode(candidate, id);
    if (!node || node->type != NodeType::Curves || !node->curves ||
        !creative_suite::effects::validTonalQualifier(qualifier)) return false;
    node->curves->parameters.qualifier = qualifier;
    graph = std::move(candidate);
    return true;
}

bool setEffectParameter(
    Graph& graph, NodeId id, std::string_view parameter, double value) {
    auto* node = findNode(graph, id);
    return node && node->type == NodeType::Effect && node->effect &&
        creative_suite::effects::setParameterValue(*node->effect, parameter, value);
}

bool setMixerInputWeight(Graph& graph, NodeId id, std::uint32_t input, double weight) {
    if (!std::isfinite(weight) || weight < 0.0 || weight > 1.0) return false;
    auto candidate = graph;
    const auto* edge = incoming(candidate, id, input);
    const auto* node = findNode(candidate, id);
    if (!edge || !node || node->type != NodeType::ParallelMixer || edge->weight == weight)
        return false;
    const auto mutable_edge = std::find_if(candidate.connections.begin(), candidate.connections.end(),
        [id, input](const Connection& item) { return item.to == id && item.input == input; });
    if (mutable_edge == candidate.connections.end()) return false;
    mutable_edge->weight = weight;
    if (!validate(candidate)) return false;
    graph = std::move(candidate);
    return true;
}

}  // namespace color
