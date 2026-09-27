#include "image_document_session.h"

#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QSaveFile>
#include <QSet>
#include <QTransform>
#include <QDir>
#include <QUuid>

#include <cmath>
#include <algorithm>
#include <atomic>
#include <utility>

namespace image_editor {
namespace {

void assignError(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
}

QString absoluteCleanPath(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QImage paintStroke(QImage image, const ImagePaintStroke& stroke) {
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(stroke.color, stroke.diameter, Qt::SolidLine,
             Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    if (stroke.points.size() == 1) {
        const qreal radius = static_cast<qreal>(stroke.diameter) / 2.0;
        painter.setBrush(stroke.color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(stroke.points.front(), radius, radius);
    } else if (!stroke.points.isEmpty()) {
        QPainterPath path;
        path.moveTo(stroke.points.front());
        for (qsizetype i = 1; i < stroke.points.size(); ++i) {
            path.lineTo(stroke.points.at(i));
        }
        painter.drawPath(path);
    }
    painter.end();
    return image;
}

QImage eraseStroke(QImage image, const ImageEraseStroke& stroke) {
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    QPen pen(Qt::black, stroke.diameter, Qt::SolidLine,
             Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    if (stroke.points.size() == 1) {
        const qreal radius = static_cast<qreal>(stroke.diameter) / 2.0;
        painter.setBrush(Qt::black);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(stroke.points.front(), radius, radius);
    } else if (!stroke.points.isEmpty()) {
        QPainterPath path;
        path.moveTo(stroke.points.front());
        for (qsizetype i = 1; i < stroke.points.size(); ++i) {
            path.lineTo(stroke.points.at(i));
        }
        painter.drawPath(path);
    }
    painter.end();
    return image;
}

QImage drawShape(QImage image, const ImageShapeData& shape) {
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF bounds(shape.start, shape.end);
    if (shape.kind == ImageShapeKind::Line) {
        if (shape.stroke_enabled) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(shape.stroke_color, shape.stroke_width,
                                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawLine(shape.start, shape.end);
        }
    } else {
        painter.setPen(shape.stroke_enabled
            ? QPen(shape.stroke_color, shape.stroke_width, Qt::SolidLine,
                   Qt::SquareCap, Qt::MiterJoin)
            : QPen(Qt::NoPen));
        painter.setBrush(shape.fill_enabled ? QBrush(shape.fill_color)
                                           : QBrush(Qt::NoBrush));
        if (shape.kind == ImageShapeKind::Rectangle) painter.drawRect(bounds.normalized());
        else painter.drawEllipse(bounds.normalized());
    }
    painter.end();
    return image;
}

QPointF transformShapePoint(QPointF point,
                            const ImageOperation& operation,
                            const QSize& canvas_size,
                            bool inverse = false) {
    if (operation.kind == OperationKind::FlipHorizontal) {
        point.setX(canvas_size.width() - 1.0 - point.x());
    } else if (operation.kind == OperationKind::FlipVertical) {
        point.setY(canvas_size.height() - 1.0 - point.y());
    } else if (operation.kind == OperationKind::Rotate) {
        QTransform transform;
        const qreal center_x = canvas_size.width() / 2.0;
        const qreal center_y = canvas_size.height() / 2.0;
        transform.translate(center_x, center_y);
        transform.rotate((inverse ? -operation.quarter_turns : operation.quarter_turns) * 90.0);
        transform.translate(-center_x, -center_y);
        point = transform.map(point);
    }
    return point;
}

QString operationObjectId(const ImageOperation& operation) {
    switch (operation.kind) {
    case OperationKind::PaintStroke: return operation.paint_stroke.id;
    case OperationKind::EraseStroke: return operation.erase_stroke.id;
    case OperationKind::Shape: return operation.shape.id;
    default: return {};
    }
}

const ImageLayerData* findLayer(const ImageDocumentData& document, const QString& id) {
    const auto found = std::find_if(document.layers.cbegin(), document.layers.cend(),
        [&id](const ImageLayerData& layer) { return layer.id == id; });
    return found == document.layers.cend() ? nullptr : &*found;
}

ImageLayerData* findLayer(ImageDocumentData& document, const QString& id) {
    const auto found = std::find_if(document.layers.begin(), document.layers.end(),
        [&id](const ImageLayerData& layer) { return layer.id == id; });
    return found == document.layers.end() ? nullptr : &*found;
}

const ImageGroupData* findGroup(const ImageDocumentData& document, const QString& id) {
    const auto found = std::find_if(document.groups.cbegin(), document.groups.cend(),
        [&id](const ImageGroupData& group) { return group.id == id; });
    return found == document.groups.cend() ? nullptr : &*found;
}

ImageGroupData* findGroup(ImageDocumentData& document, const QString& id) {
    const auto found = std::find_if(document.groups.begin(), document.groups.end(),
        [&id](const ImageGroupData& group) { return group.id == id; });
    return found == document.groups.end() ? nullptr : &*found;
}

void transformObjectGeometry(ImageOperation* operation,
                             const ImageOperation& transform,
                             const QSize& canvas_size,
                             bool inverse = false) {
    if (operation == nullptr) return;
    auto transform_points = [&transform, &canvas_size, inverse](QVector<QPointF>* points) {
        if (points == nullptr) return;
        for (QPointF& point : *points) {
            point = transformShapePoint(point, transform, canvas_size, inverse);
        }
    };
    switch (operation->kind) {
    case OperationKind::PaintStroke:
        transform_points(&operation->paint_stroke.points);
        break;
    case OperationKind::EraseStroke:
        transform_points(&operation->erase_stroke.points);
        break;
    case OperationKind::Shape:
        operation->shape.start = transformShapePoint(
            operation->shape.start, transform, canvas_size, inverse);
        operation->shape.end = transformShapePoint(
            operation->shape.end, transform, canvas_size, inverse);
        break;
    default:
        break;
    }
}

QImage applyOperations(QImage image,
                       const QVector<ImageOperation>& operations,
                       bool fixed_canvas,
                       const std::atomic_bool* cancellation_requested = nullptr) {
    const QSize canvas_size = image.size();
    for (const auto& operation : operations) {
        if (cancellation_requested != nullptr &&
            cancellation_requested->load(std::memory_order_relaxed)) {
            return {};
        }
        switch (operation.kind) {
        case OperationKind::Crop:
            if (!fixed_canvas) {
                image = image.copy(operation.crop);
            } else {
                QImage cropped(canvas_size, QImage::Format_ARGB32_Premultiplied);
                cropped.fill(Qt::transparent);
                QPainter painter(&cropped);
                painter.drawImage(operation.crop.topLeft(), image.copy(operation.crop));
                image = std::move(cropped);
            }
            break;
        case OperationKind::Rotate:
            if (!fixed_canvas) {
                QTransform transform;
                transform.rotate(operation.quarter_turns * 90.0);
                image = image.transformed(transform, Qt::FastTransformation);
            } else {
                QImage rotated(canvas_size, QImage::Format_ARGB32_Premultiplied);
                rotated.fill(Qt::transparent);
                QTransform transform;
                const qreal center_x = canvas_size.width() / 2.0;
                const qreal center_y = canvas_size.height() / 2.0;
                transform.translate(center_x, center_y);
                transform.rotate(operation.quarter_turns * 90.0);
                transform.translate(-center_x, -center_y);
                QPainter painter(&rotated);
                painter.setTransform(transform);
                painter.drawImage(0, 0, image);
                image = std::move(rotated);
            }
            break;
        case OperationKind::FlipHorizontal:
            image = image.mirrored(true, false);
            break;
        case OperationKind::FlipVertical:
            image = image.mirrored(false, true);
            break;
        case OperationKind::PaintStroke:
            image = paintStroke(std::move(image), operation.paint_stroke);
            break;
        case OperationKind::EraseStroke:
            image = eraseStroke(std::move(image), operation.erase_stroke);
            break;
        case OperationKind::Shape:
            image = drawShape(std::move(image), operation.shape);
            break;
        }
    }
    return image;
}

bool exportWasCancelled(const std::atomic_bool* cancellation_requested) {
    return cancellation_requested != nullptr &&
        cancellation_requested->load(std::memory_order_relaxed);
}

QImage renderGroup(const ImageDocumentData& document,
                   const ImageGroupData& group,
                   const QSize& size,
                   const std::atomic_bool* cancellation_requested,
                   const QStringList& excluded_object_ids);

QImage renderRasterLayer(const ImageLayerData& layer,
                         const QSize& size,
                         const std::atomic_bool* cancellation_requested,
                         const QStringList& excluded_object_ids) {
    if (exportWasCancelled(cancellation_requested)) return {};
    QImage pixels(size, QImage::Format_ARGB32_Premultiplied);
    if (pixels.isNull()) return {};
    pixels.fill(Qt::transparent);
    QVector<ImageOperation> operations = layer.operations;
    if (!excluded_object_ids.isEmpty()) {
        operations.erase(std::remove_if(operations.begin(), operations.end(),
            [&excluded_object_ids](const ImageOperation& operation) {
                return excluded_object_ids.contains(operationObjectId(operation));
            }), operations.end());
    }
    return applyOperations(std::move(pixels), operations, true, cancellation_requested);
}

QImage renderGroup(const ImageDocumentData& document,
                   const ImageGroupData& group,
                   const QSize& size,
                   const std::atomic_bool* cancellation_requested,
                   const QStringList& excluded_object_ids) {
    QImage composite(size, QImage::Format_ARGB32_Premultiplied);
    if (composite.isNull()) return {};
    composite.fill(Qt::transparent);
    if (!group.visible || group.opacity == 0 || exportWasCancelled(cancellation_requested)) {
        return composite;
    }
    QPainter painter(&composite);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    for (const QString& layer_id : group.layer_ids) {
        if (exportWasCancelled(cancellation_requested)) {
            painter.end();
            return {};
        }
        const auto* layer = findLayer(document, layer_id);
        if (layer == nullptr || !layer->visible || layer->opacity == 0) continue;
        QImage pixels = renderRasterLayer(
            *layer, size, cancellation_requested, excluded_object_ids);
        if (pixels.isNull() || exportWasCancelled(cancellation_requested)) {
            painter.end();
            return {};
        }
        painter.setOpacity(layer->opacity / 100.0);
        painter.drawImage(0, 0, pixels);
    }
    painter.end();
    QImage transformed = applyOperations(
        std::move(composite), group.operations, true, cancellation_requested);
    if (transformed.isNull() || exportWasCancelled(cancellation_requested)) return {};
    if (group.opacity == 100) return transformed;
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) return {};
    result.fill(Qt::transparent);
    QPainter opacity_painter(&result);
    opacity_painter.setOpacity(group.opacity / 100.0);
    opacity_painter.drawImage(0, 0, transformed);
    opacity_painter.end();
    return result;
}

QImage renderComposite(const QImage& source_image,
                       const ImageDocumentData& document,
                       const std::atomic_bool* cancellation_requested = nullptr,
                       const QStringList& excluded_object_ids = {}) {
    if (source_image.isNull() || exportWasCancelled(cancellation_requested)) return {};

    QImage background = applyOperations(
        source_image, document.operations, false, cancellation_requested);
    if (background.isNull() || exportWasCancelled(cancellation_requested)) return {};

    const QSize size = background.size();
    const auto* background_layer = document.layers.isEmpty()
        ? nullptr : &document.layers.front();
    QImage composite = background_layer != nullptr && background_layer->visible
        ? background.convertToFormat(QImage::Format_ARGB32)
        : QImage(size, QImage::Format_ARGB32);
    if (composite.isNull()) return {};
    if (background_layer == nullptr || !background_layer->visible) {
        composite.fill(Qt::transparent);
    }

    QPainter painter(&composite);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    for (qsizetype index = 1; index < document.root_stack.size(); ++index) {
        if (exportWasCancelled(cancellation_requested)) {
            painter.end();
            return {};
        }
        const auto& item = document.root_stack.at(index);
        qreal opacity = 1.0;
        QImage pixels;
        if (item.group) {
            const auto* group = findGroup(document, item.id);
            if (group == nullptr || !group->visible || group->opacity == 0) continue;
            pixels = renderGroup(document, *group, size,
                                 cancellation_requested, excluded_object_ids);
        } else {
            const auto* layer = findLayer(document, item.id);
            if (layer == nullptr || !layer->visible || layer->opacity == 0) continue;
            opacity = layer->opacity / 100.0;
            pixels = renderRasterLayer(
                *layer, size, cancellation_requested, excluded_object_ids);
        }
        if (pixels.isNull() || exportWasCancelled(cancellation_requested)) {
            painter.end();
            return {};
        }
        painter.setOpacity(opacity);
        painter.drawImage(0, 0, pixels);
    }
    painter.end();
    return exportWasCancelled(cancellation_requested) ? QImage{} : composite;
}

QImage renderSelectedLayer(const QImage& source_image,
                           const ImageDocumentData& document,
                           const QString& selected_layer_id,
                           const std::atomic_bool* cancellation_requested = nullptr) {
    if (source_image.isNull() || selected_layer_id.isEmpty() ||
        exportWasCancelled(cancellation_requested)) return {};

    const auto selected = std::find_if(
        document.layers.cbegin(), document.layers.cend(),
        [&selected_layer_id](const ImageLayerData& layer) {
            return layer.id == selected_layer_id;
        });
    if (selected == document.layers.cend()) return {};

    QSize size = source_image.size();
    for (const auto& operation : document.operations) {
        if (exportWasCancelled(cancellation_requested)) return {};
        if (operation.kind == OperationKind::Crop) size = operation.crop.size();
        else if (operation.kind == OperationKind::Rotate) size.transpose();
    }
    if (!size.isValid() || size.isEmpty()) return {};

    QImage rendered(size, QImage::Format_ARGB32_Premultiplied);
    if (rendered.isNull()) return {};
    rendered.fill(Qt::transparent);
    const ImageLayerData& selected_layer = *selected;
    const ImageGroupData* parent_group = selected_layer.parent_group_id.isEmpty()
        ? nullptr : findGroup(document, selected_layer.parent_group_id);
    if (!selected_layer.visible || selected_layer.opacity <= 0 ||
        (parent_group != nullptr && (!parent_group->visible || parent_group->opacity <= 0))) {
        return rendered;
    }

    QImage pixels;
    if (selected->background) {
        pixels = applyOperations(
            source_image, document.operations, false, cancellation_requested);
    } else {
        pixels = QImage(size, QImage::Format_ARGB32_Premultiplied);
        if (pixels.isNull()) return {};
        pixels.fill(Qt::transparent);
        pixels = renderRasterLayer(selected_layer, size,
                                   cancellation_requested, {});
        if (parent_group != nullptr) {
            pixels = applyOperations(std::move(pixels), parent_group->operations,
                                     true, cancellation_requested);
        }
    }
    if (pixels.isNull() || exportWasCancelled(cancellation_requested)) return {};

    QPainter painter(&rendered);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    const qreal group_opacity = parent_group == nullptr ? 1.0 : parent_group->opacity / 100.0;
    painter.setOpacity((selected_layer.opacity / 100.0) * group_opacity);
    painter.drawImage(0, 0, pixels);
    painter.end();
    return exportWasCancelled(cancellation_requested) ? QImage{} : rendered;
}

QImage renderSelectedGroup(const QImage& source_image,
                           const ImageDocumentData& document,
                           const QString& selected_group_id,
                           const std::atomic_bool* cancellation_requested = nullptr) {
    if (source_image.isNull() || selected_group_id.isEmpty() ||
        exportWasCancelled(cancellation_requested)) return {};
    const auto* group = findGroup(document, selected_group_id);
    if (group == nullptr) return {};
    QSize size = source_image.size();
    for (const auto& operation : document.operations) {
        if (exportWasCancelled(cancellation_requested)) return {};
        if (operation.kind == OperationKind::Crop) size = operation.crop.size();
        else if (operation.kind == OperationKind::Rotate) size.transpose();
    }
    if (!size.isValid() || size.isEmpty()) return {};
    return renderGroup(document, *group, size, cancellation_requested, {});
}

QImage renderLayerThumbnail(QImage image,
                            QSize virtual_size,
                            const QVector<ImageOperation>& operations,
                            bool fixed_canvas,
                            const QSize& maximum_size,
                            bool transparent_base) {
    if (!virtual_size.isValid() || virtual_size.isEmpty()) return {};
    const QSize initial_size = virtual_size.scaled(maximum_size, Qt::KeepAspectRatio);
    if (initial_size.isEmpty()) return {};
    if (transparent_base) {
        image = QImage(initial_size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
    } else {
        if (image.isNull()) return {};
        image = image.scaled(initial_size, Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation);
    }

    for (const auto& operation : operations) {
        const qreal scale_x = static_cast<qreal>(image.width()) / virtual_size.width();
        const qreal scale_y = static_cast<qreal>(image.height()) / virtual_size.height();
        ImageOperation scaled_operation = operation;
        switch (operation.kind) {
        case OperationKind::Crop: {
            const int left = static_cast<int>(std::floor(operation.crop.x() * scale_x));
            const int top = static_cast<int>(std::floor(operation.crop.y() * scale_y));
            const int right = static_cast<int>(std::ceil(
                (operation.crop.x() + operation.crop.width()) * scale_x));
            const int bottom = static_cast<int>(std::ceil(
                (operation.crop.y() + operation.crop.height()) * scale_y));
            const QRect scaled_crop(left, top, std::max(1, right - left),
                                    std::max(1, bottom - top));
            scaled_operation.crop = scaled_crop.intersected(
                QRect(QPoint(0, 0), image.size()));
            if (scaled_operation.crop.isEmpty()) return {};
            if (!fixed_canvas) virtual_size = operation.crop.size();
            break;
        }
        case OperationKind::Rotate:
            if (!fixed_canvas && std::abs(operation.quarter_turns) % 2 != 0) {
                virtual_size.transpose();
            }
            break;
        case OperationKind::PaintStroke: {
            for (auto& point : scaled_operation.paint_stroke.points) {
                point.setX(point.x() * scale_x);
                point.setY(point.y() * scale_y);
            }
            scaled_operation.paint_stroke.diameter = std::max(
                1, qRound(operation.paint_stroke.diameter * std::min(scale_x, scale_y)));
            break;
        }
        case OperationKind::EraseStroke: {
            for (auto& point : scaled_operation.erase_stroke.points) {
                point.setX(point.x() * scale_x);
                point.setY(point.y() * scale_y);
            }
            scaled_operation.erase_stroke.diameter = std::max(
                1, qRound(operation.erase_stroke.diameter * std::min(scale_x, scale_y)));
            break;
        }
        case OperationKind::Shape:
            scaled_operation.shape.start.setX(operation.shape.start.x() * scale_x);
            scaled_operation.shape.start.setY(operation.shape.start.y() * scale_y);
            scaled_operation.shape.end.setX(operation.shape.end.x() * scale_x);
            scaled_operation.shape.end.setY(operation.shape.end.y() * scale_y);
            scaled_operation.shape.stroke_width = std::max(
                1, qRound(operation.shape.stroke_width * std::min(scale_x, scale_y)));
            break;
        case OperationKind::FlipHorizontal:
        case OperationKind::FlipVertical:
            break;
        }
        image = applyOperations(std::move(image), {scaled_operation}, fixed_canvas);
    }

    return image.scaled(maximum_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace

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

    data_ = {};
    data_.base_kind = ImageBaseKind::Canvas;
    data_.source_size = size;
    data_.canvas_background = background;
    initializeDefaultLayers();
    source_image_ = std::move(canvas);
    document_path_.clear();
    recovery_session_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    baseline_source_path_.clear();
    baseline_source_size_ = size;
    baseline_base_kind_ = ImageBaseKind::Canvas;
    baseline_canvas_background_ = background;
    baseline_operations_.clear();
    baseline_layers_ = data_.layers;
    baseline_groups_ = data_.groups;
    baseline_root_stack_ = data_.root_stack;
    force_dirty_ = true;
    undo_stack_.clear();
    redo_stack_.clear();
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

    data_ = {};
    data_.base_kind = ImageBaseKind::SourceImage;
    data_.source_path = absoluteCleanPath(source_path);
    data_.source_size = decoded.size();
    initializeDefaultLayers();
    source_image_ = std::move(decoded);
    document_path_.clear();
    recovery_session_id_.clear();
    baseline_source_path_ = data_.source_path;
    baseline_source_size_ = data_.source_size;
    baseline_base_kind_ = data_.base_kind;
    baseline_canvas_background_ = data_.canvas_background;
    baseline_operations_.clear();
    baseline_layers_ = data_.layers;
    baseline_groups_ = data_.groups;
    baseline_root_stack_ = data_.root_stack;
    force_dirty_ = false;
    undo_stack_.clear();
    redo_stack_.clear();
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
    baseline_base_kind_ = data_.base_kind;
    baseline_canvas_background_ = data_.canvas_background;
    baseline_operations_ = data_.operations;
    baseline_layers_ = data_.layers;
    baseline_groups_ = data_.groups;
    baseline_root_stack_ = data_.root_stack;
    force_dirty_ = false;
    undo_stack_.clear();
    redo_stack_.clear();
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
    baseline_base_kind_ = data_.base_kind;
    baseline_canvas_background_ = data_.canvas_background;
    baseline_operations_.clear();
    baseline_layers_ = data_.layers;
    baseline_groups_ = data_.groups;
    baseline_root_stack_ = data_.root_stack;
    force_dirty_ = true;
    undo_stack_.clear();
    redo_stack_.clear();
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
    baseline_base_kind_ = data_.base_kind;
    baseline_canvas_background_ = data_.canvas_background;
    baseline_operations_ = data_.operations;
    baseline_layers_ = data_.layers;
    baseline_groups_ = data_.groups;
    baseline_root_stack_ = data_.root_stack;
    force_dirty_ = false;
    return true;
}

ImageExportResult exportImageSnapshot(
    const ImageExportSnapshot& snapshot,
    const QString& output_path,
    const ImageExportOptions& options,
    const std::atomic_bool* cancellation_requested,
    const ImageExportProgressCallback& progress) {
    const auto failed = [](const QString& cause) {
        return ImageExportResult{ImageExportStatus::Failed, cause};
    };
    if (snapshot.source_image.isNull()) {
        return failed(QStringLiteral("Open or relink an image before exporting."));
    }
    const QString suffix = QFileInfo(output_path).suffix().toLower();
    QByteArray format;
    if (suffix == "png") format = "png";
    else if (suffix == "jpg" || suffix == "jpeg") format = "jpeg";
    else {
        return failed(QStringLiteral("Export supports PNG and JPEG files."));
    }

    if (format == "jpeg" && (options.jpeg_quality < 0 || options.jpeg_quality > 100)) {
        return failed(QStringLiteral("JPEG quality must be between 0 and 100."));
    }
    if (format == "jpeg" &&
        (!options.jpeg_background.isValid() || options.jpeg_background.alpha() != 255)) {
        return failed(QStringLiteral("Choose an opaque background color for JPEG export."));
    }
    if (options.scope != ImageExportScope::Composite &&
        options.scope != ImageExportScope::SelectedLayer &&
        options.scope != ImageExportScope::SelectedGroup) {
        return failed(QStringLiteral("The requested image export scope is invalid."));
    }
    if (options.scope == ImageExportScope::SelectedLayer &&
        std::none_of(snapshot.document.layers.cbegin(), snapshot.document.layers.cend(),
                     [&snapshot](const ImageLayerData& layer) {
                         return layer.id == snapshot.selected_layer_id;
                     })) {
        return failed(QStringLiteral("The selected layer is unavailable for export."));
    }
    if (options.scope == ImageExportScope::SelectedGroup &&
        findGroup(snapshot.document, snapshot.selected_group_id) == nullptr) {
        return failed(QStringLiteral("The selected group is unavailable for export."));
    }
    if (exportWasCancelled(cancellation_requested)) {
        return {ImageExportStatus::Cancelled, {}};
    }
    if (progress) progress(ImageExportPhase::Rendering);

    QImage rendered;
    if (options.scope == ImageExportScope::SelectedLayer) {
        rendered = renderSelectedLayer(snapshot.source_image, snapshot.document,
                                       snapshot.selected_layer_id, cancellation_requested);
    } else if (options.scope == ImageExportScope::SelectedGroup) {
        rendered = renderSelectedGroup(snapshot.source_image, snapshot.document,
                                       snapshot.selected_group_id, cancellation_requested);
    } else {
        rendered = renderComposite(snapshot.source_image, snapshot.document,
                                   cancellation_requested);
    }
    if (exportWasCancelled(cancellation_requested)) {
        return {ImageExportStatus::Cancelled, {}};
    }
    if (rendered.isNull()) {
        return failed(QStringLiteral("The image could not be rendered for export."));
    }

    if (format == "jpeg") {
        QImage flattened(rendered.size(), QImage::Format_RGB32);
        if (flattened.isNull()) {
            return failed(QStringLiteral("Not enough memory to prepare the JPEG image."));
        }
        flattened.fill(options.jpeg_background);
        QPainter painter(&flattened);
        painter.drawImage(0, 0, rendered);
        painter.end();
        rendered = std::move(flattened);
    }

    if (exportWasCancelled(cancellation_requested)) {
        return {ImageExportStatus::Cancelled, {}};
    }
    if (progress) progress(ImageExportPhase::Encoding);
    if (exportWasCancelled(cancellation_requested)) {
        return {ImageExportStatus::Cancelled, {}};
    }

    QSaveFile output(output_path);
    if (!output.open(QIODevice::WriteOnly)) {
        return failed(output.errorString());
    }
    QImageWriter writer(&output, format);
    if (format == "jpeg") writer.setQuality(options.jpeg_quality);
    if (!writer.write(rendered)) {
        const QString cause = writer.errorString();
        output.cancelWriting();
        return failed(cause);
    }
    if (progress) progress(ImageExportPhase::Finalizing);
    if (exportWasCancelled(cancellation_requested)) {
        output.cancelWriting();
        return {ImageExportStatus::Cancelled, {}};
    }
    if (!output.commit()) {
        return failed(output.errorString());
    }
    return {ImageExportStatus::Succeeded, {}};
}

ImageExportSnapshot ImageDocumentSession::exportSnapshot() const {
    return {source_image_, data_, selected_layer_id_, selected_group_id_};
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
    QSize size = data_.source_size;
    for (const auto& operation : data_.operations) {
        if (operation.kind == OperationKind::Crop) size = operation.crop.size();
        else if (operation.kind == OperationKind::Rotate) size.transpose();
    }
    return size;
}

QImage ImageDocumentSession::renderedImage() const {
    return renderComposite(source_image_, data_);
}

QImage ImageDocumentSession::renderedImageWithoutShape(const QString& shape_id) const {
    return renderComposite(source_image_, data_, nullptr, QStringList{shape_id});
}

QImage ImageDocumentSession::renderedImageWithoutObjects(
    const QStringList& object_ids) const {
    return renderComposite(source_image_, data_, nullptr, object_ids);
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
                operation.kind != OperationKind::Shape) continue;
            ImageObjectPlacement placement;
            placement.operation = operation;
            placement.layer_id = layer.id;
            placement.layer_opacity = effective_opacity;
            for (qsizetype suffix = index; suffix < layer.operations.size(); ++suffix) {
                const auto& later = layer.operations.at(suffix);
                transformObjectGeometry(&placement.operation, later, size);
            }
            if (parent_group != nullptr) {
                for (const auto& group_operation : parent_group->operations) {
                    transformObjectGeometry(&placement.operation, group_operation, size);
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

    struct Mutation {
        qsizetype layer_index = -1;
        qsizetype operation_index = -1;
        ImageOperation operation;
    };
    QVector<Mutation> mutations;
    QSet<QString> seen;
    const QSize size = renderedSize();
    for (const auto& placement : objects) {
        const QString id = operationObjectId(placement.operation);
        if (id.isEmpty() || seen.contains(id)) {
            assignError(error, QStringLiteral("The selected objects are invalid or duplicated."));
            return false;
        }
        seen.insert(id);

        qsizetype layer_index = -1;
        qsizetype operation_index = -1;
        for (qsizetype candidate_layer = 1; candidate_layer < data_.layers.size(); ++candidate_layer) {
            const auto& layer = data_.layers.at(candidate_layer);
            if (layer.id != placement.layer_id || !effectiveLayerVisible(layer)) continue;
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
            return false;
        }

        const auto& layer = data_.layers.at(layer_index);
        ImageOperation stored = placement.operation;
        const auto* parent_group = layer.parent_group_id.isEmpty()
            ? nullptr : findGroup(data_, layer.parent_group_id);
        if (parent_group != nullptr) {
            for (qsizetype suffix = parent_group->operations.size(); suffix > 0; --suffix) {
                transformObjectGeometry(&stored,
                    parent_group->operations.at(suffix - 1), size, true);
            }
        }
        for (qsizetype suffix = layer.operations.size(); suffix > operation_index + 1; --suffix) {
            transformObjectGeometry(&stored, layer.operations.at(suffix - 1), size, true);
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
                    point.x() < size.width() && point.y() < size.height();
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
                    point.x() < size.width() && point.y() < size.height();
            }
        } else if (stored.kind == OperationKind::Shape) {
            valid = ImageDocumentStore::isValidShape(stored.shape, size, error);
        }
        if (!valid) {
            if (error == nullptr || error->isEmpty()) {
                assignError(error, QStringLiteral("The selected object geometry is invalid."));
            }
            return false;
        }
        if (stored != layer.operations.at(operation_index)) {
            mutations.append({layer_index, operation_index, std::move(stored)});
        }
    }

    if (mutations.isEmpty()) return false;
    pushEdit();
    for (const auto& mutation : mutations) {
        data_.layers[mutation.layer_index].operations[mutation.operation_index] =
            mutation.operation;
    }
    layer_thumbnail_cache_.clear();
    return true;
}

bool ImageDocumentSession::updateShapeStyles(const QStringList& shape_ids,
                                             const ImageShapeData& style,
                                             QString* error) {
    if (error != nullptr) error->clear();
    if (shape_ids.isEmpty() || !style.stroke_color.isValid() ||
        !style.fill_color.isValid() || style.stroke_width < 1 ||
        style.stroke_width > ImageDocumentStore::kMaximumShapeStrokeWidth) return false;

    struct Mutation {
        qsizetype layer_index = -1;
        qsizetype operation_index = -1;
        ImageShapeData shape;
    };
    QVector<Mutation> mutations;
    QSet<QString> seen;
    const QSize size = renderedSize();
    for (const QString& id : shape_ids) {
        if (id.isEmpty() || seen.contains(id)) continue;
        seen.insert(id);
        bool found = false;
        for (qsizetype layer_index = 1; layer_index < data_.layers.size() && !found; ++layer_index) {
            const auto& layer = data_.layers.at(layer_index);
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
                if (!ImageDocumentStore::isValidShape(updated, size, error)) return false;
                if (updated != operation.shape) {
                    mutations.append({layer_index, operation_index, std::move(updated)});
                }
                found = true;
                break;
            }
        }
    }
    if (mutations.isEmpty()) return false;
    pushEdit();
    for (const auto& mutation : mutations) {
        data_.layers[mutation.layer_index].operations[mutation.operation_index].shape =
            mutation.shape;
    }
    layer_thumbnail_cache_.clear();
    return true;
}

bool ImageDocumentSession::deleteObjects(const QStringList& object_ids) {
    QSet<QString> remaining;
    for (const auto& id : object_ids) if (!id.isEmpty()) remaining.insert(id);
    if (remaining.isEmpty()) return false;

    bool found = false;
    for (qsizetype layer_index = 1; layer_index < data_.layers.size() && !found; ++layer_index) {
        for (const auto& operation : data_.layers.at(layer_index).operations) {
            if (remaining.contains(operationObjectId(operation))) {
                found = true;
                break;
            }
        }
    }
    if (!found) return false;

    pushEdit();
    remaining.clear();
    for (const auto& id : object_ids) if (!id.isEmpty()) remaining.insert(id);
    for (qsizetype layer_index = 1; layer_index < data_.layers.size(); ++layer_index) {
        auto& operations = data_.layers[layer_index].operations;
        for (qsizetype index = operations.size(); index > 0; --index) {
            const QString id = operationObjectId(operations.at(index - 1));
            if (!id.isEmpty() && remaining.contains(id)) {
                remaining.remove(id);
                operations.removeAt(index - 1);
            }
        }
    }
    layer_thumbnail_cache_.clear();
    return true;
}

QImage ImageDocumentSession::renderedImageWithEraseStroke(
    const QVector<QPointF>& points, int diameter) const {
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
        ImageOperation preview;
        preview.kind = OperationKind::EraseStroke;
        preview.erase_stroke.points = points;
        preview.erase_stroke.diameter = diameter;
        selected->operations.append(std::move(preview));
    }
    return renderComposite(source_image_, preview_document);
}

QHash<QString, QImage> ImageDocumentSession::renderedLayerThumbnails(
    const QSize& maximum_size) const {
    QHash<QString, QImage> thumbnails;
    if (maximum_size.width() <= 0 || maximum_size.height() <= 0) {
        return thumbnails;
    }
    if (!hasSource()) {
        layer_thumbnail_cache_.clear();
        return thumbnails;
    }

    const QSize canvas_size = renderedSize();
    const qint64 source_cache_key = source_image_.cacheKey();
    for (const auto& layer : data_.layers) {
        const QVector<ImageOperation>& operations = layer.background
            ? data_.operations : layer.operations;
        auto cached = layer_thumbnail_cache_.find(layer.id);
        const bool cache_matches = cached != layer_thumbnail_cache_.end() &&
            cached->operations.size() == operations.size() &&
            cached->operations.constData() == operations.constData() &&
            cached->source_size == canvas_size &&
            cached->maximum_size == maximum_size &&
            cached->source_cache_key == source_cache_key &&
            cached->background == layer.background;

        if (!cache_matches) {
            QImage thumbnail = renderLayerThumbnail(
                layer.background ? source_image_ : QImage{},
                layer.background ? data_.source_size : canvas_size,
                operations, !layer.background, maximum_size, !layer.background);
            LayerThumbnailCacheEntry entry;
            entry.operations = operations;
            entry.source_size = canvas_size;
            entry.maximum_size = maximum_size;
            entry.source_cache_key = source_cache_key;
            entry.background = layer.background;
            entry.thumbnail = std::move(thumbnail);
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
        const QImage rendered_group = renderGroup(
            data_, group, canvas_size, nullptr, {});
        thumbnails.insert(group.id, rendered_group.isNull()
            ? QImage{} : rendered_group.scaled(
                maximum_size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
    pushEdit();
    ImageOperation operation;
    operation.kind = OperationKind::Crop;
    operation.crop = valid;
    data_.layers[layerIndex(selected_layer_id_)].operations.append(operation);
    return true;
}

bool ImageDocumentSession::applyPaintStroke(const QVector<QPointF>& points,
                                            const QColor& color,
                                            int diameter,
                                            QString* error) {
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
    if (color.alpha() == 0) return false;

    pushEdit();
    ImageOperation operation;
    operation.kind = OperationKind::PaintStroke;
    operation.paint_stroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    operation.paint_stroke.points = points;
    operation.paint_stroke.color = color;
    operation.paint_stroke.diameter = diameter;
    data_.layers[layerIndex(selected_layer_id_)].operations.append(std::move(operation));
    return true;
}

bool ImageDocumentSession::applyEraseStroke(const QVector<QPointF>& points,
                                            int diameter,
                                            QString* error) {
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

    pushEdit();
    ImageOperation operation;
    operation.kind = OperationKind::EraseStroke;
    operation.erase_stroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    operation.erase_stroke.points = points;
    operation.erase_stroke.diameter = diameter;
    data_.layers[layerIndex(selected_layer_id_)].operations.append(std::move(operation));
    return true;
}

QString ImageDocumentSession::addShape(ImageShapeData shape, QString* error) {
    if (error != nullptr) error->clear();
    if (!hasSource()) {
        assignError(error, QStringLiteral("Open or relink an image before creating a shape."));
        return {};
    }
    if (totalStackItemCount() >= ImageDocumentStore::kMaximumLayers) {
        assignError(error, QStringLiteral(
            "The document has reached the maximum of 512 stack items."));
        return {};
    }
    if (shape.id.isEmpty()) shape.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!ImageDocumentStore::isValidShape(shape, renderedSize(), error)) return {};
    for (const auto& layer : data_.layers) {
        for (const auto& operation : layer.operations) {
            if (operation.kind == OperationKind::Shape &&
                operation.shape.id.compare(shape.id, Qt::CaseInsensitive) == 0) {
                assignError(error, QStringLiteral("A shape with this ID already exists."));
                return {};
            }
        }
    }

    const QString shape_id = shape.id;
    int suffix = 1;
    QString layer_name;
    const auto nameExists = [this](const QString& candidate) {
        const bool layer_match = std::any_of(data_.layers.cbegin(), data_.layers.cend(),
            [&candidate](const ImageLayerData& layer) {
                return layer.name.compare(candidate, Qt::CaseInsensitive) == 0;
            });
        const bool group_match = std::any_of(data_.groups.cbegin(), data_.groups.cend(),
            [&candidate](const ImageGroupData& group) {
                return group.name.compare(candidate, Qt::CaseInsensitive) == 0;
            });
        return layer_match || group_match;
    };
    do {
        layer_name = QStringLiteral("Shape %1").arg(suffix++);
    } while (nameExists(layer_name));

    ImageLayerData shape_layer;
    shape_layer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    shape_layer.name = layer_name;
    const QString parent_group_id = selected_group_id_.isEmpty()
        ? parentGroupForLayer(selected_layer_id_) : QString{};
    shape_layer.parent_group_id = parent_group_id;
    ImageOperation operation;
    operation.kind = OperationKind::Shape;
    operation.shape = std::move(shape);

    pushEdit();
    if (!parent_group_id.isEmpty()) {
        auto* parent = findGroup(data_, parent_group_id);
        const qsizetype selected_index = parent->layer_ids.indexOf(selected_layer_id_);
        parent->layer_ids.insert(selected_index < 0 ? parent->layer_ids.size()
                                                   : selected_index + 1,
                                 shape_layer.id);
    } else {
        qsizetype insertion_index = data_.root_stack.size();
        if (!selected_group_id_.isEmpty()) {
            for (qsizetype index = 0; index < data_.root_stack.size(); ++index) {
                if (data_.root_stack.at(index).group &&
                    data_.root_stack.at(index).id == selected_group_id_) {
                    insertion_index = index + 1;
                    break;
                }
            }
        } else {
            for (qsizetype index = 0; index < data_.root_stack.size(); ++index) {
                if (!data_.root_stack.at(index).group &&
                    data_.root_stack.at(index).id == selected_layer_id_) {
                    insertion_index = index + 1;
                    break;
                }
            }
        }
        data_.root_stack.insert(insertion_index, {shape_layer.id, false});
    }
    const QString new_layer_id = shape_layer.id;
    data_.layers.append(std::move(shape_layer));
    auto* new_layer = findLayer(data_, new_layer_id);
    new_layer->operations.append(std::move(operation));
    selected_layer_id_ = new_layer_id;
    selected_group_id_.clear();
    rebuildLayerOrder();
    layer_thumbnail_cache_.clear();
    return shape_id;
}

bool ImageDocumentSession::updateShape(const ImageShapeData& shape, QString* error) {
    if (error != nullptr) error->clear();
    if (!ImageDocumentStore::isValidShape(shape, renderedSize(), error)) return false;
    for (qsizetype layer_index = 0; layer_index < data_.layers.size(); ++layer_index) {
        const auto& layer = data_.layers.at(layer_index);
        if (layer.background) continue;
        for (qsizetype operation_index = 0;
             operation_index < layer.operations.size(); ++operation_index) {
            const auto& operation = layer.operations.at(operation_index);
            if (operation.kind != OperationKind::Shape || operation.shape.id != shape.id) continue;
            if (operation.shape == shape) return false;
            pushEdit();
            data_.layers[layer_index].operations[operation_index].shape = shape;
            layer_thumbnail_cache_.clear();
            return true;
        }
    }
    assignError(error, QStringLiteral("The selected shape no longer exists."));
    return false;
}

bool ImageDocumentSession::updateShapeRendered(const ImageShapeData& rendered_shape,
                                               QString* error) {
    ImageShapeData stored_shape = rendered_shape;
    const QSize size = renderedSize();
    for (const auto& layer : data_.layers) {
        if (layer.background) continue;
        for (qsizetype index = 0; index < layer.operations.size(); ++index) {
            const auto& operation = layer.operations.at(index);
            if (operation.kind != OperationKind::Shape ||
                operation.shape.id != rendered_shape.id) continue;
            if (!layer.parent_group_id.isEmpty()) {
                const auto* parent_group = findGroup(data_, layer.parent_group_id);
                if (parent_group != nullptr) {
                    for (qsizetype suffix = parent_group->operations.size(); suffix > 0; --suffix) {
                        stored_shape.start = transformShapePoint(stored_shape.start,
                            parent_group->operations.at(suffix - 1), size, true);
                        stored_shape.end = transformShapePoint(stored_shape.end,
                            parent_group->operations.at(suffix - 1), size, true);
                    }
                }
            }
            for (qsizetype suffix = layer.operations.size(); suffix > index + 1; --suffix) {
                const auto& later = layer.operations.at(suffix - 1);
                stored_shape.start = transformShapePoint(stored_shape.start, later, size, true);
                stored_shape.end = transformShapePoint(stored_shape.end, later, size, true);
            }
            return updateShape(stored_shape, error);
        }
    }
    assignError(error, QStringLiteral("The selected shape no longer exists."));
    return false;
}

bool ImageDocumentSession::deleteShape(const QString& shape_id) {
    for (qsizetype layer_index = 0; layer_index < data_.layers.size(); ++layer_index) {
        const auto& layer = data_.layers.at(layer_index);
        if (layer.background) continue;
        for (qsizetype index = 0; index < layer.operations.size(); ++index) {
            const auto& operation = layer.operations.at(index);
            if (operation.kind != OperationKind::Shape || operation.shape.id != shape_id) continue;
            pushEdit();
            data_.layers[layer_index].operations.removeAt(index);
            layer_thumbnail_cache_.clear();
            return true;
        }
    }
    return false;
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
    if (!hasDocument() || totalStackItemCount() >= ImageDocumentStore::kMaximumLayers) return {};
    int suffix = 1;
    QString name;
    const auto nameExists = [this](const QString& candidate) {
        const auto layer_match = std::any_of(data_.layers.cbegin(), data_.layers.cend(),
            [&candidate](const ImageLayerData& layer) {
                return layer.name.compare(candidate, Qt::CaseInsensitive) == 0;
            });
        const auto group_match = std::any_of(data_.groups.cbegin(), data_.groups.cend(),
            [&candidate](const ImageGroupData& group) {
                return group.name.compare(candidate, Qt::CaseInsensitive) == 0;
            });
        return layer_match || group_match;
    };
    do {
        name = QStringLiteral("Layer %1").arg(suffix++);
    } while (nameExists(name));
    ImageLayerData layer;
    layer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    layer.name = name;
    const QString parent_group_id = selected_group_id_.isEmpty()
        ? parentGroupForLayer(selected_layer_id_) : QString{};
    layer.parent_group_id = parent_group_id;
    pushEdit();
    if (!parent_group_id.isEmpty()) {
        auto* parent = findGroup(data_, parent_group_id);
        const qsizetype selected_index = parent->layer_ids.indexOf(selected_layer_id_);
        parent->layer_ids.insert(selected_index < 0 ? parent->layer_ids.size()
                                                   : selected_index + 1,
                                 layer.id);
    } else {
        qsizetype insertion_index = data_.root_stack.size();
        if (!selected_group_id_.isEmpty()) {
            for (qsizetype index = 0; index < data_.root_stack.size(); ++index) {
                if (data_.root_stack.at(index).group &&
                    data_.root_stack.at(index).id == selected_group_id_) {
                    insertion_index = index + 1;
                    break;
                }
            }
        } else {
            for (qsizetype index = 0; index < data_.root_stack.size(); ++index) {
                if (!data_.root_stack.at(index).group &&
                    data_.root_stack.at(index).id == selected_layer_id_) {
                    insertion_index = index + 1;
                    break;
                }
            }
        }
        data_.root_stack.insert(insertion_index, {layer.id, false});
    }
    data_.layers.append(layer);
    selected_layer_id_ = layer.id;
    selected_group_id_.clear();
    rebuildLayerOrder();
    layer_thumbnail_cache_.clear();
    return layer.id;
}

bool ImageDocumentSession::deleteLayer(const QString& layer_id) {
    const qsizetype index = layerIndex(layer_id);
    if (index <= 0) return false;
    const QString parent_group_id = data_.layers.at(index).parent_group_id;
    pushEdit();
    const bool selected = selected_layer_id_ == layer_id;
    if (parent_group_id.isEmpty()) {
        for (qsizetype item = 0; item < data_.root_stack.size(); ++item) {
            if (!data_.root_stack.at(item).group && data_.root_stack.at(item).id == layer_id) {
                data_.root_stack.removeAt(item);
                break;
            }
        }
    } else if (auto* parent = findGroup(data_, parent_group_id)) {
        parent->layer_ids.removeAll(layer_id);
    }
    data_.layers.removeAt(index);
    if (selected) {
        const qsizetype replacement = std::min(index, data_.layers.size() - 1);
        selected_layer_id_ = data_.layers.at(replacement).id;
        selected_group_id_.clear();
    }
    rebuildLayerOrder();
    layer_thumbnail_cache_.clear();
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
    return true;
}

QString ImageDocumentSession::addGroup(QString* error) {
    if (error != nullptr) error->clear();
    if (!hasDocument() || totalStackItemCount() >= ImageDocumentStore::kMaximumLayers) {
        assignError(error, QStringLiteral("The document has reached the maximum of 512 stack items."));
        return {};
    }
    int suffix = 1;
    QString name;
    const auto name_exists = [this](const QString& candidate) {
        return std::any_of(data_.layers.cbegin(), data_.layers.cend(),
            [&candidate](const ImageLayerData& layer) {
                return layer.name.compare(candidate, Qt::CaseInsensitive) == 0;
            }) || std::any_of(data_.groups.cbegin(), data_.groups.cend(),
            [&candidate](const ImageGroupData& group) {
                return group.name.compare(candidate, Qt::CaseInsensitive) == 0;
            });
    };
    do { name = QStringLiteral("Group %1").arg(suffix++); } while (name_exists(name));

    ImageGroupData group;
    group.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    group.name = name;
    qsizetype insertion_index = data_.root_stack.size();
    if (!selected_group_id_.isEmpty()) {
        for (qsizetype index = 0; index < data_.root_stack.size(); ++index) {
            if (data_.root_stack.at(index).group &&
                data_.root_stack.at(index).id == selected_group_id_) {
                insertion_index = index + 1;
                break;
            }
        }
    } else if (!selected_layer_id_.isEmpty()) {
        const QString selected_parent = parentGroupForLayer(selected_layer_id_);
        for (qsizetype index = 0; index < data_.root_stack.size(); ++index) {
            const auto& item = data_.root_stack.at(index);
            const bool selected_root_layer = selected_parent.isEmpty() && !item.group &&
                item.id == selected_layer_id_;
            const bool selected_parent_group = !selected_parent.isEmpty() && item.group &&
                item.id == selected_parent;
            if (selected_root_layer || selected_parent_group) {
                insertion_index = index + 1;
                break;
            }
        }
    }
    const QString id = group.id;
    pushEdit();
    data_.groups.append(std::move(group));
    data_.root_stack.insert(insertion_index, {id, true});
    selected_layer_id_.clear();
    selected_group_id_ = id;
    layer_thumbnail_cache_.clear();
    return id;
}

QString ImageDocumentSession::groupLayers(const QStringList& layer_ids, QString* error) {
    if (error != nullptr) error->clear();
    if (layer_ids.size() < 2 || !selected_group_id_.isEmpty()) {
        assignError(error, QStringLiteral("Select at least two contiguous root layers to group."));
        return {};
    }
    QSet<QString> requested;
    for (const auto& id : layer_ids) {
        const auto* layer = findLayer(data_, id);
        if (layer == nullptr || layer->background || !layer->parent_group_id.isEmpty() ||
            requested.contains(id)) {
            assignError(error, QStringLiteral("Only distinct root raster layers can be grouped."));
            return {};
        }
        requested.insert(id);
    }
    QVector<qsizetype> positions;
    for (qsizetype index = 0; index < data_.root_stack.size(); ++index) {
        if (!data_.root_stack.at(index).group && requested.contains(data_.root_stack.at(index).id)) {
            positions.append(index);
        }
    }
    std::sort(positions.begin(), positions.end());
    if (positions.size() != requested.size() ||
        positions.back() - positions.front() + 1 != positions.size()) {
        assignError(error, QStringLiteral("Group Selected requires contiguous sibling layers."));
        return {};
    }
    if (totalStackItemCount() >= ImageDocumentStore::kMaximumLayers) {
        assignError(error, QStringLiteral("The document has reached the maximum of 512 stack items."));
        return {};
    }
    int suffix = 1;
    QString name;
    const auto name_exists = [this](const QString& candidate) {
        return std::any_of(data_.layers.cbegin(), data_.layers.cend(),
            [&candidate](const ImageLayerData& layer) {
                return layer.name.compare(candidate, Qt::CaseInsensitive) == 0;
            }) || std::any_of(data_.groups.cbegin(), data_.groups.cend(),
            [&candidate](const ImageGroupData& group) {
                return group.name.compare(candidate, Qt::CaseInsensitive) == 0;
            });
    };
    do { name = QStringLiteral("Group %1").arg(suffix++); } while (name_exists(name));

    ImageGroupData group;
    group.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    group.name = name;
    for (qsizetype index = positions.front(); index <= positions.back(); ++index) {
        group.layer_ids.append(data_.root_stack.at(index).id);
    }
    const QString group_id = group.id;
    pushEdit();
    for (const auto& child_id : group.layer_ids) {
        if (auto* child = findLayer(data_, child_id)) child->parent_group_id = group_id;
    }
    for (qsizetype index = positions.back(); index >= positions.front(); --index) {
        data_.root_stack.removeAt(index);
    }
    data_.root_stack.insert(positions.front(), {group_id, true});
    data_.groups.append(std::move(group));
    selected_layer_id_.clear();
    selected_group_id_ = group_id;
    rebuildLayerOrder();
    layer_thumbnail_cache_.clear();
    return group_id;
}

bool ImageDocumentSession::ungroup(const QString& group_id) {
    const qsizetype index = groupIndex(group_id);
    if (index < 0) return false;
    const auto root_item = std::find_if(data_.root_stack.cbegin(), data_.root_stack.cend(),
        [&group_id](const ImageStackItemData& item) { return item.group && item.id == group_id; });
    if (root_item == data_.root_stack.cend()) return false;
    const qsizetype root_index = std::distance(data_.root_stack.cbegin(), root_item);
    const QStringList children = data_.groups.at(index).layer_ids;
    pushEdit();
    data_.root_stack.removeAt(root_index);
    for (qsizetype child_index = 0; child_index < children.size(); ++child_index) {
        const QString child_id = children.at(child_index);
        data_.root_stack.insert(root_index + child_index, {child_id, false});
        if (auto* child = findLayer(data_, child_id)) child->parent_group_id.clear();
    }
    data_.groups.removeAt(index);
    if (selected_group_id_ == group_id) {
        selected_group_id_.clear();
        selected_layer_id_ = children.isEmpty()
            ? (data_.layers.isEmpty() ? QString{} : data_.layers.back().id)
            : children.back();
    }
    rebuildLayerOrder();
    layer_thumbnail_cache_.clear();
    return true;
}

bool ImageDocumentSession::deleteGroup(const QString& group_id) {
    const qsizetype index = groupIndex(group_id);
    if (index < 0) return false;
    const auto root_item = std::find_if(data_.root_stack.cbegin(), data_.root_stack.cend(),
        [&group_id](const ImageStackItemData& item) { return item.group && item.id == group_id; });
    if (root_item == data_.root_stack.cend()) return false;
    const qsizetype root_index = std::distance(data_.root_stack.cbegin(), root_item);
    const QStringList children = data_.groups.at(index).layer_ids;
    const QString previous_selection = selected_layer_id_;
    pushEdit();
    data_.root_stack.removeAt(root_index);
    for (const QString& child_id : children) {
        const qsizetype child_index = layerIndex(child_id);
        if (child_index > 0) data_.layers.removeAt(child_index);
    }
    data_.groups.removeAt(index);
    if (selected_group_id_ == group_id || children.contains(previous_selection)) {
        selected_group_id_.clear();
        selected_layer_id_ = data_.layers.isEmpty()
            ? QString{} : data_.layers.back().id;
    }
    rebuildLayerOrder();
    layer_thumbnail_cache_.clear();
    return true;
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
    return true;
}

bool ImageDocumentSession::moveStackItem(const QString& item_id,
                                         bool is_group,
                                         const QString& target_group_id,
                                         qsizetype insertion_index) {
    if (is_group) {
        if (!target_group_id.isEmpty() || groupIndex(item_id) < 0) return false;
        qsizetype source_index = -1;
        for (qsizetype index = 0; index < data_.root_stack.size(); ++index) {
            if (data_.root_stack.at(index).group && data_.root_stack.at(index).id == item_id) {
                source_index = index;
                break;
            }
        }
        if (source_index < 0) return false;
        const qsizetype bounded_index = std::clamp(insertion_index,
            qsizetype{1}, static_cast<qsizetype>(data_.root_stack.size()));
        if (bounded_index == source_index || bounded_index == source_index + 1) return false;
        pushEdit();
        data_.root_stack.removeAt(source_index);
        const qsizetype adjusted = bounded_index > source_index
            ? bounded_index - 1 : bounded_index;
        data_.root_stack.insert(adjusted, {item_id, true});
    } else {
        const qsizetype layer_index = layerIndex(item_id);
        if (layer_index <= 0) return false;
        const QString source_group_id = data_.layers.at(layer_index).parent_group_id;
        if (!target_group_id.isEmpty() && groupIndex(target_group_id) < 0) return false;

        const auto source_position = [&]() -> qsizetype {
            if (source_group_id.isEmpty()) {
                for (qsizetype index = 0; index < data_.root_stack.size(); ++index) {
                    if (!data_.root_stack.at(index).group &&
                        data_.root_stack.at(index).id == item_id) return index;
                }
            } else if (const auto* group = findGroup(data_, source_group_id)) {
                return group->layer_ids.indexOf(item_id);
            }
            return -1;
        }();
        if (source_position < 0) return false;

        qsizetype target_count = 0;
        if (target_group_id.isEmpty()) target_count = data_.root_stack.size();
        else target_count = findGroup(data_, target_group_id)->layer_ids.size();
        qsizetype bounded_index = std::clamp(insertion_index, qsizetype{0}, target_count);
        if (target_group_id.isEmpty()) bounded_index = std::max(qsizetype{1}, bounded_index);
        if (source_group_id == target_group_id && bounded_index > source_position) {
            --bounded_index;
        }
        if (source_group_id == target_group_id && bounded_index == source_position) return false;

        pushEdit();
        if (source_group_id.isEmpty()) {
            for (qsizetype index = 0; index < data_.root_stack.size(); ++index) {
                if (!data_.root_stack.at(index).group && data_.root_stack.at(index).id == item_id) {
                    data_.root_stack.removeAt(index);
                    break;
                }
            }
        } else {
            findGroup(data_, source_group_id)->layer_ids.removeAll(item_id);
        }
        auto* layer = findLayer(data_, item_id);
        layer->parent_group_id = target_group_id;
        if (target_group_id.isEmpty()) {
            data_.root_stack.insert(bounded_index, {item_id, false});
        } else {
            findGroup(data_, target_group_id)->layer_ids.insert(bounded_index, item_id);
        }
    }
    rebuildLayerOrder();
    layer_thumbnail_cache_.clear();
    return true;
}

bool ImageDocumentSession::moveStackItemBy(const QString& item_id,
                                           bool is_group,
                                           int direction) {
    if (direction != -1 && direction != 1) return false;
    if (is_group) {
        qsizetype index = -1;
        for (qsizetype candidate = 1; candidate < data_.root_stack.size(); ++candidate) {
            if (data_.root_stack.at(candidate).group &&
                data_.root_stack.at(candidate).id == item_id) {
                index = candidate;
                break;
            }
        }
        if (index < 0) return false;
        const qsizetype target = index + direction;
        if (target <= 0 || target >= data_.root_stack.size()) return false;
        return moveStackItem(item_id, true, {}, target + (direction > 0 ? 1 : 0));
    }
    const qsizetype index = layerIndex(item_id);
    if (index <= 0) return false;
    const QString parent_id = data_.layers.at(index).parent_group_id;
    qsizetype position = -1;
    qsizetype count = 0;
    if (parent_id.isEmpty()) {
        count = data_.root_stack.size();
        for (qsizetype candidate = 1; candidate < count; ++candidate) {
            if (!data_.root_stack.at(candidate).group &&
                data_.root_stack.at(candidate).id == item_id) position = candidate;
        }
    } else {
        const auto* group = findGroup(data_, parent_id);
        if (group == nullptr) return false;
        count = group->layer_ids.size();
        position = group->layer_ids.indexOf(item_id);
    }
    if (position < 0 || position + direction < (parent_id.isEmpty() ? 1 : 0) ||
        position + direction >= count) return false;
    return moveStackItem(item_id, false, parent_id,
                         position + direction + (direction > 0 ? 1 : 0));
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

qsizetype ImageDocumentSession::totalStackItemCount() const noexcept {
    return data_.layers.size() + data_.groups.size();
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

void ImageDocumentSession::rebuildLayerOrder() {
    QVector<ImageLayerData> ordered;
    ordered.reserve(data_.layers.size());
    const auto append_layer = [this, &ordered](const QString& id) {
        const auto* layer = findLayer(data_, id);
        if (layer != nullptr) ordered.append(*layer);
    };
    for (const auto& item : data_.root_stack) {
        if (!item.group) {
            append_layer(item.id);
            continue;
        }
        const auto* group = findGroup(data_, item.id);
        if (group == nullptr) continue;
        for (const auto& layer_id : group->layer_ids) append_layer(layer_id);
    }
    data_.layers = std::move(ordered);
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

void ImageDocumentSession::recordEditSnapshot(ImageDocumentData before,
                                              QString selected_layer_id,
                                              QString selected_group_id) {
    undo_stack_.append({std::move(before), std::move(selected_layer_id),
                        std::move(selected_group_id)});
    if (undo_stack_.size() > kMaximumHistoryEntries) undo_stack_.removeFirst();
    redo_stack_.clear();
}

void ImageDocumentSession::pushEdit() {
    endLayerOpacityEdit();
    recordEditSnapshot(data_, selected_layer_id_, selected_group_id_);
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
    }
    pushEdit();
    findGroup(data_, selected_group_id_)->operations.append(std::move(checked));
    layer_thumbnail_cache_.clear();
    return true;
}

void ImageDocumentSession::rotateLeft() {
    if (selectedGroupIsActive()) {
        static_cast<void>(applySelectedGroupTransform({OperationKind::Rotate, {}, -1}));
        return;
    }
    if (!selectedLayerIsEditable()) return;
    pushEdit();
    data_.layers[layerIndex(selected_layer_id_)].operations.append({OperationKind::Rotate, {}, -1});
}

void ImageDocumentSession::rotateRight() {
    if (selectedGroupIsActive()) {
        static_cast<void>(applySelectedGroupTransform({OperationKind::Rotate, {}, 1}));
        return;
    }
    if (!selectedLayerIsEditable()) return;
    pushEdit();
    data_.layers[layerIndex(selected_layer_id_)].operations.append({OperationKind::Rotate, {}, 1});
}

void ImageDocumentSession::flipHorizontal() {
    if (selectedGroupIsActive()) {
        static_cast<void>(applySelectedGroupTransform({OperationKind::FlipHorizontal, {}, 0}));
        return;
    }
    if (!selectedLayerIsEditable()) return;
    pushEdit();
    data_.layers[layerIndex(selected_layer_id_)].operations.append({OperationKind::FlipHorizontal, {}, 0});
}

void ImageDocumentSession::flipVertical() {
    if (selectedGroupIsActive()) {
        static_cast<void>(applySelectedGroupTransform({OperationKind::FlipVertical, {}, 0}));
        return;
    }
    if (!selectedLayerIsEditable()) return;
    pushEdit();
    data_.layers[layerIndex(selected_layer_id_)].operations.append({OperationKind::FlipVertical, {}, 0});
}

bool ImageDocumentSession::undo() {
    endLayerOpacityEdit();
    if (undo_stack_.isEmpty()) return false;
    redo_stack_.append({data_, selected_layer_id_, selected_group_id_});
    const auto previous = undo_stack_.takeLast();
    data_ = previous.document;
    selected_group_id_ = groupIndex(previous.selected_group_id) >= 0
        ? previous.selected_group_id : QString{};
    selected_layer_id_ = selected_group_id_.isEmpty() && layerIndex(previous.selected_layer_id) >= 0
        ? previous.selected_layer_id
        : (selected_group_id_.isEmpty() && !data_.layers.isEmpty()
            ? data_.layers.back().id : QString{});
    return true;
}

bool ImageDocumentSession::redo() {
    endLayerOpacityEdit();
    if (redo_stack_.isEmpty()) return false;
    undo_stack_.append({data_, selected_layer_id_, selected_group_id_});
    const auto next = redo_stack_.takeLast();
    data_ = next.document;
    selected_group_id_ = groupIndex(next.selected_group_id) >= 0
        ? next.selected_group_id : QString{};
    selected_layer_id_ = selected_group_id_.isEmpty() && layerIndex(next.selected_layer_id) >= 0
        ? next.selected_layer_id
        : (selected_group_id_.isEmpty() && !data_.layers.isEmpty()
            ? data_.layers.back().id : QString{});
    return true;
}

bool ImageDocumentSession::isDirty() const noexcept {
    return force_dirty_ || data_.source_path != baseline_source_path_ ||
        data_.source_size != baseline_source_size_ || data_.base_kind != baseline_base_kind_ ||
        data_.canvas_background != baseline_canvas_background_ ||
        data_.operations != baseline_operations_ || data_.layers != baseline_layers_ ||
        data_.groups != baseline_groups_ || data_.root_stack != baseline_root_stack_;
}

} // namespace image_editor
