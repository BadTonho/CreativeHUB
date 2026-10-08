#include "workspaces/fusion/nodes/evaluation/node_graph_evaluator.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

    NodeGraph animated_transform_graph;
    animated_transform_graph.nodes = {
        {1, NodeType::Input}, {2, NodeType::Transform}, {3, NodeType::Output}};
    animated_transform_graph.nodes[1].transform_keyframes.position_x = {
        {0, 0.5}, {10, 0.1}};
    animated_transform_graph.nodes[1].transform_keyframes.position_y = {
        {0, 0.5}, {10, 0.2}};
    animated_transform_graph.nodes[1].transform_keyframes.scale = {
        {0, 1.0}, {10, 0.5}};
    animated_transform_graph.nodes[1].transform_keyframes.rotation = {
        {0, 0.0}, {10, 45.0}};
    animated_transform_graph.nodes[1].transform_keyframes.opacity = {
        {0, 1.0}, {10, 0.25}};
    animated_transform_graph.connections = {{1, 2, 0}, {2, 3, 0}};
    animated_transform_graph.next_id = 4;
    auto white_square = std::make_shared<media::VideoFrame>(
        media::VideoFrame{2, 2, 8,
            {255, 255, 255, 255, 255, 255, 255, 255,
             255, 255, 255, 255, 255, 255, 255, 255}});
    const auto transform_start = evaluate(animated_transform_graph,
        {{1, white_square}}, EvaluationContext{0});
    const auto transform_end = evaluate(animated_transform_graph,
        {{1, white_square}}, EvaluationContext{10});
    require(static_cast<bool>(validate(animated_transform_graph)) &&
                transform_start.has_value() && transform_end.has_value() &&
                transform_start->rgba_pixels != transform_end->rgba_pixels,
            "Transform nodes did not evaluate their animated parameters at local clip frames.");
    require(!validKeyframeRange(animated_transform_graph, 10) &&
                validKeyframeRange(animated_transform_graph, 11),
            "Node keyframes outside the clip duration were not rejected.");

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

    const std::vector<std::pair<std::string, double>> built_in_effects{
        {"video.grayscale", 100.0}, {"video.brightness", 20.0},
        {"video.contrast", 180.0}, {"video.saturation", 0.0}};
    auto color_source = std::make_shared<media::VideoFrame>(
        media::VideoFrame{1, 1, 4, {220, 40, 15, 73}});
    for (const auto& [effect_id, amount] : built_in_effects) {
        NodeGraph effect_graph;
        effect_graph.nodes = {{1, NodeType::Input}, {2, NodeType::Effect},
                              {3, NodeType::Output}};
        effect_graph.nodes[1].effect = creative_suite::effects::makeDefaultInstance(effect_id);
        require(creative_suite::effects::setParameterValue(
                    effect_graph.nodes[1].effect, "amount", amount),
                "A built-in effect parameter could not be set for graph coverage.");
        effect_graph.connections = {{1, 2, 0}, {2, 3, 0}};
        effect_graph.next_id = 4;
        require(static_cast<bool>(validate(effect_graph)),
                "A built-in effect node graph did not validate.");
        const auto effected = evaluate(effect_graph, {{1, color_source}});
        require(effected.has_value() && effected->rgba_pixels[3] == 73 &&
                    std::vector<std::uint8_t>(effected->rgba_pixels.begin(),
                        effected->rgba_pixels.begin() + 3) !=
                    std::vector<std::uint8_t>(color_source->rgba_pixels.begin(),
                        color_source->rgba_pixels.begin() + 3),
                "A built-in effect node failed to process RGB while preserving alpha.");
        effect_graph.nodes[1].effect.enabled = false;
        const auto bypassed = evaluate(effect_graph, {{1, color_source}});
        require(bypassed.has_value() && bypassed->rgba_pixels == color_source->rgba_pixels,
                "A disabled effect node did not bypass its input frame.");
    }

    NodeGraph effect_chain;
    effect_chain.nodes = {{1, NodeType::Input}, {2, NodeType::Effect},
                          {3, NodeType::Effect}, {4, NodeType::Output}};
    effect_chain.nodes[1].effect =
        creative_suite::effects::makeDefaultInstance("video.grayscale");
    effect_chain.nodes[2].effect =
        creative_suite::effects::makeDefaultInstance("video.brightness");
    require(creative_suite::effects::setParameterValue(
                effect_chain.nodes[2].effect, "amount", 10.0),
            "A chained effect parameter could not be set.");
    effect_chain.connections = {{1, 2, 0}, {2, 3, 0}, {3, 4, 0}};
    effect_chain.next_id = 5;
    const auto chained = evaluate(effect_chain, {{1, color_source}});
    require(static_cast<bool>(validate(effect_chain)) && chained.has_value() &&
                chained->rgba_pixels[3] == 73,
            "Repeated effect nodes did not evaluate in connection order.");

    NodeGraph animated_brightness;
    animated_brightness.nodes = {{1, NodeType::Input}, {2, NodeType::Effect},
                                 {3, NodeType::Effect}, {4, NodeType::Output}};
    animated_brightness.nodes[1].effect =
        creative_suite::effects::makeDefaultInstance("video.brightness");
    animated_brightness.nodes[2].effect =
        creative_suite::effects::makeDefaultInstance("video.brightness");
    animated_brightness.nodes[1].effect_parameter_keyframes.push_back(
        {"amount", {{0, 0.0}, {10, 20.0}}});
    animated_brightness.nodes[2].effect_parameter_keyframes.push_back(
        {"amount", {{0, 0.0}, {10, 10.0}}});
    animated_brightness.connections = {{1, 2, 0}, {2, 3, 0}, {3, 4, 0}};
    animated_brightness.next_id = 5;
    auto black_source = std::make_shared<media::VideoFrame>(
        media::VideoFrame{1, 1, 4, {0, 0, 0, 71}});
    const auto brightness_start = evaluate(animated_brightness,
        {{1, black_source}}, EvaluationContext{0});
    const auto first_brightness = evaluate(animated_brightness,
        {{1, black_source}}, 2, EvaluationContext{10});
    const auto brightness_end = evaluate(animated_brightness,
        {{1, black_source}}, EvaluationContext{10});
    require(static_cast<bool>(validate(animated_brightness)) &&
                brightness_start.has_value() && brightness_end.has_value() &&
                brightness_start->rgba_pixels[0] == 0 &&
                brightness_end->rgba_pixels[0] > 70 &&
                brightness_end->rgba_pixels[3] == 71 &&
                first_brightness.has_value() &&
                first_brightness->rgba_pixels[0] == 51 &&
                first_brightness->rgba_pixels[3] == 71,
            "Brightness nodes did not animate independently, preserve alpha, or preview their intermediate output.");
    animated_brightness.nodes[1].effect.enabled = false;
    const auto disabled_brightness = evaluate(animated_brightness,
        {{1, black_source}}, EvaluationContext{10});
    require(disabled_brightness.has_value() &&
                disabled_brightness->rgba_pixels[0] == 26 &&
                disabled_brightness->rgba_pixels[3] == 71,
            "A disabled animated Brightness node did not bypass its animated parameter.");
    const auto input_preview = evaluate(effect_chain, {{1, color_source}}, 1);
    const auto grayscale_preview = evaluate(effect_chain, {{1, color_source}}, 2);
    const auto output_preview = evaluate(effect_chain, {{1, color_source}}, 4);
    require(input_preview.has_value() &&
                input_preview->rgba_pixels == color_source->rgba_pixels,
            "Previewing an Input node did not return its original frame.");
    require(grayscale_preview.has_value() &&
                grayscale_preview->rgba_pixels[0] == grayscale_preview->rgba_pixels[1] &&
                grayscale_preview->rgba_pixels[1] == grayscale_preview->rgba_pixels[2] &&
                grayscale_preview->rgba_pixels[3] == 73 && output_preview.has_value() &&
                output_preview->rgba_pixels == chained->rgba_pixels &&
                output_preview->rgba_pixels != grayscale_preview->rgba_pixels,
            "A node preview included downstream processing or Output differed from the final graph.");
    require(!evaluate(effect_chain, {{1, color_source}}, 99).has_value(),
            "An unknown node was accepted as a preview target.");

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
    const auto merge_preview = evaluate(merge, {{1, background}, {2, foreground}}, 3);
    require(merge_preview.has_value() &&
                merge_preview->rgba_pixels == composed->rgba_pixels,
            "Previewing a Merge node did not include both connected image inputs.");
    require(composed->rgba_pixels[0] > 100 && composed->rgba_pixels[2] > 100,
            "merge should blend foreground over background");
    const auto ended = evaluate(merge, {{1, background}});
    require(ended.has_value() && ended->rgba_pixels[2] == 255,
            "missing/ended foreground should be transparent");
    std::cout << "Node graph model and evaluator tests passed.\n";
}
