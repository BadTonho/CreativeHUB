#include "composition_document.h"
#include "layer_transform_hierarchy.h"
#include "track_matte_graph.h"

#include <creative_suite/media/media_library.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>
#include <utility>

namespace motion::model {
namespace {

using creative_suite::media::MediaLibrary;

constexpr std::array<FrameRate, 13> kSupportedFrameRates{{
    {24000, 1001},
    {24, 1},
    {25, 1},
    {30000, 1001},
    {30, 1},
    {48, 1},
    {50, 1},
    {60000, 1001},
    {60, 1},
    {100, 1},
    {120000, 1001},
    {120, 1},
    {240, 1},
}};

constexpr std::size_t kMaximumContentStringBytes = 32'768;
constexpr int kMaximumContentDimension = 32'768;
constexpr int kMaximumTextSizePixels = 4'096;
constexpr int kMaximumStrokeWidthPixels = 4'096;
constexpr std::size_t kMaximumLayerEffectCount = 256;

std::vector<creative_suite::animation::Keyframe>* mutableKeyframesFor(
    creative_suite::animation::TransformKeyframes& keyframes,
    creative_suite::animation::TransformProperty property) noexcept
{
    using creative_suite::animation::TransformProperty;
    switch (property) {
    case TransformProperty::PositionX: return &keyframes.position_x;
    case TransformProperty::PositionY: return &keyframes.position_y;
    case TransformProperty::Scale: return &keyframes.scale;
    case TransformProperty::Rotation: return &keyframes.rotation;
    case TransformProperty::Opacity: return &keyframes.opacity;
    }
    return nullptr;
}

int fourFifths(int value) noexcept
{
    return std::clamp((value / 5) * 4 + ((value % 5) * 4) / 5,
                      1, kMaximumContentDimension);
}

bool validLayerKind(LayerKind kind) noexcept
{
    switch (kind) {
    case LayerKind::Text:
    case LayerKind::Shape:
    case LayerKind::Image:
    case LayerKind::Video:
    case LayerKind::Null:
        return true;
    }
    return false;
}

std::optional<std::int64_t> checkedCeiling(long double value) noexcept
{
    const auto exclusive_max = std::ldexp(1.0L, 63);
    if (!std::isfinite(value) || value <= 0.0L || value >= exclusive_max) {
        return std::nullopt;
    }
    const auto rounded = std::ceil(value);
    if (rounded >= exclusive_max) return std::nullopt;
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(rounded));
}

std::optional<std::int64_t> fiveSecondFrameCount(FrameRate frame_rate) noexcept
{
    if (frame_rate.numerator <= 0 || frame_rate.denominator <= 0) return std::nullopt;
    const auto max = std::numeric_limits<std::int64_t>::max();
    const auto rounding = frame_rate.denominator - 1;
    if (frame_rate.numerator > (max - rounding) / 5) return std::nullopt;
    return (5 * frame_rate.numerator + rounding) / frame_rate.denominator;
}

bool validPositiveFinite(const std::optional<double>& value) noexcept
{
    return value.has_value() && std::isfinite(*value) && *value > 0.0;
}

bool validTextContent(const TextLayerContent& content) noexcept
{
    return content.text.size() <= kMaximumContentStringBytes &&
        !content.font_family.empty() &&
        content.font_family.size() <= kMaximumContentStringBytes &&
        content.font_size_pixels >= 1 &&
        content.font_size_pixels <= kMaximumTextSizePixels &&
        content.box_width >= 1 && content.box_width <= kMaximumContentDimension &&
        content.box_height >= 1 && content.box_height <= kMaximumContentDimension &&
        (content.alignment == TextAlignment::Left ||
         content.alignment == TextAlignment::Center ||
         content.alignment == TextAlignment::Right);
}

bool validShapeContent(const ShapeLayerContent& content) noexcept
{
    return (content.shape == ShapeKind::Rectangle || content.shape == ShapeKind::Ellipse) &&
        content.width >= 1 && content.width <= kMaximumContentDimension &&
        content.height >= 1 && content.height <= kMaximumContentDimension &&
        content.stroke_width_pixels >= 0 &&
        content.stroke_width_pixels <= kMaximumStrokeWidthPixels &&
        content.stroke_width_pixels <= std::min(content.width, content.height);
}

bool validContentForKind(LayerKind kind, const LayerContent& content) noexcept
{
    switch (kind) {
    case LayerKind::Text: {
        const auto* text = std::get_if<TextLayerContent>(&content);
        return text != nullptr && validTextContent(*text);
    }
    case LayerKind::Shape: {
        const auto* shape = std::get_if<ShapeLayerContent>(&content);
        return shape != nullptr && validShapeContent(*shape);
    }
    case LayerKind::Image:
    case LayerKind::Video:
    case LayerKind::Null:
        return std::holds_alternative<std::monostate>(content);
    }
    return false;
}

bool validLayer(const CompositionLayer& layer) noexcept
{
    using creative_suite::animation::validTransform;
    using creative_suite::animation::validTransformKeyframes;

    if ((layer.presentation && !creative_suite::composition_exchange::validPresentation(*layer.presentation)) || layer.id == 0 || !validLayerKind(layer.kind) || layer.timeline_start_frame < 0 ||
        layer.duration_frames < 0 ||
        layer.duration_frames > std::numeric_limits<std::int64_t>::max() -
            layer.timeline_start_frame ||
        layer.source_frame_count < 0 || layer.source_start_frame < 0 ||
        layer.source_duration_frames < 0 ||
        layer.maximum_timeline_duration_frames < 0 ||
        !std::isfinite(layer.source_frame_rate) || layer.source_frame_rate < 0.0 ||
        !validTransform(layer.transform) ||
        !validTransformKeyframes(layer.keyframes) ||
        !validContentForKind(layer.kind, layer.content) ||
        !creative_suite::composition::validBlendMode(layer.blend_mode) ||
        !validLayerEffects(layer.effects) || !validParentOffset(layer.parent_offset) ||
        !validAnchorPoint(layer.anchor_point) ||
        (layer.scale_y && !validScaleChannel(*layer.scale_y)) ||
        (layer.parent_affine && (!validParentAffine(*layer.parent_affine) || layer.parent_offset!=ParentTransformOffset{})) ||
        !creative_suite::animation::validScalarKeyframes(layer.anchor_keyframes[0]) ||
        !creative_suite::animation::validScalarKeyframes(layer.anchor_keyframes[1])) {
        return false;
    }
    if (layer.kind == LayerKind::Null &&
        (layer.blend_mode != creative_suite::composition::BlendMode::Normal ||
         !layer.source_path.empty() || layer.linked_image || layer.presentation ||
         layer.source_frame_count || layer.source_start_frame || layer.source_duration_frames ||
         layer.maximum_timeline_duration_frames || layer.source_frame_rate != 0 ||
         !layer.effects.empty() || layer.source_mask || layer.transform_mask ||
         layer.transform.opacity != 1 || !layer.keyframes.opacity.empty())) return false;
    if (layer.duration_frames > 0 &&
        (layer.kind == LayerKind::Image || layer.kind == LayerKind::Video) &&
        layer.source_path.empty()) {
        return false;
    }
    if (layer.kind == LayerKind::Video && layer.duration_frames > 0 &&
        (layer.maximum_timeline_duration_frames <= 0 ||
         layer.duration_frames > layer.maximum_timeline_duration_frames ||
         layer.source_frame_count <= 0 || layer.source_frame_rate <= 0.0 ||
         (!layer.presentation && layer.source_start_frame >= layer.source_frame_count))) {
        return false;
    }

    return true;
}

} // namespace

const std::array<FrameRate, 13>& supportedFrameRates() noexcept
{
    return kSupportedFrameRates;
}

bool isSupportedFrameRate(FrameRate frame_rate) noexcept
{
    return std::find(kSupportedFrameRates.begin(), kSupportedFrameRates.end(), frame_rate)
        != kSupportedFrameRates.end();
}

std::optional<std::int64_t> sourceFrameForTimelineFrame(
    std::int64_t local_timeline_frame,
    std::int64_t source_start_frame,
    double source_frame_rate,
    FrameRate timeline_frame_rate,
    std::int64_t source_frame_count) noexcept
{
    if (local_timeline_frame < 0 || source_start_frame < 0 ||
        source_frame_count < 0 ||
        (source_frame_count > 0 && source_start_frame >= source_frame_count) ||
        timeline_frame_rate.numerator <= 0 || timeline_frame_rate.denominator <= 0 ||
        !std::isfinite(source_frame_rate) || source_frame_rate <= 0.0) {
        return std::nullopt;
    }
    const long double offset = static_cast<long double>(local_timeline_frame) *
        static_cast<long double>(source_frame_rate) *
        static_cast<long double>(timeline_frame_rate.denominator) /
        static_cast<long double>(timeline_frame_rate.numerator);
    const long double rounded = std::floor(offset + 0.5L);
    const long double exclusive_max = std::ldexp(1.0L, 63);
    if (!std::isfinite(offset) || offset < 0.0L || rounded >= exclusive_max ||
        rounded > static_cast<long double>(
            std::numeric_limits<std::int64_t>::max() - source_start_frame)) {
        return std::nullopt;
    }
    const auto mapped = source_start_frame + static_cast<std::int64_t>(rounded);
    return source_frame_count > 0
        ? std::min(source_frame_count - 1, mapped)
        : mapped;
}

namespace {

[[nodiscard]] bool validEffectParameterValue(
    EffectParameterProperty property,
    double value) noexcept
{
    if (!std::isfinite(value)) return false;
    switch (property) {
    case EffectParameterProperty::GaussianBlurRadius:
        return value >= 0.0 && value <= 100.0;
    case EffectParameterProperty::ColorBrightness:
        return value >= -100.0 && value <= 100.0;
    case EffectParameterProperty::ColorContrastPercent:
    case EffectParameterProperty::ColorSaturationPercent:
        return value >= 0.0 && value <= 200.0;
    }
    return false;
}

[[nodiscard]] bool validEffectKeyframes(
    const creative_suite::animation::ScalarKeyframes& keyframes,
    EffectParameterProperty property) noexcept
{
    return creative_suite::animation::validScalarKeyframes(keyframes) &&
        std::all_of(keyframes.begin(), keyframes.end(), [property](const auto& keyframe) {
            return validEffectParameterValue(property, keyframe.value);
        });
}

} // namespace

bool validLayerEffect(const LayerEffect& effect) noexcept
{
    if (const auto* blur = std::get_if<GaussianBlurEffect>(&effect)) {
        return validEffectParameterValue(
                   EffectParameterProperty::GaussianBlurRadius, blur->radius_pixels) &&
            validEffectKeyframes(
                blur->radius_keyframes, EffectParameterProperty::GaussianBlurRadius);
    }
    if (const auto* color = std::get_if<ColorAdjustmentEffect>(&effect)) {
        return validEffectParameterValue(
                   EffectParameterProperty::ColorBrightness, color->brightness) &&
            validEffectParameterValue(
                EffectParameterProperty::ColorContrastPercent, color->contrast_percent) &&
            validEffectParameterValue(
                EffectParameterProperty::ColorSaturationPercent, color->saturation_percent) &&
            validEffectKeyframes(
                color->brightness_keyframes, EffectParameterProperty::ColorBrightness) &&
            validEffectKeyframes(
                color->contrast_keyframes, EffectParameterProperty::ColorContrastPercent) &&
            validEffectKeyframes(
                color->saturation_keyframes, EffectParameterProperty::ColorSaturationPercent);
    }
    return false;
}

bool validLayerEffects(const std::vector<LayerEffect>& effects) noexcept
{
    return effects.size() <= kMaximumLayerEffectCount &&
        std::all_of(effects.begin(), effects.end(), validLayerEffect);
}

bool effectHasProperty(
    const LayerEffect& effect,
    EffectParameterProperty property) noexcept
{
    return effectKeyframesFor(effect, property) != nullptr;
}

const creative_suite::animation::ScalarKeyframes* effectKeyframesFor(
    const LayerEffect& effect,
    EffectParameterProperty property) noexcept
{
    if (const auto* blur = std::get_if<GaussianBlurEffect>(&effect)) {
        return property == EffectParameterProperty::GaussianBlurRadius
            ? &blur->radius_keyframes
            : nullptr;
    }
    if (const auto* color = std::get_if<ColorAdjustmentEffect>(&effect)) {
        switch (property) {
        case EffectParameterProperty::ColorBrightness:
            return &color->brightness_keyframes;
        case EffectParameterProperty::ColorContrastPercent:
            return &color->contrast_keyframes;
        case EffectParameterProperty::ColorSaturationPercent:
            return &color->saturation_keyframes;
        case EffectParameterProperty::GaussianBlurRadius:
            return nullptr;
        }
    }
    return nullptr;
}

creative_suite::animation::ScalarKeyframes* mutableEffectKeyframesFor(
    LayerEffect& effect,
    EffectParameterProperty property) noexcept
{
    if (auto* blur = std::get_if<GaussianBlurEffect>(&effect)) {
        return property == EffectParameterProperty::GaussianBlurRadius
            ? &blur->radius_keyframes
            : nullptr;
    }
    if (auto* color = std::get_if<ColorAdjustmentEffect>(&effect)) {
        switch (property) {
        case EffectParameterProperty::ColorBrightness:
            return &color->brightness_keyframes;
        case EffectParameterProperty::ColorContrastPercent:
            return &color->contrast_keyframes;
        case EffectParameterProperty::ColorSaturationPercent:
            return &color->saturation_keyframes;
        case EffectParameterProperty::GaussianBlurRadius:
            return nullptr;
        }
    }
    return nullptr;
}

double evaluateEffectParameter(
    const LayerEffect& effect,
    EffectParameterProperty property,
    std::int64_t local_frame) noexcept
{
    if (const auto* blur = std::get_if<GaussianBlurEffect>(&effect)) {
        if (property == EffectParameterProperty::GaussianBlurRadius) {
            return std::clamp(
                creative_suite::animation::evaluateScalar(
                    blur->radius_pixels, blur->radius_keyframes, local_frame),
                0.0, 100.0);
        }
        return 0.0;
    }
    if (const auto* color = std::get_if<ColorAdjustmentEffect>(&effect)) {
        switch (property) {
        case EffectParameterProperty::ColorBrightness:
            return std::clamp(
                creative_suite::animation::evaluateScalar(
                    color->brightness, color->brightness_keyframes, local_frame),
                -100.0, 100.0);
        case EffectParameterProperty::ColorContrastPercent:
            return std::clamp(
                creative_suite::animation::evaluateScalar(
                    color->contrast_percent, color->contrast_keyframes, local_frame),
                0.0, 200.0);
        case EffectParameterProperty::ColorSaturationPercent:
            return std::clamp(
                creative_suite::animation::evaluateScalar(
                    color->saturation_percent, color->saturation_keyframes, local_frame),
                0.0, 200.0);
        case EffectParameterProperty::GaussianBlurRadius:
            return 0.0;
        }
    }
    return 0.0;
}

bool hasEffectKeyframeAtFrame(
    const LayerEffect& effect,
    EffectParameterProperty property,
    std::int64_t local_frame) noexcept
{
    const auto* keyframes = effectKeyframesFor(effect, property);
    if (keyframes == nullptr || local_frame < 0) return false;
    const auto it = std::lower_bound(
        keyframes->begin(), keyframes->end(), local_frame,
        [](const creative_suite::animation::Keyframe& keyframe, std::int64_t frame) {
            return keyframe.frame < frame;
        });
    return it != keyframes->end() && it->frame == local_frame;
}

bool layerEffectHasKeyframes(const LayerEffect& effect) noexcept
{
    if (const auto* blur = std::get_if<GaussianBlurEffect>(&effect)) {
        return !blur->radius_keyframes.empty();
    }
    if (const auto* color = std::get_if<ColorAdjustmentEffect>(&effect)) {
        return !color->brightness_keyframes.empty() ||
            !color->contrast_keyframes.empty() ||
            !color->saturation_keyframes.empty();
    }
    return false;
}

GaussianBlurEffect evaluateGaussianBlurEffect(
    const GaussianBlurEffect& effect,
    std::int64_t local_frame)
{
    GaussianBlurEffect evaluated = effect;
    evaluated.radius_pixels = std::clamp(
        creative_suite::animation::evaluateScalar(
            effect.radius_pixels, effect.radius_keyframes, local_frame),
        0.0, 100.0);
    evaluated.radius_keyframes.clear();
    return evaluated;
}

ColorAdjustmentEffect evaluateColorAdjustmentEffect(
    const ColorAdjustmentEffect& effect,
    std::int64_t local_frame)
{
    ColorAdjustmentEffect evaluated = effect;
    evaluated.brightness = std::clamp(
        creative_suite::animation::evaluateScalar(
            effect.brightness, effect.brightness_keyframes, local_frame),
        -100.0, 100.0);
    evaluated.contrast_percent = std::clamp(
        creative_suite::animation::evaluateScalar(
            effect.contrast_percent, effect.contrast_keyframes, local_frame),
        0.0, 200.0);
    evaluated.saturation_percent = std::clamp(
        creative_suite::animation::evaluateScalar(
            effect.saturation_percent, effect.saturation_keyframes, local_frame),
        0.0, 200.0);
    evaluated.brightness_keyframes.clear();
    evaluated.contrast_keyframes.clear();
    evaluated.saturation_keyframes.clear();
    return evaluated;
}

LayerEffect evaluateLayerEffect(
    const LayerEffect& effect,
    std::int64_t local_frame)
{
    if (const auto* blur = std::get_if<GaussianBlurEffect>(&effect)) {
        return evaluateGaussianBlurEffect(*blur, local_frame);
    }
    return evaluateColorAdjustmentEffect(
        std::get<ColorAdjustmentEffect>(effect), local_frame);
}

std::vector<LayerEffect> evaluateLayerEffects(
    const std::vector<LayerEffect>& effects,
    std::int64_t local_frame)
{
    std::vector<LayerEffect> evaluated;
    evaluated.reserve(effects.size());
    for (const auto& effect : effects) {
        evaluated.push_back(evaluateLayerEffect(effect, local_frame));
    }
    return evaluated;
}

TextLayerContent defaultTextLayerContent(CanvasSize canvas_size)
{
    TextLayerContent content;
    content.box_width = fourFifths(canvas_size.width);
    content.box_height = std::clamp(canvas_size.height / 2,
                                    1, kMaximumContentDimension);
    return content;
}

ShapeLayerContent defaultShapeLayerContent(CanvasSize canvas_size, ShapeKind shape)
{
    ShapeLayerContent content;
    content.shape = shape;
    content.width = std::clamp(canvas_size.width / 4,
                               1, kMaximumContentDimension);
    content.height = std::clamp(canvas_size.height / 4,
                                1, kMaximumContentDimension);
    return content;
}

CompositionDocument::CompositionDocument(
    int canvas_width,
    int canvas_height,
    FrameRate frame_rate)
    : canvas_size_{canvas_width, canvas_height}
    , frame_rate_(frame_rate)
{
    if (canvas_width <= 0 || canvas_height <= 0) {
        throw std::invalid_argument("Composition canvas dimensions must be positive");
    }
    if (!isSupportedFrameRate(frame_rate)) {
        throw std::invalid_argument("Composition frame rate is not supported");
    }
}

CompositionDocument::CompositionDocument(
    int canvas_width,
    int canvas_height,
    FrameRate frame_rate,
    std::vector<CompositionLayer> layers, std::vector<Mask> masks)
    : CompositionDocument(canvas_width, canvas_height, frame_rate)
{
    std::unordered_set<LayerId> ids;
    ids.reserve(layers.size());
    LayerId greatest_id = 0;
    for (auto& layer : layers) {
        // Accept older in-memory callers that construct generic Text/Shape
        // records without content; the document now stores explicit defaults.
        if (layer.kind == LayerKind::Text &&
            std::holds_alternative<std::monostate>(layer.content)) {
            layer.content = defaultTextLayerContent(canvas_size_);
        } else if (layer.kind == LayerKind::Shape &&
                   std::holds_alternative<std::monostate>(layer.content)) {
            layer.content = defaultShapeLayerContent(canvas_size_);
        }
        if (!validLayer(layer) || !ids.insert(layer.id).second) {
            throw std::invalid_argument("Composition contains an invalid or duplicate layer");
        }
        greatest_id = std::max(greatest_id, layer.id);
    }
    layers_ = std::move(layers);
    (void)LayerTransformHierarchy(canvas_size_, layers_);
    (void)TrackMatteGraph(layers_);
    std::unordered_set<MaskId> mask_ids;
    if(masks.size()>4096)throw std::invalid_argument("Too many masks.");
    for(const auto& m:masks) {
        if(!creative_suite::composition_exchange::validMask(m)||!mask_ids.insert(m.id).second)
            throw std::invalid_argument("Invalid mask definition or duplicate ID.");
        next_mask_id_=std::max(next_mask_id_,m.id+1);
    }
    masks_=std::move(masks);
    std::unordered_set<MaskId> used_masks;
    for(const auto& layer:layers_) {
        const auto check=[&](MaskId id){if(id && !mask_ids.contains(id))throw std::invalid_argument("Unknown mask reference.");if(id)used_masks.insert(id);};
        check(layer.source_mask);check(layer.transform_mask);
        for(const auto& effect:layer.effects)std::visit([&](const auto& e){check(e.mask);},effect);
    }
    for(const auto& m:masks_)if(!used_masks.contains(m.id))throw std::invalid_argument("Unreferenced mask definition.");
    next_layer_id_ = greatest_id == std::numeric_limits<LayerId>::max()
        ? 0 : greatest_id + 1;
}

CanvasSize CompositionDocument::canvasSize() const noexcept
{
    return canvas_size_;
}

FrameRate CompositionDocument::frameRate() const noexcept
{
    return frame_rate_;
}

const std::vector<CompositionLayer>& CompositionDocument::layers() const noexcept
{
    return layers_;
}

LayerId CompositionDocument::addLayer(LayerKind kind, std::string name)
{
    if (!validLayerKind(kind)) {
        throw std::invalid_argument("Unsupported composition layer kind");
    }
    if (next_layer_id_ == 0) {
        throw std::overflow_error("Composition layer ID space is exhausted");
    }

    const LayerId id = next_layer_id_;
    CompositionLayer layer{id, kind, std::move(name)};
    if (kind == LayerKind::Text) {
        layer.content = defaultTextLayerContent(canvas_size_);
    } else if (kind == LayerKind::Shape) {
        layer.content = defaultShapeLayerContent(canvas_size_);
    }
    layers_.push_back(std::move(layer));
    ++next_layer_id_;
    return id;
}

bool CompositionDocument::addContentLayer(
    LayerKind kind,
    std::string name,
    std::int64_t timeline_start_frame,
    LayerId* added_id)
{
    if ((kind != LayerKind::Text && kind != LayerKind::Shape && kind != LayerKind::Null) ||
        timeline_start_frame < 0) {
        return false;
    }
    const auto duration = fiveSecondFrameCount(frame_rate_);
    if (!duration.has_value() ||
        *duration > std::numeric_limits<std::int64_t>::max() - timeline_start_frame) {
        return false;
    }
    try {
        const auto id = addLayer(kind, std::move(name));
        auto* layer = findLayer(id);
        if (layer == nullptr) return false;
        layer->timeline_start_frame = timeline_start_frame;
        layer->duration_frames = *duration;
        if (added_id != nullptr) *added_id = id;
        return true;
    } catch (...) {
        return false;
    }
}

AddMediaLayerResult CompositionDocument::addMediaLayer(
    const creative_suite::media::VideoMetadata& metadata,
    std::int64_t timeline_start_frame,
    LayerId* added_id)
{
    using creative_suite::media::MediaKind;
    using creative_suite::media::MediaLibrary;

    if (metadata.kind != MediaKind::Video && metadata.kind != MediaKind::Image) {
        return AddMediaLayerResult::InvalidKind;
    }
    if (metadata.source_path.empty()) return AddMediaLayerResult::InvalidPath;
    if (timeline_start_frame < 0) return AddMediaLayerResult::InvalidPosition;

    const bool is_image = metadata.kind == MediaKind::Image;
    std::int64_t source_frames = 1;
    double source_rate = frame_rate_.asDouble();
    std::int64_t duration_frames = 0;

    if (is_image) {
        const auto image_duration = fiveSecondFrameCount(frame_rate_);
        if (!image_duration.has_value()) return AddMediaLayerResult::InvalidTimingMetadata;
        duration_frames = *image_duration;
    } else {
        if (!validPositiveFinite(metadata.frame_rate)) {
            return AddMediaLayerResult::InvalidTimingMetadata;
        }
        source_rate = *metadata.frame_rate;

        if (metadata.frame_count.has_value() && *metadata.frame_count > 0) {
            source_frames = *metadata.frame_count;
        } else if (validPositiveFinite(metadata.duration_seconds)) {
            const auto estimated_source_frames = checkedCeiling(
                static_cast<long double>(*metadata.duration_seconds) * source_rate);
            if (!estimated_source_frames.has_value()) {
                return AddMediaLayerResult::InvalidTimingMetadata;
            }
            source_frames = *estimated_source_frames;
        } else {
            return AddMediaLayerResult::InvalidTimingMetadata;
        }

        const auto timeline_duration = checkedCeiling(
            static_cast<long double>(source_frames) *
            static_cast<long double>(frame_rate_.numerator) /
            (static_cast<long double>(frame_rate_.denominator) * source_rate));
        if (!timeline_duration.has_value()) {
            return AddMediaLayerResult::InvalidTimingMetadata;
        }
        duration_frames = *timeline_duration;
    }

    if (duration_frames > std::numeric_limits<std::int64_t>::max() - timeline_start_frame) {
        return AddMediaLayerResult::InvalidPosition;
    }

    const auto layer_name = metadata.display_name.empty()
        ? metadata.source_path.stem().string()
        : metadata.display_name;
    const auto id = addLayer(is_image ? LayerKind::Image : LayerKind::Video, layer_name);
    auto* layer = findLayer(id);
    if (layer == nullptr) return AddMediaLayerResult::InvalidKind;
    layer->source_path = MediaLibrary::canonicalPath(metadata.source_path);
    layer->timeline_start_frame = timeline_start_frame;
    layer->duration_frames = duration_frames;
    layer->source_frame_count = source_frames;
    layer->source_frame_rate = source_rate;
    if (!is_image) {
        layer->source_duration_frames = source_frames;
        layer->maximum_timeline_duration_frames = duration_frames;
    }
    if (added_id != nullptr) *added_id = id;
    return AddMediaLayerResult::Added;
}

std::vector<LayerId> CompositionDocument::eligibleLayerParents(LayerId child) const
{
    if (!findLayer(child)) return {};
    std::unordered_map<LayerId, std::vector<LayerId>> children;
    children.reserve(layers_.size());
    for (const auto& layer : layers_) children[layer.parent_id].push_back(layer.id);
    std::unordered_set<LayerId> excluded{child};
    std::vector<LayerId> pending{child};
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto found = children.find(pending[i]);
        if (found == children.end()) continue;
        for (const auto id : found->second) if (excluded.insert(id).second) pending.push_back(id);
    }
    std::vector<LayerId> result;
    result.reserve(layers_.size() - excluded.size());
    for (const auto& layer : layers_) if (!excluded.contains(layer.id)) result.push_back(layer.id);
    return result;
}

bool CompositionDocument::canSetLayerParent(LayerId child, LayerId parent) const noexcept
{
    if (!findLayer(child)) return false;
    for (std::size_t depth = 0; parent != 0; ++depth) {
        if (parent == child || depth >= layers_.size()) return false;
        const auto* layer = findLayer(parent);
        if (!layer) return false;
        parent = layer->parent_id;
    }
    return true;
}

bool CompositionDocument::setLayerParent(LayerId child, LayerId parent, std::int64_t frame)
{
    if (frame < 0 || !canSetLayerParent(child, parent)) return false;
    auto* layer = findLayer(child);
    if (layer->parent_id == parent) return false;
    const auto global = LayerTransformHierarchy(canvas_size_, layers_).evaluate(frame);
    if(global.at(child).affine || (parent && global.at(parent).affine)) {
        namespace c=creative_suite::composition;
        const auto local_frame=frame<layer->timeline_start_frame?0:frame-layer->timeline_start_frame;
        auto relative=global.at(child).world;
        if(parent)relative=c::composeAffine(c::requireInverseAffine(global.at(parent).world),relative);
        const auto binding=c::composeAffine(relative,c::requireInverseAffine(localLayerGeometry(*layer,canvas_size_,local_frame)));
        if(!validParentAffine(binding))throw std::runtime_error("Invalid affine parent binding for layer "+std::to_string(child));
        layer->parent_id=parent;layer->parent_offset={};layer->parent_affine=binding;return true;
    }
    const auto local = creative_suite::animation::evaluateTransform(layer->transform, layer->keyframes,
        frame < layer->timeline_start_frame ? 0 : frame - layer->timeline_start_frame);
    const auto desired = geometryOffset(global.at(child), canvas_size_);
    const auto relative = parent ? composeOffsets(inverseOffset(geometryOffset(global.at(parent), canvas_size_)), desired) : desired;
    const auto offset = composeOffsets(relative, inverseOffset(geometryOffset(local, canvas_size_,
        evaluateLayerAnchor(*layer, frame < layer->timeline_start_frame ? 0 : frame - layer->timeline_start_frame))));
    layer->parent_id = parent;
    layer->parent_offset = offset;layer->parent_affine.reset();
    return true;
}

bool CompositionDocument::removeLayer(LayerId id, std::int64_t frame)
{
    const auto layer = std::find_if(layers_.begin(), layers_.end(), [id](const auto& item) {
        return item.id == id;
    });
    if (layer == layers_.end() || frame < 0) {
        return false;
    }
    // Stage every compensation before mutating; a numerical failure must leave
    // the parent and every child intact.
    struct Detached {CompositionLayer* child;ParentTransformOffset legacy;std::optional<AffineTransform> affine;};
    std::vector<Detached> detached;
    if (std::any_of(layers_.begin(), layers_.end(), [id](const auto& l) { return l.parent_id == id; })) {
        const auto global = LayerTransformHierarchy(canvas_size_, layers_).evaluate(frame);
        for (auto& child : layers_) if (child.parent_id == id) {
            if(global.at(child.id).affine) {
                namespace c=creative_suite::composition;
                const auto local_frame=frame<child.timeline_start_frame?0:frame-child.timeline_start_frame;
                const auto binding=c::composeAffine(global.at(child.id).world,c::requireInverseAffine(localLayerGeometry(child,canvas_size_,local_frame)));
                if(!validParentAffine(binding))throw std::runtime_error("Invalid affine detach for layer "+std::to_string(child.id));
                detached.push_back({&child,{},binding});continue;
            }
            const auto local = creative_suite::animation::evaluateTransform(child.transform, child.keyframes,
                frame < child.timeline_start_frame ? 0 : frame - child.timeline_start_frame);
            detached.push_back({&child, composeOffsets(geometryOffset(global.at(child.id), canvas_size_),
                inverseOffset(geometryOffset(local, canvas_size_, evaluateLayerAnchor(child,
                    frame < child.timeline_start_frame ? 0 : frame - child.timeline_start_frame)))),{}});
        }
    }
    for(auto& item:detached){item.child->parent_id=0;item.child->parent_offset=item.legacy;item.child->parent_affine=item.affine;}
    for(auto& item:layers_)if(item.track_matte && item.track_matte->source_id==id)item.track_matte.reset();
    layers_.erase(layer);
    collectUnreferencedMasks();
    return true;
}

bool CompositionDocument::moveLayer(LayerId id, std::size_t final_index) noexcept
{
    const auto layer = std::find_if(layers_.begin(), layers_.end(), [id](const auto& item) {
        return item.id == id;
    });
    if (layer == layers_.end() || final_index >= layers_.size()) {
        return false;
    }

    const auto current_index = static_cast<std::size_t>(std::distance(layers_.begin(), layer));
    if (current_index == final_index) {
        return true;
    }

    CompositionLayer moved = std::move(*layer);
    layers_.erase(layer);
    layers_.insert(layers_.begin() + static_cast<std::ptrdiff_t>(final_index), std::move(moved));
    return true;
}

bool CompositionDocument::moveLayerInTimeline(
    LayerId id,
    std::int64_t timeline_start_frame) noexcept
{
    auto* layer = findLayer(id);
    if (layer == nullptr || timeline_start_frame < 0 || layer->duration_frames <= 0 ||
        layer->duration_frames >
            std::numeric_limits<std::int64_t>::max() - timeline_start_frame) {
        return false;
    }
    layer->timeline_start_frame = timeline_start_frame;
    return true;
}

bool CompositionDocument::resizeLayerDuration(
    LayerId id,
    std::int64_t duration_frames) noexcept
{
    auto* layer = findLayer(id);
    if (layer == nullptr || duration_frames <= 0 ||
        duration_frames > std::numeric_limits<std::int64_t>::max() -
            layer->timeline_start_frame ||
        (layer->kind == LayerKind::Video &&
         (layer->maximum_timeline_duration_frames <= 0 ||
          duration_frames > layer->maximum_timeline_duration_frames))) {
        return false;
    }
    layer->duration_frames = duration_frames;
    return true;
}

bool CompositionDocument::setVideoSourceRange(
    LayerId id,
    std::int64_t source_start_frame,
    std::int64_t duration_frames,
    std::int64_t maximum_timeline_duration_frames) noexcept
{
    auto* layer = findLayer(id);
    if (layer == nullptr || layer->kind != LayerKind::Video ||
        source_start_frame < 0 || source_start_frame >= layer->source_frame_count ||
        duration_frames <= 0 || maximum_timeline_duration_frames < duration_frames ||
        duration_frames > std::numeric_limits<std::int64_t>::max() -
            layer->timeline_start_frame) return false;
    layer->source_start_frame = source_start_frame;
    layer->duration_frames = duration_frames;
    layer->maximum_timeline_duration_frames = maximum_timeline_duration_frames;
    return true;
}

bool CompositionDocument::setLayerName(LayerId id, std::string name)
{
    auto* layer = findLayer(id);
    if (layer == nullptr) {
        return false;
    }
    layer->name = std::move(name);
    return true;
}

bool CompositionDocument::setLayerLinkedImage(
    LayerId id,
    std::optional<LinkedImageDocument> linked_image)
{
    auto* layer = findLayer(id);
    if (layer == nullptr || layer->kind != LayerKind::Image ||
        layer->source_path.empty()) return false;
    if (linked_image.has_value() &&
        (linked_image->document_path.empty() ||
         linked_image->published_output_path.empty() ||
         linked_image->source_snapshot_path.empty() ||
         !linked_image->document_path.is_absolute() ||
         !linked_image->published_output_path.is_absolute() ||
         !linked_image->source_snapshot_path.is_absolute() ||
         MediaLibrary::canonicalPath(linked_image->document_path) ==
             MediaLibrary::canonicalPath(layer->source_path) ||
         MediaLibrary::canonicalPath(linked_image->published_output_path) ==
             MediaLibrary::canonicalPath(layer->source_path) ||
         MediaLibrary::canonicalPath(linked_image->source_snapshot_path) ==
             MediaLibrary::canonicalPath(layer->source_path))) {
        return false;
    }
    if (linked_image.has_value()) {
        const std::array candidate_paths{
            MediaLibrary::canonicalPath(linked_image->document_path),
            MediaLibrary::canonicalPath(linked_image->published_output_path),
            MediaLibrary::canonicalPath(linked_image->source_snapshot_path)};
        for (const auto& other : layers_) {
            if (other.id == id) continue;
            for (const auto& candidate : candidate_paths) {
                if ((!other.source_path.empty() &&
                     candidate == MediaLibrary::canonicalPath(other.source_path)) ||
                    (other.linked_image.has_value() &&
                     (candidate == MediaLibrary::canonicalPath(
                          other.linked_image->document_path) ||
                      candidate == MediaLibrary::canonicalPath(
                          other.linked_image->published_output_path) ||
                      candidate == MediaLibrary::canonicalPath(
                          other.linked_image->source_snapshot_path)))) {
                    return false;
                }
            }
        }
    }
    layer->linked_image = std::move(linked_image);
    return true;
}

bool CompositionDocument::setLayerVisible(LayerId id, bool visible) noexcept
{
    auto* layer = findLayer(id);
    if (layer == nullptr) {
        return false;
    }
    layer->visible = visible;
    return true;
}

bool CompositionDocument::setLayerBlendMode(LayerId id, creative_suite::composition::BlendMode mode) noexcept
{
    auto* layer=findLayer(id);
    if(!layer || !creative_suite::composition::validBlendMode(mode) || layer->blend_mode==mode ||
        (layer->kind==LayerKind::Null && mode!=creative_suite::composition::BlendMode::Normal))return false;
    layer->blend_mode=mode;return true;
}

bool CompositionDocument::setLayerTransform(
    LayerId id,
    const creative_suite::animation::Transform2D& transform) noexcept
{
    if (!creative_suite::animation::validTransform(transform)) {
        return false;
    }
    auto* layer = findLayer(id);
    if (layer == nullptr || (layer->kind == LayerKind::Null && transform.opacity != 1)) {
        return false;
    }
    if(layer->transform.scale!=transform.scale)layer->scale_y.reset(); // Legacy uniform edit.
    layer->transform = transform;
    return true;
}

bool CompositionDocument::setTextLayerContent(
    LayerId id,
    const TextLayerContent& content)
{
    auto* layer = findLayer(id);
    if (layer == nullptr || layer->kind != LayerKind::Text ||
        !validTextContent(content)) {
        return false;
    }
    const auto& previous = std::get<TextLayerContent>(layer->content);
    if (layer->presentation && layer->presentation->profile == creative_suite::composition_exchange::Profile::FusionCanvas &&
        (previous.box_width != content.box_width || previous.box_height != content.box_height)) {
        layer->presentation->profile = creative_suite::composition_exchange::Profile::MotionLayer;
        layer->presentation->text_width = content.box_width;
        layer->presentation->text_height = content.box_height;
    }
    layer->content = content;
    return true;
}

bool CompositionDocument::setShapeLayerContent(
    LayerId id,
    const ShapeLayerContent& content)
{
    auto* layer = findLayer(id);
    if (layer == nullptr || layer->kind != LayerKind::Shape ||
        !validShapeContent(content)) {
        return false;
    }
    layer->content = content;
    return true;
}

bool CompositionDocument::setLayerEffects(
    LayerId id,
    const std::vector<LayerEffect>& effects)
{
    auto* layer = findLayer(id);
    if (layer == nullptr || !validLayerEffects(effects) ||
        (layer->kind == LayerKind::Null && !effects.empty())) return false;
    for(const auto& effect:effects)if(!std::visit([&](const auto& e){return !e.mask || findMask(e.mask);},effect))return false;
    layer->effects = effects;
    collectUnreferencedMasks();
    return true;
}

bool CompositionDocument::replaceLayerKeyframes(
    LayerId id,
    creative_suite::animation::TransformProperty property,
    const std::vector<creative_suite::animation::Keyframe>& keyframes)
{
    auto* layer = findLayer(id);
    if (layer == nullptr) return false;
    if (layer->kind == LayerKind::Null && property == creative_suite::animation::TransformProperty::Opacity) return false;
    auto candidate = layer->keyframes;
    auto* target = mutableKeyframesFor(candidate, property);
    if (target == nullptr) return false;
    *target = keyframes;
    if (!creative_suite::animation::validTransformKeyframes(candidate)) return false;
    layer->keyframes = std::move(candidate);
    if(property==creative_suite::animation::TransformProperty::Scale)layer->scale_y.reset();
    return true;
}

bool CompositionDocument::setLayerKeyframe(
    LayerId id,
    creative_suite::animation::TransformProperty property,
    std::int64_t local_frame,
    double value) noexcept
{
    auto* layer = findLayer(id);
    const bool changed=layer != nullptr
        && !(layer->kind == LayerKind::Null && property == creative_suite::animation::TransformProperty::Opacity)
        && creative_suite::animation::setKeyframe(layer->keyframes, property, local_frame, value);
    if(changed && property==creative_suite::animation::TransformProperty::Scale)layer->scale_y.reset();
    return changed;
}

bool CompositionDocument::setLayerKeyframeInterpolation(
    LayerId id,
    creative_suite::animation::TransformProperty property,
    std::int64_t local_frame,
    creative_suite::animation::InterpolationMode interpolation,
    const creative_suite::animation::CubicBezierEasing& easing) noexcept
{
    auto* layer = findLayer(id);
    const bool changed=layer != nullptr && creative_suite::animation::setKeyframeInterpolation(layer->keyframes,property,local_frame,interpolation,easing);
    if(changed && property==creative_suite::animation::TransformProperty::Scale)layer->scale_y.reset();
    return changed;
}

bool CompositionDocument::removeLayerKeyframe(
    LayerId id,
    creative_suite::animation::TransformProperty property,
    std::int64_t local_frame) noexcept
{
    auto* layer = findLayer(id);
    const bool changed=layer != nullptr && creative_suite::animation::removeKeyframe(layer->keyframes,property,local_frame);
    if(changed && property==creative_suite::animation::TransformProperty::Scale)layer->scale_y.reset();
    return changed;
}

bool CompositionDocument::moveLayerKeyframe(
    LayerId id,
    creative_suite::animation::TransformProperty property,
    std::int64_t from_local_frame,
    std::int64_t to_local_frame) noexcept
{
    auto* layer = findLayer(id);
    if (layer == nullptr || from_local_frame < 0 || to_local_frame < 0 ||
        layer->duration_frames <= 0 || from_local_frame >= layer->duration_frames ||
        to_local_frame >= layer->duration_frames) {
        return false;
    }

    using creative_suite::animation::Keyframe;
    std::vector<Keyframe>* frames = nullptr;
    switch (property) {
    case creative_suite::animation::TransformProperty::PositionX:
        frames = &layer->keyframes.position_x;
        break;
    case creative_suite::animation::TransformProperty::PositionY:
        frames = &layer->keyframes.position_y;
        break;
    case creative_suite::animation::TransformProperty::Scale:
        frames = &layer->keyframes.scale;
        break;
    case creative_suite::animation::TransformProperty::Rotation:
        frames = &layer->keyframes.rotation;
        break;
    case creative_suite::animation::TransformProperty::Opacity:
        frames = &layer->keyframes.opacity;
        break;
    }
    if (frames == nullptr || from_local_frame == to_local_frame) return false;

    const auto source = std::lower_bound(
        frames->begin(), frames->end(), from_local_frame,
        [](const Keyframe& keyframe, std::int64_t frame) {
            return keyframe.frame < frame;
        });
    if (source == frames->end() || source->frame != from_local_frame) return false;

    const auto destination = std::lower_bound(
        frames->begin(), frames->end(), to_local_frame,
        [](const Keyframe& keyframe, std::int64_t frame) {
            return keyframe.frame < frame;
        });
    if (destination != frames->end() && destination->frame == to_local_frame) return false;

    if(property==creative_suite::animation::TransformProperty::Scale)layer->scale_y.reset();
    source->frame = to_local_frame;
    std::sort(frames->begin(), frames->end(), [](const Keyframe& left, const Keyframe& right) {
        return left.frame < right.frame;
    });
    return true;
}

bool CompositionDocument::replaceEffectKeyframes(
    LayerId id,
    std::size_t effect_index,
    EffectParameterProperty property,
    const creative_suite::animation::ScalarKeyframes& keyframes)
{
    auto* layer = findLayer(id);
    if (layer == nullptr || effect_index >= layer->effects.size()) return false;
    auto candidate = layer->effects[effect_index];
    auto* target = mutableEffectKeyframesFor(candidate, property);
    if (target == nullptr) return false;
    *target = keyframes;
    if (!validLayerEffect(candidate)) return false;
    layer->effects[effect_index] = std::move(candidate);
    return true;
}

bool CompositionDocument::setEffectKeyframe(
    LayerId id,
    std::size_t effect_index,
    EffectParameterProperty property,
    std::int64_t local_frame,
    double value) noexcept
{
    auto* layer = findLayer(id);
    if (layer == nullptr || effect_index >= layer->effects.size() ||
        local_frame < 0 ||
        (layer->duration_frames > 0 && local_frame >= layer->duration_frames) ||
        !validEffectParameterValue(property, value)) {
        return false;
    }
    auto* target = mutableEffectKeyframesFor(layer->effects[effect_index], property);
    return target != nullptr &&
        creative_suite::animation::setKeyframe(*target, local_frame, value);
}

bool CompositionDocument::setEffectKeyframeInterpolation(
    LayerId id,
    std::size_t effect_index,
    EffectParameterProperty property,
    std::int64_t local_frame,
    creative_suite::animation::InterpolationMode interpolation,
    const creative_suite::animation::CubicBezierEasing& easing) noexcept
{
    auto* layer = findLayer(id);
    if (layer == nullptr || effect_index >= layer->effects.size() ||
        local_frame < 0 ||
        !creative_suite::animation::validKeyframeInterpolation(interpolation, easing)) {
        return false;
    }
    auto* target = mutableEffectKeyframesFor(layer->effects[effect_index], property);
    if (target == nullptr) return false;
    const auto it = std::lower_bound(
        target->begin(), target->end(), local_frame,
        [](const creative_suite::animation::Keyframe& keyframe, std::int64_t frame) {
            return keyframe.frame < frame;
        });
    if (it == target->end() || it->frame != local_frame) return false;
    it->interpolation = interpolation;
    it->easing = easing;
    return true;
}

bool CompositionDocument::removeEffectKeyframe(
    LayerId id,
    std::size_t effect_index,
    EffectParameterProperty property,
    std::int64_t local_frame) noexcept
{
    auto* layer = findLayer(id);
    if (layer == nullptr || effect_index >= layer->effects.size()) return false;
    auto* target = mutableEffectKeyframesFor(layer->effects[effect_index], property);
    return target != nullptr &&
        creative_suite::animation::removeKeyframe(*target, local_frame);
}

bool CompositionDocument::moveEffectKeyframe(
    LayerId id,
    std::size_t effect_index,
    EffectParameterProperty property,
    std::int64_t from_local_frame,
    std::int64_t to_local_frame) noexcept
{
    auto* layer = findLayer(id);
    if (layer == nullptr || effect_index >= layer->effects.size() ||
        from_local_frame < 0 || to_local_frame < 0 ||
        layer->duration_frames <= 0 || from_local_frame >= layer->duration_frames ||
        to_local_frame >= layer->duration_frames ||
        from_local_frame == to_local_frame) {
        return false;
    }
    auto* frames = mutableEffectKeyframesFor(layer->effects[effect_index], property);
    if (frames == nullptr) return false;

    using creative_suite::animation::Keyframe;
    const auto source = std::lower_bound(
        frames->begin(), frames->end(), from_local_frame,
        [](const Keyframe& keyframe, std::int64_t frame) {
            return keyframe.frame < frame;
        });
    if (source == frames->end() || source->frame != from_local_frame) return false;

    const auto destination = std::lower_bound(
        frames->begin(), frames->end(), to_local_frame,
        [](const Keyframe& keyframe, std::int64_t frame) {
            return keyframe.frame < frame;
        });
    if (destination != frames->end() && destination->frame == to_local_frame) return false;

    source->frame = to_local_frame;
    std::sort(frames->begin(), frames->end(), [](const Keyframe& left, const Keyframe& right) {
        return left.frame < right.frame;
    });
    return true;
}

CompositionLayer* CompositionDocument::findLayer(LayerId id) noexcept
{
    const auto layer = std::find_if(layers_.begin(), layers_.end(), [id](const auto& item) {
        return item.id == id;
    });
    return layer == layers_.end() ? nullptr : &*layer;
}

const CompositionLayer* CompositionDocument::findLayer(LayerId id) const noexcept {
    const auto it=std::find_if(layers_.begin(),layers_.end(),[id](const auto& layer){return layer.id==id;});
    return it==layers_.end()?nullptr:&*it;
}
const Mask* CompositionDocument::findMask(MaskId id) const noexcept {
    const auto it=std::find_if(masks_.begin(),masks_.end(),[id](const auto& m){return m.id==id;});
    return it==masks_.end()?nullptr:&*it;
}
MaskId CompositionDocument::maskForTarget(LayerId id,int target) const noexcept {
    const auto* layer=findLayer(id);if(!layer)return 0;
    if(target==-2)return layer->source_mask;
    if(target==-1)return layer->transform_mask;
    if(target<0 || std::size_t(target)>=layer->effects.size())return 0;
    return std::visit([](const auto& e){return e.mask;},layer->effects[target]);
}
std::size_t CompositionDocument::maskUseCount(MaskId id) const noexcept {
    if(!id)return 0;std::size_t count=0;
    for(const auto& layer:layers_) {
        count+=(layer.source_mask==id)+(layer.transform_mask==id);
        for(const auto& effect:layer.effects)count+=std::visit([id](const auto& e){return e.mask==id;},effect);
    }
    return count;
}
void CompositionDocument::collectUnreferencedMasks() noexcept {
    if (masks_.empty()) return;
    // The validated collection is capped at 4096. A bounded stack index keeps
    // removal allocation-free, including the existing noexcept layer-removal API.
    std::array<MaskId,4096> ids{};
    std::array<bool,4096> used{};
    for(std::size_t i=0;i<masks_.size();++i)ids[i]=masks_[i].id;
    const auto end=ids.begin()+masks_.size();
    std::sort(ids.begin(),end);
    const auto mark=[&](MaskId id) {
        if(!id)return;
        const auto found=std::lower_bound(ids.begin(),end,id);
        if(found!=end && *found==id)used[std::distance(ids.begin(),found)]=true;
    };
    for(const auto& layer:layers_) {
        mark(layer.source_mask);mark(layer.transform_mask);
        for(const auto& effect:layer.effects)std::visit([&](const auto& e){mark(e.mask);},effect);
    }
    std::erase_if(masks_,[&](const auto& m){const auto at=std::lower_bound(ids.begin(),end,m.id);return at==end||!used[std::distance(ids.begin(),at)];});
}

bool CompositionDocument::associateMask(LayerId id,int target,MaskId mask) {
    auto* layer=findLayer(id);
    if(!layer || layer->kind==LayerKind::Null || (mask && !findMask(mask)) || target < -2 || (target>=0 && std::size_t(target)>=layer->effects.size()))return false;
    if(maskForTarget(id,target)==mask)return false;
    if(target==-2)layer->source_mask=mask;
    else if(target==-1)layer->transform_mask=mask;
    else std::visit([mask](auto& e){e.mask=mask;},layer->effects[target]);
    collectUnreferencedMasks();return true;
}
MaskId CompositionDocument::createMask(LayerId id,int target,creative_suite::composition::ShapeMaskKind kind) {
    if(!findLayer(id)||findLayer(id)->kind==LayerKind::Null||target < -2 || (target>=0 && std::size_t(target)>=findLayer(id)->effects.size()) ||
       masks_.size()>=4096 || next_mask_id_>std::uint64_t(INT64_MAX) ||
       (kind!=creative_suite::composition::ShapeMaskKind::Rectangle && kind!=creative_suite::composition::ShapeMaskKind::Ellipse))return 0;
    const auto next=next_mask_id_++;
    masks_.push_back({next,kind});
    if(!associateMask(id,target,next)) {masks_.pop_back();return 0;}
    return next;
}
bool CompositionDocument::setMaskParameters(MaskId id,const creative_suite::composition::ShapeMaskParameters& p) {
    const auto* old=findMask(id);if(!old || old->parameters==p || !creative_suite::composition::validShapeMask(p,old->keyframes))return false;
    for(auto& m:masks_)if(m.id==id){m.parameters=p;return true;}return false;
}
bool CompositionDocument::replaceMaskKeyframes(MaskId id,std::size_t property,creative_suite::animation::ScalarKeyframes keys) {
    const auto* old=findMask(id);if(!old||property>=5)return false;
    auto m=*old;m.keyframes[property]=std::move(keys);
    if(!creative_suite::composition_exchange::validMask(m)||m.keyframes==old->keyframes)return false;
    for(auto& existing:masks_)if(existing.id==id){existing=std::move(m);return true;}return false;
}
bool CompositionDocument::setMaskKeyframe(MaskId id,std::size_t property,std::int64_t frame,double value) {
    const auto* m=findMask(id);if(!m||property>=5)return false;
    auto keys=m->keyframes[property];if(!creative_suite::animation::setKeyframe(keys,frame,value))return false;
    return replaceMaskKeyframes(id,property,std::move(keys));
}
bool CompositionDocument::removeMaskKeyframe(MaskId id,std::size_t property,std::int64_t frame) {
    const auto* m=findMask(id);if(!m||property>=5)return false;
    auto keys=m->keyframes[property];if(!creative_suite::animation::removeKeyframe(keys,frame))return false;
    return replaceMaskKeyframes(id,property,std::move(keys));
}
bool CompositionDocument::setMaskKeyframeInterpolation(MaskId id,std::size_t property,std::int64_t frame,
    creative_suite::animation::InterpolationMode interpolation,const creative_suite::animation::CubicBezierEasing& easing) {
    const auto* m=findMask(id);if(!m||property>=5||!creative_suite::animation::validKeyframeInterpolation(interpolation,easing))return false;
    auto keys=m->keyframes[property];
    for(auto& k:keys)if(k.frame==frame){k.interpolation=interpolation;k.easing=easing;return replaceMaskKeyframes(id,property,std::move(keys));}
    return false;
}
} // namespace motion::model
