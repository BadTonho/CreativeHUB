#pragma once
#include "composition_document.h"

#include <cstdint>
#include <optional>

namespace motion::model {

// The Bezier easing that describes the part of a segment after `progress` (0 < progress < 1) when a
// segment eased by `easing` is cut at that normalized time. The cut curve is subdivided exactly
// (de Casteljau) and re-normalized, so the later piece reproduces the original values. Returns
// nullopt when the remainder cannot be expressed as a valid easing, which only happens for
// non-monotone easings whose control values dip below the cut value.
[[nodiscard]] std::optional<creative_suite::animation::CubicBezierEasing> subdivideEasingRight(
    const creative_suite::animation::CubicBezierEasing& easing, double progress) noexcept;

// Splits one scalar curve at `split_frame` (layer-local). `first` keeps every key up to and including
// the first key at or after the split, so frames before the split evaluate identically. `second` is
// rebased so its frame 0 is the split frame: keys move by -split_frame and, when the split falls inside
// a segment, a boundary key carries the evaluated value and the exactly subdivided easing. An empty
// source stays empty in both halves. Returns false when the easing cannot be subdivided exactly.
[[nodiscard]] bool splitScalarKeyframes(
    const creative_suite::animation::ScalarKeyframes& source, std::int64_t split_frame,
    creative_suite::animation::ScalarKeyframes& first,
    creative_suite::animation::ScalarKeyframes& second);

// Same contract for a keyed Bezier path animation; geometry is evaluated at the boundary.
[[nodiscard]] bool splitPathAnimation(
    const PathAnimation& source, std::int64_t split_frame, PathAnimation& first,
    PathAnimation& second);

// Number of animation keys a layer contributes to the document keyframe budget.
[[nodiscard]] std::size_t layerKeyframeCount(const CompositionLayer& layer) noexcept;

struct LayerSplit {
    CompositionLayer first;
    CompositionLayer second;
};

// Splits `layer` at layer-local `split_frame` (0 < split_frame < duration) into two layers that
// together render identically to the original. `second` receives `second_id` and starts at the split
// frame; media layers advance their source start for the later part. Masks, track mattes, children
// and linked media are checked by the document, not here.
[[nodiscard]] SplitLayerResult splitLayer(
    const CompositionLayer& layer, std::int64_t split_frame, LayerId second_id,
    FrameRate timeline_rate, LayerSplit& result);

} // namespace motion::model
