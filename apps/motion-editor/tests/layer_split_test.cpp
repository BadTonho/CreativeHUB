#include "model/composition_document.h"
#include "model/layer_split.h"
#include "../../../cmake/test_support/test_check.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

using namespace creative_suite::animation;
using namespace motion::model;

bool near(double a, double b, double tolerance = 1e-6) { return std::fabs(a - b) <= tolerance; }

const CompositionLayer* find(const CompositionDocument& document, LayerId id) {
    const auto& layers = document.layers();
    const auto it = std::find_if(layers.begin(), layers.end(), [id](const auto& l) { return l.id == id; });
    return it == layers.end() ? nullptr : &*it;
}

// The two halves must reproduce the original curve at every composition frame of the layer.
void checkScalarSplit(const ScalarKeyframes& source, std::int64_t split, std::int64_t duration) {
    ScalarKeyframes first, second;
    CS_TEST_CHECK(splitScalarKeyframes(source, split, first, second));
    for (std::int64_t f = 0; f < split; ++f) {
        CS_TEST_CHECK(near(evaluateScalar(7.0, first, f), evaluateScalar(7.0, source, f)));
    }
    for (std::int64_t f = split; f < duration; ++f) {
        CS_TEST_CHECK(near(evaluateScalar(7.0, second, f - split), evaluateScalar(7.0, source, f)));
    }
    CS_TEST_CHECK(validScalarKeyframes(first));
    CS_TEST_CHECK(validScalarKeyframes(second));
}

void testEasingSubdivision() {
    const std::vector<CubicBezierEasing> easings{
        {1.0 / 3, 1.0 / 3, 2.0 / 3, 2.0 / 3},   // linear-equivalent
        {0.42, 0.0, 0.58, 1.0},                 // ease in/out
        {0.25, 0.1, 0.25, 1.0},                 // CSS ease
        {0.0, 0.0, 0.0, 1.0},                   // hard start
        {1.0, 0.0, 1.0, 1.0},                   // hard end
    };
    for (const auto& easing : easings) {
        for (const double p : {0.05, 0.25, 0.5, 0.8, 0.95}) {
            const auto right = subdivideEasingRight(easing, p);
            CS_TEST_CHECK(right.has_value());
            CS_TEST_CHECK(validCubicBezierEasing(*right));
            const double yp = evaluateEasing(p, InterpolationMode::CubicBezier, easing);
            for (int i = 0; i <= 20; ++i) {
                const double t = i / 20.0;
                const double expected = evaluateEasing(p + t * (1 - p), InterpolationMode::CubicBezier, easing);
                const double actual =
                    yp + (1 - yp) * evaluateEasing(t, InterpolationMode::CubicBezier, *right);
                CS_TEST_CHECK(near(expected, actual, 1e-5));
            }
        }
    }
    CS_TEST_CHECK(!subdivideEasingRight(easings[1], 0.0).has_value());
    CS_TEST_CHECK(!subdivideEasingRight(easings[1], 1.0).has_value());
}

void testScalarSplits() {
    ScalarKeyframes eased{
        {10, 0.0, InterpolationMode::CubicBezier, {0.42, 0.0, 0.58, 1.0}},
        {90, 100.0, InterpolationMode::Linear, {}},
        {140, 20.0, InterpolationMode::CubicBezier, {0.25, 0.1, 0.25, 1.0}},
        {190, -30.0, InterpolationMode::Linear, {}},
    };
    for (const std::int64_t split : {1, 5, 10, 11, 50, 89, 90, 100, 139, 140, 165, 189, 190, 199}) {
        checkScalarSplit(eased, split, 200);
    }
    ScalarKeyframes empty;
    ScalarKeyframes first, second;
    CS_TEST_CHECK(splitScalarKeyframes(empty, 30, first, second));
    CS_TEST_CHECK(first.empty() && second.empty());

    // Split exactly on a key: the later half starts with that key and no extra key is added.
    CS_TEST_CHECK(splitScalarKeyframes(eased, 90, first, second));
    CS_TEST_CHECK(second.size() == 3 && second.front().frame == 0 && near(second.front().value, 100.0));
    // Split before the first key adds nothing: both halves already clamp to the first value.
    CS_TEST_CHECK(splitScalarKeyframes(eased, 5, first, second));
    CS_TEST_CHECK(second.size() == eased.size() && second.front().frame == 5);
    // Split after the last key keeps a single constant key.
    CS_TEST_CHECK(splitScalarKeyframes(eased, 199, first, second));
    CS_TEST_CHECK(second.size() == 1 && second.front().frame == 0 && near(second.front().value, -30.0));
    // Only keys up to the first key at or after the split remain in the earlier half.
    CS_TEST_CHECK(splitScalarKeyframes(eased, 50, first, second));
    CS_TEST_CHECK(first.size() == 2 && first.back().frame == 90);
}

CompositionDocument makeDocument(LayerId& id) {
    CompositionDocument document(1920, 1080, FrameRate{30, 1});
    CS_TEST_CHECK(document.addContentLayer(LayerKind::Shape, "Box", 20, &id));
    return document;
}

void testDocumentSplit() {
    LayerId id = 0;
    auto document = makeDocument(id);
    const auto duration = find(document, id)->duration_frames;  // five seconds
    CS_TEST_CHECK(duration == 150);

    CS_TEST_CHECK(document.setLayerKeyframe(id, TransformProperty::PositionX, 0, 0.1));
    CS_TEST_CHECK(document.setLayerKeyframe(id, TransformProperty::PositionX, 100, 0.9));
    CS_TEST_CHECK(document.setLayerKeyframeInterpolation(
        id, TransformProperty::PositionX, 0, InterpolationMode::CubicBezier, {0.42, 0.0, 0.58, 1.0}));
    CS_TEST_CHECK(document.setLayerKeyframe(id, TransformProperty::Opacity, 30, 0.2));
    CS_TEST_CHECK(document.setLayerKeyframe(id, TransformProperty::Opacity, 120, 1.0));
    GaussianBlurEffect blur;
    blur.radius_keyframes = {{0, 2.0, InterpolationMode::Linear, {}}, {149, 60.0, InterpolationMode::Linear, {}}};
    CS_TEST_CHECK(document.setLayerEffects(id, {blur}));

    const CompositionLayer original = *find(document, id);
    LayerId second_id = 0;
    CS_TEST_CHECK(document.splitLayerAtFrame(id, 20 + 61, &second_id) == SplitLayerResult::Split);
    CS_TEST_CHECK(second_id != 0 && second_id != id);
    CS_TEST_CHECK(document.layers().size() == 2);
    // The later half sits directly above the earlier one.
    CS_TEST_CHECK(document.layers()[0].id == id && document.layers()[1].id == second_id);

    const auto* first = find(document, id);
    const auto* second = find(document, second_id);
    CS_TEST_CHECK(first->name == "Box" && second->name == "Box");
    CS_TEST_CHECK(first->timeline_start_frame == 20 && first->duration_frames == 61);
    CS_TEST_CHECK(second->timeline_start_frame == 81 && second->duration_frames == 89);

    for (std::int64_t f = 0; f < duration; ++f) {
        const bool in_first = f < 61;
        const auto& half = in_first ? *first : *second;
        const std::int64_t local = in_first ? f : f - 61;
        const auto expected = evaluateTransform(original.transform, original.keyframes, f);
        const auto actual = evaluateTransform(half.transform, half.keyframes, local);
        CS_TEST_CHECK(near(expected.position_x, actual.position_x));
        CS_TEST_CHECK(near(expected.opacity, actual.opacity));
        CS_TEST_CHECK(near(evaluateEffectParameter(original.effects[0], EffectParameterProperty::GaussianBlurRadius, f),
                           evaluateEffectParameter(half.effects[0], EffectParameterProperty::GaussianBlurRadius, local)));
    }
}

void testRefusals() {
    LayerId id = 0;
    auto document = makeDocument(id);
    const auto untouched = document.layers();
    const auto expect = [&](std::int64_t frame, SplitLayerResult result) {
        LayerId added = 0;
        CS_TEST_CHECK(document.splitLayerAtFrame(id, frame, &added) == result);
        CS_TEST_CHECK(added == 0);
        CS_TEST_CHECK(document.layers() == untouched);
    };
    expect(20, SplitLayerResult::InvalidFrame);          // first frame: nothing before it
    expect(170, SplitLayerResult::InvalidFrame);         // one past the last frame
    expect(5, SplitLayerResult::InvalidFrame);           // before the layer
    CS_TEST_CHECK(document.splitLayerAtFrame(999, 40) == SplitLayerResult::InvalidLayer);

    LayerId child = 0;
    CS_TEST_CHECK(document.addContentLayer(LayerKind::Shape, "Child", 20, &child));
    CS_TEST_CHECK(document.setLayerParent(child, id, 20));
    const auto with_child = document.layers();
    CS_TEST_CHECK(document.splitLayerAtFrame(id, 60) == SplitLayerResult::HasChildren);
    CS_TEST_CHECK(document.layers() == with_child);
    // The child itself can be split; it keeps its parent on both halves.
    LayerId later = 0;
    CS_TEST_CHECK(document.splitLayerAtFrame(child, 60, &later) == SplitLayerResult::Split);
    CS_TEST_CHECK(find(document, later)->parent_id == id);

    LayerId masked = 0;
    CS_TEST_CHECK(document.addContentLayer(LayerKind::Shape, "Masked", 20, &masked));
    CS_TEST_CHECK(document.createMask(masked, -1, creative_suite::composition::ShapeMaskKind::Rectangle) != 0);
    CS_TEST_CHECK(document.splitLayerAtFrame(masked, 60) == SplitLayerResult::HasMasks);

    LayerId target = 0, source = 0;
    CS_TEST_CHECK(document.addContentLayer(LayerKind::Shape, "Target", 20, &target));
    CS_TEST_CHECK(document.addContentLayer(LayerKind::Shape, "Matte", 20, &source));
    CS_TEST_CHECK(document.setLayerTrackMatte(
        target, TrackMatteBinding{source, creative_suite::composition::TrackMatteMode::Alpha}));
    CS_TEST_CHECK(document.splitLayerAtFrame(target, 60) == SplitLayerResult::HasTrackMatte);
    CS_TEST_CHECK(document.splitLayerAtFrame(source, 60) == SplitLayerResult::HasTrackMatte);
}

void testPathSplit() {
    PathGeometry square{{{{-10, -10}, {}, {}}, {{10, -10}, {}, {}}, {{10, 10}, {}, {}}, {{-10, 10}, {}, {}}}, true};
    PathGeometry moved = square;
    for (auto& node : moved.nodes) node.point.x += 40;
    PathAnimation animation{square, {
        {0, square, InterpolationMode::CubicBezier, {0.42, 0.0, 0.58, 1.0}},
        {100, moved, InterpolationMode::Linear, {}}}};
    PathAnimation first, second;
    CS_TEST_CHECK(splitPathAnimation(animation, 37, first, second));
    for (std::int64_t f = 0; f < 37; ++f) {
        CS_TEST_CHECK(near(evaluatePath(first, f).nodes[0].point.x, evaluatePath(animation, f).nodes[0].point.x));
    }
    for (std::int64_t f = 37; f < 120; ++f) {
        CS_TEST_CHECK(near(evaluatePath(second, f - 37).nodes[2].point.x, evaluatePath(animation, f).nodes[2].point.x));
    }
}

void testVideoSourceAdvance() {
    CompositionLayer layer{1, LayerKind::Video, "Clip"};
    layer.source_path = "clip.mp4";
    layer.timeline_start_frame = 10;
    layer.duration_frames = 100;
    layer.source_frame_count = 1000;
    layer.source_start_frame = 200;
    layer.source_frame_rate = 60.0;  // source runs at twice the 30 fps timeline rate
    layer.maximum_timeline_duration_frames = 400;
    LayerSplit split;
    CS_TEST_CHECK(splitLayer(layer, 40, 2, FrameRate{30, 1}, split) == SplitLayerResult::Split);
    CS_TEST_CHECK(split.first.source_start_frame == 200 && split.first.duration_frames == 40);
    CS_TEST_CHECK(split.second.id == 2 && split.second.timeline_start_frame == 50);
    CS_TEST_CHECK(split.second.duration_frames == 60);
    CS_TEST_CHECK(split.second.source_start_frame == 280);            // 40 timeline frames = 80 source frames
    CS_TEST_CHECK(split.second.maximum_timeline_duration_frames == 360);
}

} // namespace

int main() {
    testEasingSubdivision();
    testScalarSplits();
    testDocumentSplit();
    testRefusals();
    testPathSplit();
    testVideoSourceAdvance();
    std::cout << "Layer split tests passed\n";
    return 0;
}
