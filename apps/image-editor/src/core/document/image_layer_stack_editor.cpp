#include "image_layer_stack_editor.h"

#include "image_document_utils.h"

#include <QUuid>
#include <QSet>

#include <algorithm>
#include <iterator>
#include <utility>

namespace image_editor {
namespace {

void assignError(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
}

bool documentExists(const ImageDocumentData& document) {
    return document.base_kind == ImageBaseKind::Canvas ||
        !document.source_path.isEmpty();
}

qsizetype layerIndex(const ImageDocumentData& document, const QString& layer_id) {
    for (qsizetype index = 0; index < document.layers.size(); ++index) {
        if (document.layers.at(index).id == layer_id) return index;
    }
    return -1;
}

qsizetype groupIndex(const ImageDocumentData& document, const QString& group_id) {
    for (qsizetype index = 0; index < document.groups.size(); ++index) {
        if (document.groups.at(index).id == group_id) return index;
    }
    return -1;
}

QString uniqueStackName(const ImageDocumentData& document,
                        const QString& prefix) {
    const auto name_exists = [&document](const QString& candidate) {
        return std::any_of(document.layers.cbegin(), document.layers.cend(),
            [&candidate](const ImageLayerData& layer) {
                return layer.name.compare(candidate, Qt::CaseInsensitive) == 0;
            }) || std::any_of(document.groups.cbegin(), document.groups.cend(),
            [&candidate](const ImageGroupData& group) {
                return group.name.compare(candidate, Qt::CaseInsensitive) == 0;
            });
    };
    int suffix = 1;
    QString name;
    do {
        name = QStringLiteral("%1 %2").arg(prefix).arg(suffix++);
    } while (name_exists(name));
    return name;
}

ImageLayerStackEditResult makeResult(ImageDocumentData document,
                                     QString selected_layer_id,
                                     QString selected_group_id) {
    return {std::move(document), std::move(selected_layer_id),
            std::move(selected_group_id)};
}

} // namespace

qsizetype ImageLayerStackEditor::itemCount(
    const ImageDocumentData& document) noexcept {
    return document.layers.size() + document.groups.size();
}

void ImageLayerStackEditor::rebuildLayerOrder(ImageDocumentData& document) {
    QVector<ImageLayerData> ordered;
    ordered.reserve(document.layers.size());
    const auto append_layer = [&document, &ordered](const QString& id) {
        const auto* layer = findLayer(document, id);
        if (layer != nullptr) ordered.append(*layer);
    };
    for (const auto& item : document.root_stack) {
        if (!item.group) {
            append_layer(item.id);
            continue;
        }
        const auto* group = findGroup(document, item.id);
        if (group == nullptr) continue;
        for (const auto& layer_id : group->layer_ids) append_layer(layer_id);
    }
    document.layers = std::move(ordered);
}

std::optional<ImageLayerStackEditResult> ImageLayerStackEditor::addLayer(
    const ImageDocumentData& document,
    const QString& selected_layer_id,
    const QString& selected_group_id) {
    if (!documentExists(document) ||
        itemCount(document) >= ImageDocumentStore::kMaximumLayers) {
        return {};
    }

    ImageDocumentData updated = document;
    ImageLayerData layer;
    layer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    layer.name = uniqueStackName(updated, QStringLiteral("Layer"));
    const auto* selected_layer = findLayer(updated, selected_layer_id);
    const QString parent_group_id = selected_group_id.isEmpty() &&
        selected_layer != nullptr ? selected_layer->parent_group_id : QString{};
    layer.parent_group_id = parent_group_id;

    if (!parent_group_id.isEmpty()) {
        auto* parent = findGroup(updated, parent_group_id);
        if (parent == nullptr) return {};
        const qsizetype selected_index = parent->layer_ids.indexOf(selected_layer_id);
        parent->layer_ids.insert(selected_index < 0 ? parent->layer_ids.size()
                                                   : selected_index + 1,
                                 layer.id);
    } else {
        qsizetype insertion_index = updated.root_stack.size();
        if (!selected_group_id.isEmpty()) {
            for (qsizetype index = 0; index < updated.root_stack.size(); ++index) {
                if (updated.root_stack.at(index).group &&
                    updated.root_stack.at(index).id == selected_group_id) {
                    insertion_index = index + 1;
                    break;
                }
            }
        } else {
            for (qsizetype index = 0; index < updated.root_stack.size(); ++index) {
                if (!updated.root_stack.at(index).group &&
                    updated.root_stack.at(index).id == selected_layer_id) {
                    insertion_index = index + 1;
                    break;
                }
            }
        }
        updated.root_stack.insert(insertion_index, {layer.id, false});
    }
    const QString layer_id = layer.id;
    updated.layers.append(std::move(layer));
    rebuildLayerOrder(updated);
    return makeResult(std::move(updated), layer_id, {});
}

std::optional<ImageLayerStackEditResult> ImageLayerStackEditor::deleteItems(
    const ImageDocumentData& document,
    const QVector<ImageStackItemData>& items,
    const QString& selected_layer_id,
    const QString& selected_group_id) {
    QSet<QString> layer_ids;
    QSet<QString> group_ids;
    for (const auto& item : items) {
        if (item.group) {
            const qsizetype index = groupIndex(document, item.id);
            if (index < 0) continue;
            group_ids.insert(item.id);
            for (const auto& child : document.groups.at(index).layer_ids) {
                if (layerIndex(document, child) > 0) layer_ids.insert(child);
            }
        } else if (layerIndex(document, item.id) > 0) {
            layer_ids.insert(item.id);
        }
    }
    if (layer_ids.isEmpty() && group_ids.isEmpty()) return {};

    const qsizetype selected_index = layerIndex(document, selected_layer_id);
    const bool removed_layer = layer_ids.contains(selected_layer_id);
    const bool removed_group = group_ids.contains(selected_group_id);
    ImageDocumentData updated = document;
    updated.root_stack.erase(std::remove_if(updated.root_stack.begin(), updated.root_stack.end(),
        [&group_ids, &layer_ids](const ImageStackItemData& item) {
            return item.group ? group_ids.contains(item.id) : layer_ids.contains(item.id);
        }), updated.root_stack.end());
    for (auto& group : updated.groups) {
        group.layer_ids.erase(std::remove_if(group.layer_ids.begin(), group.layer_ids.end(),
            [&layer_ids](const QString& id) { return layer_ids.contains(id); }),
            group.layer_ids.end());
    }
    updated.layers.erase(std::remove_if(updated.layers.begin(), updated.layers.end(),
        [&layer_ids](const ImageLayerData& layer) {
            return layer_ids.contains(layer.id);
        }), updated.layers.end());
    updated.groups.erase(std::remove_if(updated.groups.begin(), updated.groups.end(),
        [&group_ids](const ImageGroupData& group) {
            return group_ids.contains(group.id);
        }), updated.groups.end());

    QString next_layer_id = selected_layer_id;
    QString next_group_id = selected_group_id;
    if (removed_layer || removed_group) {
        const qsizetype replacement = std::clamp(
            selected_index, qsizetype{0}, updated.layers.size() - 1);
        next_layer_id = updated.layers.at(replacement).id;
        next_group_id.clear();
    }
    rebuildLayerOrder(updated);
    return makeResult(std::move(updated), std::move(next_layer_id),
                      std::move(next_group_id));
}

std::optional<ImageLayerStackEditResult> ImageLayerStackEditor::addGroup(
    const ImageDocumentData& document,
    const QString& selected_layer_id,
    const QString& selected_group_id,
    QString* error) {
    if (error != nullptr) error->clear();
    if (!documentExists(document) ||
        itemCount(document) >= ImageDocumentStore::kMaximumLayers) {
        assignError(error, QStringLiteral(
            "The document has reached the maximum of 512 stack items."));
        return {};
    }

    ImageDocumentData updated = document;
    ImageGroupData group;
    group.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    group.name = uniqueStackName(updated, QStringLiteral("Group"));
    qsizetype insertion_index = updated.root_stack.size();
    if (!selected_group_id.isEmpty()) {
        for (qsizetype index = 0; index < updated.root_stack.size(); ++index) {
            if (updated.root_stack.at(index).group &&
                updated.root_stack.at(index).id == selected_group_id) {
                insertion_index = index + 1;
                break;
            }
        }
    } else if (!selected_layer_id.isEmpty()) {
        const auto* selected_layer = findLayer(updated, selected_layer_id);
        const QString selected_parent = selected_layer == nullptr
            ? QString{} : selected_layer->parent_group_id;
        for (qsizetype index = 0; index < updated.root_stack.size(); ++index) {
            const auto& item = updated.root_stack.at(index);
            const bool selected_root_layer = selected_parent.isEmpty() &&
                !item.group && item.id == selected_layer_id;
            const bool selected_parent_group = !selected_parent.isEmpty() &&
                item.group && item.id == selected_parent;
            if (selected_root_layer || selected_parent_group) {
                insertion_index = index + 1;
                break;
            }
        }
    }
    const QString group_id = group.id;
    updated.groups.append(std::move(group));
    updated.root_stack.insert(insertion_index, {group_id, true});
    return makeResult(std::move(updated), {}, group_id);
}

std::optional<ImageLayerStackEditResult> ImageLayerStackEditor::groupLayers(
    const ImageDocumentData& document,
    const QStringList& layer_ids,
    const QString& selected_group_id,
    QString* error) {
    if (error != nullptr) error->clear();
    if (layer_ids.size() < 2 || !selected_group_id.isEmpty()) {
        assignError(error, QStringLiteral(
            "Select at least two contiguous root layers to group."));
        return {};
    }

    QSet<QString> requested;
    for (const auto& id : layer_ids) {
        const auto* layer = findLayer(document, id);
        if (layer == nullptr || layer->background ||
            !layer->parent_group_id.isEmpty() || requested.contains(id)) {
            assignError(error, QStringLiteral(
                "Only distinct root raster layers can be grouped."));
            return {};
        }
        requested.insert(id);
    }
    QVector<qsizetype> positions;
    for (qsizetype index = 0; index < document.root_stack.size(); ++index) {
        if (!document.root_stack.at(index).group &&
            requested.contains(document.root_stack.at(index).id)) {
            positions.append(index);
        }
    }
    std::sort(positions.begin(), positions.end());
    if (positions.size() != requested.size() || positions.isEmpty() ||
        positions.back() - positions.front() + 1 != positions.size()) {
        assignError(error, QStringLiteral(
            "Group Selected requires contiguous sibling layers."));
        return {};
    }
    if (itemCount(document) >= ImageDocumentStore::kMaximumLayers) {
        assignError(error, QStringLiteral(
            "The document has reached the maximum of 512 stack items."));
        return {};
    }

    ImageDocumentData updated = document;
    ImageGroupData group;
    group.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    group.name = uniqueStackName(updated, QStringLiteral("Group"));
    for (qsizetype index = positions.front(); index <= positions.back(); ++index) {
        group.layer_ids.append(updated.root_stack.at(index).id);
    }
    const QString group_id = group.id;
    for (const auto& child_id : group.layer_ids) {
        if (auto* child = findLayer(updated, child_id)) {
            child->parent_group_id = group_id;
        }
    }
    for (qsizetype index = positions.back(); index >= positions.front(); --index) {
        updated.root_stack.removeAt(index);
    }
    updated.root_stack.insert(positions.front(), {group_id, true});
    updated.groups.append(std::move(group));
    rebuildLayerOrder(updated);
    return makeResult(std::move(updated), {}, group_id);
}

std::optional<ImageLayerStackEditResult> ImageLayerStackEditor::ungroup(
    const ImageDocumentData& document,
    const QString& group_id,
    const QString& selected_layer_id,
    const QString& selected_group_id) {
    const qsizetype index = groupIndex(document, group_id);
    if (index < 0) return {};
    const auto root_item = std::find_if(document.root_stack.cbegin(),
        document.root_stack.cend(), [&group_id](const ImageStackItemData& item) {
            return item.group && item.id == group_id;
        });
    if (root_item == document.root_stack.cend()) return {};
    const qsizetype root_index = std::distance(document.root_stack.cbegin(), root_item);
    const QStringList children = document.groups.at(index).layer_ids;

    ImageDocumentData updated = document;
    updated.root_stack.removeAt(root_index);
    for (qsizetype child_index = 0; child_index < children.size(); ++child_index) {
        const QString child_id = children.at(child_index);
        updated.root_stack.insert(root_index + child_index, {child_id, false});
        if (auto* child = findLayer(updated, child_id)) {
            child->parent_group_id.clear();
        }
    }
    updated.groups.removeAt(index);

    QString next_layer_id = selected_layer_id;
    QString next_group_id = selected_group_id;
    if (selected_group_id == group_id) {
        next_group_id.clear();
        next_layer_id = children.isEmpty()
            ? (updated.layers.isEmpty() ? QString{} : updated.layers.back().id)
            : children.back();
    }
    rebuildLayerOrder(updated);
    return makeResult(std::move(updated), std::move(next_layer_id),
                      std::move(next_group_id));
}

std::optional<ImageLayerStackEditResult> ImageLayerStackEditor::moveItem(
    const ImageDocumentData& document,
    const QString& item_id,
    bool is_group,
    const QString& target_group_id,
    qsizetype insertion_index,
    const QString& selected_layer_id,
    const QString& selected_group_id) {
    ImageDocumentData updated = document;
    if (is_group) {
        if (!target_group_id.isEmpty() || groupIndex(updated, item_id) < 0) {
            return {};
        }
        qsizetype source_index = -1;
        for (qsizetype index = 0; index < updated.root_stack.size(); ++index) {
            if (updated.root_stack.at(index).group &&
                updated.root_stack.at(index).id == item_id) {
                source_index = index;
                break;
            }
        }
        if (source_index < 0) return {};
        const qsizetype bounded_index = std::clamp(insertion_index,
            qsizetype{1}, static_cast<qsizetype>(updated.root_stack.size()));
        if (bounded_index == source_index || bounded_index == source_index + 1) {
            return {};
        }
        updated.root_stack.removeAt(source_index);
        const qsizetype adjusted = bounded_index > source_index
            ? bounded_index - 1 : bounded_index;
        updated.root_stack.insert(adjusted, {item_id, true});
    } else {
        const qsizetype index = layerIndex(updated, item_id);
        if (index <= 0) return {};
        const QString source_group_id = updated.layers.at(index).parent_group_id;
        if (!target_group_id.isEmpty() && groupIndex(updated, target_group_id) < 0) {
            return {};
        }

        const qsizetype source_position = [&]() -> qsizetype {
            if (source_group_id.isEmpty()) {
                for (qsizetype stack_index = 0;
                     stack_index < updated.root_stack.size(); ++stack_index) {
                    if (!updated.root_stack.at(stack_index).group &&
                        updated.root_stack.at(stack_index).id == item_id) {
                        return stack_index;
                    }
                }
            } else if (const auto* group = findGroup(updated, source_group_id)) {
                return group->layer_ids.indexOf(item_id);
            }
            return -1;
        }();
        if (source_position < 0) return {};

        qsizetype target_count = 0;
        if (target_group_id.isEmpty()) {
            target_count = updated.root_stack.size();
        } else {
            target_count = findGroup(updated, target_group_id)->layer_ids.size();
        }
        qsizetype bounded_index = std::clamp(
            insertion_index, qsizetype{0}, target_count);
        if (target_group_id.isEmpty()) {
            bounded_index = std::max(qsizetype{1}, bounded_index);
        }
        if (source_group_id == target_group_id &&
            bounded_index > source_position) {
            --bounded_index;
        }
        if (source_group_id == target_group_id &&
            bounded_index == source_position) {
            return {};
        }

        if (source_group_id.isEmpty()) {
            for (qsizetype stack_index = 0;
                 stack_index < updated.root_stack.size(); ++stack_index) {
                if (!updated.root_stack.at(stack_index).group &&
                    updated.root_stack.at(stack_index).id == item_id) {
                    updated.root_stack.removeAt(stack_index);
                    break;
                }
            }
        } else {
            findGroup(updated, source_group_id)->layer_ids.removeAll(item_id);
        }
        auto* layer = findLayer(updated, item_id);
        layer->parent_group_id = target_group_id;
        if (target_group_id.isEmpty()) {
            updated.root_stack.insert(bounded_index, {item_id, false});
        } else {
            findGroup(updated, target_group_id)->layer_ids.insert(
                bounded_index, item_id);
        }
    }
    rebuildLayerOrder(updated);
    return makeResult(std::move(updated), selected_layer_id, selected_group_id);
}

std::optional<ImageLayerStackEditResult> ImageLayerStackEditor::moveItemBy(
    const ImageDocumentData& document,
    const QString& item_id,
    bool is_group,
    int direction,
    const QString& selected_layer_id,
    const QString& selected_group_id) {
    if (direction != -1 && direction != 1) return {};
    if (is_group) {
        qsizetype index = -1;
        for (qsizetype candidate = 1;
             candidate < document.root_stack.size(); ++candidate) {
            if (document.root_stack.at(candidate).group &&
                document.root_stack.at(candidate).id == item_id) {
                index = candidate;
                break;
            }
        }
        if (index < 0) return {};
        const qsizetype target = index + direction;
        if (target <= 0 || target >= document.root_stack.size()) return {};
        return moveItem(document, item_id, true, {},
            target + (direction > 0 ? 1 : 0), selected_layer_id,
            selected_group_id);
    }

    const qsizetype index = layerIndex(document, item_id);
    if (index <= 0) return {};
    const QString parent_id = document.layers.at(index).parent_group_id;
    qsizetype position = -1;
    qsizetype count = 0;
    if (parent_id.isEmpty()) {
        count = document.root_stack.size();
        for (qsizetype candidate = 1; candidate < count; ++candidate) {
            if (!document.root_stack.at(candidate).group &&
                document.root_stack.at(candidate).id == item_id) {
                position = candidate;
            }
        }
    } else {
        const auto* group = findGroup(document, parent_id);
        if (group == nullptr) return {};
        count = group->layer_ids.size();
        position = group->layer_ids.indexOf(item_id);
    }
    if (position < 0 || position + direction < (parent_id.isEmpty() ? 1 : 0) ||
        position + direction >= count) {
        return {};
    }
    return moveItem(document, item_id, false, parent_id,
        position + direction + (direction > 0 ? 1 : 0), selected_layer_id,
        selected_group_id);
}

} // namespace image_editor
