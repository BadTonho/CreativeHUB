#pragma once

#include "composition_document.h"

#include <unordered_map>

namespace motion::model {

using AffineTransform=creative_suite::composition::AffineTransform2D;
struct ResolvedLayerTransform : creative_suite::animation::Transform2D {
    AffineTransform world;
    bool affine=false;
};
[[nodiscard]] AffineTransform affineOffset(const ParentTransformOffset&);
[[nodiscard]] AffineTransform localGeometry(const creative_suite::animation::Transform2D&,double scale_y,CanvasSize,AnchorPoint);
[[nodiscard]] AffineTransform localLayerGeometry(const CompositionLayer&,CanvasSize,std::int64_t local_frame);
// Source pixels -> canvas pixels. W already contains Anchor and parenting.
[[nodiscard]] AffineTransform sourcePixelGeometry(const AffineTransform& world,int source_width,int source_height,double fit);
[[nodiscard]] std::array<double,2> resolvedAnchorPosition(const ResolvedLayerTransform&,AnchorPoint,CanvasSize);

// Pixel-space composition, left * right. Invalid/overflowing geometry throws.
[[nodiscard]] ParentTransformOffset composeOffsets(const ParentTransformOffset& left,
                                                  const ParentTransformOffset& right);
[[nodiscard]] ParentTransformOffset inverseOffset(const ParentTransformOffset& value);
[[nodiscard]] ParentTransformOffset geometryOffset(
    const creative_suite::animation::Transform2D& transform, CanvasSize canvas, AnchorPoint anchor = {});
// Resolve the local pivot into the selected layer's existing Viewer marker.
[[nodiscard]] std::array<double, 2> resolvedAnchorPosition(
    const creative_suite::animation::Transform2D& resolved, AnchorPoint anchor, CanvasSize canvas);

// Immutable, media-free snapshot. Construction validates IDs, references and
// cycles; evaluation is iterative O(layers), independent of visibility/activity.
// The owner may share a const snapshot with preview/export workers.
class LayerTransformHierarchy {
public:
    LayerTransformHierarchy(CanvasSize canvas, const std::vector<CompositionLayer>& layers);
    [[nodiscard]] bool matches(CanvasSize canvas, const std::vector<CompositionLayer>& layers) const noexcept;
    [[nodiscard]] std::unordered_map<LayerId, ResolvedLayerTransform>
        evaluate(std::int64_t composition_frame, std::optional<LayerPositionOverride> position = {}) const;

private:
    struct Node {
        LayerId id;
        LayerId parent_id;
        ParentTransformOffset offset;
        std::int64_t start;
        creative_suite::animation::Transform2D transform;
        creative_suite::animation::TransformKeyframes keyframes;
        AnchorPoint anchor;
        AnchorKeyframes anchor_keys;
        std::optional<ScaleChannel> scale_y;
        std::optional<AffineTransform> parent_affine;
    };
    CanvasSize canvas_;
    std::vector<Node> nodes_;
    std::unordered_map<LayerId, std::size_t> indices_;
    std::vector<std::size_t> order_;
};

} // namespace motion::model
