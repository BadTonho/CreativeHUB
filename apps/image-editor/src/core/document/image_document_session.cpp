#include "image_document_session.h"

#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QSaveFile>
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

QImage applyOperations(QImage image,
                       const QVector<ImageOperation>& operations,
                       bool fixed_canvas) {
    const QSize canvas_size = image.size();
    for (const auto& operation : operations) {
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
        }
    }
    return image;
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
    force_dirty_ = false;
    return true;
}

bool ImageDocumentSession::exportImage(const QString& output_path, QString* error) const {
    if (!hasSource()) {
        assignError(error, QStringLiteral("Open or relink an image before exporting."));
        return false;
    }
    const QString suffix = QFileInfo(output_path).suffix().toLower();
    QByteArray format;
    if (suffix == "png") format = "png";
    else if (suffix == "jpg" || suffix == "jpeg") format = "jpeg";
    else {
        assignError(error, QStringLiteral("Export supports PNG and JPEG files."));
        return false;
    }

    QImage rendered = renderedImage();
    if (format == "jpeg") {
        QImage flattened(rendered.size(), QImage::Format_RGB32);
        flattened.fill(Qt::white);
        QPainter painter(&flattened);
        painter.drawImage(0, 0, rendered);
        painter.end();
        rendered = std::move(flattened);
    }

    QSaveFile output(output_path);
    if (!output.open(QIODevice::WriteOnly)) {
        assignError(error, output.errorString());
        return false;
    }
    QImageWriter writer(&output, format);
    if (format == "jpeg") writer.setQuality(95);
    if (!writer.write(rendered)) {
        assignError(error, writer.errorString());
        output.cancelWriting();
        return false;
    }
    if (!output.commit()) {
        assignError(error, output.errorString());
        return false;
    }
    return true;
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
    if (!hasSource()) return {};
    QImage background = applyOperations(source_image_, data_.operations, false);
    const QSize size = background.size();
    QImage composite = !data_.layers.isEmpty() && data_.layers.front().visible
        ? background.convertToFormat(QImage::Format_ARGB32)
        : QImage(size, QImage::Format_ARGB32);
    if (composite.isNull()) return {};
    if (data_.layers.isEmpty() || !data_.layers.front().visible) {
        composite.fill(Qt::transparent);
    }
    QPainter painter(&composite);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    for (qsizetype index = 1; index < data_.layers.size(); ++index) {
        const auto& layer = data_.layers.at(index);
        if (!layer.visible || layer.opacity == 0) continue;
        QImage pixels(size, QImage::Format_ARGB32_Premultiplied);
        pixels.fill(Qt::transparent);
        pixels = applyOperations(std::move(pixels), layer.operations, true);
        painter.setOpacity(layer.opacity / 100.0);
        painter.drawImage(0, 0, pixels);
    }
    painter.end();
    return composite;
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
    return thumbnails;
}

bool ImageDocumentSession::applyCrop(const QRect& crop, QString* error) {
    if (!hasSource()) {
        assignError(error, QStringLiteral("Open or relink an image before cropping."));
        return false;
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
    operation.paint_stroke.points = points;
    operation.paint_stroke.color = color;
    operation.paint_stroke.diameter = diameter;
    data_.layers[layerIndex(selected_layer_id_)].operations.append(std::move(operation));
    return true;
}

QString ImageDocumentSession::addLayer() {
    if (!hasDocument() || data_.layers.size() >= ImageDocumentStore::kMaximumLayers) return {};
    int suffix = 1;
    QString name;
    const auto nameExists = [this](const QString& candidate) {
        return std::any_of(data_.layers.cbegin(), data_.layers.cend(),
            [&candidate](const ImageLayerData& layer) { return layer.name == candidate; });
    };
    do {
        name = QStringLiteral("Layer %1").arg(suffix++);
    } while (nameExists(name));
    ImageLayerData layer;
    layer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    layer.name = name;
    const qsizetype selected_index = layerIndex(selected_layer_id_);
    const qsizetype insertion_index = selected_index < 0
        ? data_.layers.size() : selected_index + 1;
    pushEdit();
    data_.layers.insert(insertion_index, layer);
    selected_layer_id_ = layer.id;
    return layer.id;
}

bool ImageDocumentSession::deleteLayer(const QString& layer_id) {
    const qsizetype index = layerIndex(layer_id);
    if (index <= 0) return false;
    pushEdit();
    const bool selected = selected_layer_id_ == layer_id;
    data_.layers.removeAt(index);
    if (selected) {
        const qsizetype replacement = std::min(index, data_.layers.size() - 1);
        selected_layer_id_ = data_.layers.at(replacement).id;
    }
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
    const qsizetype index = layerIndex(layer_id);
    if (index <= 0 || (direction != -1 && direction != 1)) return false;
    const qsizetype target = index + direction;
    if (target <= 0 || target >= data_.layers.size()) return false;
    pushEdit();
    data_.layers.swapItemsAt(index, target);
    return true;
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

void ImageDocumentSession::beginLayerOpacityEdit() {
    if (opacity_edit_active_) return;
    opacity_edit_snapshot_ = data_;
    opacity_edit_active_ = true;
}

void ImageDocumentSession::endLayerOpacityEdit() {
    if (!opacity_edit_active_) return;
    opacity_edit_active_ = false;
    if (opacity_edit_snapshot_ != data_) {
        recordEditSnapshot(std::move(opacity_edit_snapshot_), selected_layer_id_);
    }
    opacity_edit_snapshot_ = {};
}

bool ImageDocumentSession::selectLayer(const QString& layer_id) {
    if (layerIndex(layer_id) < 0 || selected_layer_id_ == layer_id) return false;
    selected_layer_id_ = layer_id;
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
    return index > 0 && index < data_.layers.size();
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
    selected_layer_id_ = first_layer.id;
}

void ImageDocumentSession::recordEditSnapshot(ImageDocumentData before,
                                              QString selected_layer_id) {
    undo_stack_.append({std::move(before), std::move(selected_layer_id)});
    if (undo_stack_.size() > kMaximumHistoryEntries) undo_stack_.removeFirst();
    redo_stack_.clear();
}

void ImageDocumentSession::pushEdit() {
    endLayerOpacityEdit();
    recordEditSnapshot(data_, selected_layer_id_);
}

void ImageDocumentSession::rotateLeft() {
    if (!selectedLayerIsEditable()) return;
    pushEdit();
    data_.layers[layerIndex(selected_layer_id_)].operations.append({OperationKind::Rotate, {}, -1});
}

void ImageDocumentSession::rotateRight() {
    if (!selectedLayerIsEditable()) return;
    pushEdit();
    data_.layers[layerIndex(selected_layer_id_)].operations.append({OperationKind::Rotate, {}, 1});
}

void ImageDocumentSession::flipHorizontal() {
    if (!selectedLayerIsEditable()) return;
    pushEdit();
    data_.layers[layerIndex(selected_layer_id_)].operations.append({OperationKind::FlipHorizontal, {}, 0});
}

void ImageDocumentSession::flipVertical() {
    if (!selectedLayerIsEditable()) return;
    pushEdit();
    data_.layers[layerIndex(selected_layer_id_)].operations.append({OperationKind::FlipVertical, {}, 0});
}

bool ImageDocumentSession::undo() {
    endLayerOpacityEdit();
    if (undo_stack_.isEmpty()) return false;
    redo_stack_.append({data_, selected_layer_id_});
    const auto previous = undo_stack_.takeLast();
    data_ = previous.document;
    if (layerIndex(selected_layer_id_) < 0) {
        selected_layer_id_ = layerIndex(previous.selected_layer_id) >= 0
            ? previous.selected_layer_id
            : (data_.layers.isEmpty() ? QString{} : data_.layers.back().id);
    }
    return true;
}

bool ImageDocumentSession::redo() {
    endLayerOpacityEdit();
    if (redo_stack_.isEmpty()) return false;
    undo_stack_.append({data_, selected_layer_id_});
    const auto next = redo_stack_.takeLast();
    const bool restores_recorded_selection = layerIndex(next.selected_layer_id) < 0 &&
        std::any_of(next.document.layers.cbegin(), next.document.layers.cend(),
            [&next](const ImageLayerData& layer) {
                return layer.id == next.selected_layer_id;
            });
    data_ = next.document;
    if (restores_recorded_selection) {
        selected_layer_id_ = next.selected_layer_id;
    } else if (layerIndex(selected_layer_id_) < 0) {
        selected_layer_id_ = layerIndex(next.selected_layer_id) >= 0
            ? next.selected_layer_id
            : (data_.layers.isEmpty() ? QString{} : data_.layers.back().id);
    }
    return true;
}

bool ImageDocumentSession::isDirty() const noexcept {
    return force_dirty_ || data_.source_path != baseline_source_path_ ||
        data_.source_size != baseline_source_size_ || data_.base_kind != baseline_base_kind_ ||
        data_.canvas_background != baseline_canvas_background_ ||
        data_.operations != baseline_operations_ || data_.layers != baseline_layers_;
}

} // namespace image_editor
