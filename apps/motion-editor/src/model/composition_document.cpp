#include "composition_document.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
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

CompositionLayer* CompositionDocument::findLayer(LayerId id) noexcept
{
    const auto layer = std::find_if(layers_.begin(), layers_.end(), [id](const auto& item) {
        return item.id == id;
    });
    return layer == layers_.end() ? nullptr : &*layer;
}

} // namespace motion::model
