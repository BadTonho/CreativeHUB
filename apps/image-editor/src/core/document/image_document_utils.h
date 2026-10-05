#pragma once

#include "image_document_store.h"

#include <QDir>
#include <QFileInfo>

#include <atomic>

#include <algorithm>

namespace image_editor {

[[nodiscard]] inline QString absoluteCleanPath(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

[[nodiscard]] inline const ImageLayerData* findLayer(
    const ImageDocumentData& document, const QString& id) {
    const auto found = std::find_if(document.layers.cbegin(), document.layers.cend(),
        [&id](const ImageLayerData& layer) { return layer.id == id; });
    return found == document.layers.cend() ? nullptr : &*found;
}

[[nodiscard]] inline ImageLayerData* findLayer(
    ImageDocumentData& document, const QString& id) {
    const auto found = std::find_if(document.layers.begin(), document.layers.end(),
        [&id](const ImageLayerData& layer) { return layer.id == id; });
    return found == document.layers.end() ? nullptr : &*found;
}

[[nodiscard]] inline const ImageGroupData* findGroup(
    const ImageDocumentData& document, const QString& id) {
    const auto found = std::find_if(document.groups.cbegin(), document.groups.cend(),
        [&id](const ImageGroupData& group) { return group.id == id; });
    return found == document.groups.cend() ? nullptr : &*found;
}

[[nodiscard]] inline ImageGroupData* findGroup(
    ImageDocumentData& document, const QString& id) {
    const auto found = std::find_if(document.groups.begin(), document.groups.end(),
        [&id](const ImageGroupData& group) { return group.id == id; });
    return found == document.groups.end() ? nullptr : &*found;
}

[[nodiscard]] inline QString operationObjectId(const ImageOperation& operation) {
    switch (operation.kind) {
    case OperationKind::PaintStroke: return operation.paint_stroke.id;
    case OperationKind::EraseStroke: return operation.erase_stroke.id;
    case OperationKind::Shape: return operation.shape.id;
    case OperationKind::Text: return operation.text.id;
    case OperationKind::RasterImage: return operation.raster.id;
    default: return {};
    }
}

[[nodiscard]] inline bool exportWasCancelled(
    const std::atomic_bool* cancellation_requested) noexcept {
    return cancellation_requested != nullptr &&
        cancellation_requested->load(std::memory_order_relaxed);
}

} // namespace image_editor
