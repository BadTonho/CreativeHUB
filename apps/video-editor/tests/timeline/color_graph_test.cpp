#include "workspaces/color/evaluation/color_graph_evaluator.h"

#include <creative_suite/effects/effects.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

color::Graph weightedBrightnessGraph() {
    auto graph = color::makePassthroughGraph();
    auto brightness = creative_suite::effects::makeDefaultInstance("video.brightness");
    require(creative_suite::effects::setParameterValue(brightness, "amount", 20.0),
            "Brightness setup failed.");
    require(color::addParallelBranch(graph, 1, brightness),
            "The first correction branch could not be added.");
    const auto mixer = std::find_if(graph.nodes.begin(), graph.nodes.end(), [](const auto& node) {
        return node.type == color::NodeType::ParallelMixer;
    });
    require(mixer != graph.nodes.end(), "The parallel mixer was not created.");
    require(color::setMixerInputWeight(graph, mixer->id, 0, 0.2) &&
                color::setMixerInputWeight(graph, mixer->id, 1, 0.6),
            "Custom branch weights could not be stored.");
    return graph;
}

media::VideoFrame sampleFrame() {
    media::VideoFrame frame;
    frame.width = 1;
    frame.height = 1;
    frame.stride = 4;
    frame.rgba_pixels = {100, 100, 100, 255};
    return frame;
}

}  // namespace

int main() {
    try {
        const auto empty = color::makePassthroughGraph();
        require(color::validate(empty) && color::linearEffectStack(empty).has_value() &&
                    color::linearEffectStack(empty)->empty(),
                "An empty clip must use a valid Input-to-Output graph.");

        auto disconnected_effect = empty;
        disconnected_effect.nodes.push_back({3, color::NodeType::Effect, 260, 300,
            creative_suite::effects::makeDefaultInstance("video.grayscale")});
        disconnected_effect.next_id = 4;
        require(color::validate(disconnected_effect) &&
                    color::linearEffectStack(disconnected_effect).has_value() &&
                    color::linearEffectStack(disconnected_effect)->empty(),
                "A disconnected effect must not be projected into the output stack.");
        const auto untouched = color::evaluate(disconnected_effect, sampleFrame());
        require(untouched && untouched->rgba_pixels ==
                    std::vector<std::uint8_t>{100, 100, 100, 255},
                "A disconnected effect must not alter the Output image.");

        auto serial = color::fromEffectStack(std::vector{
            creative_suite::effects::makeDefaultInstance("video.grayscale"),
            creative_suite::effects::makeDefaultInstance("video.brightness")});
        require(color::validate(serial) && color::linearEffectStack(serial)->size() == 2,
                "Legacy effect stacks must migrate to an ordered serial graph.");
        const auto ordered_ids = color::effectNodesInOrder(serial);
        require(ordered_ids.size() == 2, "Serial graph node order is incomplete.");
        auto cyclic_connection = serial;
        require(!color::connect(cyclic_connection, ordered_ids[1], ordered_ids[0], 0) &&
                    cyclic_connection == serial,
                "A cycle forming reconnection must be rejected without mutation.");

        auto disconnected_cycle = empty;
        disconnected_cycle.nodes.push_back({3, color::NodeType::Effect, 600, 100,
            creative_suite::effects::makeDefaultInstance("video.brightness")});
        disconnected_cycle.nodes.push_back({4, color::NodeType::Effect, 800, 100,
            creative_suite::effects::makeDefaultInstance("video.contrast")});
        disconnected_cycle.connections.push_back({3, 4, 0, 1.0});
        disconnected_cycle.connections.push_back({4, 3, 0, 1.0});
        disconnected_cycle.next_id = 5;
        require(!color::validate(disconnected_cycle),
                "Disconnected cycles must also be rejected.");

        auto graph = weightedBrightnessGraph();
        require(color::validate(graph) && !color::linearEffectStack(graph),
                "A branched graph must not be flattened into an effect stack.");
        const auto mixer = std::find_if(graph.nodes.begin(), graph.nodes.end(), [](const auto& node) {
            return node.type == color::NodeType::ParallelMixer;
        });
        const auto branch_nodes = color::effectNodesInOrder(graph);
        require(mixer != graph.nodes.end() && branch_nodes.size() == 1,
                "The weighted graph fixture is incomplete.");
        auto reconnected = graph;
        require(color::connect(reconnected, branch_nodes.front(), mixer->id, 0, 0.2) &&
                    color::validate(reconnected),
                "A valid branch reconnection was rejected.");
        auto weight_preserved = graph;
        require(color::connect(weight_preserved, branch_nodes.front(), mixer->id, 1) &&
                    std::find_if(weight_preserved.connections.begin(),
                        weight_preserved.connections.end(), [&](const auto& edge) {
                            return edge.to == mixer->id && edge.input == 1;
                        })->weight == 0.6,
                "Reconnecting a mixer port must preserve its custom weight.");
        auto removed_branch = graph;
        require(color::removeEffect(removed_branch, branch_nodes.front()) &&
                    color::validate(removed_branch),
                "Removing a branch node must reconnect the graph safely.");
        auto removed_mixer = graph;
        require(color::removeParallelMixer(removed_mixer, mixer->id) &&
                    color::validate(removed_mixer) &&
                    color::linearEffectStack(removed_mixer).has_value() &&
                    color::linearEffectStack(removed_mixer)->empty(),
                "Removing a mixer must retain its first input as the serial output path.");
        auto all_zero_weights = graph;
        require(color::setMixerInputWeight(all_zero_weights, mixer->id, 0, 0.0) &&
                    !color::setMixerInputWeight(all_zero_weights, mixer->id, 1, 0.0) &&
                    color::validate(all_zero_weights),
                "Mixer weights must remain normalized by a positive total.");
        const auto frame = sampleFrame();
        const auto result = color::evaluate(graph, frame);
        require(result && result->width == frame.width && result->height == frame.height &&
                    result->rgba_pixels == std::vector<std::uint8_t>{138, 138, 138, 255},
                "The mixer must compute the normalized weighted branch average; got " +
                    (result ? std::to_string(result->rgba_pixels[0]) + "," +
                        std::to_string(result->rgba_pixels[1]) + "," +
                        std::to_string(result->rgba_pixels[2]) + "," +
                        std::to_string(result->rgba_pixels[3]) : std::string("no frame")));

        auto multiple_branches = graph;
        require(color::addParallelBranch(multiple_branches, 1,
                    creative_suite::effects::makeDefaultInstance("video.contrast")) &&
                    color::validate(multiple_branches),
                "A common source must accept another branch on the existing mixer.");
        const auto expanded_mixer = std::find_if(multiple_branches.nodes.begin(),
            multiple_branches.nodes.end(), [](const auto& node) {
                return node.type == color::NodeType::ParallelMixer;
            });
        require(expanded_mixer != multiple_branches.nodes.end() &&
                    std::count_if(multiple_branches.connections.begin(),
                        multiple_branches.connections.end(), [&](const auto& edge) {
                            return edge.to == expanded_mixer->id;
                        }) == 3 && color::evaluate(multiple_branches, frame).has_value(),
                "Repeated branch creation must extend the mixer without breaking evaluation.");

        auto common_correction = multiple_branches;
        require(color::insertEffectAfter(common_correction, 1,
                    creative_suite::effects::makeDefaultInstance("video.grayscale")) &&
                    color::validate(common_correction) &&
                    color::evaluate(common_correction, frame).has_value(),
                "A correction inserted before a fork must feed every branch.");

        auto corrected_anchor = color::fromEffectStack(std::vector{
            creative_suite::effects::makeDefaultInstance("video.grayscale")});
        const auto common_effect = color::effectNodesInOrder(corrected_anchor).front();
        require(color::addParallelBranch(corrected_anchor, common_effect,
                    creative_suite::effects::makeDefaultInstance("video.contrast")),
                "A branch after an existing correction could not be created.");
        const auto corrected_mixer = std::find_if(corrected_anchor.nodes.begin(),
            corrected_anchor.nodes.end(), [](const auto& node) {
                return node.type == color::NodeType::ParallelMixer;
            });
        const auto branch_effects = color::effectNodesInOrder(corrected_anchor);
        require(corrected_mixer != corrected_anchor.nodes.end() &&
                    branch_effects.size() == 2 &&
                    std::any_of(corrected_anchor.connections.begin(),
                        corrected_anchor.connections.end(), [&](const auto& edge) {
                            return edge.from == common_effect &&
                                edge.to == branch_effects.back();
                        }) && color::validate(corrected_anchor),
                "Parallel correction branches must share the selected upstream correction.");

        const auto branch = color::effectNodesInOrder(graph).back();
        require(color::setEffectEnabled(graph, branch, false),
                "An effect node could not be disabled.");
        auto disabled_result = color::evaluate(graph, frame);
        require(disabled_result && disabled_result->rgba_pixels ==
                    std::vector<std::uint8_t>{100, 100, 100, 255},
                "A disabled branch must pass through its input unchanged.");
        require(color::moveNode(graph, branch, 555.0, 333.0) &&
                    color::findNode(graph, branch)->x == 555.0,
                "Node layout movement must update the persisted graph model.");

        auto curves_graph = color::makePassthroughGraph();
        require(color::insertCurvesAfter(curves_graph, 1) && color::validate(curves_graph),
                "A Curves node must be insertable into a serial correction path.");
        const auto curves_node = std::find_if(curves_graph.nodes.begin(), curves_graph.nodes.end(),
            [](const auto& node) { return node.type == color::NodeType::Curves; });
        require(curves_node != curves_graph.nodes.end() &&
                    color::linearEffectStack(curves_graph)->size() == 1 &&
                    color::linearEffectStack(curves_graph)->front().id == "video.curves",
                "Curves must project to the shared serial effect pipeline.");
        const auto curves_id = curves_node->id;
        auto points = creative_suite::effects::CurvesPointList{{0, 0}, {128, 200}, {255, 255}};
        require(color::setCurvePoints(curves_graph, curves_id, color::CurveKind::Master, points),
                "Curves control points must update through the Color graph model.");
        auto qualifier = curves_graph.nodes.back().curves->parameters.qualifier;
        qualifier.enabled = true;
        qualifier.hue_min = 350;
        qualifier.hue_max = 12;
        qualifier.saturation_min = 20;
        require(color::setCurvesQualifier(curves_graph, curves_id, qualifier) &&
                    color::validate(curves_graph),
                "The Curves node must retain a wrapped hue and tonal qualifier.");
        const auto curves_frame = color::evaluate(curves_graph, sampleFrame());
        require(curves_frame && curves_frame->rgba_pixels.size() == 4 &&
                    curves_frame->rgba_pixels[3] == 255,
                "Color graph Curves evaluation must preserve alpha.");
        qualifier.denoise_percent = 50.0;
        require(color::setCurvesQualifier(curves_graph, curves_id, qualifier),
                "The Curves graph qualifier refinement could not be enabled.");
        int cancellation_checks = 0;
        const auto cancelled_curves = color::evaluate(curves_graph, sampleFrame(), [&] {
            return ++cancellation_checks >= 3;
        });
        require(!cancelled_curves && cancellation_checks >= 3,
                "Color graph evaluation must pass cancellation into matte refinement.");
        auto disabled_curves = curves_graph;
        require(color::setCurvesEnabled(disabled_curves, curves_id, false) &&
                    color::evaluate(disabled_curves, sampleFrame())->rgba_pixels ==
                        sampleFrame().rgba_pixels,
                "A disabled Curves node must pass through its input.");
        require(color::removeEffect(curves_graph, curves_id) && color::validate(curves_graph) &&
                    color::linearEffectStack(curves_graph)->empty(),
                "Removing a Curves node must reconnect the serial graph.");

        const media::VideoFrame no_frame{};
        require(!color::evaluate(graph, no_frame),
                "The evaluator must reject a missing source frame.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
