#include "image_document_object_editor.h"

#include "image_document_geometry.h"
#include "image_document_utils.h"
#include "image_layer_stack_editor.h"

#include <QSet>
#include <QTransform>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <utility>

namespace image_editor {
namespace {

void assignError(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
}

ImageDocumentObjectEditResult resultFor(
    ImageDocumentData document,
    const QString& selected_layer_id,
    const QString& selected_group_id,
    QString object_id = {}) {
    return {std::move(document), selected_layer_id, selected_group_id,
            std::move(object_id)};
}

bool effectiveLayerVisible(const ImageDocumentData& document,
                           const ImageLayerData& layer) {
    if (!layer.visible || layer.opacity <= 0) return false;
    if (layer.parent_group_id.isEmpty()) return true;
    const auto* group = findGroup(document, layer.parent_group_id);
    return group != nullptr && group->visible && group->opacity > 0;
}

bool objectIdExists(const ImageDocumentData& document, const QString& id,
                    bool shapes_only = false) {
    for (const auto& layer : document.layers) {
        for (const auto& operation : layer.operations) {
            if (shapes_only && operation.kind != OperationKind::Shape) continue;
            if (operationObjectId(operation).compare(id, Qt::CaseInsensitive) == 0)
                return true;
        }
    }
    return false;
}

QString nextLayerName(const ImageDocumentData& document, const QString& prefix) {
    int suffix = 1;
    while (true) {
        const QString candidate = QStringLiteral("%1 %2").arg(prefix).arg(suffix++);
        const bool layer_match = std::any_of(document.layers.cbegin(), document.layers.cend(),
            [&candidate](const ImageLayerData& layer) {
                return layer.name.compare(candidate, Qt::CaseInsensitive) == 0;
            });
        const bool group_match = std::any_of(document.groups.cbegin(), document.groups.cend(),
            [&candidate](const ImageGroupData& group) {
                return group.name.compare(candidate, Qt::CaseInsensitive) == 0;
            });
        if (!layer_match && !group_match) return candidate;
    }
}

void insertObjectLayer(ImageDocumentData* document,
                       ImageLayerData layer,
                       const QString& selected_layer_id,
                       const QString& selected_group_id,
                       ImageOperation operation) {
    if (document == nullptr) return;
    const auto* selected_layer = findLayer(*document, selected_layer_id);
    const QString parent_group_id = selected_group_id.isEmpty()
        ? (selected_layer == nullptr ? QString{} : selected_layer->parent_group_id)
        : QString{};
    layer.parent_group_id = parent_group_id;

    if (!parent_group_id.isEmpty()) {
        auto* parent = findGroup(*document, parent_group_id);
        if (parent != nullptr) {
            const qsizetype selected_index = parent->layer_ids.indexOf(selected_layer_id);
            parent->layer_ids.insert(selected_index < 0 ? parent->layer_ids.size()
                                                        : selected_index + 1,
                                     layer.id);
        }
    } else {
        qsizetype insertion_index = document->root_stack.size();
        const QString anchor_id = selected_group_id.isEmpty()
            ? selected_layer_id : selected_group_id;
        for (qsizetype index = 0; index < document->root_stack.size(); ++index) {
            if (document->root_stack.at(index).id == anchor_id) {
                insertion_index = index + 1;
                break;
            }
        }
        document->root_stack.insert(insertion_index, {layer.id, false});
    }

    layer.operations.append(std::move(operation));
    document->layers.append(std::move(layer));
    ImageLayerStackEditor::rebuildLayerOrder(*document);
}

} // namespace

void ImageDocumentObjectEditor::transformGeometry(
    ImageOperation* operation, const ImageOperation& transform,
    const QSize& canvas_size, bool inverse) {
    if (operation == nullptr) return;
    auto transform_points = [&transform, &canvas_size, inverse](QVector<QPointF>* points) {
        if (points == nullptr) return;
        for (QPointF& point : *points) {
            point = ImageDocumentGeometry::transformPoint(
                point, transform, canvas_size, inverse);
        }
    };
    switch (operation->kind) {
    case OperationKind::RasterImage: {
        const auto origin = ImageDocumentGeometry::transformPoint(
            {}, transform, canvas_size, inverse);
        const auto x = ImageDocumentGeometry::transformPoint(
            {1, 0}, transform, canvas_size, inverse) - origin;
        const auto y = ImageDocumentGeometry::transformPoint(
            {0, 1}, transform, canvas_size, inverse) - origin;
        // Raster geometry describes pixel edges; flips use the canvas edge.
        QPointF offset = origin;
        if (transform.kind == OperationKind::FlipHorizontal) offset.rx() += 1;
        if (transform.kind == OperationKind::FlipVertical) offset.ry() += 1;
        operation->raster.transform *= QTransform(
            x.x(), x.y(), y.x(), y.y(), offset.x(), offset.y());
        break;
    }
    case OperationKind::PaintStroke:
        transform_points(&operation->paint_stroke.points);
        if (operation->paint_stroke.clipping_path.has_value()) {
            operation->paint_stroke.clipping_path = ImageDocumentGeometry::operationTransform(
                transform, canvas_size, inverse).map(
                    *operation->paint_stroke.clipping_path);
        }
        break;
    case OperationKind::EraseStroke:
        transform_points(&operation->erase_stroke.points);
        if (operation->erase_stroke.clipping_path.has_value()) {
            operation->erase_stroke.clipping_path = ImageDocumentGeometry::operationTransform(
                transform, canvas_size, inverse).map(
                    *operation->erase_stroke.clipping_path);
        }
        break;
    case OperationKind::Shape:
        operation->shape.start = ImageDocumentGeometry::transformPoint(
            operation->shape.start, transform, canvas_size, inverse);
        operation->shape.end = ImageDocumentGeometry::transformPoint(
            operation->shape.end, transform, canvas_size, inverse);
        break;
    case OperationKind::Text: {
        const QPointF top_left = ImageDocumentGeometry::transformPoint(
            operation->text.position, transform, canvas_size, inverse);
        const QPointF bottom_right = ImageDocumentGeometry::transformPoint(
            operation->text.position + QPointF(operation->text.box_width, 0.0),
            transform, canvas_size, inverse);
        operation->text.position = QPointF(std::min(top_left.x(), bottom_right.x()),
                                           std::min(top_left.y(), bottom_right.y()));
        operation->text.box_width = std::max<qreal>(1.0,
            std::abs(bottom_right.x() - top_left.x()));
        break;
    }
    default:
        break;
    }
}

std::optional<ImageDocumentObjectEditResult> ImageDocumentObjectEditor::addShape(
    const ImageDocumentData& document, ImageShapeData shape,
    const QSize& canvas_size, const QString& selected_layer_id,
    const QString& selected_group_id, QString* error) {
    if (error != nullptr) error->clear();
    if (ImageLayerStackEditor::itemCount(document) >= ImageDocumentStore::kMaximumLayers) {
        assignError(error, QStringLiteral(
            "The document has reached the maximum of 512 stack items."));
        return std::nullopt;
    }
    if (shape.id.isEmpty()) shape.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!ImageDocumentStore::isValidShape(shape, canvas_size, error)) return std::nullopt;
    if (objectIdExists(document, shape.id, true)) {
        assignError(error, QStringLiteral("A shape with this ID already exists."));
        return std::nullopt;
    }

    const QString object_id = shape.id;
    ImageDocumentData updated = document;
    ImageLayerData layer;
    layer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    layer.name = nextLayerName(document, QStringLiteral("Shape"));
    const QString new_layer_id = layer.id;
    ImageOperation operation;
    operation.kind = OperationKind::Shape;
    operation.shape = std::move(shape);
    insertObjectLayer(&updated, std::move(layer), selected_layer_id,
                      selected_group_id, std::move(operation));
    return resultFor(std::move(updated), new_layer_id, {}, object_id);
}

std::optional<ImageDocumentObjectEditResult> ImageDocumentObjectEditor::addText(
    const ImageDocumentData& document, ImageTextData text,
    const QSize& canvas_size, const QString& selected_layer_id,
    const QString& selected_group_id, QString* error) {
    if (error != nullptr) error->clear();
    if (ImageLayerStackEditor::itemCount(document) >= ImageDocumentStore::kMaximumLayers) {
        assignError(error, QStringLiteral(
            "The document has reached the maximum of 512 stack items."));
        return std::nullopt;
    }
    if (text.id.isEmpty()) text.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!ImageDocumentStore::isValidText(text, canvas_size, error)) return std::nullopt;
    if (objectIdExists(document, text.id)) {
        assignError(error, QStringLiteral("An object with this ID already exists."));
        return std::nullopt;
    }

    const QString object_id = text.id;
    ImageDocumentData updated = document;
    ImageLayerData layer;
    layer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    layer.name = nextLayerName(document, QStringLiteral("Text"));
    const QString new_layer_id = layer.id;
    ImageOperation operation;
    operation.kind = OperationKind::Text;
    operation.text = std::move(text);
    insertObjectLayer(&updated, std::move(layer), selected_layer_id,
                      selected_group_id, std::move(operation));
    return resultFor(std::move(updated), new_layer_id, {}, object_id);
}

std::optional<ImageDocumentObjectEditResult> ImageDocumentObjectEditor::updateText(
    const ImageDocumentData& document, const ImageTextData& text,
    const QSize& canvas_size, const QString& selected_layer_id,
    const QString& selected_group_id, QString* error) {
    if (error != nullptr) error->clear();
    if (!ImageDocumentStore::isValidText(text, canvas_size, error)) return std::nullopt;
    for (qsizetype layer_index = 0; layer_index < document.layers.size(); ++layer_index) {
        const auto& layer = document.layers.at(layer_index);
        if (layer.background) continue;
        for (qsizetype operation_index = 0;
             operation_index < layer.operations.size(); ++operation_index) {
            const auto& operation = layer.operations.at(operation_index);
            if (operation.kind != OperationKind::Text || operation.text.id != text.id) continue;
            if (operation.text == text) return std::nullopt;
            ImageDocumentData updated = document;
            updated.layers[layer_index].operations[operation_index].text = text;
            return resultFor(std::move(updated), selected_layer_id, selected_group_id);
        }
    }
    assignError(error, QStringLiteral("The selected text no longer exists."));
    return std::nullopt;
}

std::optional<ImageDocumentObjectEditResult> ImageDocumentObjectEditor::updateShape(
    const ImageDocumentData& document, const ImageShapeData& shape,
    const QSize& canvas_size, const QString& selected_layer_id,
    const QString& selected_group_id, QString* error) {
    if (error != nullptr) error->clear();
    if (!ImageDocumentStore::isValidShape(shape, canvas_size, error)) return std::nullopt;
    for (qsizetype layer_index = 0; layer_index < document.layers.size(); ++layer_index) {
        const auto& layer = document.layers.at(layer_index);
        if (layer.background) continue;
        for (qsizetype operation_index = 0;
             operation_index < layer.operations.size(); ++operation_index) {
            const auto& operation = layer.operations.at(operation_index);
            if (operation.kind != OperationKind::Shape || operation.shape.id != shape.id) continue;
            if (operation.shape == shape) return std::nullopt;
            ImageDocumentData updated = document;
            updated.layers[layer_index].operations[operation_index].shape = shape;
            return resultFor(std::move(updated), selected_layer_id, selected_group_id);
        }
    }
    assignError(error, QStringLiteral("The selected shape no longer exists."));
    return std::nullopt;
}

std::optional<ImageDocumentObjectEditResult> ImageDocumentObjectEditor::updateShapeRendered(
    const ImageDocumentData& document, const ImageShapeData& rendered_shape,
    const QSize& canvas_size, const QString& selected_layer_id,
    const QString& selected_group_id, QString* error) {
    ImageOperation stored;
    stored.kind = OperationKind::Shape;
    stored.shape = rendered_shape;
    for (const auto& layer : document.layers) {
        if (layer.background) continue;
        for (qsizetype index = 0; index < layer.operations.size(); ++index) {
            const auto& operation = layer.operations.at(index);
            if (operation.kind != OperationKind::Shape ||
                operation.shape.id != rendered_shape.id) continue;
            if (!layer.parent_group_id.isEmpty()) {
                const auto* parent_group = findGroup(document, layer.parent_group_id);
                if (parent_group != nullptr) {
                    for (qsizetype suffix = parent_group->operations.size(); suffix > 0; --suffix) {
                        transformGeometry(&stored,
                            parent_group->operations.at(suffix - 1), canvas_size, true);
                    }
                }
            }
            for (qsizetype suffix = layer.operations.size(); suffix > index + 1; --suffix) {
                transformGeometry(&stored, layer.operations.at(suffix - 1), canvas_size, true);
            }
            return updateShape(document, stored.shape, canvas_size,
                               selected_layer_id, selected_group_id, error);
        }
    }
    if (error != nullptr) error->clear();
    assignError(error, QStringLiteral("The selected shape no longer exists."));
    return std::nullopt;
}

std::optional<ImageDocumentObjectEditResult> ImageDocumentObjectEditor::updateObjectsRendered(
    const ImageDocumentData& document,
    const QVector<ImageObjectPlacement>& objects,
    const QSize& canvas_size,
    const QString& selected_layer_id,
    const QString& selected_group_id,
    QString* error) {
    if (error != nullptr) error->clear();
    if (objects.isEmpty()) return std::nullopt;

    struct Mutation {
        qsizetype layer_index = -1;
        qsizetype operation_index = -1;
        ImageOperation operation;
    };
    QVector<Mutation> mutations;
    QSet<QString> seen;
    for (const auto& placement : objects) {
        const QString id = operationObjectId(placement.operation);
        if (id.isEmpty() || seen.contains(id)) {
            assignError(error, QStringLiteral("The selected objects are invalid or duplicated."));
            return std::nullopt;
        }
        seen.insert(id);

        qsizetype layer_index = -1;
        qsizetype operation_index = -1;
        for (qsizetype candidate_layer = 1;
             candidate_layer < document.layers.size(); ++candidate_layer) {
            const auto& layer = document.layers.at(candidate_layer);
            if (layer.id != placement.layer_id || !effectiveLayerVisible(document, layer)) continue;
            for (qsizetype candidate_operation = 0;
                 candidate_operation < layer.operations.size(); ++candidate_operation) {
                const auto& operation = layer.operations.at(candidate_operation);
                if (operationObjectId(operation) == id &&
                    operation.kind == placement.operation.kind) {
                    layer_index = candidate_layer;
                    operation_index = candidate_operation;
                    break;
                }
            }
            if (layer_index >= 0) break;
        }
        if (layer_index < 0) {
            assignError(error, QStringLiteral("A selected object is no longer available."));
            return std::nullopt;
        }

        const auto& layer = document.layers.at(layer_index);
        ImageOperation stored = placement.operation;
        const auto* parent_group = layer.parent_group_id.isEmpty()
            ? nullptr : findGroup(document, layer.parent_group_id);
        if (parent_group != nullptr) {
            for (qsizetype suffix = parent_group->operations.size(); suffix > 0; --suffix) {
                transformGeometry(&stored,
                    parent_group->operations.at(suffix - 1), canvas_size, true);
            }
        }
        for (qsizetype suffix = layer.operations.size(); suffix > operation_index + 1; --suffix) {
            transformGeometry(&stored, layer.operations.at(suffix - 1), canvas_size, true);
        }

        bool valid = false;
        if (stored.kind == OperationKind::PaintStroke) {
            const auto& stroke = stored.paint_stroke;
            valid = !stroke.id.isEmpty() && stroke.color.isValid() &&
                stroke.diameter >= 1 &&
                stroke.diameter <= ImageDocumentStore::kMaximumPaintBrushDiameter &&
                !stroke.points.isEmpty() &&
                stroke.points.size() <= ImageDocumentStore::kMaximumPaintStrokePoints;
            for (const auto& point : stroke.points) {
                valid = valid && std::isfinite(point.x()) && std::isfinite(point.y()) &&
                    point.x() >= 0.0 && point.y() >= 0.0 &&
                    point.x() < canvas_size.width() && point.y() < canvas_size.height();
            }
        } else if (stored.kind == OperationKind::EraseStroke) {
            const auto& stroke = stored.erase_stroke;
            valid = !stroke.id.isEmpty() && stroke.diameter >= 1 &&
                stroke.diameter <= ImageDocumentStore::kMaximumPaintBrushDiameter &&
                !stroke.points.isEmpty() &&
                stroke.points.size() <= ImageDocumentStore::kMaximumPaintStrokePoints;
            for (const auto& point : stroke.points) {
                valid = valid && std::isfinite(point.x()) && std::isfinite(point.y()) &&
                    point.x() >= 0.0 && point.y() >= 0.0 &&
                    point.x() < canvas_size.width() && point.y() < canvas_size.height();
            }
        } else if (stored.kind == OperationKind::Shape) {
            valid = ImageDocumentStore::isValidShape(stored.shape, canvas_size, error);
        } else if (stored.kind == OperationKind::RasterImage) {
            const auto& original = layer.operations.at(operation_index).raster;
            valid = stored.raster.id == original.id &&
                stored.raster.source_path == original.source_path &&
                stored.raster.source_size == original.source_size &&
                ImageDocumentStore::isValidRaster(stored.raster, error);
        } else if (stored.kind == OperationKind::Text) {
            valid = ImageDocumentStore::isValidText(stored.text, canvas_size, error);
        }
        if (!valid) {
            if (error == nullptr || error->isEmpty()) {
                assignError(error, QStringLiteral("The selected object geometry is invalid."));
            }
            return std::nullopt;
        }
        if (stored != layer.operations.at(operation_index))
            mutations.append({layer_index, operation_index, std::move(stored)});
    }

    if (mutations.isEmpty()) return std::nullopt;
    ImageDocumentData updated = document;
    for (const auto& mutation : mutations) {
        updated.layers[mutation.layer_index].operations[mutation.operation_index] =
            mutation.operation;
    }
    return resultFor(std::move(updated), selected_layer_id, selected_group_id);
}

std::optional<ImageDocumentObjectEditResult> ImageDocumentObjectEditor::updateShapeStyles(
    const ImageDocumentData& document, const QStringList& shape_ids,
    const ImageShapeData& style, const QSize& canvas_size,
    const QString& selected_layer_id, const QString& selected_group_id,
    QString* error) {
    if (error != nullptr) error->clear();
    if (shape_ids.isEmpty() || !style.stroke_color.isValid() ||
        !style.fill_color.isValid() || style.stroke_width < 1 ||
        style.stroke_width > ImageDocumentStore::kMaximumShapeStrokeWidth)
        return std::nullopt;

    struct Mutation {
        qsizetype layer_index = -1;
        qsizetype operation_index = -1;
        ImageShapeData shape;
    };
    QVector<Mutation> mutations;
    QSet<QString> seen;
    for (const QString& id : shape_ids) {
        if (id.isEmpty() || seen.contains(id)) continue;
        seen.insert(id);
        bool found = false;
        for (qsizetype layer_index = 1;
             layer_index < document.layers.size() && !found; ++layer_index) {
            const auto& layer = document.layers.at(layer_index);
            if (!layer.visible || layer.opacity == 0) continue;
            for (qsizetype operation_index = 0;
                 operation_index < layer.operations.size(); ++operation_index) {
                const auto& operation = layer.operations.at(operation_index);
                if (operation.kind != OperationKind::Shape || operation.shape.id != id) continue;
                ImageShapeData updated = operation.shape;
                updated.stroke_enabled = updated.kind == ImageShapeKind::Line
                    ? true : style.stroke_enabled;
                updated.stroke_color = style.stroke_color;
                updated.stroke_width = style.stroke_width;
                updated.fill_enabled = updated.kind == ImageShapeKind::Line
                    ? false : style.fill_enabled;
                updated.fill_color = style.fill_color;
                if (!ImageDocumentStore::isValidShape(updated, canvas_size, error))
                    return std::nullopt;
                if (updated != operation.shape)
                    mutations.append({layer_index, operation_index, std::move(updated)});
                found = true;
                break;
            }
        }
    }
    if (mutations.isEmpty()) return std::nullopt;
    ImageDocumentData updated = document;
    for (const auto& mutation : mutations) {
        updated.layers[mutation.layer_index].operations[mutation.operation_index].shape =
            mutation.shape;
    }
    return resultFor(std::move(updated), selected_layer_id, selected_group_id);
}

std::optional<ImageDocumentObjectEditResult> ImageDocumentObjectEditor::deleteShape(
    const ImageDocumentData& document, const QString& shape_id,
    const QString& selected_layer_id, const QString& selected_group_id) {
    for (qsizetype layer_index = 0; layer_index < document.layers.size(); ++layer_index) {
        const auto& layer = document.layers.at(layer_index);
        if (layer.background) continue;
        for (qsizetype index = 0; index < layer.operations.size(); ++index) {
            const auto& operation = layer.operations.at(index);
            if (operation.kind != OperationKind::Shape || operation.shape.id != shape_id) continue;
            ImageDocumentData updated = document;
            updated.layers[layer_index].operations.removeAt(index);
            return resultFor(std::move(updated), selected_layer_id, selected_group_id);
        }
    }
    return std::nullopt;
}

std::optional<ImageDocumentObjectEditResult> ImageDocumentObjectEditor::deleteObjects(
    const ImageDocumentData& document, const QStringList& object_ids,
    const QString& selected_layer_id, const QString& selected_group_id) {
    QSet<QString> remaining;
    for (const auto& id : object_ids) if (!id.isEmpty()) remaining.insert(id);
    if (remaining.isEmpty()) return std::nullopt;

    bool found = false;
    for (qsizetype layer_index = 1;
         layer_index < document.layers.size() && !found; ++layer_index) {
        for (const auto& operation : document.layers.at(layer_index).operations) {
            if (remaining.contains(operationObjectId(operation))) {
                found = true;
                break;
            }
        }
    }
    if (!found) return std::nullopt;

    ImageDocumentData updated = document;
    remaining.clear();
    for (const auto& id : object_ids) if (!id.isEmpty()) remaining.insert(id);
    for (qsizetype layer_index = 1; layer_index < updated.layers.size(); ++layer_index) {
        auto& operations = updated.layers[layer_index].operations;
        for (qsizetype index = operations.size(); index > 0; --index) {
            const QString id = operationObjectId(operations.at(index - 1));
            if (!id.isEmpty() && remaining.contains(id)) {
                remaining.remove(id);
                operations.removeAt(index - 1);
            }
        }
    }
    return resultFor(std::move(updated), selected_layer_id, selected_group_id);
}

} // namespace image_editor
