#include "fusion/nodes/evaluation/node_graph_evaluator.h"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    using namespace fusion::nodes;
    auto graph = makePassthroughGraph();
    require(static_cast<bool>(validate(graph)), "default graph must validate");
    const auto transform_id = graph.next_id++;
    graph.nodes.push_back(Node{transform_id, NodeType::Transform, 200.0, 120.0});
    graph.connections = {{1, transform_id, 0}, {transform_id, 2, 0}};
    require(static_cast<bool>(validate(graph)), "transform chain must validate");
    require(!connect(graph, 2, transform_id, 0), "cycle must be rejected");
    require(!connect(graph, 1, 2, 1), "incompatible input port must be rejected");

    auto solid = std::make_shared<media::VideoFrame>(
        media::VideoFrame{2, 1, 8, {255, 255, 255, 255, 255, 255, 255, 255}});
    NodeGraph transform_graph;
    transform_graph.nodes = {{1, NodeType::Input}, {2, NodeType::Transform},
                             {3, NodeType::Output}};
    transform_graph.nodes[1].transform.scale = 0.5;
    transform_graph.connections = {{1, 2, 0}, {2, 3, 0}};
    transform_graph.next_id = 4;
    const auto transformed = evaluate(transform_graph, {{1, solid}});
    require(transformed.has_value() && transformed->rgba_pixels[3] == 255 &&
                transformed->rgba_pixels[7] == 0,
            "Transform should update pixel geometry and preserve transparency.");
    const auto detached_transform = disconnect(transform_graph, 2, 0);
    require(detached_transform && static_cast<bool>(validate(*detached_transform)),
            "A node input should be disconnectable without invalidating the graph.");
    const auto empty_transform = evaluate(*detached_transform, {{1, solid}});
    require(empty_transform.has_value() && empty_transform->rgba_pixels[3] == 0 &&
                empty_transform->rgba_pixels[7] == 0,
            "A node with a disconnected input should render transparent output.");

    const auto passthrough = makePassthroughGraph();
    const auto detached_output = disconnect(passthrough, 2, 0);
    require(detached_output && static_cast<bool>(validate(*detached_output)) &&
                detached_output->connections.empty(),
            "The default Input-to-Output cable should be disconnectable.");
    const auto empty_output = evaluate(*detached_output, {{1, solid}});
    require(empty_output.has_value() && empty_output->rgba_pixels[3] == 0 &&
                empty_output->rgba_pixels[7] == 0,
            "An Output node without an input should render transparent output.");

    NodeGraph color_graph;
    color_graph.nodes = {{1, NodeType::Input}, {2, NodeType::Color},
                         {3, NodeType::Output}};
    color_graph.nodes[1].color.brightness = 20.0;
    color_graph.connections = {{1, 2, 0}, {2, 3, 0}};
    color_graph.next_id = 4;
    auto black = std::make_shared<media::VideoFrame>(
        media::VideoFrame{1, 1, 4, {0, 0, 0, 255}});
    const auto adjusted = evaluate(color_graph, {{1, black}});
    require(adjusted.has_value() && adjusted->rgba_pixels[0] > 0,
            "Color node should apply brightness to the RGBA input.");

    auto background = std::make_shared<media::VideoFrame>(
        media::VideoFrame{2, 1, 8, {0, 0, 255, 255, 0, 0, 255, 255}});
    auto foreground = std::make_shared<media::VideoFrame>(
        media::VideoFrame{2, 1, 8, {255, 0, 0, 128, 0, 0, 0, 0}});
    NodeGraph merge;
    merge.nodes = {{1, NodeType::Input}, {2, NodeType::Input},
                   {3, NodeType::Merge}, {4, NodeType::Output}};
    merge.nodes[1].source_path = "media/overlay.png";
    merge.connections = {{1, 3, 0}, {2, 3, 1}, {3, 4, 0}};
    merge.next_id = 5;
    const auto composed = evaluate(merge, {{1, background}, {2, foreground}});
    require(composed.has_value() && composed->width == background->width &&
                composed->height == background->height,
            "Merge should use the selected clip as its output canvas.");
    require(composed->rgba_pixels[0] > 100 && composed->rgba_pixels[2] > 100,
            "merge should blend foreground over background");
    const auto ended = evaluate(merge, {{1, background}});
    require(ended.has_value() && ended->rgba_pixels[2] == 255,
            "missing/ended foreground should be transparent");
    std::cout << "Node graph model and evaluator tests passed.\n";
}
