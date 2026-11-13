#include "image_document_session.h"
#include "image_document_utils.h"
#include "rendering/image_layer_adjustment.h"
#include "rendering/image_interactive_raster_cache.h"
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
using namespace image_editor;
using namespace creative_suite::effects;
using creative_suite::composition::BlendMode;

namespace {
// Rewrites the first adjustment layer of a saved document with `adjustment`.
bool rewriteAdjustment(const QString& path, int version, const QJsonObject& adjustment) {
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) return false;
    auto root = QJsonDocument::fromJson(in.readAll()).object();
    in.close();
    root.insert("version", version);
    auto layers = root.value("layers").toArray();
    for (int i = 0; i < layers.size(); ++i) {
        auto layer = layers[i].toObject();
        if (layer.value("kind") != "adjustment") continue;
        layer.insert("adjustment", adjustment);
        layers[i] = layer;
        root.insert("layers", layers);
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        out.write(QJsonDocument(root).toJson());
        return true;
    }
    return false;
}
}

class InvertTest : public QObject {
    Q_OBJECT
private slots:
    void numericContract() {
        QVERIFY(validAdjustment(InvertParameters{}));
        // Invert has no neutral state: it must never be skipped as an identity.
        QVERIFY(!identityAdjustment(InvertParameters{}));
        for (int v = 0; v <= 255; ++v) {
            const auto out = adjustmentRgb(InvertParameters{}, {v / 255., v / 255., v / 255.});
            QCOMPARE(int(out[0]), 255 - v);
            QCOMPARE(int(out[1]), 255 - v);
            QCOMPARE(int(out[2]), 255 - v);
            // Applying Invert twice restores the original byte.
            const auto twice = adjustmentRgb(InvertParameters{}, {out[0] / 255., out[1] / 255., out[2] / 255.});
            QCOMPARE(int(twice[0]), v);
        }
        // Channels are independent and out-of-range input is clamped.
        const auto mixed = adjustmentRgb(InvertParameters{}, {0., 1., 2.});
        QCOMPARE(int(mixed[0]), 255);
        QCOMPARE(int(mixed[1]), 0);
        QCOMPARE(int(mixed[2]), 0);
        const auto low = adjustmentRgb(InvertParameters{}, {-1., .5, .25});
        QCOMPARE(int(low[0]), 255);
    }

    void pixelsPreserveAlphaAndHonorCoverage() {
        for (int alpha : {1, 64, 128, 255}) {
            QImage pixels(1, 1, QImage::Format_ARGB32_Premultiplied);
            pixels.fill(QColor(200, 100, 40, alpha));
            const auto before = pixels.pixel(0, 0);
            QImage full(1, 1, QImage::Format_Grayscale8);
            full.fill(255);
            QVERIFY(applyLayerAdjustment(&pixels, InvertParameters{}, full, BlendMode::Normal, 100));
            const auto after = pixels.pixel(0, 0);
            QCOMPARE(qAlpha(after), alpha);
            // Premultiplied channels of the inverted color: (255 - c) scaled by alpha.
            QVERIFY(std::abs(qRed(after) - int(std::lround((255 - qRed(before) * 255. / alpha) * alpha / 255.))) <= 1);
        }
        // Zero coverage and zero opacity leave pixels untouched.
        QImage pixels(1, 1, QImage::Format_ARGB32_Premultiplied);
        pixels.fill(QColor(10, 20, 30, 255));
        const auto original = pixels;
        QImage none(1, 1, QImage::Format_Grayscale8);
        none.fill(0);
        QVERIFY(applyLayerAdjustment(&pixels, InvertParameters{}, none, BlendMode::Normal, 100));
        QCOMPARE(pixels, original);
        QVERIFY(applyLayerAdjustment(&pixels, InvertParameters{}, {}, BlendMode::Normal, 0));
        QCOMPARE(pixels, original);
        // Fully transparent pixels stay transparent.
        QImage clear(1, 1, QImage::Format_ARGB32_Premultiplied);
        clear.fill(Qt::transparent);
        QVERIFY(applyLayerAdjustment(&clear, InvertParameters{}, {}, BlendMode::Normal, 100));
        QCOMPARE(qAlpha(clear.pixel(0, 0)), 0);
    }

    void modelHistoryAndPersistence() {
        ImageDocumentSession s;
        QVERIFY(s.createCanvas({40, 30}, QColor(200, 100, 50, 255)));
        const auto raw = s.renderedImage();
        const auto id = s.addInvertAdjustment();
        QVERIFY(!id.isEmpty());
        const auto layer = findLayer(s.data(), id);
        QVERIFY(layer && layer->adjustment);
        QVERIFY(std::holds_alternative<InvertParameters>(*layer->adjustment));
        // The adjustment has no controls of another type to edit.
        QVERIFY(!s.setBrightnessContrast(id, {1, 1}));
        QVERIFY(!s.setGradientMap(id, {}));
        QVERIFY(!s.setExposureOffsetGamma(id, {1, 0, 1}));
        // The lower layer pixels are inverted; the raw pixels are not.
        const auto inverted = s.renderedImage();
        QVERIFY(inverted != raw);
        QCOMPARE(qRed(inverted.pixel(5, 5)), 255 - 200);
        QCOMPARE(qGreen(inverted.pixel(5, 5)), 255 - 100);
        QCOMPARE(qBlue(inverted.pixel(5, 5)), 255 - 50);
        QCOMPARE(qAlpha(inverted.pixel(5, 5)), 255);
        // Undo removes the adjustment and Redo restores it.
        QVERIFY(s.undo());
        QCOMPARE(s.renderedImage(), raw);
        QVERIFY(s.redo());
        QCOMPARE(s.renderedImage(), inverted);

        // Selection-seeded mask limits the inverted region.
        ImageDocumentSession masked;
        QVERIFY(masked.createCanvas({40, 30}, QColor(200, 100, 50, 255)));
        QPainterPath selection;
        selection.addRect(0, 0, 20, 30);
        QVERIFY(!masked.addInvertAdjustment(selection).isEmpty());
        QCOMPARE(qRed(masked.renderedImage().pixel(5, 5)), 55);
        QCOMPARE(qRed(masked.renderedImage().pixel(30, 5)), 200);

        // Save and reopen (v31) and recovery preserve the adjustment.
        QTemporaryDir dir;
        const auto path = dir.filePath("invert.cimg");
        QVERIFY(s.saveDocument(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        QCOMPARE(root.value("version").toInt(), ImageDocumentStore::kCurrentDocumentVersion);
        QVERIFY(ImageDocumentStore::kCurrentDocumentVersion >= 31);
        ImageDocumentSession loaded;
        QVERIFY(loaded.openDocument(path));
        QCOMPARE(loaded.data(), s.data());
        QCOMPARE(loaded.renderedImage(), inverted);
        RecoveryDocumentData recovery{s.data(), path, "invert"}, read;
        QVERIFY(ImageDocumentStore::saveRecovery(dir.filePath("recovery.json"), recovery));
        QVERIFY(ImageDocumentStore::loadRecovery(dir.filePath("recovery.json"), &read));
        QCOMPARE(read.document, s.data());
    }

    void previewMatchesRenderAndExport() {
        ImageDocumentSession s;
        QVERIFY(s.createCanvas({60, 40}, QColor(40, 80, 150, 170)));
        QVERIFY(s.applyPaintStroke({{10, 20}, {50, 20}}, Qt::red, 12));
        QVERIFY(!s.addInvertAdjustment().isEmpty());
        ImageInteractiveRasterCache cache;
        QCOMPARE(ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(), "invert")),
                 s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
        QTemporaryDir dir;
        ImageExportOptions options;
        options.scope = ImageExportScope::Composite;
        QVERIFY(s.exportImage(dir.filePath("full.png"), options));
        QCOMPARE(QImage(dir.filePath("full.png")).convertToFormat(QImage::Format_ARGB32_Premultiplied),
                 s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
        // The adjustment cannot be exported in isolation.
        QVERIFY(s.selectLayer(s.selectedLayerId()));
        options.scope = ImageExportScope::SelectedLayer;
        QVERIFY(!s.exportImage(dir.filePath("isolated.png"), options));
    }

    void rejectsInvalidAndLegacyDocuments() {
        ImageDocumentSession s;
        QVERIFY(s.createCanvas({20, 20}, QColor(10, 20, 30, 255)));
        QVERIFY(!s.addInvertAdjustment().isEmpty());
        QTemporaryDir dir;
        const auto path = dir.filePath("base.cimg");
        QVERIFY(s.saveDocument(path));
        const int version = ImageDocumentStore::kCurrentDocumentVersion;
        const QJsonObject valid{{"type", "invert"}};
        // The unmodified payload loads.
        QVERIFY(rewriteAdjustment(path, version, valid));
        {
            ImageDocumentSession ok;
            QVERIFY(ok.openDocument(path));
        }
        // Invert did not exist before v31.
        QVERIFY(rewriteAdjustment(path, 30, valid));
        {
            ImageDocumentSession old;
            QString error;
            QVERIFY(!old.openDocument(path, &error));
            QVERIFY(!error.isEmpty());
        }
        // Extra fields from another adjustment type are invalid.
        QVERIFY(rewriteAdjustment(path, version, QJsonObject{{"type", "invert"}, {"brightness", 5}}));
        {
            ImageDocumentSession bad;
            QString error;
            QVERIFY(!bad.openDocument(path, &error));
            QVERIFY(!error.isEmpty());
        }
    }
};

QTEST_MAIN(InvertTest)
#include "image_editor_invert_test.moc"
