#pragma once

#include "image_document_store.h"

#include <optional>

namespace image_editor {

struct ImageLayerStackEditResult {
    ImageDocumentData document;
    QString selected_layer_id;
    QString selected_group_id;
};

// Prepares structural stack edits without accessing session history or state.
class ImageLayerStackEditor final {
public:
    [[nodiscard]] static qsizetype itemCount(
        const ImageDocumentData& document) noexcept;
    static void rebuildLayerOrder(ImageDocumentData& document);

    [[nodiscard]] static std::optional<ImageLayerStackEditResult> addLayer(
        const ImageDocumentData& document,
        const QString& selected_layer_id,
        const QString& selected_group_id);
    [[nodiscard]] static std::optional<ImageLayerStackEditResult> deleteItems(
        const ImageDocumentData& document,
        const QVector<ImageStackItemData>& items,
        const QString& selected_layer_id,
        const QString& selected_group_id);
    [[nodiscard]] static std::optional<ImageLayerStackEditResult> addGroup(
        const ImageDocumentData& document,
        const QString& selected_layer_id,
        const QString& selected_group_id,
        QString* error = nullptr);
    [[nodiscard]] static std::optional<ImageLayerStackEditResult> groupLayers(
        const ImageDocumentData& document,
        const QStringList& layer_ids,
        const QString& selected_group_id,
        QString* error = nullptr);
    [[nodiscard]] static std::optional<ImageLayerStackEditResult> ungroup(
        const ImageDocumentData& document,
        const QString& group_id,
        const QString& selected_layer_id,
        const QString& selected_group_id);
    [[nodiscard]] static std::optional<ImageLayerStackEditResult> moveItem(
        const ImageDocumentData& document,
        const QString& item_id,
        bool is_group,
        const QString& target_group_id,
        qsizetype insertion_index,
        const QString& selected_layer_id,
        const QString& selected_group_id);
    [[nodiscard]] static std::optional<ImageLayerStackEditResult> moveItemBy(
        const ImageDocumentData& document,
        const QString& item_id,
        bool is_group,
        int direction,
        const QString& selected_layer_id,
        const QString& selected_group_id);
};

} // namespace image_editor
