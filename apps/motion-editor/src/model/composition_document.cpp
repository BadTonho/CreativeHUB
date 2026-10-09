#include "composition_document.h"

#include <creative_suite/media/media_library.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_set>
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
        return std::holds_alternative<std::monostate>(content);
    }
    return false;
}

bool validLayer(const CompositionLayer& layer) noexcept
{
    using creative_suite::animation::validTransform;
    using creative_suite::animation::validTransformKeyframes;

    if (layer.id == 0 || !validLayerKind(layer.kind) || layer.timeline_start_frame < 0 ||
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
        !validLayerEffects(layer.effects)) {
        return false;
    }
    if (layer.duration_frames > 0 &&
        (layer.kind == LayerKind::Image || layer.kind == LayerKind::Video) &&
        layer.source_path.empty()) {
        return false;
    }
    if (layer.kind == LayerKind::Video && layer.duration_frames > 0 &&
        (layer.maximum_timeline_duration_frames <= 0 ||
         layer.duration_frames > layer.maximum_timeline_duration_frames ||
         layer.source_frame_count <= 0 || layer.source_frame_rate <= 0.0 ||
         layer.source_start_frame >= layer.source_frame_count)) {
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

bool validLayerEffect(const LayerEffect& effect) noexcept
{
    if (const auto* blur = std::get_if<GaussianBlurEffect>(&effect)) {
        return std::isfinite(blur->radius_pixels) &&
            blur->radius_pixels >= 0.0 && blur->radius_pixels <= 100.0;
    }
    if (const auto* color = std::get_if<ColorAdjustmentEffect>(&effect)) {
        return std::isfinite(color->brightness) &&
            color->brightness >= -100.0 && color->brightness <= 100.0 &&
            std::isfinite(color->contrast_percent) &&
            color->contrast_percent >= 0.0 && color->contrast_percent <= 200.0 &&
            std::isfinite(color->saturation_percent) &&
            color->saturation_percent >= 0.0 && color->saturation_percent <= 200.0;
    }
    return false;
}

bool validLayerEffects(const std::vector<LayerEffect>& effects) noexcept
{
    return effects.size() <= kMaximumLayerEffectCount &&
        std::all_of(effects.begin(), effects.end(), validLayerEffect);
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
    std::vector<CompositionLayer> layers)
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
    if ((kind != LayerKind::Text && kind != LayerKind::Shape) ||
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

bool CompositionDocument::removeLayer(LayerId id) noexcept
{
    const auto layer = std::find_if(layers_.begin(), layers_.end(), [id](const auto& item) {
        return item.id == id;
    });
    if (layer == layers_.end()) {
        return false;
    }
    layers_.erase(layer);
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

bool CompositionDocument::setLayerTransform(
    LayerId id,
    const creative_suite::animation::Transform2D& transform) noexcept
{
    if (!creative_suite::animation::validTransform(transform)) {
        return false;
    }
    auto* layer = findLayer(id);
    if (layer == nullptr) {
        return false;
    }
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
    if (layer == nullptr || !validLayerEffects(effects)) return false;
    layer->effects = effects;
    return true;
}

bool CompositionDocument::replaceLayerKeyframes(
    LayerId id,
    creative_suite::animation::TransformProperty property,
    const std::vector<creative_suite::animation::Keyframe>& keyframes)
{
    auto* layer = findLayer(id);
    if (layer == nullptr) return false;
    auto candidate = layer->keyframes;
    auto* target = mutableKeyframesFor(candidate, property);
    if (target == nullptr) return false;
    *target = keyframes;
    if (!creative_suite::animation::validTransformKeyframes(candidate)) return false;
    layer->keyframes = std::move(candidate);
    return true;
}

bool CompositionDocument::setLayerKeyframe(
    LayerId id,
    creative_suite::animation::TransformProperty property,
    std::int64_t local_frame,
    double value) noexcept
{
    auto* layer = findLayer(id);
    return layer != nullptr
        && creative_suite::animation::setKeyframe(
            layer->keyframes, property, local_frame, value);
}

bool CompositionDocument::setLayerKeyframeInterpolation(
    LayerId id,
    creative_suite::animation::TransformProperty property,
    std::int64_t local_frame,
    creative_suite::animation::InterpolationMode interpolation,
    const creative_suite::animation::CubicBezierEasing& easing) noexcept
{
    auto* layer = findLayer(id);
    return layer != nullptr && creative_suite::animation::setKeyframeInterpolation(
        layer->keyframes, property, local_frame, interpolation, easing);
}

bool CompositionDocument::removeLayerKeyframe(
    LayerId id,
    creative_suite::animation::TransformProperty property,
    std::int64_t local_frame) noexcept
{
    auto* layer = findLayer(id);
    return layer != nullptr && creative_suite::animation::removeKeyframe(
        layer->keyframes, property, local_frame);
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

} // namespace motion::model
