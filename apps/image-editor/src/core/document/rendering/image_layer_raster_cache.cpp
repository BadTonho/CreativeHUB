#include "rendering/image_layer_raster_cache.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace image_editor {

QVector<ImageLayerRasterCache::RasterResourceSignature>
ImageLayerRasterCache::resourceSignatures(
    const ImageLayerData& layer,
    const QHash<QString, QImage>& raster_images) {
    QVector<RasterResourceSignature> signatures;
    for (const auto& operation : layer.operations) {
        if (operation.kind != OperationKind::RasterImage) continue;
        const QImage image = raster_images.value(
            operation.raster.id,
            raster_images.value(operation.raster.source_path));
        signatures.append({operation.raster.id, image.cacheKey()});
    }
    return signatures;
}

bool ImageLayerRasterCache::sameOperations(
    const QVector<ImageOperation>& left,
    const QVector<ImageOperation>& right) noexcept {
    return left.size() == right.size() && left.constData() == right.constData();
}

bool ImageLayerRasterCache::sameMask(
    const std::optional<ImageLayerMaskData>& left,
    const std::optional<ImageLayerMaskData>& right) noexcept {
    if (left.has_value() != right.has_value()) return false;
    if (!left.has_value()) return true;
    return left->enabled == right->enabled &&
        sameOperations(left->operations, right->operations);
}

ImageLayerRasterCache::LookupResult ImageLayerRasterCache::lookup(
    const ImageLayerData& layer,
    const QSize& canvas_size,
    const QHash<QString, QImage>& raster_images) {
    auto entry = entries_.find(layer.id);
    if (entry != entries_.end()) {
        if (entry->canvas_size == canvas_size &&
            sameOperations(entry->operations, layer.operations) &&
            sameMask(entry->mask, layer.mask) &&
            entry->raster_resources == resourceSignatures(layer, raster_images)) {
            return {LookupState::Hit, entry->pixels};
        }
        removeEntry(entry);
    }

    if (canvas_size.width() <= 0 || canvas_size.height() <= 0) {
        return {LookupState::Bypass, {}};
    }
    const auto width = static_cast<std::uint64_t>(canvas_size.width());
    const auto height = static_cast<std::uint64_t>(canvas_size.height());
    if (width > std::numeric_limits<std::uint64_t>::max() / 4ULL / height) {
        return {LookupState::Bypass, {}};
    }
    const std::uint64_t requested_bytes = width * height * 4ULL;
    if (requested_bytes > maximum_bytes ||
        retained_bytes_ > maximum_bytes - requested_bytes) {
        return {LookupState::Bypass, {}};
    }
    return {LookupState::Miss, {}};
}

void ImageLayerRasterCache::insert(
    const ImageLayerData& layer,
    const QSize& canvas_size,
    const QHash<QString, QImage>& raster_images,
    const QImage& pixels) {
    if (pixels.isNull() || pixels.size() != canvas_size ||
        pixels.format() != QImage::Format_ARGB32_Premultiplied) return;
    const qsizetype image_size = pixels.sizeInBytes();
    if (image_size <= 0) return;
    const auto bytes = static_cast<std::uint64_t>(image_size);
    if (bytes > maximum_bytes) return;

    auto existing = entries_.find(layer.id);
    if (existing != entries_.end()) removeEntry(existing);
    if (retained_bytes_ > maximum_bytes - bytes) return;

    Entry entry;
    entry.operations = layer.operations;
    entry.mask = layer.mask;
    entry.raster_resources = resourceSignatures(layer, raster_images);
    entry.canvas_size = canvas_size;
    entry.pixels = pixels;
    entry.bytes = bytes;
    retained_bytes_ += bytes;
    entries_.insert(layer.id, std::move(entry));
}

void ImageLayerRasterCache::synchronize(
    const QVector<ImageLayerData>& layers,
    const QSize& canvas_size,
    const QHash<QString, QImage>& raster_images) {
    for (auto entry = entries_.begin(); entry != entries_.end();) {
        const auto layer = std::find_if(layers.cbegin(), layers.cend(),
            [&entry](const ImageLayerData& candidate) {
                return candidate.id == entry.key();
            });
        if (layer == layers.cend() || entry->canvas_size != canvas_size ||
            !sameOperations(entry->operations, layer->operations) ||
            !sameMask(entry->mask, layer->mask) ||
            entry->raster_resources != resourceSignatures(*layer, raster_images)) {
            entry = entries_.erase(entry);
        } else {
            ++entry;
        }
    }
}

void ImageLayerRasterCache::invalidateLayer(const QString& layer_id) noexcept {
    auto entry = entries_.find(layer_id);
    if (entry != entries_.end()) removeEntry(entry);
}

void ImageLayerRasterCache::clear() noexcept {
    entries_.clear();
    retained_bytes_ = 0;
}

void ImageLayerRasterCache::removeEntry(
    QHash<QString, Entry>::iterator entry) noexcept {
    retained_bytes_ -= entry->bytes;
    entries_.erase(entry);
}

} // namespace image_editor
