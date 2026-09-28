#pragma once

#include <creative_suite/animation/animation.h>

#include <array>
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

struct FrameRate {
    std::int64_t numerator = 0;
    std::int64_t denominator = 1;

    [[nodiscard]] constexpr double asDouble() const noexcept
    {
        return denominator > 0
            ? static_cast<double>(numerator) / static_cast<double>(denominator)
            : 0.0;
    }

    friend bool operator==(const FrameRate&, const FrameRate&) = default;
};

// Motion Studio currently accepts these common exact rates when creating a
// composition; fractional rates keep their rational numerator and denominator.
[[nodiscard]] const std::array<FrameRate, 13>& supportedFrameRates() noexcept;
// Returns true only for an entry in supportedFrameRates().
[[nodiscard]] bool isSupportedFrameRate(FrameRate frame_rate) noexcept;

struct CompositionSettings {
    CanvasSize canvas_size;
    FrameRate frame_rate;
    std::int64_t duration_frames;

    friend bool operator==(const CompositionSettings&, const CompositionSettings&) = default;
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
// composited on top. Canvas dimensions, a supported exact frame rate, and a
// positive duration in frames are explicitly required.
class CompositionDocument {
public:
    CompositionDocument(
        int canvas_width,
        int canvas_height,
        FrameRate frame_rate,
        std::int64_t duration_frames);

    [[nodiscard]] CanvasSize canvasSize() const noexcept;
    [[nodiscard]] FrameRate frameRate() const noexcept;
    [[nodiscard]] std::int64_t durationFrames() const noexcept;
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
    FrameRate frame_rate_;
    std::int64_t duration_frames_;
    LayerId next_layer_id_ = 1;
    std::vector<CompositionLayer> layers_;
};

} // namespace motion::model
