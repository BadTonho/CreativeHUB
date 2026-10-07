#include "rendering/image_document_renderer.h"
#include "rendering/image_bucket_fill.h"
#include "rendering/image_blur_stroke.h"
#include "rendering/image_linear_gradient.h"
#include "image_document_utils.h"
#include "../diagnostics/image_editor_performance_metrics.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QTextLayout>
#include <QTextOption>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace image_editor {
namespace {

ImageEditorPerformanceStage operationPerformanceStage(OperationKind kind) noexcept {
    switch (kind) {
    case OperationKind::PaintStroke: return ImageEditorPerformanceStage::PaintStroke;
    case OperationKind::EraseStroke: return ImageEditorPerformanceStage::EraseStroke;
    case OperationKind::BucketFill: return ImageEditorPerformanceStage::PaintStroke;
    case OperationKind::LinearGradient: return ImageEditorPerformanceStage::LinearGradient;
    case OperationKind::BlurStroke: return ImageEditorPerformanceStage::OperationReplay;
    case OperationKind::Shape: return ImageEditorPerformanceStage::Shape;
    case OperationKind::Text: return ImageEditorPerformanceStage::Text;
    case OperationKind::RasterImage: return ImageEditorPerformanceStage::RasterImage;
    case OperationKind::Crop:
    case OperationKind::Rotate:
    case OperationKind::FlipHorizontal:
    case OperationKind::FlipVertical:
        return ImageEditorPerformanceStage::Transform;
    }
    return ImageEditorPerformanceStage::OperationReplay;
}

QImage paintStroke(QImage image, const ImagePaintStroke& stroke) {
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (stroke.clipping_path.has_value()) {
        painter.setClipPath(*stroke.clipping_path, Qt::IntersectClip);
    }
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
    if (stroke.clipping_path.has_value()) {
        painter.setClipPath(*stroke.clipping_path, Qt::IntersectClip);
    }
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

QImage drawText(QImage image, const ImageTextData& text) {
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    QFont font(text.font_family);
    font.setPixelSize(text.font_pixel_size);
    painter.setFont(font);
    painter.setPen(text.color);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(text.alignment == ImageTextAlignment::Center
        ? Qt::AlignHCenter : (text.alignment == ImageTextAlignment::Right
            ? Qt::AlignRight : Qt::AlignLeft));
    QTextLayout layout(text.content, font);
    layout.setTextOption(option);
    layout.beginLayout();
    qreal height = 0.0;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(text.box_width);
        line.setPosition(QPointF(0.0, height));
        height += line.height();
    }
    layout.endLayout();
    layout.draw(&painter, text.position);
    return image;
}

QImage applyOperations(QImage image,
                       const QVector<ImageOperation>& operations,
                       bool fixed_canvas,
                       const std::atomic_bool* cancellation_requested = nullptr,
                       const QHash<QString, QImage>& resources = {}) {
    ImageEditorPerformanceScope replay_scope(
        ImageEditorPerformanceMetrics::instance(),
        ImageEditorPerformanceStage::OperationReplay);
    const QSize canvas_size = image.size();
    for (const auto& operation : operations) {
        if (cancellation_requested != nullptr &&
            cancellation_requested->load(std::memory_order_relaxed)) {
            return {};
        }
        ImageEditorPerformanceScope operation_scope(
            ImageEditorPerformanceMetrics::instance(),
            operationPerformanceStage(operation.kind));
        switch (operation.kind) {
        case OperationKind::RasterImage: {
            const QImage source = resources.value(operation.raster.id, resources.value(operation.raster.source_path));
            if (!source.isNull() && source.size() == operation.raster.source_size) {
                QPainter painter(&image);
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                painter.setTransform(operation.raster.transform);
                painter.drawImage(0, 0, source);
            }
            break;
        }
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
                const QRect bounds = operation.transform_bounds.isValid() &&
                    !operation.transform_bounds.isEmpty()
                    ? operation.transform_bounds : QRect(QPoint(), canvas_size);
                QTransform transform;
                const qreal center_x = bounds.x() + bounds.width() / 2.0;
                const qreal center_y = bounds.y() + bounds.height() / 2.0;
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
        case OperationKind::FlipVertical:
        {
            const QRect bounds = operation.transform_bounds.isValid() &&
                !operation.transform_bounds.isEmpty()
                ? operation.transform_bounds : QRect(QPoint(), canvas_size);
            if (bounds == QRect(QPoint(), image.size())) {
                image = image.mirrored(operation.kind == OperationKind::FlipHorizontal,
                                       operation.kind == OperationKind::FlipVertical);
                break;
            }
            QImage flipped(canvas_size, QImage::Format_ARGB32_Premultiplied);
            flipped.fill(Qt::transparent);
            QTransform transform;
            if (operation.kind == OperationKind::FlipHorizontal) {
                transform.translate(2.0 * bounds.x() + bounds.width() - 1.0, 0.0);
                transform.scale(-1.0, 1.0);
            } else {
                transform.translate(0.0, 2.0 * bounds.y() + bounds.height() - 1.0);
                transform.scale(1.0, -1.0);
            }
            QPainter painter(&flipped);
            painter.setTransform(transform);
            painter.drawImage(0, 0, image);
            image = std::move(flipped);
            break;
        }
        case OperationKind::PaintStroke:
            image = paintStroke(std::move(image), operation.paint_stroke);
            break;
        case OperationKind::EraseStroke:
            image = eraseStroke(std::move(image), operation.erase_stroke);
            break;
        case OperationKind::BucketFill: {
            bool changed = false;
            if (!ImageBucketFill::apply(&image, operation.bucket_fill, &changed)) return {};
            break;
        }
        case OperationKind::LinearGradient: {
            bool changed = false;
            if (!ImageLinearGradient::apply(&image, operation.linear_gradient, &changed)) return {};
            break;
        }
        case OperationKind::BlurStroke: {
            bool changed = false;
            if (!ImageBlurStrokeRenderer::apply(
                    &image, operation.blur_stroke, &changed)) return {};
            break;
        }
        case OperationKind::Shape:
            image = drawShape(std::move(image), operation.shape);
            break;
        case OperationKind::Text:
            image = drawText(std::move(image), operation.text);
            break;
        }
    }
    return image;
}

QVector<ImageOperation> maskRenderOperations(const ImageLayerMaskData& mask) {
    QVector<ImageOperation> operations = mask.operations;
    for (auto& operation : operations) {
        if (operation.kind == OperationKind::EraseStroke) {
            operation.kind = OperationKind::PaintStroke;
            operation.paint_stroke.points = operation.erase_stroke.points;
            operation.paint_stroke.diameter = operation.erase_stroke.diameter;
            operation.paint_stroke.clipping_path = operation.erase_stroke.clipping_path;
            operation.paint_stroke.color = Qt::black;
        } else if (operation.kind == OperationKind::BucketFill) {
            const QColor color = operation.bucket_fill.color;
            const int gray = qGray(color.rgb());
            operation.bucket_fill.color = QColor(gray, gray, gray, color.alpha());
        } else if (operation.kind == OperationKind::LinearGradient) {
            const QColor color = operation.linear_gradient.color;
            const int gray = qGray(color.rgb());
            operation.linear_gradient.color = QColor(gray, gray, gray, color.alpha());
        }
    }
    return operations;
}

// Both images are premultiplied. Mask luminance already includes coverage at
// antialiased edges and transparent areas introduced by fixed-canvas transforms.
bool multiplyLayerMask(QImage* pixels, const QImage& mask,
                       const std::atomic_bool* cancellation_requested = nullptr) {
    ImageEditorPerformanceScope mask_scope(
        ImageEditorPerformanceMetrics::instance(),
        ImageEditorPerformanceStage::MaskApplication);
    if (pixels == nullptr || pixels->isNull() || mask.size() != pixels->size()) return false;
    *pixels = pixels->convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < pixels->height(); ++y) {
        if (exportWasCancelled(cancellation_requested)) return false;
        auto* row = reinterpret_cast<QRgb*>(pixels->scanLine(y));
        const auto* mask_row = reinterpret_cast<const QRgb*>(mask.constScanLine(y));
        for (int x = 0; x < pixels->width(); ++x) {
            const int value = qGray(mask_row[x]);
            const QRgb pixel = row[x];
            row[x] = qRgba((qRed(pixel) * value + 127) / 255,
                           (qGreen(pixel) * value + 127) / 255,
                           (qBlue(pixel) * value + 127) / 255,
                           (qAlpha(pixel) * value + 127) / 255);
        }
    }
    return true;
}

QImage renderGroup(const QHash<QString, QImage>& resources,
                   const ImageDocumentData& document,
                   const ImageGroupData& group,
                   const QSize& size,
                   const std::atomic_bool* cancellation_requested,
                   const QStringList& excluded_object_ids,
                   ImageLayerRasterCache* layer_raster_cache = nullptr);

QImage renderRasterLayer(const QHash<QString, QImage>& resources,
                         const ImageLayerData& layer,
                         const QSize& size,
                         const std::atomic_bool* cancellation_requested,
                         const QStringList& excluded_object_ids,
                         ImageLayerRasterCache* layer_raster_cache = nullptr) {
    ImageEditorPerformanceScope layer_scope(
        ImageEditorPerformanceMetrics::instance(),
        ImageEditorPerformanceStage::LayerComposition);
    if (exportWasCancelled(cancellation_requested)) return {};

    ImageLayerRasterCache::LookupState cache_state =
        ImageLayerRasterCache::LookupState::Bypass;
    std::optional<ImageEditorPerformanceScope> cache_scope;
    if (layer_raster_cache != nullptr && excluded_object_ids.isEmpty()) {
        const auto cached = layer_raster_cache->lookup(layer, size, resources);
        cache_state = cached.state;
        const auto stage = cache_state == ImageLayerRasterCache::LookupState::Hit
            ? ImageEditorPerformanceStage::LayerRasterCacheHit
            : (cache_state == ImageLayerRasterCache::LookupState::Miss
                ? ImageEditorPerformanceStage::LayerRasterCacheMiss
                : ImageEditorPerformanceStage::LayerRasterCacheBypass);
        cache_scope.emplace(ImageEditorPerformanceMetrics::instance(), stage);
        if (cache_state == ImageLayerRasterCache::LookupState::Hit) {
            return cached.pixels;
        }
    }

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
    pixels = applyOperations(std::move(pixels), operations, true, cancellation_requested, resources);
    if (layer.mask.has_value() && layer.mask->enabled && !layer.mask->operations.isEmpty() && !pixels.isNull()) {
        QImage mask(size, QImage::Format_ARGB32_Premultiplied);
        if (mask.isNull()) return {};
        mask.fill(Qt::white);
        mask = applyOperations(std::move(mask), maskRenderOperations(*layer.mask),
                               true, cancellation_requested);
        if (!multiplyLayerMask(&pixels, mask, cancellation_requested)) return {};
    }
    if (layer_raster_cache != nullptr &&
        cache_state == ImageLayerRasterCache::LookupState::Miss &&
        !pixels.isNull()) {
        layer_raster_cache->insert(layer, size, resources, pixels);
    }
    return pixels;
}

QImage renderGroup(const QHash<QString, QImage>& resources,
                   const ImageDocumentData& document,
                   const ImageGroupData& group,
                   const QSize& size,
                   const std::atomic_bool* cancellation_requested,
                   const QStringList& excluded_object_ids,
                   ImageLayerRasterCache* layer_raster_cache) {
    ImageEditorPerformanceScope group_scope(
        ImageEditorPerformanceMetrics::instance(),
        ImageEditorPerformanceStage::GroupComposition);
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
        QImage pixels = renderRasterLayer(resources,
            *layer, size, cancellation_requested, excluded_object_ids,
            layer_raster_cache);
        if (pixels.isNull() || exportWasCancelled(cancellation_requested)) {
            painter.end();
            return {};
        }
        painter.setOpacity(layer->opacity / 100.0);
        painter.drawImage(0, 0, pixels);
    }
    painter.end();
    QImage transformed = applyOperations(
        std::move(composite), group.operations, true, cancellation_requested, resources);
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

QImage renderComposite(const QHash<QString, QImage>& resources,
                           const QImage& source_image,
                       const ImageDocumentData& document,
                       const std::atomic_bool* cancellation_requested = nullptr,
                       const QStringList& excluded_object_ids = {},
                       ImageLayerRasterCache* layer_raster_cache = nullptr) {
    ImageEditorPerformanceScope composite_scope(
        ImageEditorPerformanceMetrics::instance(),
        ImageEditorPerformanceStage::Composite);
    if (source_image.isNull() || exportWasCancelled(cancellation_requested)) return {};

    QImage background = applyOperations(
        source_image, document.operations, false, cancellation_requested);
    if (background.isNull() || exportWasCancelled(cancellation_requested)) return {};

    QSize size = document.canvas_size;
    if (!size.isValid() || size.isEmpty()) size = background.size();
    QImage base;
    if (document.canvas_base_offset.isNull() && size == background.size()) {
        base = background.convertToFormat(QImage::Format_ARGB32);
    } else {
        base = QImage(size, QImage::Format_ARGB32);
        if (base.isNull()) return {};
        base.fill(document.base_kind == ImageBaseKind::Canvas
            ? document.canvas_background : QColor(0, 0, 0, 0));
        QPainter base_painter(&base);
        base_painter.setCompositionMode(QPainter::CompositionMode_Source);
        base_painter.drawImage(document.canvas_base_offset, background);
    }
    const auto* background_layer = document.layers.isEmpty()
        ? nullptr : &document.layers.front();
    QImage composite = background_layer != nullptr && background_layer->visible
        ? base
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
            pixels = renderGroup(resources, document, *group, size,
                                 cancellation_requested, excluded_object_ids,
                                 layer_raster_cache);
        } else {
            const auto* layer = findLayer(document, item.id);
            if (layer == nullptr || !layer->visible || layer->opacity == 0) continue;
            opacity = layer->opacity / 100.0;
            pixels = renderRasterLayer(resources,
                *layer, size, cancellation_requested, excluded_object_ids,
                layer_raster_cache);
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

QImage renderSelectedLayer(const QHash<QString, QImage>& resources,
                           const QImage& source_image,
                           const ImageDocumentData& document,
                           const QString& selected_layer_id,
                           const std::atomic_bool* cancellation_requested = nullptr) {
    ImageEditorPerformanceScope selected_layer_scope(
        ImageEditorPerformanceMetrics::instance(),
        ImageEditorPerformanceStage::SelectedLayerRender);
    if (source_image.isNull() || selected_layer_id.isEmpty() ||
        exportWasCancelled(cancellation_requested)) return {};

    const auto selected = std::find_if(
        document.layers.cbegin(), document.layers.cend(),
        [&selected_layer_id](const ImageLayerData& layer) {
            return layer.id == selected_layer_id;
        });
    if (selected == document.layers.cend()) return {};

    const QSize size = ImageDocumentRenderer::documentSize(document, source_image.size());
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
        QImage source = applyOperations(
            source_image, document.operations, false, cancellation_requested);
        pixels = QImage(size, QImage::Format_ARGB32);
        if (!pixels.isNull()) {
            pixels.fill(document.base_kind == ImageBaseKind::Canvas
                ? document.canvas_background : QColor(0, 0, 0, 0));
            QPainter painter(&pixels);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.drawImage(document.canvas_base_offset, source);
        }
    } else {
        pixels = QImage(size, QImage::Format_ARGB32_Premultiplied);
        if (pixels.isNull()) return {};
        pixels.fill(Qt::transparent);
        pixels = renderRasterLayer(resources, selected_layer, size,
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

QImage renderSelectedGroup(const QHash<QString, QImage>& resources,
                           const QImage& source_image,
                           const ImageDocumentData& document,
                           const QString& selected_group_id,
                           const std::atomic_bool* cancellation_requested = nullptr) {
    ImageEditorPerformanceScope selected_group_scope(
        ImageEditorPerformanceMetrics::instance(),
        ImageEditorPerformanceStage::SelectedGroupRender);
    if (source_image.isNull() || selected_group_id.isEmpty() ||
        exportWasCancelled(cancellation_requested)) return {};
    const auto* group = findGroup(document, selected_group_id);
    if (group == nullptr) return {};
    const QSize size = ImageDocumentRenderer::documentSize(document, source_image.size());
    if (!size.isValid() || size.isEmpty()) return {};
    return renderGroup(resources, document, *group, size, cancellation_requested, {});
}

QImage renderLayerThumbnail(const QHash<QString, QImage>& resources,
                            QImage image,
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
            scaled_operation.crop = fixed_canvas ? scaled_crop : scaled_crop.intersected(
                QRect(QPoint(0, 0), image.size()));
            if (scaled_operation.crop.isEmpty()) return {};
            if (!fixed_canvas) virtual_size = operation.crop.size();
            break;
        }
        case OperationKind::Rotate:
            if (operation.transform_bounds.isValid() && !operation.transform_bounds.isEmpty()) {
                scaled_operation.transform_bounds = QRect(
                    qRound(operation.transform_bounds.x() * scale_x),
                    qRound(operation.transform_bounds.y() * scale_y),
                    std::max(1, qRound(operation.transform_bounds.width() * scale_x)),
                    std::max(1, qRound(operation.transform_bounds.height() * scale_y)));
            }
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
        case OperationKind::BucketFill:
            scaled_operation.bucket_fill.seed = QPoint(
                static_cast<int>(std::floor(operation.bucket_fill.seed.x() * scale_x)),
                static_cast<int>(std::floor(operation.bucket_fill.seed.y() * scale_y)));
            if (operation.bucket_fill.clipping_path.has_value()) {
                scaled_operation.bucket_fill.clipping_path = QTransform::fromScale(
                    scale_x, scale_y).map(*operation.bucket_fill.clipping_path);
            }
            break;
        case OperationKind::LinearGradient:
            scaled_operation.linear_gradient.start.setX(
                operation.linear_gradient.start.x() * scale_x);
            scaled_operation.linear_gradient.start.setY(
                operation.linear_gradient.start.y() * scale_y);
            scaled_operation.linear_gradient.end.setX(
                operation.linear_gradient.end.x() * scale_x);
            scaled_operation.linear_gradient.end.setY(
                operation.linear_gradient.end.y() * scale_y);
            if (operation.linear_gradient.clipping_path.has_value()) {
                scaled_operation.linear_gradient.clipping_path = QTransform::fromScale(
                    scale_x, scale_y).map(*operation.linear_gradient.clipping_path);
            }
            break;
        case OperationKind::BlurStroke: {
            for (auto& point : scaled_operation.blur_stroke.points) {
                point.setX(point.x() * scale_x);
                point.setY(point.y() * scale_y);
            }
            const qreal scale = std::min(scale_x, scale_y);
            scaled_operation.blur_stroke.diameter = std::max(
                1, qRound(operation.blur_stroke.diameter * scale));
            scaled_operation.blur_stroke.radius = qRound(
                operation.blur_stroke.radius * scale);
            if (operation.blur_stroke.clipping_path.has_value()) {
                scaled_operation.blur_stroke.clipping_path = QTransform::fromScale(
                    scale_x, scale_y).map(*operation.blur_stroke.clipping_path);
            }
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
        case OperationKind::RasterImage:
            scaled_operation.raster.transform *= QTransform::fromScale(scale_x, scale_y);
            break;
        case OperationKind::Text:
            scaled_operation.text.position.setX(operation.text.position.x() * scale_x);
            scaled_operation.text.position.setY(operation.text.position.y() * scale_y);
            scaled_operation.text.box_width = std::max<qreal>(1.0,
                operation.text.box_width * scale_x);
            scaled_operation.text.font_pixel_size = std::max(1,
                qRound(operation.text.font_pixel_size * std::min(scale_x, scale_y)));
            break;
        case OperationKind::FlipHorizontal:
        case OperationKind::FlipVertical:
            if (operation.transform_bounds.isValid() && !operation.transform_bounds.isEmpty()) {
                scaled_operation.transform_bounds = QRect(
                    qRound(operation.transform_bounds.x() * scale_x),
                    qRound(operation.transform_bounds.y() * scale_y),
                    std::max(1, qRound(operation.transform_bounds.width() * scale_x)),
                    std::max(1, qRound(operation.transform_bounds.height() * scale_y)));
            }
            break;
        }
        image = applyOperations(std::move(image), {scaled_operation}, fixed_canvas, nullptr, resources);
    }

    return image.scaled(maximum_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace

QSize ImageDocumentRenderer::documentSize(const ImageDocumentData& document,
                                          const QSize& source_size) noexcept {
    if (document.canvas_size.isValid() && !document.canvas_size.isEmpty())
        return document.canvas_size;
    QSize size = document.source_size;
    if (!size.isValid() || size.isEmpty()) size = source_size;
    for (const auto& operation : document.operations) {
        if (operation.kind == OperationKind::Crop) size = operation.crop.size();
        else if (operation.kind == OperationKind::Rotate) size.transpose();
    }
    return size;
}

QImage ImageDocumentRenderer::composite(const ImageDocumentData& document,
    const QImage& source_image, const QHash<QString, QImage>& raster_images,
    const QStringList& excluded_object_ids,
    const std::atomic_bool* cancellation_requested,
    ImageLayerRasterCache* layer_raster_cache) {
    return renderComposite(raster_images, source_image, document,
                           cancellation_requested, excluded_object_ids,
                           layer_raster_cache);
}

QImage ImageDocumentRenderer::selectedLayer(const ImageDocumentData& document,
    const QImage& source_image, const QHash<QString, QImage>& raster_images,
    const QString& layer_id, const std::atomic_bool* cancellation_requested) {
    return renderSelectedLayer(raster_images, source_image, document, layer_id,
                               cancellation_requested);
}

QImage ImageDocumentRenderer::selectedGroup(const ImageDocumentData& document,
    const QImage& source_image, const QHash<QString, QImage>& raster_images,
    const QString& group_id, const std::atomic_bool* cancellation_requested) {
    return renderSelectedGroup(raster_images, source_image, document, group_id,
                               cancellation_requested);
}

QImage ImageDocumentRenderer::editableLayerTarget(
    const ImageDocumentData& document, const QImage& source_image,
    const QHash<QString, QImage>& raster_images, const QString& layer_id,
    bool mask_target) {
    if (source_image.isNull() || layer_id.isEmpty()) return {};
    const auto* layer = findLayer(document, layer_id);
    if (layer == nullptr || layer->background ||
        (mask_target && !layer->mask.has_value())) return {};
    const QSize size = documentSize(document, source_image.size());
    if (!size.isValid() || size.isEmpty()) return {};

    QImage pixels(size, QImage::Format_ARGB32_Premultiplied);
    if (pixels.isNull()) return {};
    pixels.fill(mask_target ? Qt::white : Qt::transparent);
    const auto& operations = mask_target
        ? maskRenderOperations(*layer->mask) : layer->operations;
    return applyOperations(std::move(pixels), operations, true, nullptr,
                           mask_target ? QHash<QString, QImage>{} : raster_images);
}

QImage ImageDocumentRenderer::layerThumbnail(const ImageDocumentData& document,
    const QImage& source_image, const QHash<QString, QImage>& raster_images,
    const ImageLayerData& layer, const QSize& maximum_size) {
    ImageEditorPerformanceScope thumbnail_scope(
        ImageEditorPerformanceMetrics::instance(),
        ImageEditorPerformanceStage::LayerThumbnail);
    if (maximum_size.width() <= 0 || maximum_size.height() <= 0) return {};
    const QSize canvas_size = documentSize(document, source_image.size());
    QImage thumbnail;
    if (layer.background) {
        const QImage base = applyOperations(source_image, document.operations, false);
        QImage canvas(canvas_size, QImage::Format_ARGB32);
        if (!canvas.isNull()) {
            canvas.fill(document.base_kind == ImageBaseKind::Canvas
                ? document.canvas_background : QColor(0, 0, 0, 0));
            QPainter painter(&canvas);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.drawImage(document.canvas_base_offset, base);
            painter.end();
            thumbnail = canvas.scaled(maximum_size, Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation);
        }
    } else {
        thumbnail = renderLayerThumbnail(raster_images, QImage{}, canvas_size,
            layer.operations, true, maximum_size, true);
    }
    if (layer.mask.has_value() && layer.mask->enabled && !thumbnail.isNull()) {
        QImage white(thumbnail.size(), QImage::Format_ARGB32_Premultiplied);
        white.fill(Qt::white);
        const QImage mask = renderLayerThumbnail(raster_images, white, canvas_size,
            maskRenderOperations(*layer.mask), true, maximum_size, false);
        if (!multiplyLayerMask(&thumbnail, mask)) thumbnail = {};
    }
    return thumbnail;
}

QImage ImageDocumentRenderer::groupThumbnail(const ImageDocumentData& document,
    const QHash<QString, QImage>& raster_images, const ImageGroupData& group,
    const QSize& maximum_size) {
    ImageEditorPerformanceScope thumbnail_scope(
        ImageEditorPerformanceMetrics::instance(),
        ImageEditorPerformanceStage::GroupThumbnail);
    if (maximum_size.width() <= 0 || maximum_size.height() <= 0) return {};
    const QImage rendered = renderGroup(raster_images, document, group,
        documentSize(document, document.source_size), nullptr, {});
    return rendered.isNull() ? QImage{}
        : rendered.scaled(maximum_size, Qt::KeepAspectRatio,
                          Qt::SmoothTransformation);
}

QHash<QString, QImage> ImageDocumentRenderer::maskThumbnails(
    const ImageDocumentData& document, const QImage& source_image,
    const QHash<QString, QImage>& raster_images, const QSize& maximum_size) {
    ImageEditorPerformanceScope thumbnail_scope(
        ImageEditorPerformanceMetrics::instance(),
        ImageEditorPerformanceStage::MaskThumbnail);
    QHash<QString, QImage> thumbnails;
    if (source_image.isNull() || maximum_size.isEmpty()) return thumbnails;
    const QSize size = documentSize(document, source_image.size());
    const QSize small_size = size.scaled(maximum_size, Qt::KeepAspectRatio);
    if (small_size.isEmpty()) return thumbnails;
    QImage white(small_size, QImage::Format_ARGB32_Premultiplied);
    white.fill(Qt::white);
    for (const auto& layer : document.layers) {
        if (!layer.mask.has_value()) continue;
        thumbnails.insert(layer.id, renderLayerThumbnail(raster_images, white, size,
            maskRenderOperations(*layer.mask), true, maximum_size, false));
    }
    return thumbnails;
}

} // namespace image_editor
