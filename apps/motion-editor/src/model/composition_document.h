#pragma once

#include <creative_suite/animation/animation.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace motion::model {

using LayerId = std::uint64_t;

struct CanvasSize {
    int width;
    int height;

    friend bool operator==(const CanvasSize&, const CanvasSize&) = default;
};

enum class LayerKind : std::uint8_t {
    Text,
    Shape,
    Image,
    Video,
};

struct CompositionLayer {
    LayerId id;
    LayerKind kind;
    std::string name;
    bool visible = true;
    creative_suite::animation::Transform2D transform;
    creative_suite::animation::TransformKeyframes keyframes;
};

// An in-memory document model. Layer order is back-to-front; the last layer is
// composited on top. A document always requires an explicit positive canvas.
class CompositionDocument {
public:
    CompositionDocument(int canvas_width, int canvas_height);

    [[nodiscard]] CanvasSize canvasSize() const noexcept;
    [[nodiscard]] const std::vector<CompositionLayer>& layers() const noexcept;

    [[nodiscard]] LayerId addLayer(LayerKind kind, std::string name);
    [[nodiscard]] bool removeLayer(LayerId id) noexcept;
    // Moves the layer to its final index in the back-to-front layer order.
    [[nodiscard]] bool moveLayer(LayerId id, std::size_t final_index) noexcept;
    [[nodiscard]] bool setLayerName(LayerId id, std::string name);
    [[nodiscard]] bool setLayerVisible(LayerId id, bool visible) noexcept;
    [[nodiscard]] bool setLayerTransform(
        LayerId id,
        const creative_suite::animation::Transform2D& transform) noexcept;
    [[nodiscard]] bool setLayerKeyframe(
        LayerId id,
        creative_suite::animation::TransformProperty property,
        std::int64_t local_frame,
        double value) noexcept;

private:
    [[nodiscard]] CompositionLayer* findLayer(LayerId id) noexcept;

    CanvasSize canvas_size_;
    LayerId next_layer_id_ = 1;
    std::vector<CompositionLayer> layers_;
};

} // namespace motion::model
