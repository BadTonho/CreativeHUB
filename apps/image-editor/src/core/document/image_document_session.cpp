#include "image_document_session.h"
#include "image_bucket_fill.h"
#include "../diagnostics/image_editor_performance_metrics.h"
#include "image_document_geometry.h"
#include "image_document_object_editor.h"
#include "image_document_renderer.h"
#include "image_document_utils.h"
#include "image_layer_mask_editor.h"
#include "image_layer_stack_editor.h"

#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>
#include <QDir>
#include <QUuid>

#include <cmath>
#include <algorithm>
#include <utility>

namespace image_editor {
namespace {

void assignError(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
}

bool layerTransformHasCapacity(const ImageLayerData& layer) {
    return layer.operations.size() < ImageDocumentStore::kMaximumOperations &&
        (!layer.mask.has_value() ||
         layer.mask->operations.size() < ImageDocumentStore::kMaximumOperations);
}


} // namespace

void ImageDocumentSession::loadRasterSources() {
    raster_images_.clear();
    raster_errors_.clear();
    layer_thumbnail_cache_.clear();
    invalidateGroupThumbnailCache();
    layer_raster_cache_.clear();
    for (const auto& layer : data_.layers) for (const auto& op : layer.operations) {
        if (op.kind != OperationKind::RasterImage) continue;
        const QString path = op.raster.source_path;
        if (raster_images_.contains(path) || raster_errors_.contains(path)) continue;
        const auto result = prepareRasterImport({path});
        if (result.status == RasterImportStatus::Ready)
            raster_images_.insert(path, result.images.front().image);
        else raster_errors_.insert(path, result.cause);
    }
}

QHash<QString, QString> ImageDocumentSession::rasterSourceProblems() const {
    QHash<QString, QString> problems;
    for (const auto& layer : data_.layers) for (const auto& op : layer.operations) {
        if (op.kind != OperationKind::RasterImage) continue;
        const auto image = raster_images_.value(op.raster.id, raster_images_.value(op.raster.source_path));
        if (image.isNull())
            problems.insert(op.raster.id, QStringLiteral("%1: %2").arg(op.raster.source_path,
                raster_errors_.value(op.raster.source_path, QStringLiteral("Source image unavailable."))));
        else if (image.size() != op.raster.source_size)
            problems.insert(op.raster.id, QStringLiteral("%1: Source dimensions no longer match.").arg(op.raster.source_path));
    }
    return problems;
}

bool ImageDocumentSession::importRasterImages(const QVector<PreparedRasterImage>& images,
    std::optional<QPointF> center, QString* error) {
    if (!hasSource() || images.isEmpty() ||
        images.size() > ImageDocumentStore::kMaximumLayers -
            ImageLayerStackEditor::itemCount(data_) ||
        (center && (!std::isfinite(center->x()) || !std::isfinite(center->y())))) {
        assignError(error, QStringLiteral("Open a document and choose a batch within the layer limit."));
        return false;
    }
    for (const auto& image : images) {
        if (image.path.isEmpty() || image.path.contains(QChar::Null) ||
            QFileInfo(image.path).fileName().isEmpty() || !QDir::isAbsolutePath(image.path) ||
            image.image.isNull() || !ImageDocumentStore::isValidCanvasSize(image.image.size())) {
            assignError(error, QStringLiteral("The imported image batch is invalid."));
            return false;
        }
    }
    const QSize canvas = renderedSize();
    const QPointF anchor = center.value_or(QPointF(canvas.width() / 2.0, canvas.height() / 2.0));
    const QString parent_id = selected_group_id_.isEmpty()
        ? parentGroupForLayer(selected_layer_id_) : QString{};
    qsizetype insertion = 0;
    if (auto* parent = findGroup(data_, parent_id)) {
        insertion = parent->layer_ids.indexOf(selected_layer_id_) + 1;
    } else {
        const QString selected = selected_group_id_.isEmpty() ? selected_layer_id_ : selected_group_id_;
        insertion = data_.root_stack.size();
        for (qsizetype i = 0; i < data_.root_stack.size(); ++i)
            if (data_.root_stack[i].id == selected) { insertion = i + 1; break; }
    }
    pushEdit();
    for (const auto& prepared : images) {
        const QString path = QDir::cleanPath(prepared.path);
        if (!raster_images_.contains(path)) raster_images_.insert(path, prepared.image);
        const QSize size = raster_images_.value(path).size();
        const qreal scale = std::min({1.0, qreal(canvas.width()) / size.width(),
                                          qreal(canvas.height()) / size.height()});
        ImageOperation op;
        op.kind = OperationKind::RasterImage;
        op.raster = {QUuid::createUuid().toString(QUuid::WithoutBraces), path, size,
            QTransform(scale, 0, 0, scale,
                anchor.x() - size.width() * scale / 2, anchor.y() - size.height() * scale / 2)};
        if (const auto* parent = findGroup(data_, parent_id))
            for (qsizetype index = parent->operations.size(); index > 0; --index)
                ImageDocumentObjectEditor::transformGeometry(
                    &op, parent->operations[index - 1], canvas, true);
        ImageLayerData layer;
        layer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        layer.name = QFileInfo(path).fileName().left(ImageDocumentStore::kMaximumLayerNameLength);
        layer.parent_group_id = parent_id;
        layer.operations.append(op);
        if (auto* parent = findGroup(data_, parent_id))
            parent->layer_ids.insert(insertion++, layer.id);
        else data_.root_stack.insert(insertion++, {layer.id, false});
        data_.layers.append(layer);
        selected_layer_id_ = layer.id;
    }
    selected_group_id_.clear();
    ImageLayerStackEditor::rebuildLayerOrder(data_);
    layer_thumbnail_cache_.clear();
    invalidateGroupThumbnailCache();
    return true;
}

bool ImageDocumentSession::findRaster(const QString& id, ImageRasterData* raster, QString* layer_id) const {
    for (const auto& layer : data_.layers) for (const auto& op : layer.operations)
        if (op.kind == OperationKind::RasterImage && op.raster.id == id) {
            if (raster) *raster = op.raster;
            if (layer_id) *layer_id = layer.id;
            return true;
        }
    return false;
}

bool ImageDocumentSession::relinkRaster(const QString& id, const PreparedRasterImage& image, QString* error) {
    ImageRasterData raster;
    QString layer_id;
    if (!findRaster(id, &raster, &layer_id) || image.image.isNull() ||
        image.image.size() != raster.source_size || !QDir::isAbsolutePath(image.path)) {
        assignError(error, QStringLiteral("Choose an image with the original dimensions for this reference."));
        return false;
    }
    const QString path = QDir::cleanPath(image.path);
    // Retain a per-object handle so other references keep their loaded pixels.
    pushEdit();
    for (auto& layer : data_.layers) for (auto& op : layer.operations)
        if (op.kind == OperationKind::RasterImage && op.raster.id == id) op.raster.source_path = path;
    raster_images_.insert(id, image.image);
    raster_errors_.remove(path);
    layer_thumbnail_cache_.clear();
    invalidateGroupThumbnailCache();
    layer_raster_cache_.invalidateLayer(layer_id);
    return true;
}

QImage ImageDocumentSession::renderedImageWithObjects(const QVector<ImageObjectPlacement>& objects) const {
    ImageDocumentSession preview;
    preview.data_ = data_;
    preview.source_image_ = source_image_;
    preview.raster_images_ = raster_images_;
    if (!preview.updateObjectsRendered(objects)) return renderedImage();
    return ImageDocumentRenderer::composite(
        preview.data_, preview.source_image_, preview.raster_images_);
}

bool ImageDocumentSession::createCanvas(const QSize& size,
                                        const QColor& background,
                                        QString* error) {
    if (!ImageDocumentStore::isValidCanvasSize(size) || !background.isValid()) {
        assignError(error, QStringLiteral("Choose valid canvas dimensions and a valid background."));
        return false;
    }

    QImage canvas(size, QImage::Format_ARGB32);
    if (canvas.isNull()) {
        assignError(error, QStringLiteral("The canvas could not be allocated. Try smaller dimensions."));
        return false;
    }
    canvas.fill(background);

    raster_images_.clear();
    raster_errors_.clear();
    layer_thumbnail_cache_.clear();
    invalidateGroupThumbnailCache();
    layer_raster_cache_.clear();
    data_ = {};
    data_.base_kind = ImageBaseKind::Canvas;
    data_.source_size = size;
    data_.canvas_size = size;
    data_.canvas_background = background;
    initializeDefaultLayers();
    source_image_ = std::move(canvas);
    document_path_.clear();
    recovery_session_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    baseline_source_path_.clear();
    baseline_source_size_ = size;
    baseline_canvas_size_ = data_.canvas_size;
    baseline_canvas_base_offset_ = data_.canvas_base_offset;
    baseline_base_kind_ = ImageBaseKind::Canvas;
    baseline_canvas_background_ = background;
    baseline_operations_.clear();
    baseline_layers_ = data_.layers;
    baseline_groups_ = data_.groups;
    baseline_root_stack_ = data_.root_stack;
    force_dirty_ = true;
    history_.clear();
    opacity_edit_active_ = false;
    return true;
}

bool ImageDocumentSession::loadSource(const QString& source_path,
                                      QImage* image,
                                      QString* error) const {
    if (source_path.isEmpty()) {
        assignError(error, QStringLiteral("An image source path is required."));
        return false;
    }
    QImageReader reader(source_path);
    reader.setAutoTransform(true);
    reader.setDecideFormatFromContent(true);
    auto decoded = reader.read();
    if (decoded.isNull()) {
        assignError(error, reader.errorString().isEmpty()
            ? QStringLiteral("The image could not be decoded.")
            : reader.errorString());
        return false;
    }
    *image = std::move(decoded);
    return true;
}

bool ImageDocumentSession::openImage(const QString& source_path, QString* error) {
    QImage decoded;
    if (!loadSource(source_path, &decoded, error)) return false;

    raster_images_.clear();
    raster_errors_.clear();
    layer_thumbnail_cache_.clear();
    invalidateGroupThumbnailCache();
    layer_raster_cache_.clear();
    data_ = {};
    data_.base_kind = ImageBaseKind::SourceImage;
    data_.source_path = absoluteCleanPath(source_path);
    data_.source_size = decoded.size();
    data_.canvas_size = decoded.size();
    initializeDefaultLayers();
    source_image_ = std::move(decoded);
    document_path_.clear();
    recovery_session_id_.clear();
    baseline_source_path_ = data_.source_path;
    baseline_source_size_ = data_.source_size;
    baseline_canvas_size_ = data_.canvas_size;
    baseline_canvas_base_offset_ = data_.canvas_base_offset;
    baseline_base_kind_ = data_.base_kind;
    baseline_canvas_background_ = data_.canvas_background;
    baseline_operations_.clear();
    baseline_layers_ = data_.layers;
    baseline_groups_ = data_.groups;
    baseline_root_stack_ = data_.root_stack;
    force_dirty_ = false;
    history_.clear();
    opacity_edit_active_ = false;
    return true;
}

bool ImageDocumentSession::openDocument(const QString& document_path, QString* error) {
    ImageDocumentData candidate;
    if (!ImageDocumentStore::loadDocument(document_path, &candidate, error)) return false;

    QImage decoded;
    if (candidate.base_kind == ImageBaseKind::Canvas) {
        decoded = QImage(candidate.source_size, QImage::Format_ARGB32);
        if (decoded.isNull()) {
            assignError(error, QStringLiteral("The canvas could not be allocated. Try smaller dimensions."));
            return false;
        }
        decoded.fill(candidate.canvas_background);
    } else {
        const bool missing = !QFileInfo::exists(candidate.source_path);
        if (!missing) {
            if (!loadSource(candidate.source_path, &decoded, error)) return false;
            if (decoded.size() != candidate.source_size) {
                assignError(error, QStringLiteral("The source image dimensions no longer match the document."));
                return false;
            }
        }
    }

    data_ = std::move(candidate);
    loadRasterSources();
    source_image_ = std::move(decoded);
    selected_layer_id_.clear();
    selected_group_id_.clear();
    for (auto it = data_.layers.crbegin(); it != data_.layers.crend(); ++it) {
        if (!it->background) {
            selected_layer_id_ = it->id;
            break;
        }
    }
    if (selected_layer_id_.isEmpty() && !data_.layers.isEmpty()) {
        selected_layer_id_ = data_.layers.front().id;
    }
    document_path_ = absoluteCleanPath(document_path);
    recovery_session_id_.clear();
    baseline_source_path_ = data_.source_path;
    baseline_source_size_ = data_.source_size;
    baseline_canvas_size_ = data_.canvas_size;
    baseline_canvas_base_offset_ = data_.canvas_base_offset;
    baseline_base_kind_ = data_.base_kind;
    baseline_canvas_background_ = data_.canvas_background;
    baseline_operations_ = data_.operations;
    baseline_layers_ = data_.layers;
    baseline_groups_ = data_.groups;
    baseline_root_stack_ = data_.root_stack;
    force_dirty_ = false;
    history_.clear();
    opacity_edit_active_ = false;
    return true;
}

bool ImageDocumentSession::restoreRecovery(const QString& recovery_path, QString* error) {
    RecoveryDocumentData recovery;
    if (!ImageDocumentStore::loadRecovery(recovery_path, &recovery, error)) return false;

    QImage decoded;
    if (recovery.document.base_kind == ImageBaseKind::Canvas) {
        decoded = QImage(recovery.document.source_size, QImage::Format_ARGB32);
        if (decoded.isNull()) {
            assignError(error, QStringLiteral("The recovered canvas could not be allocated."));
            return false;
        }
        decoded.fill(recovery.document.canvas_background);
    } else {
        const bool missing = !QFileInfo::exists(recovery.document.source_path);
        if (!missing) {
            if (!loadSource(recovery.document.source_path, &decoded, error)) return false;
            if (decoded.size() != recovery.document.source_size) {
                assignError(error, QStringLiteral("The recovery source dimensions no longer match."));
                return false;
            }
        }
    }

    data_ = std::move(recovery.document);
    loadRasterSources();
    source_image_ = std::move(decoded);
    selected_layer_id_.clear();
    selected_group_id_.clear();
    for (auto it = data_.layers.crbegin(); it != data_.layers.crend(); ++it) {
        if (!it->background) {
            selected_layer_id_ = it->id;
            break;
        }
    }
    if (selected_layer_id_.isEmpty() && !data_.layers.isEmpty()) {
        selected_layer_id_ = data_.layers.front().id;
    }
    document_path_ = recovery.target_document_path;
    recovery_session_id_ = recovery.session_id;
    if (recovery_session_id_.isEmpty() && data_.base_kind == ImageBaseKind::Canvas) {
        recovery_session_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    baseline_source_path_ = data_.source_path;
    baseline_source_size_ = data_.source_size;
    baseline_canvas_size_ = data_.canvas_size;
    baseline_canvas_base_offset_ = data_.canvas_base_offset;
    baseline_base_kind_ = data_.base_kind;
    baseline_canvas_background_ = data_.canvas_background;
    baseline_operations_.clear();
    baseline_layers_ = data_.layers;
    baseline_groups_ = data_.groups;
    baseline_root_stack_ = data_.root_stack;
    force_dirty_ = true;
    history_.clear();
    opacity_edit_active_ = false;
    return true;
}

bool ImageDocumentSession::relinkSource(const QString& source_path, QString* error) {
    if (data_.base_kind != ImageBaseKind::SourceImage || !sourceIsMissing()) {
        assignError(error, QStringLiteral("This document does not need a source image to be relinked."));
        return false;
    }
    QImage decoded;
    if (!loadSource(source_path, &decoded, error)) return false;
    if (decoded.size() != data_.source_size) {
        assignError(error, QStringLiteral("Choose an image with the original dimensions."));
        return false;
    }

    if (absoluteCleanPath(source_path) == data_.source_path && hasSource()) return true;
    data_.source_path = absoluteCleanPath(source_path);
    source_image_ = std::move(decoded);
    layer_raster_cache_.clear();
    return true;
}

bool ImageDocumentSession::resizeCanvas(const QSize& size,
                                        CanvasAnchor anchor,
                                        QString* error) {
    if (error != nullptr) error->clear();
    if (!hasSource() || !ImageDocumentStore::isValidCanvasSize(size)) {
        assignError(error, QStringLiteral("Choose valid canvas dimensions before resizing."));
        return false;
    }
    const QSize old_size = renderedSize();
    if (!ImageDocumentStore::isValidCanvasSize(old_size)) {
        assignError(error, QStringLiteral("The current canvas dimensions cannot be resized."));
        return false;
    }
    if (size == old_size) return false;

    int horizontal = 1;
    int vertical = 1;
    switch (anchor) {
    case CanvasAnchor::TopLeft: case CanvasAnchor::Left: case CanvasAnchor::BottomLeft:
        horizontal = 0; break;
    case CanvasAnchor::TopRight: case CanvasAnchor::Right: case CanvasAnchor::BottomRight:
        horizontal = 2; break;
    default: break;
    }
    switch (anchor) {
    case CanvasAnchor::TopLeft: case CanvasAnchor::Top: case CanvasAnchor::TopRight:
        vertical = 0; break;
    case CanvasAnchor::BottomLeft: case CanvasAnchor::Bottom: case CanvasAnchor::BottomRight:
        vertical = 2; break;
    default: break;
    }
    const auto anchored_offset = [](int difference, int alignment) {
        if (alignment == 0) return 0;
        if (alignment == 2) return difference;
        return difference / 2;
    };
    const QPoint delta(anchored_offset(size.width() - old_size.width(), horizontal),
                       anchored_offset(size.height() - old_size.height(), vertical));

    ImageDocumentData candidate = data_;
    const auto coordinate_safe = [](qint64 value) {
        return std::abs(value) <= 1'000'000;
    };
    const auto shift_point = [&delta](QPointF* point) {
        if (point == nullptr) return false;
        point->rx() += delta.x();
        point->ry() += delta.y();
        return std::isfinite(point->x()) && std::isfinite(point->y()) &&
            std::abs(point->x()) <= 1'000'000.0 && std::abs(point->y()) <= 1'000'000.0;
    };
    const auto shift_operations = [&](QVector<ImageOperation>* operations) {
        if (operations == nullptr) return true;
        for (auto& operation : *operations) {
            if (operation.kind == OperationKind::Crop) {
                const qint64 x = static_cast<qint64>(operation.crop.x()) + delta.x();
                const qint64 y = static_cast<qint64>(operation.crop.y()) + delta.y();
                if (!coordinate_safe(x) || !coordinate_safe(y) ||
                    !coordinate_safe(x + operation.crop.width()) ||
                    !coordinate_safe(y + operation.crop.height())) return false;
                operation.crop.translate(delta);
            } else if (operation.kind == OperationKind::Rotate ||
                       operation.kind == OperationKind::FlipHorizontal ||
                       operation.kind == OperationKind::FlipVertical) {
                if (!operation.transform_bounds.isValid() || operation.transform_bounds.isEmpty())
                    operation.transform_bounds = QRect(QPoint(), old_size);
                const qint64 x = static_cast<qint64>(operation.transform_bounds.x()) + delta.x();
                const qint64 y = static_cast<qint64>(operation.transform_bounds.y()) + delta.y();
                if (!coordinate_safe(x) || !coordinate_safe(y) ||
                    !coordinate_safe(x + operation.transform_bounds.width()) ||
                    !coordinate_safe(y + operation.transform_bounds.height())) return false;
                operation.transform_bounds.translate(delta);
            } else if (operation.kind == OperationKind::PaintStroke) {
                for (auto& point : operation.paint_stroke.points)
                    if (!shift_point(&point)) return false;
                if (operation.paint_stroke.clipping_path.has_value()) {
                    operation.paint_stroke.clipping_path =
                        operation.paint_stroke.clipping_path->translated(delta);
                }
            } else if (operation.kind == OperationKind::EraseStroke) {
                for (auto& point : operation.erase_stroke.points)
                    if (!shift_point(&point)) return false;
                if (operation.erase_stroke.clipping_path.has_value()) {
                    operation.erase_stroke.clipping_path =
                        operation.erase_stroke.clipping_path->translated(delta);
                }
            } else if (operation.kind == OperationKind::BucketFill) {
                const qint64 x = static_cast<qint64>(operation.bucket_fill.seed.x()) + delta.x();
                const qint64 y = static_cast<qint64>(operation.bucket_fill.seed.y()) + delta.y();
                if (!coordinate_safe(x) || !coordinate_safe(y)) return false;
                operation.bucket_fill.seed = QPoint(static_cast<int>(x), static_cast<int>(y));
                if (operation.bucket_fill.clipping_path.has_value()) {
                    operation.bucket_fill.clipping_path =
                        operation.bucket_fill.clipping_path->translated(delta);
                }
            } else if (operation.kind == OperationKind::Shape) {
                if (!shift_point(&operation.shape.start) || !shift_point(&operation.shape.end))
                    return false;
            } else if (operation.kind == OperationKind::Text) {
                if (!shift_point(&operation.text.position)) return false;
            } else if (operation.kind == OperationKind::RasterImage) {
                const auto& transform = operation.raster.transform;
                operation.raster.transform = QTransform(
                    transform.m11(), transform.m12(), transform.m21(), transform.m22(),
                    transform.dx() + delta.x(), transform.dy() + delta.y());
                if (!std::isfinite(operation.raster.transform.dx()) ||
                    !std::isfinite(operation.raster.transform.dy()) ||
                    std::abs(operation.raster.transform.dx()) > 1'000'000.0 ||
                    std::abs(operation.raster.transform.dy()) > 1'000'000.0) return false;
            }
        }
        return true;
    };
    for (auto& layer : candidate.layers) {
        if (layer.background) continue;
        if (!shift_operations(&layer.operations) ||
            (layer.mask.has_value() && !shift_operations(&layer.mask->operations))) {
            assignError(error, QStringLiteral("The canvas change would move existing content beyond the supported edit range."));
            return false;
        }
    }
    for (auto& group : candidate.groups) {
        if (!shift_operations(&group.operations)) {
            assignError(error, QStringLiteral("The canvas change would move existing content beyond the supported edit range."));
            return false;
        }
    }
    const qint64 base_x = static_cast<qint64>(candidate.canvas_base_offset.x()) + delta.x();
    const qint64 base_y = static_cast<qint64>(candidate.canvas_base_offset.y()) + delta.y();
    if (!coordinate_safe(base_x) || !coordinate_safe(base_y)) {
        assignError(error, QStringLiteral("The canvas change would move the base image beyond the supported edit range."));
        return false;
    }
    candidate.canvas_size = size;
    candidate.canvas_base_offset = QPoint(static_cast<int>(base_x), static_cast<int>(base_y));

    pushEdit();
    data_ = std::move(candidate);
    layer_thumbnail_cache_.clear();
    invalidateGroupThumbnailCache();
    layer_raster_cache_.clear();
    return true;
}

bool ImageDocumentSession::saveDocument(QString document_path, QString* error) {
    if (!hasSource()) {
        assignError(error, QStringLiteral("Relink the source image before saving the document."));
        return false;
    }
    if (document_path.isEmpty()) document_path = document_path_;
    if (document_path.isEmpty()) {
        assignError(error, QStringLiteral("Choose a path for the editable document."));
        return false;
    }
    if (!ImageDocumentStore::saveDocument(document_path, data_, error)) return false;
    document_path_ = absoluteCleanPath(document_path);
    baseline_source_path_ = data_.source_path;
    baseline_source_size_ = data_.source_size;
    baseline_canvas_size_ = data_.canvas_size;
    baseline_canvas_base_offset_ = data_.canvas_base_offset;
    baseline_base_kind_ = data_.base_kind;
    baseline_canvas_background_ = data_.canvas_background;
    baseline_operations_ = data_.operations;
    baseline_layers_ = data_.layers;
    baseline_groups_ = data_.groups;
    baseline_root_stack_ = data_.root_stack;
    force_dirty_ = false;
    return true;
}

ImageExportSnapshot ImageDocumentSession::exportSnapshot() const {
    return {source_image_, data_, selected_layer_id_, selected_group_id_, raster_images_};
}

bool ImageDocumentSession::exportImage(const QString& output_path, QString* error) const {
    return exportImage(output_path, ImageExportOptions{}, error);
}

bool ImageDocumentSession::exportImage(const QString& output_path,
                                       const ImageExportOptions& options,
                                       QString* error) const {
    const ImageExportResult result = exportImageSnapshot(exportSnapshot(), output_path, options);
    if (result.status == ImageExportStatus::Succeeded) return true;
    if (result.status == ImageExportStatus::Failed) assignError(error, result.error);
    return false;
}

QSize ImageDocumentSession::renderedSize() const {
    return ImageDocumentRenderer::documentSize(data_, source_image_.size());
}

QImage ImageDocumentSession::renderedImage() const {
    layer_raster_cache_.synchronize(data_.layers, renderedSize(), raster_images_);
    return ImageDocumentRenderer::composite(data_, source_image_, raster_images_,
        {}, nullptr, &layer_raster_cache_);
}

QImage ImageDocumentSession::renderedImageWithoutShape(const QString& shape_id) const {
    return ImageDocumentRenderer::composite(
        data_, source_image_, raster_images_, QStringList{shape_id});
}

QImage ImageDocumentSession::renderedImageWithoutObjects(
    const QStringList& object_ids) const {
    return ImageDocumentRenderer::composite(data_, source_image_, raster_images_, object_ids);
}

QVector<ImageShapePlacement> ImageDocumentSession::visibleShapes() const {
    QVector<ImageShapePlacement> result;
    const auto objects = visibleObjects();
    result.reserve(objects.size());
    for (const auto& object : objects) {
        if (object.operation.kind != OperationKind::Shape) continue;
        result.append({object.operation.shape, object.layer_id, object.layer_opacity});
    }
    return result;
}

QVector<ImageObjectPlacement> ImageDocumentSession::visibleObjects() const {
    QVector<ImageObjectPlacement> result;
    const QSize size = renderedSize();
    for (qsizetype layer_index = data_.layers.size(); layer_index > 1; --layer_index) {
        const auto& layer = data_.layers.at(layer_index - 1);
        if (!effectiveLayerVisible(layer)) continue;
        const auto* parent_group = layer.parent_group_id.isEmpty()
            ? nullptr : findGroup(data_, layer.parent_group_id);
        const int effective_opacity = parent_group == nullptr
            ? layer.opacity : qRound(layer.opacity * parent_group->opacity / 100.0);
        for (qsizetype index = layer.operations.size(); index > 0; --index) {
            const auto& operation = layer.operations.at(index - 1);
            if (operation.kind != OperationKind::PaintStroke &&
                operation.kind != OperationKind::EraseStroke &&
                operation.kind != OperationKind::Shape &&
                operation.kind != OperationKind::Text &&
                operation.kind != OperationKind::RasterImage) continue;
            ImageObjectPlacement placement;
            placement.operation = operation;
            placement.layer_id = layer.id;
            placement.layer_opacity = effective_opacity;
            for (qsizetype suffix = index; suffix < layer.operations.size(); ++suffix) {
                const auto& later = layer.operations.at(suffix);
                ImageDocumentObjectEditor::transformGeometry(
                    &placement.operation, later, size);
            }
            if (parent_group != nullptr) {
                for (const auto& group_operation : parent_group->operations) {
                    ImageDocumentObjectEditor::transformGeometry(
                        &placement.operation, group_operation, size);
                }
            }
            result.append(std::move(placement));
        }
    }
    return result;
}

bool ImageDocumentSession::updateObjectsRendered(
    const QVector<ImageObjectPlacement>& objects, QString* error) {
    if (error != nullptr) error->clear();
    if (objects.isEmpty() || !hasSource()) return false;
    auto prepared = ImageDocumentObjectEditor::updateObjectsRendered(
        data_, objects, renderedSize(), selected_layer_id_, selected_group_id_, error);
    if (!prepared.has_value()) return false;
    commitDocumentEdit(std::move(prepared->document),
                       std::move(prepared->selected_layer_id),
                       std::move(prepared->selected_group_id));
    return true;
}

bool ImageDocumentSession::updateShapeStyles(const QStringList& shape_ids,
                                             const ImageShapeData& style,
                                             QString* error) {
    auto prepared = ImageDocumentObjectEditor::updateShapeStyles(
        data_, shape_ids, style, renderedSize(), selected_layer_id_,
        selected_group_id_, error);
    if (!prepared.has_value()) return false;
    commitDocumentEdit(std::move(prepared->document),
                       std::move(prepared->selected_layer_id),
                       std::move(prepared->selected_group_id));
    return true;
}

bool ImageDocumentSession::deleteObjects(const QStringList& object_ids) {
    auto prepared = ImageDocumentObjectEditor::deleteObjects(
        data_, object_ids, selected_layer_id_, selected_group_id_);
    if (!prepared.has_value()) return false;
    commitDocumentEdit(std::move(prepared->document),
                       std::move(prepared->selected_layer_id),
                       std::move(prepared->selected_group_id));
    return true;
}

QImage ImageDocumentSession::renderedImageWithEraseStroke(
    const QVector<QPointF>& points, int diameter,
    std::optional<QPainterPath> clipping_path) const {
    if (!hasSource() || !selectedLayerIsEditable() || points.isEmpty() ||
        diameter < 1 || diameter > ImageDocumentStore::kMaximumPaintBrushDiameter) {
        return renderedImage();
    }
    const QSize size = renderedSize();
    for (const QPointF& point : points) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y()) ||
            point.x() < 0.0 || point.y() < 0.0 ||
            point.x() >= size.width() || point.y() >= size.height()) {
            return renderedImage();
        }
    }

    ImageDocumentData preview_document = data_;
    if (auto* selected = findLayer(preview_document, selected_layer_id_)) {
        QVector<QPointF> local_points = points;
        const auto* parent = findGroup(preview_document, selected->parent_group_id);
        ImageDocumentGeometry::mapStrokeGeometryThroughGroup(
            &local_points, &clipping_path, parent, size);
        if (clipping_path.has_value()) {
            clipping_path = clipping_path->intersected(
                ImageDocumentGeometry::imageBoundsPath(size));
            if (!ImageDocumentGeometry::isValidStrokeClipPath(*clipping_path))
                return renderedImage();
        }
        ImageOperation preview;
        preview.kind = OperationKind::EraseStroke;
        preview.erase_stroke.points = std::move(local_points);
        preview.erase_stroke.diameter = diameter;
        preview.erase_stroke.clipping_path = std::move(clipping_path);
        selected->operations.append(std::move(preview));
    }
    return ImageDocumentRenderer::composite(preview_document, source_image_, raster_images_);
}

QHash<QString, QImage> ImageDocumentSession::renderedLayerThumbnails(
    const QSize& maximum_size) const {
    QHash<QString, QImage> thumbnails;
    if (maximum_size.width() <= 0 || maximum_size.height() <= 0) return thumbnails;
    if (!hasSource()) {
        layer_thumbnail_cache_.clear();
        invalidateGroupThumbnailCache();
        return thumbnails;
    }

    const QSize canvas_size = renderedSize();
    const qint64 source_cache_key = source_image_.cacheKey();
    for (const auto& layer : data_.layers) {
        ImageEditorPerformanceScope cache_scope(
            ImageEditorPerformanceMetrics::instance(),
            ImageEditorPerformanceStage::ThumbnailCacheHit);
        const QVector<ImageOperation>& operations = layer.background
            ? data_.operations : layer.operations;
        auto cached = layer_thumbnail_cache_.find(layer.id);
        const bool cache_matches = cached != layer_thumbnail_cache_.end() &&
            cached->operations.size() == operations.size() &&
            cached->operations.constData() == operations.constData() &&
            cached->source_size == canvas_size &&
            cached->maximum_size == maximum_size &&
            cached->source_cache_key == source_cache_key &&
            cached->background == layer.background && cached->mask == layer.mask;
        cache_scope.setStage(cache_matches
            ? ImageEditorPerformanceStage::ThumbnailCacheHit
            : ImageEditorPerformanceStage::ThumbnailCacheMiss);
        if (!cache_matches) {
            LayerThumbnailCacheEntry entry;
            entry.operations = operations;
            entry.source_size = canvas_size;
            entry.maximum_size = maximum_size;
            entry.source_cache_key = source_cache_key;
            entry.background = layer.background;
            entry.mask = layer.mask;
            entry.thumbnail = ImageDocumentRenderer::layerThumbnail(
                data_, source_image_, raster_images_, layer, maximum_size);
            cached = layer_thumbnail_cache_.insert(layer.id, std::move(entry));
        }
        thumbnails.insert(layer.id, cached->thumbnail);
    }
    for (auto cached = layer_thumbnail_cache_.begin();
         cached != layer_thumbnail_cache_.end();) {
        if (!thumbnails.contains(cached.key())) cached = layer_thumbnail_cache_.erase(cached);
        else ++cached;
    }
    for (const auto& group : data_.groups) {
        ImageEditorPerformanceScope cache_scope(
            ImageEditorPerformanceMetrics::instance(),
            ImageEditorPerformanceStage::ThumbnailCacheHit);
        auto cached = group_thumbnail_cache_.find(group.id);
        const bool cache_matches = cached != group_thumbnail_cache_.end() &&
            cached->maximum_size == maximum_size;
        cache_scope.setStage(cache_matches
            ? ImageEditorPerformanceStage::ThumbnailCacheHit
            : ImageEditorPerformanceStage::ThumbnailCacheMiss);

        QImage thumbnail;
        if (cache_matches) {
            thumbnail = cached->thumbnail;
        } else {
            thumbnail = ImageDocumentRenderer::groupThumbnail(
                data_, raster_images_, group, maximum_size);
            if (!thumbnail.isNull()) {
                GroupThumbnailCacheEntry entry;
                entry.maximum_size = maximum_size;
                entry.thumbnail = thumbnail;
                group_thumbnail_cache_.insert(group.id, std::move(entry));
            } else {
                group_thumbnail_cache_.remove(group.id);
            }
        }
        thumbnails.insert(group.id, std::move(thumbnail));
    }
    for (auto cached = group_thumbnail_cache_.begin();
         cached != group_thumbnail_cache_.end();) {
        if (!thumbnails.contains(cached.key())) cached = group_thumbnail_cache_.erase(cached);
        else ++cached;
    }
    return thumbnails;
}

bool ImageDocumentSession::applyCrop(const QRect& crop, QString* error) {
    if (!hasSource()) {
        assignError(error, QStringLiteral("Open or relink an image before cropping."));
        return false;
    }
    if (selectedGroupIsActive()) {
        ImageOperation operation;
        operation.kind = OperationKind::Crop;
        operation.crop = crop;
        return applySelectedGroupTransform(operation, error);
    }
    if (!selectedLayerIsEditable()) {
        assignError(error, QStringLiteral("Select an editable layer before cropping."));
        return false;
    }
    const QSize size = renderedSize();
    const QRect valid = crop.normalized().intersected(QRect(QPoint(0, 0), size));
    if (valid.isEmpty()) {
        assignError(error, QStringLiteral("The crop area is empty."));
        return false;
    }
    if (valid == QRect(QPoint(0, 0), size)) return false;
    if (!layerTransformHasCapacity(data_.layers.at(layerIndex(selected_layer_id_)))) {
        assignError(error, QStringLiteral("The layer or its mask has reached the operation limit."));
        return false;
    }
    pushEdit();
    ImageOperation operation;
    operation.kind = OperationKind::Crop;
    operation.crop = valid;
    auto& layer = data_.layers[layerIndex(selected_layer_id_)];
    layer.operations.append(operation);
    if (layer.mask.has_value()) layer.mask->operations.append(operation);
    layer_raster_cache_.invalidateLayer(selected_layer_id_);
    return true;
}

bool ImageDocumentSession::applyPaintStroke(const QVector<QPointF>& points,
                                            const QColor& color,
                                            int diameter,
                                            QString* error,
                                            std::optional<QPainterPath> clipping_path) {
    if (error != nullptr) error->clear();
    if (!hasSource()) {
        assignError(error, QStringLiteral("Open or relink an image before painting."));
        return false;
    }
    if (!selectedLayerIsEditable()) {
        assignError(error, QStringLiteral("Select an editable layer before painting."));
        return false;
    }
    if (points.isEmpty() ||
        points.size() > ImageDocumentStore::kMaximumPaintStrokePoints) {
        assignError(error, QStringLiteral("The paint stroke has an invalid number of points."));
        return false;
    }
    if (!color.isValid()) {
        assignError(error, QStringLiteral("Choose a valid paint color."));
        return false;
    }
    if (diameter < 1 || diameter > ImageDocumentStore::kMaximumPaintBrushDiameter) {
        assignError(error, QStringLiteral("The paint brush diameter must be between 1 and 1024 pixels."));
        return false;
    }
    const QSize size = renderedSize();
    for (const auto& point : points) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y()) ||
            point.x() < 0.0 || point.y() < 0.0 ||
            point.x() >= size.width() || point.y() >= size.height()) {
            assignError(error, QStringLiteral("The paint stroke contains a point outside the image."));
            return false;
        }
    }
    if (clipping_path.has_value() &&
        !ImageDocumentGeometry::isValidStrokeClipPath(*clipping_path)) {
        assignError(error, QStringLiteral("The paint selection has invalid or excessive geometry."));
        return false;
    }
    if (color.alpha() == 0) return false;

    QVector<QPointF> local_points = points;
    const auto* parent = findGroup(data_, data_.layers.at(layerIndex(selected_layer_id_)).parent_group_id);
    ImageDocumentGeometry::mapStrokeGeometryThroughGroup(
        &local_points, &clipping_path, parent, size);
    if (clipping_path.has_value()) {
        clipping_path = clipping_path->intersected(
            ImageDocumentGeometry::imageBoundsPath(size));
        if (!ImageDocumentGeometry::isValidStrokeClipPath(*clipping_path)) return false;
    }
    for (QPointF& point : local_points) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y()) ||
            std::abs(point.x()) > 1'000'000.0 || std::abs(point.y()) > 1'000'000.0) {
            assignError(error, QStringLiteral("The paint stroke exceeds the supported local coordinate range."));
            return false;
        }
    }

    pushEdit();
    ImageOperation operation;
    operation.kind = OperationKind::PaintStroke;
    operation.paint_stroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    operation.paint_stroke.points = std::move(local_points);
    operation.paint_stroke.color = color;
    operation.paint_stroke.diameter = diameter;
    operation.paint_stroke.clipping_path = std::move(clipping_path);
    data_.layers[layerIndex(selected_layer_id_)].operations.append(std::move(operation));
    layer_raster_cache_.invalidateLayer(selected_layer_id_);
    return true;
}

bool ImageDocumentSession::applyEraseStroke(const QVector<QPointF>& points,
                                            int diameter,
                                            QString* error,
                                            std::optional<QPainterPath> clipping_path) {
    if (error != nullptr) error->clear();
    if (!hasSource()) {
        assignError(error, QStringLiteral("Open or relink an image before erasing."));
        return false;
    }
    if (!selectedLayerIsEditable()) {
        assignError(error, QStringLiteral("Select an editable layer before erasing."));
        return false;
    }
    if (points.isEmpty() ||
        points.size() > ImageDocumentStore::kMaximumPaintStrokePoints) {
        assignError(error, QStringLiteral("The erase stroke has an invalid number of points."));
        return false;
    }
    if (diameter < 1 || diameter > ImageDocumentStore::kMaximumPaintBrushDiameter) {
        assignError(error, QStringLiteral("The eraser diameter must be between 1 and 1024 pixels."));
        return false;
    }
    const QSize size = renderedSize();
    for (const auto& point : points) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y()) ||
            point.x() < 0.0 || point.y() < 0.0 ||
            point.x() >= size.width() || point.y() >= size.height()) {
            assignError(error, QStringLiteral("The erase stroke contains a point outside the image."));
            return false;
        }
    }
    if (clipping_path.has_value() &&
        !ImageDocumentGeometry::isValidStrokeClipPath(*clipping_path)) {
        assignError(error, QStringLiteral("The erase selection has invalid or excessive geometry."));
        return false;
    }

    QVector<QPointF> local_points = points;
    const auto* parent = findGroup(data_, data_.layers.at(layerIndex(selected_layer_id_)).parent_group_id);
    ImageDocumentGeometry::mapStrokeGeometryThroughGroup(
        &local_points, &clipping_path, parent, size);
    if (clipping_path.has_value()) {
        clipping_path = clipping_path->intersected(
            ImageDocumentGeometry::imageBoundsPath(size));
        if (!ImageDocumentGeometry::isValidStrokeClipPath(*clipping_path)) return false;
    }
    for (QPointF& point : local_points) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y()) ||
            std::abs(point.x()) > 1'000'000.0 || std::abs(point.y()) > 1'000'000.0) {
            assignError(error, QStringLiteral("The erase stroke exceeds the supported local coordinate range."));
            return false;
        }
    }

    pushEdit();
    ImageOperation operation;
    operation.kind = OperationKind::EraseStroke;
    operation.erase_stroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    operation.erase_stroke.points = std::move(local_points);
    operation.erase_stroke.diameter = diameter;
    operation.erase_stroke.clipping_path = std::move(clipping_path);
    data_.layers[layerIndex(selected_layer_id_)].operations.append(std::move(operation));
    layer_raster_cache_.invalidateLayer(selected_layer_id_);
    return true;
}

bool ImageDocumentSession::applyBucketFill(
    const QPoint& seed, const QColor& color, int tolerance, QString* error,
    std::optional<QPainterPath> clipping_path, bool mask_target) {
    if (error != nullptr) error->clear();
    if (!hasSource() || !selectedLayerIsEditable()) {
        assignError(error, QStringLiteral("Select an editable layer before filling."));
        return false;
    }
    if (!color.isValid() || tolerance < 0 || tolerance > 255) {
        assignError(error, QStringLiteral("The fill color or tolerance is invalid."));
        return false;
    }
    if (mask_target) {
        const qsizetype index = layerIndex(selected_layer_id_);
        if (index < 0 || !data_.layers.at(index).mask.has_value()) {
            assignError(error, QStringLiteral("Select a layer mask before filling it."));
            return false;
        }
    }
    if (clipping_path.has_value() &&
        !ImageDocumentGeometry::isValidStrokeClipPath(*clipping_path)) {
        assignError(error, QStringLiteral("The fill selection has invalid or excessive geometry."));
        return false;
    }

    const QSize size = renderedSize();
    if (seed.x() < 0 || seed.y() < 0 || seed.x() >= size.width() || seed.y() >= size.height())
        return false;
    QPointF local_seed(seed.x() + 0.5, seed.y() + 0.5);
    QVector<QPointF> mapped_seed{local_seed};
    const auto* layer = &data_.layers.at(layerIndex(selected_layer_id_));
    const auto* parent = findGroup(data_, layer->parent_group_id);
    ImageDocumentGeometry::mapStrokeGeometryThroughGroup(
        &mapped_seed, &clipping_path, parent, size);
    if (mapped_seed.size() != 1 || !std::isfinite(mapped_seed.front().x()) ||
        !std::isfinite(mapped_seed.front().y()) ||
        std::abs(mapped_seed.front().x()) > 1'000'000.0 ||
        std::abs(mapped_seed.front().y()) > 1'000'000.0) {
        assignError(error, QStringLiteral("The fill seed exceeds the supported local coordinate range."));
        return false;
    }
    if (clipping_path.has_value()) {
        clipping_path = clipping_path->intersected(ImageDocumentGeometry::imageBoundsPath(size));
        if (!ImageDocumentGeometry::isValidStrokeClipPath(*clipping_path)) return false;
    }

    ImageBucketFillData fill;
    fill.seed = QPoint(static_cast<int>(std::floor(mapped_seed.front().x())),
                       static_cast<int>(std::floor(mapped_seed.front().y())));
    fill.color = mask_target
        ? QColor(qGray(color.rgb()), qGray(color.rgb()), qGray(color.rgb()), color.alpha())
        : color;
    fill.tolerance = tolerance;
    fill.clipping_path = std::move(clipping_path);

    QImage target = ImageDocumentRenderer::editableLayerTarget(
        data_, source_image_, raster_images_, selected_layer_id_, mask_target);
    bool changed = false;
    if (target.isNull() || !ImageBucketFill::apply(&target, fill, &changed, error)) {
        if (error != nullptr && error->isEmpty())
            assignError(error, QStringLiteral("The selected layer could not be rendered for filling."));
        return false;
    }
    if (!changed) return false;

    ImageOperation operation;
    operation.kind = OperationKind::BucketFill;
    operation.bucket_fill = std::move(fill);
    pushEdit();
    auto& operations = mask_target
        ? data_.layers[layerIndex(selected_layer_id_)].mask->operations
        : data_.layers[layerIndex(selected_layer_id_)].operations;
    operations.append(std::move(operation));
    layer_raster_cache_.invalidateLayer(selected_layer_id_);
    return true;
}

QString ImageDocumentSession::addShape(ImageShapeData shape, QString* error) {
    if (error != nullptr) error->clear();
    if (!hasSource()) {
        assignError(error, QStringLiteral("Open or relink an image before creating a shape."));
        return {};
    }
    auto prepared = ImageDocumentObjectEditor::addShape(
        data_, std::move(shape), renderedSize(), selected_layer_id_,
        selected_group_id_, error);
    if (!prepared.has_value()) return {};
    const QString object_id = prepared->object_id;
    commitDocumentEdit(std::move(prepared->document),
                       std::move(prepared->selected_layer_id),
                       std::move(prepared->selected_group_id));
    return object_id;
}

QString ImageDocumentSession::addText(ImageTextData text, QString* error) {
    if (error != nullptr) error->clear();
    if (!hasSource()) {
        assignError(error, QStringLiteral("Open or relink an image before creating text."));
        return {};
    }
    auto prepared = ImageDocumentObjectEditor::addText(
        data_, std::move(text), renderedSize(), selected_layer_id_,
        selected_group_id_, error);
    if (!prepared.has_value()) return {};
    const QString object_id = prepared->object_id;
    commitDocumentEdit(std::move(prepared->document),
                       std::move(prepared->selected_layer_id),
                       std::move(prepared->selected_group_id));
    return object_id;
}

bool ImageDocumentSession::updateText(const ImageTextData& text, QString* error) {
    auto prepared = ImageDocumentObjectEditor::updateText(
        data_, text, renderedSize(), selected_layer_id_, selected_group_id_, error);
    if (!prepared.has_value()) return false;
    commitDocumentEdit(std::move(prepared->document),
                       std::move(prepared->selected_layer_id),
                       std::move(prepared->selected_group_id));
    return true;
}

bool ImageDocumentSession::findText(const QString& text_id,
                                    ImageTextData* text,
                                    QString* layer_id) const {
    for (const auto& layer : data_.layers) {
        if (layer.background) continue;
        for (auto operation = layer.operations.crbegin();
             operation != layer.operations.crend(); ++operation) {
            if (operation->kind != OperationKind::Text || operation->text.id != text_id) continue;
            if (text != nullptr) *text = operation->text;
            if (layer_id != nullptr) *layer_id = layer.id;
            return true;
        }
    }
    return false;
}

bool ImageDocumentSession::updateShape(const ImageShapeData& shape, QString* error) {
    auto prepared = ImageDocumentObjectEditor::updateShape(
        data_, shape, renderedSize(), selected_layer_id_, selected_group_id_, error);
    if (!prepared.has_value()) return false;
    commitDocumentEdit(std::move(prepared->document),
                       std::move(prepared->selected_layer_id),
                       std::move(prepared->selected_group_id));
    return true;
}

bool ImageDocumentSession::updateShapeRendered(const ImageShapeData& rendered_shape,
                                               QString* error) {
    auto prepared = ImageDocumentObjectEditor::updateShapeRendered(
        data_, rendered_shape, renderedSize(), selected_layer_id_,
        selected_group_id_, error);
    if (!prepared.has_value()) return false;
    commitDocumentEdit(std::move(prepared->document),
                       std::move(prepared->selected_layer_id),
                       std::move(prepared->selected_group_id));
    return true;
}

bool ImageDocumentSession::deleteShape(const QString& shape_id) {
    auto prepared = ImageDocumentObjectEditor::deleteShape(
        data_, shape_id, selected_layer_id_, selected_group_id_);
    if (!prepared.has_value()) return false;
    commitDocumentEdit(std::move(prepared->document),
                       std::move(prepared->selected_layer_id),
                       std::move(prepared->selected_group_id));
    return true;
}

bool ImageDocumentSession::findShape(const QString& shape_id,
                                     ImageShapeData* shape,
                                     QString* layer_id) const {
    for (const auto& layer : data_.layers) {
        if (layer.background) continue;
        for (auto operation = layer.operations.crbegin();
             operation != layer.operations.crend(); ++operation) {
            if (operation->kind != OperationKind::Shape ||
                operation->shape.id != shape_id) continue;
            if (shape != nullptr) *shape = operation->shape;
            if (layer_id != nullptr) *layer_id = layer.id;
            return true;
        }
    }
    return false;
}

QString ImageDocumentSession::addLayer() {
    auto edit = ImageLayerStackEditor::addLayer(
        data_, selected_layer_id_, selected_group_id_);
    if (!edit.has_value()) return {};
    const QString layer_id = edit->selected_layer_id;
    commitDocumentEdit(std::move(edit->document),
                         std::move(edit->selected_layer_id),
                         std::move(edit->selected_group_id));
    return layer_id;
}

bool ImageDocumentSession::deleteLayer(const QString& layer_id) {
    return deleteStackItems({{layer_id, false}});
}

bool ImageDocumentSession::deleteStackItems(const QVector<ImageStackItemData>& items) {
    auto edit = ImageLayerStackEditor::deleteItems(
        data_, items, selected_layer_id_, selected_group_id_);
    if (!edit.has_value()) return false;
    commitDocumentEdit(std::move(edit->document),
                         std::move(edit->selected_layer_id),
                         std::move(edit->selected_group_id));
    return true;
}

bool ImageDocumentSession::renameLayer(const QString& layer_id,
                                       const QString& name,
                                       QString* error) {
    const qsizetype index = layerIndex(layer_id);
    if (index <= 0) {
        assignError(error, QStringLiteral("The Background layer cannot be renamed."));
        return false;
    }
    const QString clean_name = name.trimmed();
    if (clean_name.isEmpty() || clean_name.size() > ImageDocumentStore::kMaximumLayerNameLength) {
        assignError(error, QStringLiteral("Layer names must contain between 1 and 128 characters."));
        return false;
    }
    if (data_.layers.at(index).name == clean_name) return false;
    pushEdit();
    data_.layers[index].name = clean_name;
    return true;
}

bool ImageDocumentSession::moveLayer(const QString& layer_id, int direction) {
    return moveStackItemBy(layer_id, false, direction);
}

bool ImageDocumentSession::setLayerVisible(const QString& layer_id, bool visible) {
    const qsizetype index = layerIndex(layer_id);
    if (index < 0 || data_.layers.at(index).visible == visible) return false;
    pushEdit();
    data_.layers[index].visible = visible;
    return true;
}

bool ImageDocumentSession::setLayerOpacity(const QString& layer_id, int opacity) {
    const qsizetype index = layerIndex(layer_id);
    if (index <= 0 || opacity < 0 || opacity > 100 ||
        data_.layers.at(index).opacity == opacity) return false;
    if (!opacity_edit_active_) pushEdit();
    data_.layers[index].opacity = opacity;
    invalidateGroupThumbnailCache();
    return true;
}

bool ImageDocumentSession::addLayerMask(const QString& layer_id) {
    const qsizetype index = layerIndex(layer_id);
    if (!hasSource() || index <= 0 || data_.layers.at(index).mask.has_value()) return false;
    pushEdit();
    data_.layers[index].mask = ImageLayerMaskData{};
    layer_raster_cache_.invalidateLayer(layer_id);
    return true;
}

bool ImageDocumentSession::removeLayerMask(const QString& layer_id) {
    const qsizetype index = layerIndex(layer_id);
    if (index <= 0 || !data_.layers.at(index).mask.has_value()) return false;
    pushEdit();
    data_.layers[index].mask.reset();
    layer_raster_cache_.invalidateLayer(layer_id);
    return true;
}

bool ImageDocumentSession::setLayerMaskEnabled(const QString& layer_id, bool enabled) {
    const qsizetype index = layerIndex(layer_id);
    if (index <= 0 || !data_.layers.at(index).mask.has_value() ||
        data_.layers.at(index).mask->enabled == enabled) return false;
    pushEdit();
    data_.layers[index].mask->enabled = enabled;
    layer_raster_cache_.invalidateLayer(layer_id);
    return true;
}

bool ImageDocumentSession::applyLayerMaskStroke(const QVector<QPointF>& points,
                                                const QColor& color, int diameter,
                                                QString* error,
                                                std::optional<QPainterPath> clipping_path) {
    return applyLayerMaskStrokeInternal(points, color, diameter, error,
                                        std::move(clipping_path), false);
}

bool ImageDocumentSession::applyLayerMaskEraseStroke(const QVector<QPointF>& points,
                                                     int diameter, QString* error,
                                                     std::optional<QPainterPath> clipping_path) {
    // Erasing a mask writes opaque black, represented by an erase operation.
    return applyLayerMaskStrokeInternal(points, Qt::black, diameter, error,
                                        std::move(clipping_path), true);
}

bool ImageDocumentSession::applyLayerMaskStrokeInternal(
    const QVector<QPointF>& points, const QColor& color, int diameter,
    QString* error, std::optional<QPainterPath> clipping_path, bool erase) {
    if (error != nullptr) error->clear();
    if (!hasSource() || !selectedLayerIsEditable()) {
        assignError(error, QStringLiteral(
            "Select a raster layer with a mask before painting its mask."));
        return false;
    }

    auto operation = ImageLayerMaskEditor::prepareStroke(
        data_, selected_layer_id_, renderedSize(), points, color, diameter,
        std::move(clipping_path), erase ? ImageLayerMaskStrokeKind::Erase
                                        : ImageLayerMaskStrokeKind::Paint,
        error);
    if (!operation.has_value()) return false;

    const qsizetype index = layerIndex(selected_layer_id_);
    if (index < 0 || !data_.layers.at(index).mask.has_value()) return false;
    pushEdit();
    data_.layers[index].mask->operations.append(std::move(*operation));
    layer_raster_cache_.invalidateLayer(selected_layer_id_);
    return true;
}

QImage ImageDocumentSession::renderedImageWithMaskStroke(
    const QVector<QPointF>& points, const QColor& color, int diameter,
    std::optional<QPainterPath> clipping_path) const {
    if (!hasSource() || !selectedLayerIsEditable()) return renderedImage();
    auto operation = ImageLayerMaskEditor::prepareStroke(
        data_, selected_layer_id_, renderedSize(), points, color, diameter,
        std::move(clipping_path), ImageLayerMaskStrokeKind::Paint);
    if (!operation.has_value()) return renderedImage();

    // The temporary document shares image buffers and leaves session history untouched.
    ImageDocumentData preview_document = data_;
    auto* layer = findLayer(preview_document, selected_layer_id_);
    if (layer == nullptr || !layer->mask.has_value()) return renderedImage();
    layer->mask->operations.append(std::move(*operation));
    return ImageDocumentRenderer::composite(
        preview_document, source_image_, raster_images_);
}

QHash<QString, QImage> ImageDocumentSession::renderedLayerMaskThumbnails(
    const QSize& maximum_size) const {
    return ImageDocumentRenderer::maskThumbnails(
        data_, source_image_, raster_images_, maximum_size);
}

QString ImageDocumentSession::addGroup(QString* error) {
    auto edit = ImageLayerStackEditor::addGroup(
        data_, selected_layer_id_, selected_group_id_, error);
    if (!edit.has_value()) return {};
    const QString group_id = edit->selected_group_id;
    commitDocumentEdit(std::move(edit->document),
                         std::move(edit->selected_layer_id),
                         std::move(edit->selected_group_id));
    return group_id;
}

QString ImageDocumentSession::groupLayers(const QStringList& layer_ids, QString* error) {
    auto edit = ImageLayerStackEditor::groupLayers(
        data_, layer_ids, selected_group_id_, error);
    if (!edit.has_value()) return {};
    const QString group_id = edit->selected_group_id;
    commitDocumentEdit(std::move(edit->document),
                         std::move(edit->selected_layer_id),
                         std::move(edit->selected_group_id));
    return group_id;
}

bool ImageDocumentSession::ungroup(const QString& group_id) {
    auto edit = ImageLayerStackEditor::ungroup(
        data_, group_id, selected_layer_id_, selected_group_id_);
    if (!edit.has_value()) return false;
    commitDocumentEdit(std::move(edit->document),
                         std::move(edit->selected_layer_id),
                         std::move(edit->selected_group_id));
    return true;
}

bool ImageDocumentSession::deleteGroup(const QString& group_id) {
    return deleteStackItems({{group_id, true}});
}

bool ImageDocumentSession::renameGroup(const QString& group_id,
                                       const QString& name,
                                       QString* error) {
    if (error != nullptr) error->clear();
    auto* group = findGroup(data_, group_id);
    if (group == nullptr) return false;
    const QString clean_name = name.trimmed();
    if (clean_name.isEmpty() || clean_name.size() > ImageDocumentStore::kMaximumLayerNameLength) {
        assignError(error, QStringLiteral("Group names must contain between 1 and 128 characters."));
        return false;
    }
    if (group->name == clean_name) return false;
    pushEdit();
    group = findGroup(data_, group_id);
    group->name = clean_name;
    return true;
}

bool ImageDocumentSession::setGroupVisible(const QString& group_id, bool visible) {
    auto* group = findGroup(data_, group_id);
    if (group == nullptr || group->visible == visible) return false;
    pushEdit();
    group = findGroup(data_, group_id);
    group->visible = visible;
    return true;
}

bool ImageDocumentSession::setGroupOpacity(const QString& group_id, int opacity) {
    auto* group = findGroup(data_, group_id);
    if (group == nullptr || opacity < 0 || opacity > 100 || group->opacity == opacity) return false;
    if (!opacity_edit_active_) pushEdit();
    group = findGroup(data_, group_id);
    group->opacity = opacity;
    invalidateGroupThumbnailCache();
    return true;
}

bool ImageDocumentSession::moveStackItem(const QString& item_id,
                                         bool is_group,
                                         const QString& target_group_id,
                                         qsizetype insertion_index) {
    auto edit = ImageLayerStackEditor::moveItem(
        data_, item_id, is_group, target_group_id, insertion_index,
        selected_layer_id_, selected_group_id_);
    if (!edit.has_value()) return false;
    commitDocumentEdit(std::move(edit->document),
                         std::move(edit->selected_layer_id),
                         std::move(edit->selected_group_id));
    return true;
}

bool ImageDocumentSession::moveStackItemBy(const QString& item_id,
                                           bool is_group,
                                           int direction) {
    auto edit = ImageLayerStackEditor::moveItemBy(
        data_, item_id, is_group, direction,
        selected_layer_id_, selected_group_id_);
    if (!edit.has_value()) return false;
    commitDocumentEdit(std::move(edit->document),
                         std::move(edit->selected_layer_id),
                         std::move(edit->selected_group_id));
    return true;
}

void ImageDocumentSession::beginLayerOpacityEdit() {
    if (opacity_edit_active_) return;
    opacity_edit_snapshot_ = data_;
    opacity_edit_active_ = true;
}

void ImageDocumentSession::endLayerOpacityEdit() {
    if (!opacity_edit_active_) return;
    opacity_edit_active_ = false;
    if (opacity_edit_snapshot_ != data_) {
        recordEditSnapshot(std::move(opacity_edit_snapshot_), selected_layer_id_,
                           selected_group_id_);
    }
    opacity_edit_snapshot_ = {};
}

bool ImageDocumentSession::selectLayer(const QString& layer_id) {
    if (layerIndex(layer_id) < 0 ||
        (selected_layer_id_ == layer_id && selected_group_id_.isEmpty())) return false;
    selected_layer_id_ = layer_id;
    selected_group_id_.clear();
    return true;
}

bool ImageDocumentSession::selectGroup(const QString& group_id) {
    if (groupIndex(group_id) < 0 ||
        (selected_group_id_ == group_id && selected_layer_id_.isEmpty())) return false;
    selected_layer_id_.clear();
    selected_group_id_ = group_id;
    return true;
}

qsizetype ImageDocumentSession::layerIndex(const QString& layer_id) const noexcept {
    for (qsizetype index = 0; index < data_.layers.size(); ++index) {
        if (data_.layers.at(index).id == layer_id) return index;
    }
    return -1;
}

bool ImageDocumentSession::selectedLayerIsEditable() const noexcept {
    const qsizetype index = layerIndex(selected_layer_id_);
    return selected_group_id_.isEmpty() && index > 0 && index < data_.layers.size();
}

bool ImageDocumentSession::selectedGroupIsActive() const noexcept {
    return groupIndex(selected_group_id_) >= 0;
}

qsizetype ImageDocumentSession::groupIndex(const QString& group_id) const noexcept {
    for (qsizetype index = 0; index < data_.groups.size(); ++index) {
        if (data_.groups.at(index).id == group_id) return index;
    }
    return -1;
}

QString ImageDocumentSession::parentGroupForLayer(const QString& layer_id) const {
    const auto* layer = findLayer(data_, layer_id);
    return layer == nullptr ? QString{} : layer->parent_group_id;
}

bool ImageDocumentSession::effectiveLayerVisible(const ImageLayerData& layer) const {
    if (!layer.visible || layer.opacity <= 0) return false;
    if (layer.parent_group_id.isEmpty()) return true;
    const auto* group = findGroup(data_, layer.parent_group_id);
    return group != nullptr && group->visible && group->opacity > 0;
}

void ImageDocumentSession::initializeDefaultLayers() {
    ImageLayerData background;
    background.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    background.name = QStringLiteral("Background");
    background.background = true;
    ImageLayerData first_layer;
    first_layer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    first_layer.name = QStringLiteral("Layer 1");
    data_.layers = {background, first_layer};
    data_.root_stack = {{background.id, false}, {first_layer.id, false}};
    data_.groups.clear();
    selected_layer_id_ = first_layer.id;
    selected_group_id_.clear();
}

void ImageDocumentSession::invalidateGroupThumbnailCache() const noexcept {
    group_thumbnail_cache_.clear();
}

void ImageDocumentSession::recordEditSnapshot(ImageDocumentData before,
                                              QString selected_layer_id,
                                              QString selected_group_id) {
    history_.record({std::move(before), std::move(selected_layer_id),
                     std::move(selected_group_id), raster_images_});
}

void ImageDocumentSession::restoreHistorySnapshot(
    ImageDocumentHistory::Snapshot snapshot) {
    data_ = std::move(snapshot.document);
    const auto retained = raster_images_;
    raster_images_ = std::move(snapshot.raster_images);
    for (auto it = retained.cbegin(); it != retained.cend(); ++it) {
        if (QDir::isAbsolutePath(it.key()) && !raster_images_.contains(it.key()))
            raster_images_.insert(it.key(), it.value());
    }
    layer_thumbnail_cache_.clear();
    invalidateGroupThumbnailCache();
    layer_raster_cache_.clear();
    selected_group_id_ = groupIndex(snapshot.selected_group_id) >= 0
        ? std::move(snapshot.selected_group_id) : QString{};
    selected_layer_id_ = selected_group_id_.isEmpty() &&
            layerIndex(snapshot.selected_layer_id) >= 0
        ? std::move(snapshot.selected_layer_id)
        : (selected_group_id_.isEmpty() && !data_.layers.isEmpty()
            ? data_.layers.back().id : QString{});
}

void ImageDocumentSession::pushEdit() {
    endLayerOpacityEdit();
    recordEditSnapshot(data_, selected_layer_id_, selected_group_id_);
    invalidateGroupThumbnailCache();
}

void ImageDocumentSession::commitDocumentEdit(
    ImageDocumentData document, QString selected_layer_id,
    QString selected_group_id) {
    pushEdit();
    data_ = std::move(document);
    selected_layer_id_ = std::move(selected_layer_id);
    selected_group_id_ = std::move(selected_group_id);
    layer_thumbnail_cache_.clear();
    invalidateGroupThumbnailCache();
    layer_raster_cache_.synchronize(data_.layers, renderedSize(), raster_images_);
}

bool ImageDocumentSession::applySelectedGroupTransform(const ImageOperation& operation,
                                                       QString* error) {
    if (error != nullptr) error->clear();
    auto* group = findGroup(data_, selected_group_id_);
    if (group == nullptr || (operation.kind != OperationKind::Crop &&
        operation.kind != OperationKind::Rotate &&
        operation.kind != OperationKind::FlipHorizontal &&
        operation.kind != OperationKind::FlipVertical)) {
        assignError(error, QStringLiteral("Select a group to apply a group transform."));
        return false;
    }
    ImageOperation checked = operation;
    if (checked.kind == OperationKind::Crop) {
        checked.crop = checked.crop.normalized().intersected(
            QRect(QPoint(0, 0), renderedSize()));
        if (checked.crop.isEmpty()) {
            assignError(error, QStringLiteral("The crop area is empty."));
            return false;
        }
    } else {
        checked.transform_bounds = QRect(QPoint(), renderedSize());
    }
    pushEdit();
    findGroup(data_, selected_group_id_)->operations.append(std::move(checked));
    layer_thumbnail_cache_.clear();
    invalidateGroupThumbnailCache();
    return true;
}

void ImageDocumentSession::rotateLeft() {
    if (selectedGroupIsActive()) {
        static_cast<void>(applySelectedGroupTransform({OperationKind::Rotate, {}, -1}));
        return;
    }
    if (!selectedLayerIsEditable()) return;
    if (!layerTransformHasCapacity(data_.layers.at(layerIndex(selected_layer_id_)))) return;
    pushEdit();
    auto& layer = data_.layers[layerIndex(selected_layer_id_)];
    ImageOperation operation{OperationKind::Rotate, {}, -1};
    operation.transform_bounds = QRect(QPoint(), renderedSize());
    layer.operations.append(operation);
    if (layer.mask.has_value()) layer.mask->operations.append(operation);
}

void ImageDocumentSession::rotateRight() {
    if (selectedGroupIsActive()) {
        static_cast<void>(applySelectedGroupTransform({OperationKind::Rotate, {}, 1}));
        return;
    }
    if (!selectedLayerIsEditable()) return;
    if (!layerTransformHasCapacity(data_.layers.at(layerIndex(selected_layer_id_)))) return;
    pushEdit();
    auto& layer = data_.layers[layerIndex(selected_layer_id_)];
    ImageOperation operation{OperationKind::Rotate, {}, 1};
    operation.transform_bounds = QRect(QPoint(), renderedSize());
    layer.operations.append(operation);
    if (layer.mask.has_value()) layer.mask->operations.append(operation);
}

void ImageDocumentSession::flipHorizontal() {
    if (selectedGroupIsActive()) {
        static_cast<void>(applySelectedGroupTransform({OperationKind::FlipHorizontal, {}, 0}));
        return;
    }
    if (!selectedLayerIsEditable()) return;
    if (!layerTransformHasCapacity(data_.layers.at(layerIndex(selected_layer_id_)))) return;
    pushEdit();
    auto& layer = data_.layers[layerIndex(selected_layer_id_)];
    ImageOperation operation{OperationKind::FlipHorizontal, {}, 0};
    operation.transform_bounds = QRect(QPoint(), renderedSize());
    layer.operations.append(operation);
    if (layer.mask.has_value()) layer.mask->operations.append(operation);
}

void ImageDocumentSession::flipVertical() {
    if (selectedGroupIsActive()) {
        static_cast<void>(applySelectedGroupTransform({OperationKind::FlipVertical, {}, 0}));
        return;
    }
    if (!selectedLayerIsEditable()) return;
    if (!layerTransformHasCapacity(data_.layers.at(layerIndex(selected_layer_id_)))) return;
    pushEdit();
    auto& layer = data_.layers[layerIndex(selected_layer_id_)];
    ImageOperation operation{OperationKind::FlipVertical, {}, 0};
    operation.transform_bounds = QRect(QPoint(), renderedSize());
    layer.operations.append(operation);
    if (layer.mask.has_value()) layer.mask->operations.append(operation);
}

bool ImageDocumentSession::undo() {
    endLayerOpacityEdit();
    auto previous = history_.undo(
        {data_, selected_layer_id_, selected_group_id_, raster_images_});
    if (!previous.has_value()) return false;
    restoreHistorySnapshot(std::move(*previous));
    return true;
}

bool ImageDocumentSession::redo() {
    endLayerOpacityEdit();
    auto next = history_.redo(
        {data_, selected_layer_id_, selected_group_id_, raster_images_});
    if (!next.has_value()) return false;
    restoreHistorySnapshot(std::move(*next));
    return true;
}

bool ImageDocumentSession::isDirty() const noexcept {
    return force_dirty_ || data_.source_path != baseline_source_path_ ||
        data_.source_size != baseline_source_size_ || data_.base_kind != baseline_base_kind_ ||
        data_.canvas_size != baseline_canvas_size_ ||
        data_.canvas_base_offset != baseline_canvas_base_offset_ ||
        data_.canvas_background != baseline_canvas_background_ ||
        data_.operations != baseline_operations_ || data_.layers != baseline_layers_ ||
        data_.groups != baseline_groups_ || data_.root_stack != baseline_root_stack_;
}

} // namespace image_editor
