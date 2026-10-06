#include "image_editor_performance_log.h"
#include "image_editor_performance_metrics.h"
#include "image_document_session.h"
#include "image_document_renderer.h"
#include "image_exporter.h"

#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
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
