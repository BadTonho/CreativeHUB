#pragma once
#include <creative_suite/composition/affine_transform.h>

#include <creative_suite/animation/animation.h>
#include <creative_suite/composition_exchange/document.h>
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
inline constexpr std::size_t maximum_document_keyframes = 2'000'000;

// Evaluated local Position only; never serialized as gesture state.
struct LayerPositionOverride {
    LayerId layer_id = 0;
    double x = 0, y = 0;
    friend bool operator==(const LayerPositionOverride&, const LayerPositionOverride&) = default;
};

// Evaluated local Rotation only; never serialized as gesture state.
struct LayerRotationOverride {
    LayerId layer_id = 0;
    double degrees = 0;
    friend bool operator==(const LayerRotationOverride&, const LayerRotationOverride&) = default;
};

struct LayerScaleOverride {
    LayerId layer_id = 0;
    double x = 1, y = 1;
    friend bool operator==(const LayerScaleOverride&, const LayerScaleOverride&) = default;
};

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
    std::string name = "Composition";
    std::int64_t duration = 0;

    friend bool operator==(const CompositionSettings&, const CompositionSettings&) = default;
};

enum class LayerKind : std::uint8_t {
    Text,
    Shape,
    Image,
    Video,
    Null,
};

// A canvas-pixel similarity transform. Identity also applies to unparented
// layers; a retained offset preserves animation when a parent is removed.
struct ParentTransformOffset {
    double translation_x_pixels = 0.0;
    double translation_y_pixels = 0.0;
    double scale = 1.0;
    double rotation_degrees = 0.0;

    friend bool operator==(const ParentTransformOffset&, const ParentTransformOffset&) = default;
};

[[nodiscard]] bool validParentOffset(const ParentTransformOffset&) noexcept;

enum class AnchorProperty : std::uint8_t { X, Y };

// Pixels relative to the center of the canvas-fitted layer, before editable
// scale/rotation. This space does not require online media or source dimensions.
struct AnchorPoint {
    double x_pixels = 0.0;
    double y_pixels = 0.0;
    friend bool operator==(const AnchorPoint&, const AnchorPoint&) = default;
};
using AnchorKeyframes = std::array<creative_suite::animation::ScalarKeyframes, 2>;
[[nodiscard]] bool validAnchorPoint(const AnchorPoint&) noexcept;

enum class ScaleAxis : std::uint8_t { X, Y };
struct ScaleChannel {
    double value=1;
    creative_suite::animation::ScalarKeyframes keyframes;
    friend bool operator==(const ScaleChannel&,const ScaleChannel&)=default;
};
[[nodiscard]] bool validScaleChannel(const ScaleChannel&) noexcept;
[[nodiscard]] bool validParentAffine(const creative_suite::composition::AffineTransform2D&) noexcept;

using ColorRgba = std::array<std::uint8_t, 4>;

enum class TextAlignment : std::uint8_t {
    Left,
    Center,
    Right,
};

struct TextLayerContent {
    std::string text = "Text";
    std::string font_family = "Sans Serif";
    double font_size_pixels = 48;
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

using Mask = creative_suite::composition_exchange::Mask;
using MaskId = creative_suite::composition_exchange::MaskId;

enum class EffectParameterProperty : std::uint8_t {
    GaussianBlurRadius,
    ColorBrightness,
    ColorContrastPercent,
    ColorSaturationPercent,
};

struct GaussianBlurEffect {
    bool enabled = true;
    double radius_pixels = 10.0;
    MaskId mask = 0;
    creative_suite::animation::ScalarKeyframes radius_keyframes;

    friend bool operator==(const GaussianBlurEffect&, const GaussianBlurEffect&) = default;
};

struct ColorAdjustmentEffect {
    bool enabled = true;
    double brightness = 0.0;
    double contrast_percent = 100.0;
    double saturation_percent = 100.0;
    MaskId mask = 0;
    creative_suite::animation::ScalarKeyframes brightness_keyframes;
    creative_suite::animation::ScalarKeyframes contrast_keyframes;
    creative_suite::animation::ScalarKeyframes saturation_keyframes;

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
[[nodiscard]] bool effectHasProperty(
    const LayerEffect& effect,
    EffectParameterProperty property) noexcept;
[[nodiscard]] const creative_suite::animation::ScalarKeyframes* effectKeyframesFor(
    const LayerEffect& effect,
    EffectParameterProperty property) noexcept;
[[nodiscard]] creative_suite::animation::ScalarKeyframes* mutableEffectKeyframesFor(
    LayerEffect& effect,
    EffectParameterProperty property) noexcept;
[[nodiscard]] double evaluateEffectParameter(
    const LayerEffect& effect,
    EffectParameterProperty property,
    std::int64_t local_frame) noexcept;
[[nodiscard]] bool hasEffectKeyframeAtFrame(
    const LayerEffect& effect,
    EffectParameterProperty property,
    std::int64_t local_frame) noexcept;
[[nodiscard]] bool layerEffectHasKeyframes(const LayerEffect& effect) noexcept;
[[nodiscard]] GaussianBlurEffect evaluateGaussianBlurEffect(
    const GaussianBlurEffect& effect,
    std::int64_t local_frame);
[[nodiscard]] ColorAdjustmentEffect evaluateColorAdjustmentEffect(
    const ColorAdjustmentEffect& effect,
    std::int64_t local_frame);
[[nodiscard]] LayerEffect evaluateLayerEffect(
    const LayerEffect& effect,
    std::int64_t local_frame);
[[nodiscard]] std::vector<LayerEffect> evaluateLayerEffects(
    const std::vector<LayerEffect>& effects,
    std::int64_t local_frame);

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
    std::optional<creative_suite::composition_exchange::Presentation> presentation;
    MaskId source_mask = 0, transform_mask = 0;
    LayerId parent_id = 0;
    ParentTransformOffset parent_offset;
    AnchorPoint anchor_point;
    AnchorKeyframes anchor_keyframes;
    // Absent Y follows the legacy Scale base/curve exactly; X retains that storage.
    std::optional<ScaleChannel> scale_y;
    bool scale_linked=true;
    std::optional<creative_suite::composition::AffineTransform2D> parent_affine;

    friend bool operator==(const CompositionLayer&, const CompositionLayer&) = default;
};

[[nodiscard]] AnchorPoint evaluateLayerAnchor(const CompositionLayer&, std::int64_t local_frame) noexcept;
[[nodiscard]] double layerScaleBase(const CompositionLayer&,ScaleAxis) noexcept;
[[nodiscard]] const creative_suite::animation::ScalarKeyframes& layerScaleKeys(const CompositionLayer&,ScaleAxis) noexcept;
[[nodiscard]] std::array<double,2> evaluateLayerScale(const CompositionLayer&,std::int64_t local_frame) noexcept;

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
    std::string interchange_name = "Composition";
    std::int64_t interchange_duration = 0;
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
        std::vector<CompositionLayer> layers, std::vector<Mask> masks = {});

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
    // Detaches direct children without changing their pose at composition_frame.
    [[nodiscard]] bool removeLayer(LayerId id, std::int64_t composition_frame = 0);
    [[nodiscard]] bool canSetLayerParent(LayerId child, LayerId parent) const noexcept;
    // Existing nonzero IDs in document order, excluding child and descendants.
    // Iterative O(layers), independent of visibility, media and layer intervals.
    [[nodiscard]] std::vector<LayerId> eligibleLayerParents(LayerId child) const;
    // Keeps the current global pose and all local keyframes. Zero clears the parent.
    [[nodiscard]] bool setLayerParent(LayerId child, LayerId parent, std::int64_t composition_frame);
    [[nodiscard]] bool setLayerAnchorPoint(LayerId, AnchorPoint);
    [[nodiscard]] bool setLayerScaleLinked(LayerId,bool);
    [[nodiscard]] bool setLayerScaleAxisBase(LayerId,ScaleAxis,double);
    [[nodiscard]] bool replaceLayerScaleAxisKeyframes(LayerId,ScaleAxis,creative_suite::animation::ScalarKeyframes);
    [[nodiscard]] bool setLayerScaleAxisKeyframe(LayerId,ScaleAxis,std::int64_t,double);
    [[nodiscard]] bool removeLayerScaleAxisKeyframe(LayerId,ScaleAxis,std::int64_t);
    [[nodiscard]] bool moveLayerScaleAxisKeyframe(LayerId,ScaleAxis,std::int64_t,std::int64_t);
    [[nodiscard]] bool setLayerScaleAxisKeyframeInterpolation(LayerId,ScaleAxis,std::int64_t,
        creative_suite::animation::InterpolationMode,const creative_suite::animation::CubicBezierEasing&);
    // Inspector edit: respects per-axis keyed/read-only rules, optionally edits
    // both axes proportionally in one staged transaction. Values are factors.
    [[nodiscard]] bool editLayerScaleAxis(LayerId,ScaleAxis,double,std::int64_t composition_frame,
        std::optional<double> linked_y_over_x = {});
    [[nodiscard]] bool replaceLayerAnchorKeyframes(LayerId, AnchorProperty, creative_suite::animation::ScalarKeyframes);
    [[nodiscard]] bool setLayerAnchorKeyframe(LayerId, AnchorProperty, std::int64_t local_frame, double);
    [[nodiscard]] bool removeLayerAnchorKeyframe(LayerId, AnchorProperty, std::int64_t local_frame);
    [[nodiscard]] bool moveLayerAnchorKeyframe(LayerId, AnchorProperty, std::int64_t from, std::int64_t to);
    [[nodiscard]] bool setLayerAnchorKeyframeInterpolation(LayerId, AnchorProperty, std::int64_t local_frame,
        creative_suite::animation::InterpolationMode, const creative_suite::animation::CubicBezierEasing&);
    // Atomic current-frame edit. Animated affected properties get keys at this
    // frame; existing keys and binding compensation stay intact.
    [[nodiscard]] bool repositionLayerAnchorKeepingPose(LayerId, AnchorPoint, std::int64_t composition_frame);
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
    // Atomic static/animated Position edit at a composition frame, including
    // insertion between keys. All other authored properties are retained.
    [[nodiscard]] bool setLayerPositionAtFrame(LayerId, double x, double y, std::int64_t composition_frame);
    // Atomic Rotation base/key edit with the same frame/key-budget policy.
    [[nodiscard]] bool setLayerRotationAtFrame(LayerId, double degrees, std::int64_t composition_frame);
    // Final local factors; linkage is already applied by the caller. Inserts
    // only changed animated axes and counts materialized Y keys atomically.
    [[nodiscard]] bool setLayerScaleAtFrame(LayerId, double x, double y, std::int64_t composition_frame);
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
    [[nodiscard]] bool replaceEffectKeyframes(
        LayerId id,
        std::size_t effect_index,
        EffectParameterProperty property,
        const creative_suite::animation::ScalarKeyframes& keyframes);
    [[nodiscard]] bool setEffectKeyframe(
        LayerId id,
        std::size_t effect_index,
        EffectParameterProperty property,
        std::int64_t local_frame,
        double value) noexcept;
    [[nodiscard]] bool setEffectKeyframeInterpolation(
        LayerId id,
        std::size_t effect_index,
        EffectParameterProperty property,
        std::int64_t local_frame,
        creative_suite::animation::InterpolationMode interpolation,
        const creative_suite::animation::CubicBezierEasing& easing) noexcept;
    [[nodiscard]] bool removeEffectKeyframe(
        LayerId id,
        std::size_t effect_index,
        EffectParameterProperty property,
        std::int64_t local_frame) noexcept;
    [[nodiscard]] bool moveEffectKeyframe(
        LayerId id,
        std::size_t effect_index,
        EffectParameterProperty property,
        std::int64_t from_local_frame,
        std::int64_t to_local_frame) noexcept;

    // Targets: -2 content, -1 transform, non-negative effect index.
    [[nodiscard]] const std::vector<Mask>& masks() const noexcept { return masks_; }
    [[nodiscard]] const Mask* findMask(MaskId) const noexcept;
    [[nodiscard]] MaskId maskForTarget(LayerId, int target) const noexcept;
    [[nodiscard]] std::size_t maskUseCount(MaskId) const noexcept;
    [[nodiscard]] MaskId createMask(LayerId, int target, creative_suite::composition::ShapeMaskKind);
    [[nodiscard]] bool associateMask(LayerId, int target, MaskId);
    [[nodiscard]] bool setMaskParameters(MaskId, const creative_suite::composition::ShapeMaskParameters&);
    [[nodiscard]] bool replaceMaskKeyframes(MaskId, std::size_t property, creative_suite::animation::ScalarKeyframes);
    [[nodiscard]] bool setMaskKeyframe(MaskId, std::size_t property, std::int64_t composition_frame, double value);
    [[nodiscard]] bool removeMaskKeyframe(MaskId, std::size_t property, std::int64_t composition_frame);
    [[nodiscard]] bool setMaskKeyframeInterpolation(MaskId, std::size_t property, std::int64_t composition_frame,
        creative_suite::animation::InterpolationMode, const creative_suite::animation::CubicBezierEasing&);

private:
    [[nodiscard]] bool canInsertKeyframes(std::size_t count) const noexcept;
    [[nodiscard]] CompositionLayer* findLayer(LayerId id) noexcept;
    [[nodiscard]] const CompositionLayer* findLayer(LayerId id) const noexcept;

    void collectUnreferencedMasks() noexcept;
    std::vector<Mask> masks_;
    MaskId next_mask_id_ = 1;
    CanvasSize canvas_size_;
    FrameRate frame_rate_;
    LayerId next_layer_id_ = 1;
    std::vector<CompositionLayer> layers_;
};

} // namespace motion::model
