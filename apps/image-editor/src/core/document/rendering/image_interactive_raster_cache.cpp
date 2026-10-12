#include "image_interactive_raster_cache.h"
#include "image_document_renderer.h"
#include "image_document_utils.h"
#include "image_editor_performance_metrics.h"
#include <QPainter>
#include <algorithm>

namespace image_editor {
namespace {
bool local(const ImageOperation& op) {
    return op.kind == OperationKind::PaintStroke || op.kind == OperationKind::EraseStroke ||
        op.kind == OperationKind::Shape || op.kind == OperationKind::Text || op.kind == OperationKind::RasterImage;
}
QRect strokeBounds(const ImageOperation& op) {
    const auto& points = op.kind == OperationKind::PaintStroke ? op.paint_stroke.points : op.erase_stroke.points;
    const int diameter = op.kind == OperationKind::PaintStroke ? op.paint_stroke.diameter : op.erase_stroke.diameter;
    if (points.isEmpty()) return {};
    double left = points.front().x(), right = left, top = points.front().y(), bottom = top;
    for (const auto& point : points) {
        left = std::min(left, point.x()); right = std::max(right, point.x());
        top = std::min(top, point.y()); bottom = std::max(bottom, point.y());
    }
    const double margin = diameter / 2.0 + 2.0;
    return QRectF(QPointF(left-margin, top-margin), QPointF(right+margin, bottom+margin)).toAlignedRect();
}
bool cancelled(const std::atomic_bool* cancel) { return cancel && cancel->load(); }
}
void ImageInteractiveRasterCache::clear() {
    entries_.clear(); revisions_.clear(); background_ = {}; background_snapshot_ = {}; document_id_.clear(); bytes_ = 0;
}
void ImageInteractiveRasterCache::setByteLimit(std::uint64_t limit) {
    byte_limit_ = std::min(limit, maximum_bytes);
    while (bytes_ > byte_limit_ && !entries_.isEmpty()) {
        auto victim=std::min_element(entries_.begin(),entries_.end(),[](const Entry& a,const Entry& b){return a.used<b.used;});
        bytes_-=victim->pixels.sizeInBytes(); entries_.erase(victim);
    }
    if (bytes_ > byte_limit_ && !background_.isNull()) { bytes_-=background_.sizeInBytes(); background_={}; }
}
InteractiveRaster ImageInteractiveRasterCache::raster(const QString& identity, const QSize& size,
    const QVector<ImageOperation>& operations, const QHash<QString, QImage>& resources,
    bool mask, bool transient, const std::atomic_bool* cancel) {
    QHash<QString, qint64> signatures;
    for (const auto& op : operations) if (op.kind == OperationKind::RasterImage)
        signatures.insert(op.raster.id, resources.value(op.raster.id, resources.value(op.raster.source_path)).cacheKey());
    const auto old_revision = revisions_.constFind(identity);
    const bool unchanged = old_revision != revisions_.cend() && old_revision->resources == signatures && old_revision->operations == operations;
    const qint64 revision = unchanged ? old_revision->revision : ++next_revision_;
    if (!transient) revisions_.insert(identity, {operations, signatures, {}, 0, revision});
    auto found = entries_.find(identity);
    if (found != entries_.end() && found->pixels.size() == size && found->resources == signatures && found->operations == operations) {
        found->used = ++clock_;
        return {identity, found->pixels, found->revision, {}, revision};
    }
    QImage previous; QRect dirty; qint64 previous_key = 0;
    if (found != entries_.end() && found->pixels.size() == size && found->resources == signatures &&
        std::all_of(operations.cbegin(), operations.cend(), local) &&
        std::all_of(found->operations.cbegin(), found->operations.cend(), local)) {
        const auto& before = found->operations;
        qsizetype common = 0;
        while (common < before.size() && common < operations.size() && before[common] == operations[common]) ++common;
        bool bounded = true;
        for (const auto* list : {&before, &operations}) for (qsizetype i = common; i < list->size(); ++i) {
            const auto& op = list->at(i);
            if (op.kind != OperationKind::PaintStroke && op.kind != OperationKind::EraseStroke) { bounded = false; break; }
            dirty = dirty.united(strokeBounds(op));
        }
        dirty = dirty.intersected(QRect(QPoint(), size));
        if (bounded && !dirty.isEmpty()) { previous = found->pixels; previous_key = found->revision; }
        else dirty = {};
    }
    QImage pixels = ImageDocumentRenderer::rasterize(size, operations, resources, mask, std::move(previous), dirty, cancel);
    if (pixels.isNull() || cancelled(cancel)) return {};
    if (mask) {
        QImage coverage(size,QImage::Format_Grayscale8);
        if (coverage.isNull()) return {};
        for (int y=0;y<size.height();++y) {
            if (cancelled(cancel)) return {};
            auto* target=coverage.scanLine(y); const auto* source=reinterpret_cast<const QRgb*>(pixels.constScanLine(y));
            for(int x=0;x<size.width();++x) target[x]=static_cast<uchar>(qGray(source[x]));
        }
        pixels=std::move(coverage);
    }
    reconstructed_pixels_ += dirty.isEmpty() ? std::uint64_t(size.width()) * size.height() : std::uint64_t(dirty.width()) * dirty.height();
    if (!transient) {
        if (found != entries_.end()) { bytes_ -= found->pixels.sizeInBytes(); entries_.erase(found); }
        const auto requested = std::uint64_t(pixels.sizeInBytes());
        while (requested <= byte_limit_ && bytes_ + requested > byte_limit_ && !entries_.isEmpty()) {
            auto victim = std::min_element(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) { return a.used < b.used; });
            bytes_ -= victim->pixels.sizeInBytes(); entries_.erase(victim);
        }
        if (requested <= byte_limit_ && bytes_ + requested <= byte_limit_) {
            entries_.insert(identity, {operations, signatures, pixels, ++clock_, revision}); bytes_ += requested;
        }
    }
    return {identity, std::move(pixels), previous_key, dirty, revision};
}
InteractiveComposition ImageInteractiveRasterCache::prepare(const ImageExportSnapshot& snapshot,
    const QString& document_id, bool transient, const std::atomic_bool* cancel) {
    if (document_id_ != document_id) { clear(); document_id_ = document_id; }
    const auto& doc = snapshot.document;
    const QSize size = ImageDocumentRenderer::documentSize(doc, snapshot.source_image.size());
    if (snapshot.source_image.isNull() || cancelled(cancel)) return {};
    InteractiveComposition result; result.size = size;
    QSet<QString> live;
    for (const auto& layer : doc.layers) { live.insert(layer.id + "/content"); live.insert(layer.id + "/mask"); }
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (!live.contains(it.key())) { bytes_ -= it->pixels.sizeInBytes(); it = entries_.erase(it); } else ++it;
    }
    for (auto it = revisions_.begin(); it != revisions_.end();) {
        if (!live.contains(it.key())) it = revisions_.erase(it); else ++it;
    }
    const bool background_changed = background_.isNull() || background_.size() != size ||
        background_snapshot_.source_image.cacheKey() != snapshot.source_image.cacheKey() ||
        background_snapshot_.document.operations != doc.operations ||
        background_snapshot_.document.canvas_base_offset != doc.canvas_base_offset ||
        background_snapshot_.document.canvas_background != doc.canvas_background;
    if (!doc.layers.isEmpty() && doc.layers.front().visible) {
        if (background_changed) {
            if (!background_.isNull()) bytes_ -= background_.sizeInBytes();
            background_ = ImageDocumentRenderer::selectedLayer(doc, snapshot.source_image, snapshot.raster_images, doc.layers.front().id, cancel)
                .convertToFormat(QImage::Format_ARGB32_Premultiplied);
            if (background_.isNull()) return {};
            background_revision_ = ++next_revision_;
            background_snapshot_ = snapshot;
            // Only retain background if it fits the same cache budget.
            while (!entries_.isEmpty() && bytes_ + std::uint64_t(background_.sizeInBytes()) > byte_limit_) {
                auto it = entries_.begin(); bytes_ -= it->pixels.sizeInBytes(); entries_.erase(it);
            }
            if (std::uint64_t(background_.sizeInBytes()) <= byte_limit_) bytes_ += background_.sizeInBytes();
        }
        InteractiveNode base;
        base.layers.append({{document_id + "/background", background_, 0, {}, background_revision_}, {}, 100});
        result.nodes.append(std::move(base));
        if (std::uint64_t(background_.sizeInBytes()) > byte_limit_) background_ = {};
    }
    const auto layer_input = [&](const ImageLayerData& layer) {
        InteractiveLayer value;
        value.content = raster(layer.id + "/content", size, layer.operations, snapshot.raster_images, false, transient, cancel);
        value.content.identity = document_id + "/" + value.content.identity;
        if (layer.mask && layer.mask->enabled && !layer.mask->operations.isEmpty()) {
            value.mask = raster(layer.id + "/mask", size, layer.mask->operations, {}, true, transient, cancel);
            value.mask.identity = document_id + "/" + value.mask.identity;
            if (value.mask.pixels.isNull()) value.content.pixels = {};
        }
        value.opacity = layer.opacity; return value;
    };
    for (qsizetype i = 1; i < doc.root_stack.size(); ++i) {
        const auto& item = doc.root_stack[i]; InteractiveNode node;
        if (item.group) {
            const auto* group = findGroup(doc, item.id);
            if (!group || !group->visible || group->opacity == 0) continue;
            node.group = true; node.opacity = group->opacity; node.transforms = group->operations;
            for (const auto& id : group->layer_ids) {
                const auto* layer = findLayer(doc, id);
                if (layer && layer->visible && layer->opacity > 0) node.layers.append(layer_input(*layer));
            }
        } else {
            const auto* layer = findLayer(doc, item.id);
            if (!layer || !layer->visible || layer->opacity == 0) continue;
            node.layers.append(layer_input(*layer));
        }
        if (cancelled(cancel)) return {};
        for (const auto& layer : node.layers) if (layer.content.pixels.isNull()) return {};
        result.nodes.append(std::move(node));
    }
    return result;
}
QImage ImageInteractiveRasterCache::composite(const InteractiveComposition& composition, const std::atomic_bool* cancel) {
    ImageEditorPerformanceScope scope(ImageEditorPerformanceMetrics::instance(),ImageEditorPerformanceStage::Composite);
    QImage output(composition.size, QImage::Format_ARGB32);
    if (output.isNull()) return {}; output.fill(Qt::transparent);
    for (const auto& node : composition.nodes) {
        if (cancelled(cancel)) return {};
        QImage group;
        if (node.group) { group = QImage(composition.size, QImage::Format_ARGB32_Premultiplied); if (group.isNull()) return {}; group.fill(Qt::transparent); }
        QPainter painter(node.group ? &group : &output);
        for (const auto& layer : node.layers) {
            QImage pixels = layer.content.pixels;
            if (!layer.mask.pixels.isNull()) {
                for (int y = 0; y < pixels.height(); ++y) {
                    if (cancelled(cancel)) return {};
                    auto* row = reinterpret_cast<QRgb*>(pixels.scanLine(y));
                    const auto* mask = layer.mask.pixels.constScanLine(y);
                    for (int x = 0; x < pixels.width(); ++x) {
                        const int coverage = layer.mask.pixels.format()==QImage::Format_Grayscale8 ? mask[x] : qGray(reinterpret_cast<const QRgb*>(mask)[x]); const auto pixel = row[x];
                        row[x] = qRgba((qRed(pixel)*coverage+127)/255, (qGreen(pixel)*coverage+127)/255,
                            (qBlue(pixel)*coverage+127)/255, (qAlpha(pixel)*coverage+127)/255);
                    }
                }
            }
            painter.setOpacity(layer.opacity / 100.0); painter.drawImage(0, 0, pixels);
        }
        painter.end();
        if (node.group) {
            group = ImageDocumentRenderer::transformRaster(std::move(group), node.transforms, cancel);
            if (group.isNull()) return {};
            if (node.opacity != 100) {
                QImage faded(composition.size,QImage::Format_ARGB32_Premultiplied); if (faded.isNull()) return {}; faded.fill(Qt::transparent);
                QPainter opacity(&faded); opacity.setOpacity(node.opacity/100.0); opacity.drawImage(0,0,group); opacity.end();
                group=std::move(faded);
            }
            QPainter target(&output); target.drawImage(0, 0, group);
        }
    }
    return output.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}
} // namespace image_editor
