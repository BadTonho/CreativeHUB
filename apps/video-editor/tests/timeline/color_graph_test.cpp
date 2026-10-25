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

        auto catalog_drop_graph = empty;
        const auto initial_connections = catalog_drop_graph.connections;
        const auto dropped_id = color::addDisconnectedEffectNode(
            catalog_drop_graph,
            creative_suite::effects::makeDefaultInstance("video.brightness"),
            260.0, 300.0);
        const auto* dropped_node = dropped_id
            ? color::findNode(catalog_drop_graph, *dropped_id) : nullptr;
        require(dropped_id == 3 && dropped_node &&
                    dropped_node->type == color::NodeType::Effect &&
                    dropped_node->effect &&
                    dropped_node->effect->id == "video.brightness" &&
                    dropped_node->x == 260.0 && dropped_node->y == 300.0 &&
                    catalog_drop_graph.connections == initial_connections &&
                    color::validate(catalog_drop_graph),
                "A catalog drop must create a valid, disconnected effect at its canvas position.");
        const auto before_invalid_drop = catalog_drop_graph;
        require(!color::addDisconnectedEffectNode(
                    catalog_drop_graph,
                    creative_suite::effects::makeDefaultInstance("unknown.effect"),
                    400.0, 500.0) && catalog_drop_graph == before_invalid_drop,
                "An invalid catalog effect must be rejected without changing the graph.");

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
        auto upstream_graph = color::makePassthroughGraph();
        const auto upstream_effect = creative_suite::effects::makeDefaultInstance("video.grayscale");
        require(color::insertEffectAfter(upstream_graph, 1, upstream_effect),
                "The upstream input fixture could not add its first correction.");
        const auto upstream_id = color::effectNodesInOrder(upstream_graph).front();
        require(color::insertCurvesAfter(upstream_graph, upstream_id),
                "The upstream input fixture could not add Curves.");
        const auto upstream_curves_id = std::find_if(upstream_graph.nodes.begin(),
            upstream_graph.nodes.end(), [](const auto& node) {
                return node.type == color::NodeType::Curves;
            })->id;
        auto color_input_frame = sampleFrame();
        color_input_frame.rgba_pixels = {220, 40, 80, 173};
        const auto curves_input = color::evaluateInput(
            upstream_graph, upstream_curves_id, color_input_frame);
        require(curves_input && curves_input->rgba_pixels ==
                    std::vector<std::uint8_t>{81, 81, 81, 173},
                "Curves input inspection must include preceding graph nodes and preserve alpha.");
        const auto prefix_stack = color::linearEffectStackBefore(
            upstream_graph, upstream_curves_id);
        require(prefix_stack && prefix_stack->size() == 1 &&
                    prefix_stack->front().id == "video.grayscale",
                "The GPU Curves tap must contain only corrections before the selected node.");
        require(!color::evaluateInput(upstream_graph, upstream_id, color_input_frame),
                "Input inspection must reject a non-Curves node.");
        auto branched_input_graph = color::makePassthroughGraph();
        require(color::addParallelBranch(branched_input_graph, 1, upstream_effect),
                "The branched Curves tap fixture could not add a parallel branch.");
        const auto branch_mixer = std::find_if(branched_input_graph.nodes.begin(),
            branched_input_graph.nodes.end(), [](const color::Node& node) {
                return node.type == color::NodeType::ParallelMixer;
            });
        require(branch_mixer != branched_input_graph.nodes.end() &&
                    color::insertCurvesAfter(branched_input_graph, branch_mixer->id),
                "The branched Curves tap fixture could not place Curves after its mixer.");
        const auto branched_curves = std::find_if(branched_input_graph.nodes.begin(),
            branched_input_graph.nodes.end(), [](const color::Node& node) {
                return node.type == color::NodeType::Curves;
            });
        require(branched_curves != branched_input_graph.nodes.end() &&
                    !color::linearEffectStackBefore(branched_input_graph,
                        branched_curves->id),
                "The GPU prefix tap must decline paths that require a Parallel Mixer.");
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

        auto window_graph = color::fromEffectStack(std::vector{
            creative_suite::effects::makeDefaultInstance("video.brightness")});
        const auto window_node = color::effectNodesInOrder(window_graph).front();
        const auto first_window = color::addPowerWindow(window_graph, window_node,
            creative_suite::composition::ShapeMaskKind::Rectangle);
        const auto second_window = color::addPowerWindow(window_graph, window_node,
            creative_suite::composition::ShapeMaskKind::Ellipse);
        require(first_window && second_window && color::validate(window_graph) &&
                    !color::linearEffectStack(window_graph),
                "An effect node must store multiple Power Windows and require graph evaluation.");
        auto* effect_node = color::findNode(window_graph, window_node);
        auto first_geometry = effect_node->power_windows[0].parameters;
        first_geometry.width = 1.0;
        first_geometry.height = 1.0;
        auto second_geometry = first_geometry;
        second_geometry.width = .4;
        second_geometry.height = .4;
        require(color::setPowerWindowGeometry(window_graph, window_node, *first_window, 0,
                    first_geometry) &&
                color::setPowerWindowGeometry(window_graph, window_node, *second_window, 0,
                    second_geometry) &&
                color::setPowerWindowSettings(window_graph, window_node, *second_window,
                    .02, false, color::WindowCombineMode::Subtract),
                "Power Window geometry, softness and ordered combine settings must be editable.");
        require(color::setEffectParameter(window_graph, window_node, "amount", 100.0),
                "The Power Window correction fixture could not be configured.");
        auto window_source = media::VideoFrame{5, 5, 20,
            std::vector<std::uint8_t>(5 * 5 * 4)};
        for (std::size_t pixel = 0; pixel < 25; ++pixel) {
            window_source.rgba_pixels[pixel * 4] = 100;
            window_source.rgba_pixels[pixel * 4 + 1] = 80;
            window_source.rgba_pixels[pixel * 4 + 2] = 60;
            window_source.rgba_pixels[pixel * 4 + 3] = 173;
        }
        const auto subtracted = color::evaluateAtFrame(window_graph, window_source, 0);
        require(subtracted && subtracted->rgba_pixels[2 * 20] > 100 &&
                    subtracted->rgba_pixels[2 * 20 + 3] == 173 &&
                    subtracted->rgba_pixels[(2 * 5 + 2) * 4] == 100,
                "Subtracted Power Windows must preserve correction outside the cutout and retain source alpha.");
        require(color::setPowerWindowSettings(window_graph, window_node, *second_window,
                    .02, true, color::WindowCombineMode::Subtract),
                "Power Window inversion must be editable.");
        const auto inverted_subtraction = color::evaluateAtFrame(window_graph,
            window_source, 0);
        require(inverted_subtraction &&
                    inverted_subtraction->rgba_pixels[(2 * 5 + 2) * 4] > 100,
                "An inverted subtraction must use the inverted window coverage.");
        require(color::setPowerWindowSettings(window_graph, window_node, *second_window,
                    .02, false, color::WindowCombineMode::Intersect),
                "Intersect must be a valid ordered combine operation.");
        const auto intersected = color::evaluateAtFrame(window_graph, window_source, 0);
        require(intersected && intersected->rgba_pixels[(2 * 5 + 2) * 4] > 100 &&
                    intersected->rgba_pixels[0] == 100 &&
                    intersected->rgba_pixels[(2 * 5 + 2) * 4 + 3] == 173,
                "Intersect must constrain correction to the overlap and preserve alpha.");
        require(color::reorderPowerWindow(window_graph, window_node, 1, 0) &&
                    color::findNode(window_graph, window_node)->power_windows.front().combine_mode ==
                        color::WindowCombineMode::Add && color::validate(window_graph),
                "Reordering windows must make the first operation Add and preserve a valid graph.");
        auto tracked_geometry = first_geometry;
        auto tracked_keys = creative_suite::composition::ShapeMaskKeyframes{};
        tracked_keys[0] = {{0, .25}, {10, .75}};
        require(color::setPowerWindowTrack(window_graph, window_node, *first_window,
                    tracked_geometry, tracked_keys),
                "Power Window geometry keyframes must be stored in the node graph.");
        const auto frame_zero = color::evaluateAtFrame(window_graph, window_source, 0);
        const auto frame_ten = color::evaluateAtFrame(window_graph, window_source, 10);
        require(frame_zero && frame_ten && frame_zero->rgba_pixels.size() ==
                    frame_ten->rgba_pixels.size(),
                "Animated window geometry must evaluate on the clip-local frame.");
        require(color::clearPowerWindowTrack(window_graph, window_node, *first_window) &&
                    color::removePowerWindow(window_graph, window_node, *first_window) &&
                    color::validate(window_graph),
                "A tracked window must support clearing its animation and removal.");

        auto qualified_graph = color::makePassthroughGraph();
        require(color::insertCurvesAfter(qualified_graph, 1),
                "The qualifier Power Window fixture could not add Curves.");
        const auto qualified_node = std::find_if(qualified_graph.nodes.begin(),
            qualified_graph.nodes.end(), [](const auto& item) {
                return item.type == color::NodeType::Curves;
            })->id;
        const auto qualified_window = color::addPowerWindow(qualified_graph,
            qualified_node, creative_suite::composition::ShapeMaskKind::Ellipse);
        auto qualified_curves = color::findNode(qualified_graph, qualified_node)->curves->parameters;
        qualified_curves.rgb.points = {{0, 0}, {128, 210}, {255, 255}};
        auto red_qualifier = qualified_curves.qualifier;
        red_qualifier.enabled = true;
        red_qualifier.hue_min = 345;
        red_qualifier.hue_max = 15;
        require(qualified_window &&
                    color::setCurvePoints(qualified_graph, qualified_node,
                        color::CurveKind::Master, qualified_curves.rgb.points) &&
                    color::setCurvesQualifier(qualified_graph, qualified_node,
                        red_qualifier),
                "Power Windows must coexist with Curves qualifier controls.");
        const auto qualified_frame = color::evaluateAtFrame(qualified_graph,
            media::VideoFrame{2, 2, 8, {210, 40, 30, 111, 30, 40, 210, 92,
                80, 20, 12, 201, 40, 40, 40, 54}}, 0);
        require(qualified_frame && qualified_frame->rgba_pixels[3] == 111 &&
                    qualified_frame->rgba_pixels[7] == 92 &&
                    qualified_frame->rgba_pixels[11] == 201 &&
                    qualified_frame->rgba_pixels[15] == 54,
                "Power Window and qualifier evaluation must preserve source alpha.");

        const media::VideoFrame no_frame{};
        require(!color::evaluate(graph, no_frame),
                "The evaluator must reject a missing source frame.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
