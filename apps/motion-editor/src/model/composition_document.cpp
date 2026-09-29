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

bool validPositiveFinite(const std::optional<double>& value) noexcept
{
    return value.has_value() && std::isfinite(*value) && *value > 0.0;
}

bool validLayer(const CompositionLayer& layer) noexcept
{
    using creative_suite::animation::TransformProperty;
    using creative_suite::animation::validKeyframeValue;
    using creative_suite::animation::validTransform;

    if (layer.id == 0 || !validLayerKind(layer.kind) || layer.timeline_start_frame < 0 ||
        layer.duration_frames < 0 ||
        layer.duration_frames > std::numeric_limits<std::int64_t>::max() -
            layer.timeline_start_frame ||
        layer.source_frame_count < 0 || layer.source_duration_frames < 0 ||
        layer.maximum_timeline_duration_frames < 0 ||
        !std::isfinite(layer.source_frame_rate) || layer.source_frame_rate < 0.0 ||
        !validTransform(layer.transform)) {
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
         layer.source_frame_count <= 0 || layer.source_frame_rate <= 0.0)) {
        return false;
    }

    const auto valid_keyframes = [](const auto& frames, TransformProperty property) {
        std::int64_t previous_frame = -1;
        for (const auto& keyframe : frames) {
            if (keyframe.frame < 0 || keyframe.frame <= previous_frame ||
                !validKeyframeValue(property, keyframe.value)) {
                return false;
            }
            previous_frame = keyframe.frame;
        }
        return true;
    };
    return valid_keyframes(layer.keyframes.position_x, TransformProperty::PositionX) &&
        valid_keyframes(layer.keyframes.position_y, TransformProperty::PositionY) &&
        valid_keyframes(layer.keyframes.scale, TransformProperty::Scale) &&
        valid_keyframes(layer.keyframes.rotation, TransformProperty::Rotation) &&
        valid_keyframes(layer.keyframes.opacity, TransformProperty::Opacity);
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
    for (const auto& layer : layers) {
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
    layers_.push_back(CompositionLayer{id, kind, std::move(name)});
    ++next_layer_id_;
    return id;
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
        const auto image_duration = checkedCeiling(
            5.0L * static_cast<long double>(frame_rate_.numerator) /
            static_cast<long double>(frame_rate_.denominator));
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

bool CompositionDocument::setLayerName(LayerId id, std::string name)
{
    auto* layer = findLayer(id);
    if (layer == nullptr) {
        return false;
    }
    layer->name = std::move(name);
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
