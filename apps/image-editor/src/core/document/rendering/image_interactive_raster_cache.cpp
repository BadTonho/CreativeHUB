#include "image_layer_adjustment.h"
#include "image_layer_clipping.h"
#include "image_interactive_raster_cache.h"
#include "image_layer_outline.h"
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
    bool mask, bool transient, const std::atomic_bool* cancel,const std::optional<QPainterPath>& initial_selection) {
    QHash<QString, qint64> signatures;
    for (const auto& op : operations) if (op.kind == OperationKind::RasterImage)
        signatures.insert(op.raster.id, resources.value(op.raster.id, resources.value(op.raster.source_path)).cacheKey());
    const auto old_revision = revisions_.constFind(identity);
    const bool unchanged = old_revision != revisions_.cend() && old_revision->resources == signatures && old_revision->operations == operations && old_revision->initial_selection==initial_selection;
    const qint64 revision = unchanged ? old_revision->revision : ++next_revision_;
    if (!transient) {Entry entry;entry.operations=operations;entry.resources=signatures;entry.revision=revision;entry.initial_selection=initial_selection;revisions_.insert(identity,std::move(entry));}
    auto found = entries_.find(identity);
    if (found != entries_.end() && found->pixels.size() == size && found->resources == signatures && found->operations == operations && found->initial_selection==initial_selection) {
        found->used = ++clock_;
        return {identity, found->pixels, found->revision, {}, revision};
    }
    QImage previous; QRect dirty; qint64 previous_key = 0;
    if (!initial_selection && found != entries_.end() && found->pixels.size() == size && found->resources == signatures &&
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
    QImage pixels = ImageDocumentRenderer::rasterize(size, operations, resources, mask, std::move(previous), dirty, cancel,initial_selection);
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
            Entry entry;entry.operations=operations;entry.resources=signatures;entry.pixels=pixels;entry.used=++clock_;entry.revision=revision;entry.initial_selection=initial_selection;entries_.insert(identity,std::move(entry)); bytes_ += requested;
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
    InteractiveComposition result; result.size = size;result.adjustment_document=std::any_of(doc.layers.cbegin(),doc.layers.cend(),[](const auto& l){return needsAdjustmentPrecision(l);});
    QSet<QString> live;
    for (const auto& layer : doc.layers) {
        if (!layer.clip_to_base && clippingChain(doc, layer.id).size() > 1) live.insert(layer.id + "/clipping");
        live.insert(layer.id + "/content"); live.insert(layer.id + "/mask");
        if (layer.outline && layer.outline->enabled) live.insert(layer.id + "/outline");
    }
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
    const bool clipped_background = !doc.layers.isEmpty() && clippingChain(doc, doc.layers.front().id).size() > 1;
    if (clipped_background && !background_.isNull()) { bytes_ -= background_.sizeInBytes(); background_ = {}; }
    if (!doc.layers.isEmpty() && doc.layers.front().visible && !clipped_background) {
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
        QHash<QString,qint64> outline_resources;
        if (layer.outline && layer.outline->enabled) {
            for (const auto& op : layer.operations) if (op.kind == OperationKind::RasterImage)
                outline_resources.insert(op.raster.id, snapshot.raster_images.value(op.raster.id,
                    snapshot.raster_images.value(op.raster.source_path)).cacheKey());
            auto cached = entries_.find(layer.id + "/outline");
            if (!transient && cached != entries_.end() && cached->pixels.size() == size &&
                cached->operations == layer.operations && cached->resources == outline_resources &&
                cached->outline == layer.outline && cached->layer_mask == layer.mask) {
                cached->used = ++clock_;
                value.content = {document_id + "/" + layer.id + "/outline", cached->pixels, 0, {}, cached->revision};
                value.opacity = layer.opacity; value.blend_mode = layer.blend_mode; return value;
            }
        }
        if(layer.adjustment){
            value.adjustment=layer.adjustment;value.opacity=layer.opacity;value.blend_mode=layer.blend_mode;
            if(layer.mask && layer.mask->enabled && (!layer.mask->operations.isEmpty() || layer.mask->initial_selection)) {
                value.mask=raster(layer.id+"/mask",size,layer.mask->operations,{},true,transient,cancel,layer.mask->initial_selection);
                value.mask.identity=document_id+"/"+value.mask.identity;
                if(value.mask.pixels.isNull())value.adjustment.reset();
            }
            return value;
        }
        value.content = raster(layer.id + "/content", size, layer.operations, snapshot.raster_images, false, transient, cancel);
        value.content.identity = document_id + "/" + value.content.identity;
        if (layer.mask && layer.mask->enabled && (!layer.mask->operations.isEmpty() || layer.mask->initial_selection)) {
            value.mask = raster(layer.id + "/mask", size, layer.mask->operations, {}, true, transient, cancel,layer.mask->initial_selection);
            value.mask.identity = document_id + "/" + value.mask.identity;
            if (value.mask.pixels.isNull()) value.content.pixels = {};
        }
        if (layer.outline && layer.outline->enabled && !value.content.pixels.isNull()) {
            const QString key = layer.id + "/outline";
            auto found = entries_.find(key);
            const qint64 content_revision = value.content.revision, mask_revision = value.mask.revision;
            InteractiveRaster styled;
            styled.identity = document_id + "/" + key;
            if (!transient && found != entries_.end() && found->pixels.size() == size &&
                found->content_revision == content_revision && found->mask_revision == mask_revision &&
                found->outline == layer.outline) {
                found->used = ++clock_; styled.pixels = found->pixels; styled.revision = found->revision;
            } else {
                QImage pixels = value.content.pixels;
                if (!value.mask.pixels.isNull()) {
                    for (int y=0; y<size.height(); ++y) {
                        if (cancelled(cancel)) { value.content.pixels = {}; return value; }
                        auto* row = reinterpret_cast<QRgb*>(pixels.scanLine(y));
                        const auto* coverage = value.mask.pixels.constScanLine(y);
                        for (int x=0; x<size.width(); ++x) {
                            const int a = coverage[x]; const auto p = row[x];
                            row[x] = qRgba((qRed(p)*a+127)/255, (qGreen(p)*a+127)/255,
                                (qBlue(p)*a+127)/255, (qAlpha(p)*a+127)/255);
                        }
                    }
                }
                styled.pixels = applyLayerOutline(pixels, *layer.outline, cancel);
                styled.revision = ++next_revision_;
                // Release source planes before admitting the styled result so the
                // temporary pair does not evict another unchanged outlined layer.
                if (!transient) for (const auto& suffix : {QStringLiteral("/content"), QStringLiteral("/mask")}) {
                    const auto original = entries_.find(layer.id + suffix);
                    if (original != entries_.end()) { bytes_ -= original->pixels.sizeInBytes(); entries_.erase(original); }
                }
                if (!transient && !styled.pixels.isNull()) {
                    if (found != entries_.end()) { bytes_ -= found->pixels.sizeInBytes(); entries_.erase(found); }
                    const auto requested = std::uint64_t(styled.pixels.sizeInBytes());
                    while (requested <= byte_limit_ && bytes_ + requested > byte_limit_ && !entries_.isEmpty()) {
                        auto victim = std::min_element(entries_.begin(), entries_.end(),
                            [](const Entry& a, const Entry& b) { return a.used < b.used; });
                        bytes_ -= victim->pixels.sizeInBytes(); entries_.erase(victim);
                    }
                    if (requested <= byte_limit_ && bytes_ + requested <= byte_limit_) {
                        Entry entry; entry.pixels = styled.pixels; entry.revision = styled.revision;
                        entry.used = ++clock_; entry.outline = layer.outline;
                        entry.operations = layer.operations; entry.resources = outline_resources; entry.layer_mask = layer.mask;
                        entry.content_revision = content_revision; entry.mask_revision = mask_revision;
                        entries_.insert(key, std::move(entry)); bytes_ += requested;
                    }
                }
            }
            value.content = std::move(styled);
            value.mask = {};
        }
        value.opacity = layer.opacity; value.blend_mode = layer.blend_mode; return value;
    };
    const auto chain_input = [&](const ImageLayerData& base) {
        const auto ids = clippingChain(doc, base.id);
        if (ids.size() == 1) return layer_input(base);
        const QString key = base.id + "/clipping";
        ImageDocumentData signature = doc;
        signature.layers.clear(); signature.groups.clear(); signature.root_stack.clear();
        if (!base.background) signature.operations.clear();
        QHash<QString,qint64> resources;
        if (base.background) resources.insert(QStringLiteral("background"), snapshot.source_image.cacheKey());
        for (const auto& id : ids) {
            const auto* layer = findLayer(doc, id);
            signature.layers.append(*layer);
            for (const auto& op : layer->operations) if (op.kind == OperationKind::RasterImage)
                resources.insert(op.raster.id, snapshot.raster_images.value(op.raster.id,
                    snapshot.raster_images.value(op.raster.source_path)).cacheKey());
        }
        signature.layers[0].blend_mode = creative_suite::composition::BlendMode::Normal;
        InteractiveLayer input; input.opacity = base.opacity; input.blend_mode = base.blend_mode;
        input.content.identity = document_id + "/" + key;
        const auto hit = entries_.find(key);
        if (hit != entries_.end() && hit->pixels.size() == size &&
            hit->clipping_signature == signature && hit->resources == resources) {
            hit->used = ++clock_; input.content.pixels = hit->pixels;
            input.content.revision = hit->revision; return input;
        }
        input.content.pixels = ImageDocumentRenderer::layerChain(doc, snapshot.source_image,
            snapshot.raster_images, base.id, cancel, [&](const ImageLayerData& layer) {
                if (layer.background) return ImageDocumentRenderer::selectedLayer(doc,snapshot.source_image,
                    snapshot.raster_images,layer.id,cancel);
                const auto member=layer_input(layer);
                if(layer.adjustment)return member.mask.pixels;
                QImage pixels=member.content.pixels;
                if (!member.mask.pixels.isNull() && !pixels.isNull()) {
                    for (int y=0;y<size.height();++y) {
                        if (cancelled(cancel)) return QImage{};
                        auto* row=reinterpret_cast<QRgb*>(pixels.scanLine(y));
                        const auto* mask=member.mask.pixels.constScanLine(y);
                        for (int x=0;x<size.width();++x) {
                            const int a=mask[x]; const auto p=row[x];
                            row[x]=qRgba((qRed(p)*a+127)/255,(qGreen(p)*a+127)/255,
                                (qBlue(p)*a+127)/255,(qAlpha(p)*a+127)/255);
                        }
                    }
                }
                return pixels;
            });
        if (input.content.pixels.isNull() || cancelled(cancel)) { input.content.pixels = {}; return input; }
        const auto previous = revisions_.constFind(key);
        input.content.revision = previous != revisions_.cend() && previous->clipping_signature == signature &&
            previous->resources == resources ? previous->revision : ++next_revision_;
        if (!transient) {
            Entry entry; entry.clipping_signature = std::move(signature); entry.resources = resources;
            entry.revision = input.content.revision;
            revisions_.insert(key, entry);
            // Normal chains retain only their finished pixels. Blended children
            // keep available member rasters for mode edits within the same budget.
            const bool keep_members=std::any_of(entry.clipping_signature->layers.cbegin()+1,entry.clipping_signature->layers.cend(),
                [](const auto& layer){return layer.adjustment || layer.blend_mode!=creative_suite::composition::BlendMode::Normal;});
            for (const auto& id : ids) for (const auto& suffix : {"/content", "/mask", "/outline", "/clipping"}) {
                if (keep_members && QString::fromLatin1(suffix) != QStringLiteral("/clipping")) continue;
                const auto old = entries_.find(id + suffix);
                if (old != entries_.end()) { bytes_ -= old->pixels.sizeInBytes(); entries_.erase(old); }
            }
            const auto requested = std::uint64_t(input.content.pixels.sizeInBytes());
            while (requested <= byte_limit_ && bytes_ + requested > byte_limit_ && !entries_.isEmpty()) {
                auto victim = std::min_element(entries_.begin(), entries_.end(),
                    [](const Entry& a, const Entry& b) { return a.used < b.used; });
                bytes_ -= victim->pixels.sizeInBytes(); entries_.erase(victim);
            }
            if (requested <= byte_limit_ && bytes_ + requested <= byte_limit_) {
                entry.pixels = input.content.pixels; entry.used = ++clock_;
                entries_.insert(key, std::move(entry)); bytes_ += requested;
            }
        }
        return input;
    };
    for (qsizetype i = 0; i < doc.root_stack.size(); ++i) {
        const auto& item = doc.root_stack[i]; InteractiveNode node;node.id=item.id;
        if (item.group) {
            const auto* group = findGroup(doc, item.id);
            if (!group || !group->visible || group->opacity == 0) continue;
            node.group = true; node.opacity = group->opacity; node.blend_mode = group->blend_mode; node.transforms = group->operations;
            for (const auto& id : group->layer_ids) {
                const auto* layer = findLayer(doc, id);
                if (layer && !layer->clip_to_base && layer->visible && layer->opacity > 0) {
                    auto input = chain_input(*layer);
                    if (!input.adjustment && input.content.pixels.isNull()) return {};
                    node.layers.append(std::move(input));
                }
            }
        } else {
            const auto* layer = findLayer(doc, item.id);
            if (!layer || layer->clip_to_base || !layer->visible || layer->opacity == 0 ||
                (layer->background && !clipped_background)) continue;
            auto input = chain_input(*layer);
            if (!input.adjustment && input.content.pixels.isNull()) return {};
            node.layers.append(std::move(input));
        }
        if (cancelled(cancel)) return {};
        for (const auto& layer : node.layers) if (!layer.adjustment && layer.content.pixels.isNull()) return {};
        result.nodes.append(std::move(node));
    }
    return result;
}
QImage ImageInteractiveRasterCache::composite(const InteractiveComposition& composition, const std::atomic_bool* cancel) {
    ImageEditorPerformanceScope scope(ImageEditorPerformanceMetrics::instance(),ImageEditorPerformanceStage::Composite);
    QImage output(composition.size, composition.adjustment_document?QImage::Format_ARGB32_Premultiplied:QImage::Format_ARGB32);
    if (output.isNull()) return {}; output.fill(Qt::transparent);
    for (const auto& node : composition.nodes) {
        if (cancelled(cancel)) return {};
        QImage group;
        if (node.group) { group = QImage(composition.size, QImage::Format_ARGB32_Premultiplied); if (group.isNull()) return {}; group.fill(Qt::transparent); }
        for (const auto& layer : node.layers) {
            if(layer.adjustment){if(!applyLayerAdjustment(node.group?&group:&output,*layer.adjustment,layer.mask.pixels,layer.blend_mode,layer.opacity,cancel))return {};continue;}
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
            if (!blendLayerPixels(node.group ? &group : &output,pixels,layer.blend_mode,layer.opacity,cancel)) return {};
        }
        if (node.group) {
            group = ImageDocumentRenderer::transformRaster(std::move(group), node.transforms, cancel);
            if (group.isNull()) return {};
            if (node.opacity != 100) {
                QImage faded(composition.size,QImage::Format_ARGB32_Premultiplied); if (faded.isNull()) return {}; faded.fill(Qt::transparent);
                QPainter opacity(&faded); opacity.setOpacity(node.opacity/100.0); opacity.drawImage(0,0,group); opacity.end();
                group=std::move(faded);
            }
            if (!blendLayerPixels(&output,group,node.blend_mode,100,cancel)) return {};
        }
    }
    return output.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}
} // namespace image_editor
