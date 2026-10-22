#include "image_document_session.h"
#include "image_document_utils.h"
#include "rendering/image_document_renderer.h"
#include "rendering/image_layer_adjustment.h"
#include "rendering/image_interactive_raster_cache.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <limits>

using namespace image_editor;
using namespace creative_suite::effects;

class LevelsTest final : public QObject {
    Q_OBJECT
private slots:
    void numericContract() {
        QVERIFY(validLevels({}));
        QVERIFY(identityAdjustment(LevelsParameters{}));
        for (const auto& invalid : {LevelsParameters{255,255,1,0,255},
                {0,0,1,0,255}, {-1,255,1,0,255}, {0,256,1,0,255},
                {0,255,.09,0,255}, {0,255,10,0,255}, {0,255,1,200,100},
                {0,255,1,-1,255}, {0,255,1,0,256},
                {0,255,std::numeric_limits<double>::quiet_NaN(),0,255}})
            QVERIFY(!validLevels(invalid));
        const LevelsParameters p{32,224,2,16,240};
        for (int value : {0,16,32,64,127,224,250,255}) {
            const long double normalized = std::clamp(
                (static_cast<long double>(value) - p.input_black) /
                    (p.input_white - p.input_black), 0.L, 1.L);
            const int expected = int(std::lround(p.output_black +
                (p.output_white-p.output_black)*std::pow(normalized, .5L)));
            const auto actual = levelsRgb(p, {value/255., value/255., value/255.});
            for (auto channel : actual) QVERIFY(std::abs(int(channel)-expected) <= 1);
        }
        QCOMPARE(levelsRgb({100,101,1,20,230},{0.,100/255.,1.}),
            (std::array<unsigned char,3>{20,20,230}));
        QImage neutral(2,2,QImage::Format_ARGB32);
        neutral.fill(QColor(70,120,200,91));
        const auto before = neutral;
        QVERIFY(applyLayerAdjustment(&neutral, LevelsParameters{}, {},
            creative_suite::composition::BlendMode::Normal, 100));
        QCOMPARE(neutral, before);
        QCOMPARE(neutral.format(), before.format());
        for (int mode = 0; mode < 6; ++mode) {
            QImage pixels(1,1,QImage::Format_ARGB32_Premultiplied);
            pixels.fill(QColor(150,100,50,120));
            QImage mask(1,1,QImage::Format_Grayscale8); mask.fill(80);
            QVERIFY(applyLayerAdjustment(&pixels, p, mask,
                static_cast<creative_suite::composition::BlendMode>(mode), 47));
            QCOMPARE(pixels.pixelColor(0,0).alpha(),120);
        }
        QImage masked(1,1,QImage::Format_ARGB32_Premultiplied);
        masked.fill(QColor(100,120,140,120));
        const auto original=masked;
        QImage mask(1,1,QImage::Format_Grayscale8);
        mask.fill(0);
        QVERIFY(applyLayerAdjustment(&masked, p, mask,
            creative_suite::composition::BlendMode::Normal, 100));
        QCOMPARE(masked,original);
        mask.fill(255);
        QVERIFY(applyLayerAdjustment(&masked, p, mask,
            creative_suite::composition::BlendMode::Normal, 100));
        QVERIFY(masked!=original);
        QCOMPARE(masked.pixelColor(0,0).alpha(),120);
    }

    void histogramAndHistory() {
        ImageDocumentSession s;
        QVERIFY(s.createCanvas({4,2},QColor(20,40,60)));
        const auto id=s.addLevelsAdjustment();
        const auto snapshot=s.exportSnapshot();
        auto bins=ImageDocumentRenderer::levelsHistogram(snapshot.document,
            snapshot.source_image,snapshot.raster_images,id);
        QVERIFY(bins);
        QCOMPARE((*bins)[20],std::uint64_t(8));
        QCOMPARE((*bins)[40],std::uint64_t(8));
        QCOMPARE((*bins)[60],std::uint64_t(8));
        const auto unchanged=*bins;
        s.beginLayerAdjustmentEdit();
        QVERIFY(s.setLevels(id,{30,220,1.4,10,235}));
        QVERIFY(s.setLevels(id,{32,224,2,16,240}));
        s.endLayerAdjustmentEdit();
        QCOMPARE(*ImageDocumentRenderer::levelsHistogram(s.exportSnapshot().document,
            s.exportSnapshot().source_image,s.exportSnapshot().raster_images,id),unchanged);
        QVERIFY(s.undo());
        QCOMPARE(std::get<LevelsParameters>(*findLayer(s.data(),id)->adjustment),LevelsParameters{});
        QVERIFY(!s.setLevels(id,{}));
        QVERIFY(s.redo());
        QVERIFY(!s.setLevels(id,{255,255,1,0,255}));
        const auto mapped=levelsRgb({32,224,2,16,240},{20/255.,40/255.,60/255.});
        QCOMPARE(s.renderedImage().pixelColor(0,0),QColor(mapped[0],mapped[1],mapped[2]));
        QVERIFY(!s.selectedLayerIsEditable());
        QVERIFY(s.selectedLayerCanEditMask());
        QVERIFY(!s.setLayerOutline(id,ImageLayerOutlineData{}));
        QVERIFY(!s.setHueSaturation(id,{10,0,0}));
        ImageInteractiveRasterCache cache;
        const auto prepare=[&] { return ImageInteractiveRasterCache::composite(
            cache.prepare(s.exportSnapshot(),QStringLiteral("levels"))); };
        QCOMPARE(prepare(),s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
        const auto work=cache.reconstructedPixels();
        QVERIFY(s.setLevels(id,{20,200,.8,0,255}));
        QCOMPARE(prepare(),s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
        QCOMPARE(cache.reconstructedPixels(),work);
    }

    void histogramTracksLowerLayersAndClipping() {
        ImageDocumentSession s;
        QVERIFY(s.createCanvas({32,24},QColor(20,40,60)));
        const auto base=s.addLayer();
        QVERIFY(s.applyPaintStroke({{16,12}},QColor(180,20,40),8));
        const auto levels=s.addLevelsAdjustment();
        auto snapshot=s.exportSnapshot();
        const auto before=ImageDocumentRenderer::levelsHistogram(snapshot.document,
            snapshot.source_image,snapshot.raster_images,levels);
        QVERIFY(before);
        QVERIFY((*before)[180]>0);
        QVERIFY(s.selectLayer(base));
        QVERIFY(s.applyPaintStroke({{16,12}},QColor(20,200,40),8));
        snapshot=s.exportSnapshot();
        const auto after=ImageDocumentRenderer::levelsHistogram(snapshot.document,
            snapshot.source_image,snapshot.raster_images,levels);
        QVERIFY(after);
        QVERIFY((*after)[200]>0);
        QVERIFY(*after!=*before);
        QVERIFY(s.setLayerClipping(levels,true));
        snapshot=s.exportSnapshot();
        const auto clipped=ImageDocumentRenderer::levelsHistogram(snapshot.document,
            snapshot.source_image,snapshot.raster_images,levels);
        QVERIFY(clipped);
        QVERIFY((*clipped)[200]>0);
        const auto group=s.groupLayers({base,levels});
        QVERIFY(!group.isEmpty());
        snapshot=s.exportSnapshot();
        const auto grouped=ImageDocumentRenderer::levelsHistogram(snapshot.document,
            snapshot.source_image,snapshot.raster_images,levels);
        QVERIFY(grouped);
        QCOMPARE(*grouped,*clipped);
    }

    void persistenceRecoveryAndExport() {
        QTemporaryDir dir;
        ImageDocumentSession s;
        QVERIFY(s.createCanvas({24,16},QColor(50,100,150)));
        QPainterPath selection; selection.addRect(0,0,12,16);
        const auto id=s.addLevelsAdjustment(selection);
        QVERIFY(s.setLevels(id,{20,220,1.4,10,240}));
        const auto path=dir.filePath("levels.cimg");
        QVERIFY(s.saveDocument(path));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto json=QJsonDocument::fromJson(file.readAll()).object(); file.close();
        QCOMPARE(json.value("version").toInt(),25);
        ImageDocumentSession restored;
        QVERIFY(restored.openDocument(path)); QCOMPARE(restored.data(),s.data());
        RecoveryDocumentData recovery{s.data(),path,"levels"}, read;
        QVERIFY(ImageDocumentStore::saveRecovery(dir.filePath("recovery.json"),recovery));
        QVERIFY(ImageDocumentStore::loadRecovery(dir.filePath("recovery.json"),&read));
        QCOMPARE(read.document,s.data());
        ImageExportOptions options; options.scope=ImageExportScope::SelectedLayer;
        QVERIFY(!s.exportImage(dir.filePath("isolated.png"),options));
        options.scope=ImageExportScope::Composite;
        QVERIFY(s.exportImage(dir.filePath("full.png"),options));
        QCOMPARE(QImage(dir.filePath("full.png")).convertToFormat(QImage::Format_ARGB32_Premultiplied),
            s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
        const auto original=restored.data();
        for (int bad=0;bad<13;++bad) {
            auto root=json; auto layers=root.value("layers").toArray(); auto layer=layers.last().toObject();
            auto fields=layer.value("adjustment").toObject();
            switch(bad) {
            case 0: root.insert("version",23); break;
            case 1: root.insert("version",26); break;
            case 2: fields.remove("input_black"); break;
            case 3: fields.insert("input_black",255); break;
            case 4: fields.insert("input_white",20); break;
            case 5: fields.insert("input_white",256); break;
            case 6: fields.insert("gamma",.09); break;
            case 7: fields.insert("gamma","1.4"); break;
            case 8: fields.insert("output_black",250); break;
            case 9: fields.insert("output_white",256); break;
            case 10: fields.insert("output_black",.5); break;
            case 11: fields.insert("hue",10); break;
            case 12: layer.insert("kind","raster"); break;
            }
            layer.insert("adjustment",fields); layers[layers.size()-1]=layer; root.insert("layers",layers);
            QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));
            QVERIFY(file.write(QJsonDocument(root).toJson())>0); file.close();
            QVERIFY2(!restored.openDocument(path),qPrintable(QString::number(bad)));
            QCOMPARE(restored.data(),original);
        }
        auto prior=json; prior.insert("version",24);
        QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));
        QVERIFY(file.write(QJsonDocument(prior).toJson())>0); file.close();
        QVERIFY(restored.openDocument(path));
        QCOMPARE(restored.data(),original);
        auto old=json;
        auto layers=old.value("layers").toArray();
        auto layer=layers.last().toObject();
        layer.insert("adjustment",QJsonObject{{"type","hue_saturation"},
            {"hue",75},{"saturation",-25},{"lightness",10}});
        layers[layers.size()-1]=layer;
        old.insert("layers",layers); old.insert("version",23);
        QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));
        QVERIFY(file.write(QJsonDocument(old).toJson())>0); file.close();
        QVERIFY(restored.openDocument(path));
        QVERIFY(restored.saveDocument(path));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value("version").toInt(),25);
        file.close();
    }
};

QTEST_MAIN(LevelsTest)
#include "image_editor_levels_test.moc"
