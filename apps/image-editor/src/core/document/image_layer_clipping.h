#pragma once

#include "image_document_utils.h"
#include <QHash>

namespace image_editor {
// Bottom-to-top sibling segments. Root groups are barriers, never clipping bases.
[[nodiscard]] inline QVector<QStringList> clippingSegments(const ImageDocumentData& document) {
    QVector<QStringList> segments;
    QStringList root;
    for (const auto& item : document.root_stack) {
        if (!item.group) { root.append(item.id); continue; }
        if (!root.isEmpty()) { segments.append(root); root.clear(); }
        if (const auto* group = findGroup(document, item.id)) segments.append(group->layer_ids);
    }
    if (!root.isEmpty()) segments.append(root);
    return segments;
}

// Includes eligible bases for ordinary layers, so creation and rendering share one resolver.
[[nodiscard]] inline QHash<QString, QString> clippingBases(const ImageDocumentData& document) {
    QHash<QString, QString> bases;
    QHash<QString, const ImageLayerData*> layers;
    for (const auto& layer : document.layers) layers.insert(layer.id, &layer);
    for (const auto& siblings : clippingSegments(document)) {
        QString base;
        for (const auto& id : siblings) {
            const auto* layer = layers.value(id, nullptr);
            if (!layer) continue;
            if (!base.isEmpty()) bases.insert(id, base);
            if (!layer->clip_to_base) base = id;
        }
    }
    return bases;
}

[[nodiscard]] inline QStringList clippingChain(const ImageDocumentData& document, const QString& base_id) {
    QStringList chain{base_id};
    for (const auto& siblings : clippingSegments(document)) {
        const auto start = siblings.indexOf(base_id);
        if (start < 0) continue;
        for (qsizetype i = start + 1; i < siblings.size(); ++i) {
            const auto* layer = findLayer(document, siblings[i]);
            if (!layer || !layer->clip_to_base) break;
            chain.append(layer->id);
        }
        break;
    }
    return chain;
}

// Clear the entire orphan run: clearing its first member must not promote it to a new base.
inline void releaseOrphanClipping(ImageDocumentData& document) {
    const auto bases = clippingBases(document);
    for (auto& layer : document.layers)
        if (layer.clip_to_base && (layer.background || !bases.contains(layer.id))) layer.clip_to_base = false;
}

[[nodiscard]] inline bool participatesInClipping(const ImageDocumentData& document, const QString& id) {
    const auto* layer = findLayer(document, id);
    return layer && (layer->clip_to_base || clippingChain(document, id).size() > 1);
}
} // namespace image_editor
