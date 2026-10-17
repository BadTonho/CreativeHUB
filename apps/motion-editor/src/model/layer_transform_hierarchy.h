#pragma once

#include "composition_document.h"

#include <unordered_map>

namespace motion::model {

// Pixel-space composition, left * right. Invalid/overflowing geometry throws.
[[nodiscard]] ParentTransformOffset composeOffsets(const ParentTransformOffset& left,
                                                  const ParentTransformOffset& right);
[[nodiscard]] ParentTransformOffset inverseOffset(const ParentTransformOffset& value);
[[nodiscard]] ParentTransformOffset geometryOffset(
    const creative_suite::animation::Transform2D& transform, CanvasSize canvas);

// Immutable, media-free snapshot. Construction validates IDs, references and
// cycles; evaluation is iterative O(layers), independent of visibility/activity.
// The owner may share a const snapshot with preview/export workers.
class LayerTransformHierarchy {
public:
    LayerTransformHierarchy(CanvasSize canvas, const std::vector<CompositionLayer>& layers);
    [[nodiscard]] bool matches(CanvasSize canvas, const std::vector<CompositionLayer>& layers) const noexcept;
    [[nodiscard]] std::unordered_map<LayerId, creative_suite::animation::Transform2D>
        evaluate(std::int64_t composition_frame) const;

private:
    struct Node {
        LayerId id;
        LayerId parent_id;
        ParentTransformOffset offset;
        std::int64_t start;
        creative_suite::animation::Transform2D transform;
        creative_suite::animation::TransformKeyframes keyframes;
    };
    CanvasSize canvas_;
    std::vector<Node> nodes_;
    std::unordered_map<LayerId, std::size_t> indices_;
    std::vector<std::size_t> order_;
};

} // namespace motion::model
