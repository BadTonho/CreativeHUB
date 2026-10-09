#pragma once

#include <creative_suite/animation/animation.h>
#include <creative_suite/media/video_metadata.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
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
[[nodiscard]] std::optional<std::int64_t> sourceFrameForTimelineFrame(
    std::int64_t local_timeline_frame,
    std::int64_t source_start_frame,
    double source_frame_rate,
    FrameRate timeline_frame_rate,
    std::int64_t source_frame_count) noexcept;

struct CompositionSettings {
    CanvasSize canvas_size;
    FrameRate frame_rate;

    friend bool operator==(const CompositionSettings&, const CompositionSettings&) = default;
};

enum class LayerKind : std::uint8_t {
    Text,
    Shape,
    Image,
    Video,
};

using ColorRgba = std::array<std::uint8_t, 4>;

enum class TextAlignment : std::uint8_t {
    Left,
    Center,
    Right,
};

struct TextLayerContent {
    std::string text = "Text";
    std::string font_family = "Sans Serif";
    int font_size_pixels = 48;
    ColorRgba color{255, 255, 255, 255};
    TextAlignment alignment = TextAlignment::Center;
    int box_width = 1;
    int box_height = 1;

    friend bool operator==(const TextLayerContent&, const TextLayerContent&) = default;
};

enum class ShapeKind : std::uint8_t {
    Rectangle,
    Ellipse,
};

struct ShapeLayerContent {
    ShapeKind shape = ShapeKind::Rectangle;
    int width = 1;
    int height = 1;
    ColorRgba fill_color{255, 183, 54, 255};
    ColorRgba stroke_color{255, 255, 255, 255};
    int stroke_width_pixels = 0;

    friend bool operator==(const ShapeLayerContent&, const ShapeLayerContent&) = default;
};

using LayerContent = std::variant<std::monostate, TextLayerContent, ShapeLayerContent>;

struct GaussianBlurEffect {
    bool enabled = true;
    double radius_pixels = 10.0;

    friend bool operator==(const GaussianBlurEffect&, const GaussianBlurEffect&) = default;
};

struct ColorAdjustmentEffect {
    bool enabled = true;
    double brightness = 0.0;
    double contrast_percent = 100.0;
    double saturation_percent = 100.0;

    friend bool operator==(const ColorAdjustmentEffect&,
                           const ColorAdjustmentEffect&) = default;
};

using LayerEffect = std::variant<GaussianBlurEffect, ColorAdjustmentEffect>;

struct LinkedImageDocument {
    std::filesystem::path document_path;
    std::filesystem::path published_output_path;
    std::filesystem::path source_snapshot_path;

    friend bool operator==(const LinkedImageDocument&, const LinkedImageDocument&) = default;
};

[[nodiscard]] bool validLayerEffect(const LayerEffect& effect) noexcept;
[[nodiscard]] bool validLayerEffects(const std::vector<LayerEffect>& effects) noexcept;

[[nodiscard]] TextLayerContent defaultTextLayerContent(CanvasSize canvas_size);
[[nodiscard]] ShapeLayerContent defaultShapeLayerContent(
    CanvasSize canvas_size,
    ShapeKind shape = ShapeKind::Rectangle);

struct CompositionLayer {
    LayerId id;
    LayerKind kind;
    std::string name;
    std::filesystem::path source_path;
    std::optional<LinkedImageDocument> linked_image;
    std::int64_t timeline_start_frame = 0;
    std::int64_t duration_frames = 0;
    std::int64_t source_frame_count = 0;
    std::int64_t source_start_frame = 0;
    std::int64_t source_duration_frames = 0;
    std::int64_t maximum_timeline_duration_frames = 0;
    double source_frame_rate = 0.0;
    bool visible = true;
    creative_suite::animation::Transform2D transform;
    creative_suite::animation::TransformKeyframes keyframes;
    LayerContent content;
    std::vector<LayerEffect> effects;

    friend bool operator==(const CompositionLayer&, const CompositionLayer&) = default;
};

enum class AddMediaLayerResult {
    Added,
    InvalidKind,
    InvalidPath,
    InvalidPosition,
    InvalidTimingMetadata,
};

// An in-memory document model. Layer order is back-to-front; the last layer is
// composited on top. Canvas dimensions and a supported exact frame rate are
// explicitly required; the composition has no fixed end frame.
class CompositionDocument {
public:
    CompositionDocument(
        int canvas_width,
        int canvas_height,
        FrameRate frame_rate);
    // Validated reconstruction path for persisted layer records. Existing IDs
    // and ordering are retained; the next generated ID follows the greatest ID.
    CompositionDocument(
        int canvas_width,
        int canvas_height,
        FrameRate frame_rate,
        std::vector<CompositionLayer> layers);

    [[nodiscard]] CanvasSize canvasSize() const noexcept;
    [[nodiscard]] FrameRate frameRate() const noexcept;
    [[nodiscard]] const std::vector<CompositionLayer>& layers() const noexcept;

    [[nodiscard]] LayerId addLayer(LayerKind kind, std::string name);
    [[nodiscard]] bool addContentLayer(
        LayerKind kind,
        std::string name,
        std::int64_t timeline_start_frame,
        LayerId* added_id = nullptr);
    [[nodiscard]] AddMediaLayerResult addMediaLayer(
        const creative_suite::media::VideoMetadata& metadata,
        std::int64_t timeline_start_frame,
        LayerId* added_id = nullptr);
    [[nodiscard]] bool removeLayer(LayerId id) noexcept;
    // Moves the layer to its final index in the back-to-front layer order.
    [[nodiscard]] bool moveLayer(LayerId id, std::size_t final_index) noexcept;
    [[nodiscard]] bool moveLayerInTimeline(
        LayerId id,
        std::int64_t timeline_start_frame) noexcept;
    [[nodiscard]] bool resizeLayerDuration(
        LayerId id,
        std::int64_t duration_frames) noexcept;
    [[nodiscard]] bool setVideoSourceRange(
        LayerId id,
        std::int64_t source_start_frame,
        std::int64_t duration_frames,
        std::int64_t maximum_timeline_duration_frames) noexcept;
    [[nodiscard]] bool setLayerName(LayerId id, std::string name);
    [[nodiscard]] bool setLayerLinkedImage(
        LayerId id,
        std::optional<LinkedImageDocument> linked_image);
    [[nodiscard]] bool setLayerVisible(LayerId id, bool visible) noexcept;
    [[nodiscard]] bool setLayerTransform(
        LayerId id,
        const creative_suite::animation::Transform2D& transform) noexcept;
    [[nodiscard]] bool setTextLayerContent(
        LayerId id,
        const TextLayerContent& content);
    [[nodiscard]] bool setShapeLayerContent(
        LayerId id,
        const ShapeLayerContent& content);
    [[nodiscard]] bool setLayerEffects(
        LayerId id,
        const std::vector<LayerEffect>& effects);
    [[nodiscard]] bool replaceLayerKeyframes(
        LayerId id,
        creative_suite::animation::TransformProperty property,
        const std::vector<creative_suite::animation::Keyframe>& keyframes);
    [[nodiscard]] bool setLayerKeyframe(
        LayerId id,
        creative_suite::animation::TransformProperty property,
        std::int64_t local_frame,
        double value) noexcept;
    [[nodiscard]] bool setLayerKeyframeInterpolation(
        LayerId id,
        creative_suite::animation::TransformProperty property,
        std::int64_t local_frame,
        creative_suite::animation::InterpolationMode interpolation,
        const creative_suite::animation::CubicBezierEasing& easing) noexcept;
    [[nodiscard]] bool removeLayerKeyframe(
        LayerId id,
        creative_suite::animation::TransformProperty property,
        std::int64_t local_frame) noexcept;
    [[nodiscard]] bool moveLayerKeyframe(
        LayerId id,
        creative_suite::animation::TransformProperty property,
        std::int64_t from_local_frame,
        std::int64_t to_local_frame) noexcept;

private:
    [[nodiscard]] CompositionLayer* findLayer(LayerId id) noexcept;

    CanvasSize canvas_size_;
    FrameRate frame_rate_;
    LayerId next_layer_id_ = 1;
    std::vector<CompositionLayer> layers_;
};

} // namespace motion::model
