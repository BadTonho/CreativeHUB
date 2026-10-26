#include "image_document_session.h"
#include "rendering/image_document_renderer.h"
#include "rendering/image_layer_adjustment.h"
#include "rendering/image_interactive_raster_cache.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace image_editor;
using namespace creative_suite::effects;

class CurvesTest final : public QObject {
    Q_OBJECT
private slots:
    void mappingAndMask() {
        const CurvesParameters neutral{};
        QVERIFY(validCurves(neutral));
        QVERIFY(identityAdjustment(neutral));
        const CurvesParameters shape{{{0,10},{64,200},{128,40},{255,245}}};
        const auto table=curvesLut(shape);
        QCOMPARE(table[0],std::uint8_t(10));
        QCOMPARE(table[64],std::uint8_t(200));
        QCOMPARE(table[128],std::uint8_t(40));
        QCOMPARE(table[255],std::uint8_t(245));
        for(int x=1;x<64;++x) QVERIFY(table[x]>=10 && table[x]<=200);
        for(int x=65;x<128;++x) QVERIFY(table[x]>=40 && table[x]<=200);
        QCOMPARE(curvesRgb(shape,{0.,64./255.,1.}),
            (std::array<unsigned char,3>{10,200,245}));
        for(const auto& bad : {CurvesParameters{{{0,0}}},
                CurvesParameters{{{0,0},{0,10},{255,255}}},
                CurvesParameters{{{1,0},{255,255}}},
                CurvesParameters{{{0,-1},{255,255}}},
                CurvesParameters{{{0,0},{255,256}}}})
            QVERIFY(!validCurves(bad));
        QImage pixels(1,1,QImage::Format_ARGB32_Premultiplied);
        pixels.fill(QColor(100,120,140,120));
        const auto before=pixels;
        QVERIFY(applyLayerAdjustment(&pixels,neutral,{},
            creative_suite::composition::BlendMode::Normal,100));
        QCOMPARE(pixels,before);
        QImage mask(1,1,QImage::Format_Grayscale8);mask.fill(0);
        QVERIFY(applyLayerAdjustment(&pixels,shape,mask,
            creative_suite::composition::BlendMode::Normal,100));
        QCOMPARE(pixels,before);
        mask.fill(255);
        for(int mode=0;mode<6;++mode){
            pixels=before;
            QVERIFY(applyLayerAdjustment(&pixels,shape,mask,
                static_cast<creative_suite::composition::BlendMode>(mode),100));
            QCOMPARE(pixels.pixelColor(0,0).alpha(),120);
        }
        QVERIFY(pixels!=before);
    }
    void histogramHistoryAndStacks() {
        ImageDocumentSession s;
        QVERIFY(s.createCanvas({32,24},QColor(20,40,60)));
        const auto base=s.addLayer();
        QVERIFY(s.applyPaintStroke({{16,12}},QColor(180,20,40),8));
        const auto id=s.addCurvesAdjustment();
        auto snapshot=s.exportSnapshot();
        auto bins=ImageDocumentRenderer::adjustmentInputHistogram(snapshot.document,
            snapshot.source_image,snapshot.raster_images,id);
        QVERIFY(bins);QVERIFY((*bins)[180]>0);
        const auto input=*bins;
        s.beginLayerAdjustmentEdit();
        QVERIFY(s.setCurves(id,{{{0,10},{128,180},{255,245}}}));
        QVERIFY(s.setCurves(id,{{{0,20},{128,180},{255,245}}}));
        s.endLayerAdjustmentEdit();
        snapshot=s.exportSnapshot();
        QCOMPARE(*ImageDocumentRenderer::adjustmentInputHistogram(snapshot.document,
            snapshot.source_image,snapshot.raster_images,id),input);
        QVERIFY(s.undo());QCOMPARE(std::get<CurvesParameters>(*findLayer(s.data(),id)->adjustment),CurvesParameters{});
        QVERIFY(s.redo());
        QVERIFY(!s.setCurves(id,{{{0,0},{0,10},{255,255}}}));
        ImageInteractiveRasterCache cache;
        auto prepare=[&]{return ImageInteractiveRasterCache::composite(
            cache.prepare(s.exportSnapshot(),QStringLiteral("curves")));};
        QCOMPARE(prepare(),s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
        const auto work=cache.reconstructedPixels();
        QVERIFY(s.setCurves(id,{{{0,5},{128,170},{255,240}}}));
        QCOMPARE(prepare(),s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
        QCOMPARE(cache.reconstructedPixels(),work);
        QVERIFY(s.selectLayer(base));
        QVERIFY(s.applyPaintStroke({{16,12}},QColor(20,200,40),8));
        snapshot=s.exportSnapshot();
        bins=ImageDocumentRenderer::adjustmentInputHistogram(snapshot.document,
            snapshot.source_image,snapshot.raster_images,id);
        QVERIFY(bins);QVERIFY(*bins!=input);QVERIFY((*bins)[200]>0);
        QVERIFY(s.setLayerClipping(id,true));
        const auto group=s.groupLayers({base,id});QVERIFY(!group.isEmpty());
        const auto hue=s.addHueSaturationAdjustment();QVERIFY(!hue.isEmpty());
        QCOMPARE(prepare(),s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
    }
    void persistenceRecoveryAndExport() {
        QTemporaryDir directory;QVERIFY(directory.isValid());
        ImageDocumentSession s;QVERIFY(s.createCanvas({24,16},QColor(50,100,150)));
        QPainterPath selection;selection.addRect(0,0,12,16);
        const auto id=s.addCurvesAdjustment(selection);
        CurvesParameters parameters{{{0,10},{128,180},{255,245}}};
        parameters.channels[0]={{0,0},{128,220},{255,255}};
        QVERIFY(s.setCurves(id,parameters));
        const auto path=directory.filePath("curves.cimg");QVERIFY(s.saveDocument(path));
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));
        const auto root=QJsonDocument::fromJson(file.readAll()).object();file.close();
        QCOMPARE(root.value("version").toInt(),ImageDocumentStore::kCurrentDocumentVersion);
        const auto encoded_adjustment=root.value("layers").toArray().last().toObject().value("adjustment").toObject();
        QVERIFY(encoded_adjustment.value("channels").isObject());
        ImageDocumentSession restored;QVERIFY(restored.openDocument(path));QCOMPARE(restored.data(),s.data());
        QCOMPARE(restored.renderedImage().pixelColor(0,0).alpha(),s.renderedImage().pixelColor(0,0).alpha());
        RecoveryDocumentData recovery{s.data(),path,"curves"},read;
        QVERIFY(ImageDocumentStore::saveRecovery(directory.filePath("recovery.json"),recovery));
        QVERIFY(ImageDocumentStore::loadRecovery(directory.filePath("recovery.json"),&read));
        QCOMPARE(read.document,s.data());
        QVERIFY(s.exportImage(directory.filePath("output.png")));
        QCOMPARE(QImage(directory.filePath("output.png")).convertToFormat(QImage::Format_ARGB32_Premultiplied),
            s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
        const auto original=restored.data();
        for(int bad=0;bad<13;++bad){
            auto modified=root;auto layers=modified.value("layers").toArray();
            auto layer=layers.last().toObject();auto adjustment=layer.value("adjustment").toObject();
            auto points=adjustment.value("points").toArray();
            auto first=points.first().toObject();
            auto channels=adjustment.value("channels").toObject();
            switch(bad){
            case 0:modified.insert("version",25);break;
            case 1:modified.insert("version",ImageDocumentStore::kCurrentDocumentVersion + 1);break;
            case 2:adjustment.remove("points");break;
            case 3:first.insert("input",1);points[0]=first;break;
            case 4:first.insert("output",-1);points[0]=first;break;
            case 5:first.insert("input",.5);points[0]=first;break;
            case 6:first.insert("extra",1);points[0]=first;break;
            case 7:points[1]=points[0];break;
            case 8:adjustment.insert("gamma",1.4);break;
            case 9:{auto red=channels.value("red").toArray();auto point=red.first().toObject();point.insert("output",256);red[0]=point;channels.insert("red",red);adjustment.insert("channels",channels);break;}
            case 10:channels.remove("blue");adjustment.insert("channels",channels);break;
            case 11:channels.insert("alpha",channels.value("red"));adjustment.insert("channels",channels);break;
            case 12:layer.insert("kind","raster");break;
            }
            if(bad>=3 && bad<=7)adjustment.insert("points",points);
            layer.insert("adjustment",adjustment);layers[layers.size()-1]=layer;
            modified.insert("layers",layers);
            QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));
            QVERIFY(file.write(QJsonDocument(modified).toJson())>0);file.close();
            QVERIFY2(!restored.openDocument(path),qPrintable(QString::number(bad)));
            QCOMPARE(restored.data(),original);
        }
        auto curves_v26=root;curves_v26.insert("version",26);
        QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));
        QVERIFY(file.write(QJsonDocument(curves_v26).toJson())>0);file.close();
        QVERIFY(restored.openDocument(path));
        QCOMPARE(std::get<CurvesParameters>(*restored.data().layers.back().adjustment),parameters);
        QVERIFY(restored.saveDocument(path));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value("version").toInt(),ImageDocumentStore::kCurrentDocumentVersion);
        file.close();
        auto legacy=root;auto layers=legacy.value("layers").toArray();
        auto layer=layers.last().toObject();
        auto legacy_curves=layer.value("adjustment").toObject();
        legacy_curves.remove("channels");layer.insert("adjustment",legacy_curves);
        layers[layers.size()-1]=layer;legacy.insert("layers",layers);legacy.insert("version",25);
        QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));
        QVERIFY(file.write(QJsonDocument(legacy).toJson())>0);file.close();
        QVERIFY(restored.openDocument(path));
        auto migrated_expected=parameters;
        migrated_expected.channels=CurvesParameters{}.channels;
        for(const auto& channel:migrated_expected.channels) QCOMPARE(channel,identityCurvePoints());
        QCOMPARE(std::get<CurvesParameters>(*restored.data().layers.back().adjustment),migrated_expected);
        QVERIFY(restored.saveDocument(path));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto migrated=QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(migrated.value("version").toInt(),ImageDocumentStore::kCurrentDocumentVersion);
        QVERIFY(migrated.value("layers").toArray().last().toObject()
            .value("adjustment").toObject().value("channels").isObject());
    }
};
QTEST_MAIN(CurvesTest)
#include "image_editor_curves_test.moc"
