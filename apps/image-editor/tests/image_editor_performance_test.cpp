#include "image_editor_performance_log.h"
#include "image_editor_performance_metrics.h"
#include "image_document_session.h"
#include "rendering/image_document_renderer.h"
#include "import_export/image_exporter.h"

#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTransform>
#include <QtTest>

using namespace image_editor;

class ImageEditorPerformanceTests final : public QObject {
    Q_OBJECT

private slots:
    void cleanup() {
        auto& metrics = ImageEditorPerformanceMetrics::instance();
        metrics.setEnabled(false);
        metrics.reset();
    }

    void collectionIsDisabledByDefaultAndCanBeEnabled() {
        auto& metrics = ImageEditorPerformanceMetrics::instance();
        metrics.setEnabled(false);
        metrics.reset();
        metrics.record(ImageEditorPerformanceStage::Composite, 100);
        QCOMPARE(metrics.snapshot().timing(ImageEditorPerformanceStage::Composite).count,
                 std::uint64_t{0});

        metrics.setEnabled(true);
        metrics.record(ImageEditorPerformanceStage::Composite, 100);
        const auto snapshot = metrics.snapshot();
        QCOMPARE(snapshot.timing(ImageEditorPerformanceStage::Composite).count,
                 std::uint64_t{1});
        QCOMPARE(snapshot.timing(ImageEditorPerformanceStage::Composite).maximum_nanoseconds,
                 std::uint64_t{100});
    }

    void timingSummaryReportsPercentiles() {
        auto& metrics = ImageEditorPerformanceMetrics::instance();
        metrics.reset();
        metrics.setEnabled(true);
        for (std::uint64_t value = 1; value <= 5; ++value)
            metrics.record(ImageEditorPerformanceStage::CanvasPaint, value);

        const auto timing = metrics.snapshot().timing(
            ImageEditorPerformanceStage::CanvasPaint);
        QCOMPARE(timing.count, std::uint64_t{5});
        QCOMPARE(timing.total_nanoseconds, std::uint64_t{15});
        QCOMPARE(timing.maximum_nanoseconds, std::uint64_t{5});
        QCOMPARE(timing.p50_nanoseconds, std::uint64_t{3});
        QCOMPARE(timing.p95_nanoseconds, std::uint64_t{5});
    }

    void sampleStorageRemainsBounded() {
        auto& metrics = ImageEditorPerformanceMetrics::instance();
        metrics.reset();
        metrics.setEnabled(true);
        constexpr auto count = ImageEditorPerformanceMetrics::maximum_samples_per_stage + 2;
        for (std::size_t value = 1; value <= count; ++value)
            metrics.record(ImageEditorPerformanceStage::ExportEncode, value);

        const auto timing = metrics.snapshot().timing(
            ImageEditorPerformanceStage::ExportEncode);
        QCOMPARE(timing.count, static_cast<std::uint64_t>(count));
        QCOMPARE(timing.maximum_nanoseconds, static_cast<std::uint64_t>(count));
        QCOMPARE(timing.p50_nanoseconds, std::uint64_t{1026});
        QCOMPARE(timing.p95_nanoseconds, std::uint64_t{1948});
    }

    void snapshotSerializesNamedStages() {
        auto& metrics = ImageEditorPerformanceMetrics::instance();
        metrics.reset();
        metrics.setEnabled(true);
        metrics.record(ImageEditorPerformanceStage::MaskApplication, 4'000'000);
        const auto json = imageEditorPerformanceSnapshotToJson(metrics.snapshot());
        const auto stages = json.value(QStringLiteral("stages")).toObject();
        QVERIFY(stages.contains(QStringLiteral("mask_application")));
        const auto mask = stages.value(QStringLiteral("mask_application")).toObject();
        QCOMPARE(mask.value(QStringLiteral("count")).toInt(), 1);
        QCOMPARE(mask.value(QStringLiteral("maximum_ms")).toDouble(), 4.0);
        QVERIFY(mask.contains(QStringLiteral("p95_ms")));
    }

    void enablingMetricsDoesNotChangeRenderedOrExportedPixels() {
        ImageDocumentData document;
        document.base_kind = ImageBaseKind::SourceImage;
        document.source_size = QSize(48, 32);
        document.canvas_size = document.source_size;
        QImage source(document.source_size, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < source.height(); ++y) {
            for (int x = 0; x < source.width(); ++x)
                source.setPixelColor(x, y, QColor(x * 5, y * 7, (x + y) * 3, 255));
        }

        auto& metrics = ImageEditorPerformanceMetrics::instance();
        metrics.setEnabled(false);
        metrics.reset();
        const auto baseline = ImageDocumentRenderer::composite(document, source, {});
        QVERIFY(!baseline.isNull());

        metrics.setEnabled(true);
        const auto measured = ImageDocumentRenderer::composite(document, source, {});
        QCOMPARE(measured, baseline);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        ImageExportSnapshot snapshot;
        snapshot.source_image = source;
        snapshot.document = document;
        for (const QString& extension : {QStringLiteral("png"), QStringLiteral("jpg")}) {
            ImageExportOptions options;
            const QString baseline_path = directory.filePath(
                QStringLiteral("baseline.%1").arg(extension));
            const QString measured_path = directory.filePath(
                QStringLiteral("measured.%1").arg(extension));
            metrics.setEnabled(false);
            QCOMPARE(exportImageSnapshot(snapshot, baseline_path, options).status,
                     ImageExportStatus::Succeeded);
            metrics.setEnabled(true);
            QCOMPARE(exportImageSnapshot(snapshot, measured_path, options).status,
                     ImageExportStatus::Succeeded);
            const QImage baseline_export(baseline_path);
            const QImage measured_export(measured_path);
            QVERIFY(!baseline_export.isNull());
            QCOMPARE(measured_export, baseline_export);
        }
        QVERIFY(metrics.snapshot().timing(ImageEditorPerformanceStage::ExportEncode).count > 0);
    }

    void groupThumbnailCacheReusesAndInvalidatesResults() {
        ImageDocumentSession session;
        QString error;
        QVERIFY2(session.createCanvas(QSize(32, 32), Qt::transparent, &error),
                 qPrintable(error));
        const QString first_layer = session.selectedLayerId();
        QVERIFY2(session.applyPaintStroke({QPointF(8, 16)}, Qt::red, 8, &error),
                 qPrintable(error));
        const QString second_layer = session.addLayer();
        QVERIFY(!second_layer.isEmpty());
        QVERIFY2(session.applyPaintStroke({QPointF(24, 16)}, Qt::blue, 8, &error),
                 qPrintable(error));
        const QString group_id = session.groupLayers(
            {first_layer, second_layer}, &error);
        QVERIFY2(!group_id.isEmpty(), qPrintable(error));

        auto& metrics = ImageEditorPerformanceMetrics::instance();
        metrics.setEnabled(false);
        metrics.reset();
        metrics.setEnabled(true);

        const QSize thumbnail_size(24, 24);
        const auto render_group_thumbnail = [&]() {
            return session.renderedLayerThumbnails(thumbnail_size).value(group_id);
        };
        const QImage original = render_group_thumbnail();
        QVERIFY(!original.isNull());
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::GroupThumbnail).count,
                 std::uint64_t{1});

        const auto first_render_metrics = metrics.snapshot();
        QCOMPARE(render_group_thumbnail(), original);
        auto snapshot = metrics.snapshot();
        QCOMPARE(snapshot.timing(ImageEditorPerformanceStage::GroupThumbnail).count,
                 std::uint64_t{1});
        QVERIFY(snapshot.timing(ImageEditorPerformanceStage::ThumbnailCacheHit).count >
                first_render_metrics.timing(
                    ImageEditorPerformanceStage::ThumbnailCacheHit).count);

        const QImage smaller = session.renderedLayerThumbnails(QSize(16, 16)).value(group_id);
        QVERIFY(!smaller.isNull());
        QCOMPARE(smaller.size(), QSize(16, 16));
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::GroupThumbnail).count,
                 std::uint64_t{2});
        QCOMPARE(render_group_thumbnail(), original);
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::GroupThumbnail).count,
                 std::uint64_t{3});

        session.beginLayerOpacityEdit();
        QVERIFY(session.setGroupOpacity(group_id, 75));
        const QImage opacity_75 = render_group_thumbnail();
        QVERIFY(session.setGroupOpacity(group_id, 40));
        const QImage opacity_40 = render_group_thumbnail();
        session.endLayerOpacityEdit();
        QVERIFY(opacity_75 != opacity_40);
        QVERIFY(opacity_40 != original);
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::GroupThumbnail).count,
                 std::uint64_t{5});

        QVERIFY(session.undo());
        QCOMPARE(render_group_thumbnail(), original);
        QVERIFY(session.redo());
        QCOMPARE(render_group_thumbnail(), opacity_40);

        QVERIFY(session.setLayerVisible(second_layer, false));
        const QImage hidden_layer = render_group_thumbnail();
        QVERIFY(hidden_layer != opacity_40);
        QVERIFY(session.undo());
        QCOMPARE(render_group_thumbnail(), opacity_40);

        QVERIFY(session.selectLayer(second_layer));
        QVERIFY(session.addLayerMask(second_layer));
        QVERIFY2(session.applyLayerMaskEraseStroke(
                     {QPointF(24, 16)}, 6, &error), qPrintable(error));
        const QImage masked_layer = render_group_thumbnail();
        QVERIFY(masked_layer != opacity_40);
        QVERIFY(metrics.snapshot().timing(
                    ImageEditorPerformanceStage::GroupThumbnail).count >=
                std::uint64_t{10});
    }

    void layerRasterCacheReusesPixelsAndInvalidatesOnlyChangedLayers() {
        ImageDocumentSession session;
        QString error;
        const QSize canvas_size(48, 32);
        QVERIFY2(session.createCanvas(canvas_size, Qt::transparent, &error),
                 qPrintable(error));
        const QString first_layer = session.selectedLayerId();
        QVERIFY2(session.applyPaintStroke(
                     {QPointF(12, 16)}, Qt::red, 8, &error), qPrintable(error));
        const QString second_layer = session.addLayer();
        QVERIFY(!second_layer.isEmpty());
        QVERIFY2(session.applyPaintStroke(
                     {QPointF(36, 16)}, Qt::blue, 8, &error), qPrintable(error));

        QImage source(canvas_size, QImage::Format_ARGB32);
        source.fill(Qt::transparent);
        auto& metrics = ImageEditorPerformanceMetrics::instance();
        metrics.setEnabled(false);
        metrics.reset();
        metrics.setEnabled(true);

        const auto render_uncached = [&]() {
            return ImageDocumentRenderer::composite(session.data(), source, {});
        };
        const auto cached_pixels = session.renderedImage();
        QVERIFY(!cached_pixels.isNull());
        QCOMPARE(cached_pixels, render_uncached());
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheMiss).count,
                 std::uint64_t{2});
        QCOMPARE(session.cachedRasterLayerBytes(),
                 std::uint64_t{2} * static_cast<std::uint64_t>(canvas_size.width()) *
                     static_cast<std::uint64_t>(canvas_size.height()) * 4ULL);

        const auto paint_stroke_count = metrics.snapshot().timing(
            ImageEditorPerformanceStage::PaintStroke).count;
        QCOMPARE(session.renderedImage(), cached_pixels);
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheHit).count,
                 std::uint64_t{2});
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::PaintStroke).count,
                 paint_stroke_count);

        QVERIFY(session.selectLayer(first_layer));
        QVERIFY(session.selectLayer(second_layer));
        QVERIFY(session.setLayerOpacity(first_layer, 65));
        QVERIFY(session.setLayerVisible(second_layer, false));
        const auto one_visible_layer = session.renderedImage();
        QCOMPARE(one_visible_layer, render_uncached());
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheHit).count,
                 std::uint64_t{3});
        QVERIFY(session.setLayerVisible(second_layer, true));
        QVERIFY(session.moveStackItem(second_layer, false, {}, 1));
        const auto after_reorder = session.renderedImage();
        QCOMPARE(after_reorder, render_uncached());
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheHit).count,
                 std::uint64_t{5});
        const QString group_id = session.groupLayers(
            {first_layer, second_layer}, &error);
        QVERIFY2(!group_id.isEmpty(), qPrintable(error));
        QCOMPARE(session.renderedImage(), render_uncached());
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheHit).count,
                 std::uint64_t{7});
        QVERIFY(session.setGroupOpacity(group_id, 80));
        QCOMPARE(session.renderedImage(), render_uncached());
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheHit).count,
                 std::uint64_t{9});

        QVERIFY(session.selectLayer(first_layer));
        const auto misses_before_edit = metrics.snapshot().timing(
            ImageEditorPerformanceStage::LayerRasterCacheMiss).count;
        const auto hits_before_edit = metrics.snapshot().timing(
            ImageEditorPerformanceStage::LayerRasterCacheHit).count;
        QVERIFY2(session.applyPaintStroke(
                     {QPointF(12, 8)}, Qt::green, 6, &error), qPrintable(error));
        QCOMPARE(session.cachedRasterLayerBytes(),
                 static_cast<std::uint64_t>(canvas_size.width()) *
                     static_cast<std::uint64_t>(canvas_size.height()) * 4ULL);
        const auto after_one_layer_edit = session.renderedImage();
        QCOMPARE(after_one_layer_edit, render_uncached());
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheMiss).count,
                 misses_before_edit + 1);
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheHit).count,
                 hits_before_edit + 1);

        const auto misses_before_erase = metrics.snapshot().timing(
            ImageEditorPerformanceStage::LayerRasterCacheMiss).count;
        QVERIFY2(session.applyEraseStroke({QPointF(12, 8)}, 4, &error),
                 qPrintable(error));
        QCOMPARE(session.cachedRasterLayerBytes(),
                 static_cast<std::uint64_t>(canvas_size.width()) *
                     static_cast<std::uint64_t>(canvas_size.height()) * 4ULL);
        QCOMPARE(session.renderedImage(), render_uncached());
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheMiss).count,
                 misses_before_erase + 1);

        const auto misses_before_undo = metrics.snapshot().timing(
            ImageEditorPerformanceStage::LayerRasterCacheMiss).count;
        QVERIFY(session.undo());
        QCOMPARE(session.cachedRasterLayerBytes(), std::uint64_t{0});
        QCOMPARE(session.renderedImage(), render_uncached());
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheMiss).count,
                 misses_before_undo + 2);
        const auto misses_before_redo = metrics.snapshot().timing(
            ImageEditorPerformanceStage::LayerRasterCacheMiss).count;
        QVERIFY(session.redo());
        QCOMPARE(session.cachedRasterLayerBytes(), std::uint64_t{0});
        QCOMPARE(session.renderedImage(), render_uncached());
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheMiss).count,
                 misses_before_redo + 2);

        QVERIFY(session.addLayerMask(first_layer));
        const auto misses_before_mask_stroke = metrics.snapshot().timing(
            ImageEditorPerformanceStage::LayerRasterCacheMiss).count;
        QVERIFY2(session.applyLayerMaskEraseStroke(
                     {QPointF(12, 16)}, 6, &error), qPrintable(error));
        const auto after_mask_stroke = session.renderedImage();
        QCOMPARE(after_mask_stroke, render_uncached());
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheMiss).count,
                 misses_before_mask_stroke + 1);
        const auto misses_before_mask_toggle = metrics.snapshot().timing(
            ImageEditorPerformanceStage::LayerRasterCacheMiss).count;
        QVERIFY(session.setLayerMaskEnabled(first_layer, false));
        QCOMPARE(session.renderedImage(), render_uncached());
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheMiss).count,
                 misses_before_mask_toggle + 1);

        const auto retained_before_resize = session.cachedRasterLayerBytes();
        QVERIFY(retained_before_resize > 0);
        QVERIFY(session.resizeCanvas(QSize(64, 40), CanvasAnchor::TopLeft, &error));
        QCOMPARE(session.cachedRasterLayerBytes(), std::uint64_t{0});
        const auto misses_before_resize_render = metrics.snapshot().timing(
            ImageEditorPerformanceStage::LayerRasterCacheMiss).count;
        QVERIFY(!session.renderedImage().isNull());
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheMiss).count,
                 misses_before_resize_render + 2);

        QVERIFY(session.createCanvas(QSize(24, 16), Qt::transparent, &error));
        QCOMPARE(session.cachedRasterLayerBytes(), std::uint64_t{0});
        QVERIFY(!session.renderedImage().isNull());
        QVERIFY(session.cachedRasterLayerBytes() <= ImageLayerRasterCache::maximum_bytes);
    }

    void layerRasterCacheTracksResourcesAndBypassesPreviewsAndExports() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString first_path = directory.filePath(QStringLiteral("raster-first.png"));
        const QString relink_path = directory.filePath(QStringLiteral("raster-relinked.png"));
        QImage first_raster(QSize(8, 8), QImage::Format_ARGB32_Premultiplied);
        first_raster.fill(Qt::red);
        QVERIFY(first_raster.save(first_path));

        ImageDocumentSession session;
        QString error;
        QVERIFY2(session.createCanvas(QSize(32, 24), Qt::transparent, &error),
                 qPrintable(error));
        PreparedRasterImage prepared{first_path, first_raster};
        QVERIFY2(session.importRasterImages({prepared}, QPointF(16, 12), &error),
                 qPrintable(error));
        const QString raster_layer = session.selectedLayerId();
        const auto raster_operation = session.data().layers.back().operations.front();
        const QString raster_id = raster_operation.raster.id;

        auto& metrics = ImageEditorPerformanceMetrics::instance();
        metrics.setEnabled(false);
        metrics.reset();
        metrics.setEnabled(true);
        const QImage original = session.renderedImage();
        QCOMPARE(session.renderedImage(), original);
        const auto hit_count_before_preview = metrics.snapshot().timing(
            ImageEditorPerformanceStage::LayerRasterCacheHit).count;
        const auto replay_count_before_preview = metrics.snapshot().timing(
            ImageEditorPerformanceStage::OperationReplay).count;
        auto placements = session.visibleObjects();
        QCOMPARE(placements.size(), 1);
        placements.front().operation.raster.transform *=
            QTransform::fromTranslate(2, 0);
        QVERIFY(!session.renderedImageWithObjects(placements).isNull());
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheHit).count,
                 hit_count_before_preview);
        QVERIFY(metrics.snapshot().timing(
                    ImageEditorPerformanceStage::OperationReplay).count >
                replay_count_before_preview);
        const auto replay_count_before_exclusion = metrics.snapshot().timing(
            ImageEditorPerformanceStage::OperationReplay).count;
        QVERIFY(!session.renderedImageWithoutObjects({raster_id}).isNull());
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheHit).count,
                 hit_count_before_preview);
        QVERIFY(metrics.snapshot().timing(
                    ImageEditorPerformanceStage::OperationReplay).count >
                replay_count_before_exclusion);
        QVERIFY(!session.renderedImageWithEraseStroke({QPointF(16, 12)}, 4).isNull());
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheHit).count,
                 hit_count_before_preview);

        ImageExportSnapshot snapshot = session.exportSnapshot();
        const QString export_path = directory.filePath(QStringLiteral("cached-session.png"));
        ImageExportOptions options;
        QCOMPARE(exportImageSnapshot(snapshot, export_path, options).status,
                 ImageExportStatus::Succeeded);
        QCOMPARE(QImage(export_path), original);
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheHit).count,
                 hit_count_before_preview);

        QImage relinked_raster(QSize(8, 8), QImage::Format_ARGB32_Premultiplied);
        relinked_raster.fill(Qt::blue);
        QVERIFY(relinked_raster.save(relink_path));
        const auto misses_before_relink = metrics.snapshot().timing(
            ImageEditorPerformanceStage::LayerRasterCacheMiss).count;
        QVERIFY2(session.relinkRaster(raster_id,
                    PreparedRasterImage{relink_path, relinked_raster}, &error),
                 qPrintable(error));
        const QImage relinked = session.renderedImage();
        QVERIFY(relinked != original);
        QImage source(QSize(32, 24), QImage::Format_ARGB32);
        source.fill(Qt::transparent);
        QHash<QString, QImage> relinked_resources;
        relinked_resources.insert(raster_id, relinked_raster);
        QCOMPARE(relinked, ImageDocumentRenderer::composite(session.data(),
            source, relinked_resources));
        QCOMPARE(metrics.snapshot().timing(
                     ImageEditorPerformanceStage::LayerRasterCacheMiss).count,
                 misses_before_relink + 1);
        QVERIFY(metrics.snapshot().timing(
                    ImageEditorPerformanceStage::LayerRasterCacheHit).count >
                hit_count_before_preview);

        QVERIFY(session.deleteLayer(raster_layer));
        QVERIFY(!session.renderedImage().isNull());
        QCOMPARE(session.data().layers.size(), 2);
        QCOMPARE(session.cachedRasterLayerBytes(), std::uint64_t{2 * 32 * 24 * 4});

        const QString source_path = directory.filePath(QStringLiteral("source.png"));
        const QString replacement_path = directory.filePath(QStringLiteral("replacement.png"));
        QImage source_pixels(QSize(32, 24), QImage::Format_ARGB32_Premultiplied);
        source_pixels.fill(Qt::red);
        QImage replacement_pixels(QSize(32, 24), QImage::Format_ARGB32_Premultiplied);
        replacement_pixels.fill(Qt::green);
        QVERIFY(source_pixels.save(source_path));
        QVERIFY(replacement_pixels.save(replacement_path));
        QVERIFY2(session.openImage(source_path, &error), qPrintable(error));
        QCOMPARE(session.cachedRasterLayerBytes(), std::uint64_t{0});
        QVERIFY2(session.applyPaintStroke(
                     {QPointF(16, 12)}, Qt::blue, 4, &error), qPrintable(error));
        QVERIFY(!session.renderedImage().isNull());
        QVERIFY(session.cachedRasterLayerBytes() > 0);
        QVERIFY2(session.openImage(replacement_path, &error), qPrintable(error));
        QCOMPARE(session.cachedRasterLayerBytes(), std::uint64_t{0});
        QCOMPARE(session.renderedImage(), ImageDocumentRenderer::composite(
            session.data(), replacement_pixels, {}));
    }

    void layerRasterCacheStaysWithinItsMemoryBudget() {
        ImageLayerRasterCache cache;
        QHash<QString, QImage> resources;
        const QSize layer_size(3000, 2000);
        ImageLayerData first;
        first.id = QStringLiteral("first");
        ImageLayerData second;
        second.id = QStringLiteral("second");
        ImageLayerData third;
        third.id = QStringLiteral("third");

        const auto first_lookup = cache.lookup(first, layer_size, resources);
        QVERIFY(first_lookup.state == ImageLayerRasterCache::LookupState::Miss);
        QImage first_pixels(layer_size, QImage::Format_ARGB32_Premultiplied);
        QVERIFY(!first_pixels.isNull());
        first_pixels.fill(Qt::red);
        cache.insert(first, layer_size, resources, first_pixels);
        QCOMPARE(cache.retainedBytes(), std::uint64_t{24'000'000});

        const auto second_lookup = cache.lookup(second, layer_size, resources);
        QVERIFY(second_lookup.state == ImageLayerRasterCache::LookupState::Miss);
        QImage second_pixels(layer_size, QImage::Format_ARGB32_Premultiplied);
        QVERIFY(!second_pixels.isNull());
        second_pixels.fill(Qt::blue);
        cache.insert(second, layer_size, resources, second_pixels);
        QVERIFY(cache.retainedBytes() <= ImageLayerRasterCache::maximum_bytes);

        QVERIFY(cache.lookup(third, layer_size, resources).state ==
                ImageLayerRasterCache::LookupState::Bypass);
        cache.insert(third, layer_size, resources, second_pixels);
        QVERIFY(cache.retainedBytes() <= ImageLayerRasterCache::maximum_bytes);
        QVERIFY(cache.lookup(first, layer_size, resources).state ==
                ImageLayerRasterCache::LookupState::Hit);

        cache.invalidateLayer(first.id);
        QVERIFY(cache.retainedBytes() < ImageLayerRasterCache::maximum_bytes);
        QVERIFY(cache.lookup(third, layer_size, resources).state ==
                ImageLayerRasterCache::LookupState::Miss);
        cache.clear();
        QCOMPARE(cache.retainedBytes(), std::uint64_t{0});
        cache.insert(third, layer_size, resources, {});
        QCOMPARE(cache.retainedBytes(), std::uint64_t{0});
    }

    void performanceLogRotatesWithinThreeFiles() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        ImageEditorPerformanceLog log(directory.path());
        const QString payload(700 * 1024, QLatin1Char('x'));
        for (int index = 0; index < 8; ++index) {
            QString error;
            QVERIFY2(log.append({{QStringLiteral("sample"), index},
                                 {QStringLiteral("payload"), payload}}, &error),
                     qPrintable(error));
        }

        QVERIFY(QFile::exists(log.logPath()));
        QVERIFY(QFile::exists(log.logPath() + QStringLiteral(".1")));
        QVERIFY(QFile::exists(log.logPath() + QStringLiteral(".2")));
        QVERIFY(!QFile::exists(log.logPath() + QStringLiteral(".3")));
        for (const QString& path : {log.logPath(), log.logPath() + QStringLiteral(".1"),
                                    log.logPath() + QStringLiteral(".2")}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QVERIFY(file.size() <= ImageEditorPerformanceLog::maximum_file_size_bytes);
            QVERIFY(QJsonDocument::fromJson(file.readLine()).isObject());
        }
        QString error;
        const QString oversized_payload(
            static_cast<qsizetype>(ImageEditorPerformanceLog::maximum_file_size_bytes),
            QLatin1Char('x'));
        QVERIFY(!log.append({{QStringLiteral("payload"), oversized_payload}}, &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(ImageEditorPerformanceTests)
#include "image_editor_performance_test.moc"
