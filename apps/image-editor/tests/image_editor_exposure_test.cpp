#include "image_document_session.h"
#include "image_document_utils.h"
#include "rendering/image_layer_adjustment.h"
#include "rendering/image_document_renderer.h"
#include "rendering/image_interactive_raster_cache.h"
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QElapsedTimer>
#include <limits>
using namespace image_editor;
using namespace creative_suite::effects;
using creative_suite::composition::BlendMode;
namespace {
// Independent long-double reference: clamp after gamma, with scalar branches.
long double referenceScaled(double input, const ExposureOffsetGammaParameters& p) {
    const long double v = input;
    const long double l = v <= .04045L ? v / 12.92L : std::pow((v + .055L) / 1.055L, 2.4L);
    long double adjusted = std::pow(std::max(0.L, l * std::pow(2.L, (long double)p.exposure) + p.offset), 1.L / p.gamma);
    adjusted = std::clamp(adjusted, 0.L, 1.L);
    const auto encoded = adjusted <= .0031308L ? adjusted * 12.92L : 1.055L * std::pow(adjusted, 1.L / 2.4L) - .055L;
    return encoded * 255;
}
int referenceChannel(double input, const ExposureOffsetGammaParameters& p) {
    return int(std::lround(referenceScaled(input,p)));
}
double blendReference(BlendMode mode, double b, double s) {
    switch (mode) {
    case BlendMode::Normal: return s;
    case BlendMode::Multiply: return b*s;
    case BlendMode::Screen: return b+s-b*s;
    case BlendMode::Overlay: return b <= .5 ? 2*b*s : 1-2*(1-b)*(1-s);
    case BlendMode::Add: return std::min(1., b+s);
    case BlendMode::Difference: return std::abs(b-s);
    }
    return 0;
}
}
class ExposureTest : public QObject {
    Q_OBJECT
private slots:
    void numericContract() {
        QVERIFY(validExposureOffsetGamma({})); QVERIFY(identityAdjustment(ExposureOffsetGammaParameters{}));
        for (double invalid : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            QVERIFY(!validAdjustment(ExposureOffsetGammaParameters{invalid,0,1}));
            QVERIFY(!validAdjustment(ExposureOffsetGammaParameters{0,invalid,1}));
            QVERIFY(!validAdjustment(ExposureOffsetGammaParameters{0,0,invalid}));
        }
        for (auto p : {ExposureOffsetGammaParameters{20.01,0,1}, {-20.01,0,1}, {0,.5001,1}, {0,-.5001,1}, {0,0,0}, {0,0,10}})
            QVERIFY(!validAdjustment(p));
        const std::array parameters{ExposureOffsetGammaParameters{}, ExposureOffsetGammaParameters{1,0,1},
            ExposureOffsetGammaParameters{-1,0,1}, ExposureOffsetGammaParameters{0,.0123,1},
            ExposureOffsetGammaParameters{0,-.0123,1}, ExposureOffsetGammaParameters{0,0,.5},
            ExposureOffsetGammaParameters{0,0,2}, ExposureOffsetGammaParameters{1.25,-.0123,1.4},
            ExposureOffsetGammaParameters{-20,-.5,.01}, ExposureOffsetGammaParameters{20,.5,.01},
            ExposureOffsetGammaParameters{-20,.5,9.99}, ExposureOffsetGammaParameters{20,-.5,9.99}};
        for (const auto& p : parameters) {
            QVERIFY(validAdjustment(p));
            for (double v : {0.,.0031308,.040449,.04045,.040451,.18,.5,1.})
                QCOMPARE(int(adjustmentRgb(p,{v,v,v})[0]), referenceChannel(v,p));
            for (int v = 0; v <= 255; ++v) {
                const int actual = adjustmentRgb(p,{v/255.,v/255.,v/255.})[0];
                const int expected = referenceChannel(v/255.,p);
                // At exact half-byte ties, double/long-double transfer roundoff
                // may select adjacent bytes; all other values must match exactly.
                const auto scaled = referenceScaled(v/255.,p);
                const bool tie = std::abs((scaled-std::floor(scaled))-.5L)<1e-10L;
                QVERIFY2(actual == expected || (tie && std::abs(actual-expected)==1), qPrintable(QString("v=%1 exposure=%2 offset=%3 gamma=%4 actual=%5 expected=%6")
                    .arg(v).arg(p.exposure).arg(p.offset).arg(p.gamma).arg(actual).arg(expected)));
            }
            for (int alpha : {0,1,64,128,255}) for (int mask : {0,80,255}) for (int opacity : {0,47,100}) for (int m = 0; m < 6; ++m) {
                QImage pixels(1,1,QImage::Format_ARGB32_Premultiplied); pixels.fill(QColor(180,90,50,alpha));
                const auto before = pixels.pixel(0,0); QImage coverage(1,1,QImage::Format_Grayscale8); coverage.fill(mask);
                const auto mode = static_cast<BlendMode>(m);
                QVERIFY(applyLayerAdjustment(&pixels,p,coverage,mode,opacity)); const auto actual = pixels.pixel(0,0);
                QCOMPARE(qAlpha(actual),alpha);
                if (!alpha || !mask || !opacity || (p == ExposureOffsetGammaParameters{} && m == 0)) { QCOMPARE(actual,before); continue; }
                const int channels[]{qRed(before),qGreen(before),qBlue(before)};
                const int output[]{qRed(actual),qGreen(actual),qBlue(actual)};
                for (int c = 0; c < 3; ++c) {
                    const double v = double(channels[c])/alpha, strength = opacity/100.*mask/255.;
                    const double mapped = referenceChannel(v,p)/255.;
                    QVERIFY(std::abs(output[c]-int(std::lround(((1-strength)*v+strength*blendReference(mode,v,mapped))*alpha)))<=1);
                }
            }
        }
        QImage straight(2,2,QImage::Format_ARGB32); straight.fill(QColor(40,80,120,1)); const auto unchanged = straight;
        QVERIFY(applyLayerAdjustment(&straight,ExposureOffsetGammaParameters{}, {},BlendMode::Normal,100)); QCOMPARE(straight,unchanged); QCOMPARE(straight.format(),unchanged.format());
        std::atomic_bool cancelled{true}; QVERIFY(!applyLayerAdjustment(&straight,ExposureOffsetGammaParameters{1,0,1},{},BlendMode::Normal,100,&cancelled)); QCOMPARE(straight,unchanged);
    }
    void modelHistoryExportAndCache() {
        ImageDocumentSession s; QVERIFY(s.createCanvas({80,60},QColor(40,80,150,170))); const auto base = s.selectedLayerId();
        QVERIFY(s.applyPaintStroke({{10,25},{60,25}},Qt::red,25)); const auto raw = s.renderedImage();
        QPainterPath selection; selection.addRect(0,0,80,60); selection.addEllipse(25,15,20,20);
        const auto id = s.addExposureOffsetGammaAdjustment(selection); QVERIFY(!id.isEmpty()); QCOMPARE(s.renderedImage(),raw);
        QVERIFY(s.setLayerClipping(id,true)); QCOMPARE(s.renderedImage(),raw); QVERIFY(s.setLayerClipping(id,false));
        QVERIFY(!s.selectedLayerIsEditable()); QVERIFY(s.selectedLayerCanEditMask()); QVERIFY(!s.applyPaintStroke({{10,10}},Qt::red,10));
        QVERIFY(!s.setLayerOutline(id,ImageLayerOutlineData{})); QVERIFY(!s.setBrightnessContrast(id,{1,1})); QVERIFY(!s.setGradientMap(id,{}));
        s.beginLayerAdjustmentEdit(); QVERIFY(s.setExposureOffsetGamma(id,{.5,0,1})); QVERIFY(s.setExposureOffsetGamma(id,{1.25,-.0123,1.4})); s.endLayerAdjustmentEdit();
        QVERIFY(s.undo()); QCOMPARE(std::get<ExposureOffsetGammaParameters>(*findLayer(s.data(),id)->adjustment),ExposureOffsetGammaParameters{});
        QVERIFY(!s.setExposureOffsetGamma(id,{})); QVERIFY(s.redo()); QVERIFY(!s.setExposureOffsetGamma(id,{21,0,1}));
        QVERIFY(s.setLayerClipping(id,true)); const auto gradient = s.addGradientMapAdjustment(); QVERIFY(s.setLayerClipping(gradient,true));
        const auto brightness = s.addBrightnessContrastAdjustment(); QVERIFY(s.setBrightnessContrast(brightness,{25,-30})); QVERIFY(s.setLayerClipping(brightness,true)); QVERIFY(!s.setExposureOffsetGamma(brightness,{1,0,1}));
        const auto upper = s.addLayer(); QVERIFY(s.applyPaintStroke({{10,25},{60,25}},QColor(20,180,240,180),12)); QVERIFY(s.setLayerClipping(upper,true));
        ImageInteractiveRasterCache cache;
        const auto verify = [&] { QCOMPARE(ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"exposure")),s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied)); };
        verify(); const auto work = cache.reconstructedPixels(); QVERIFY(s.setExposureOffsetGamma(id,{-.5,.01,.9})); verify(); QCOMPARE(cache.reconstructedPixels(),work);
        QVERIFY(s.selectLayer(id)); QVERIFY(s.applyLayerMaskStroke({{10,10}},Qt::black,10)); verify(); QVERIFY(s.undo()); verify(); QVERIFY(s.redo()); verify();
        const auto group = s.groupLayers({base,id,gradient,brightness,upper}); QVERIFY(!group.isEmpty()); QVERIFY(s.setGroupOpacity(group,60)); s.rotateRight(); verify();
        QTemporaryDir dir; const auto path = dir.filePath("exposure.cimg"); QVERIFY(s.saveDocument(path)); ImageDocumentSession loaded; QVERIFY(loaded.openDocument(path)); QCOMPARE(loaded.data(),s.data());
        RecoveryDocumentData recovery{s.data(),path,"exposure"}, read; QVERIFY(ImageDocumentStore::saveRecovery(dir.filePath("recovery.json"),recovery)); QVERIFY(ImageDocumentStore::loadRecovery(dir.filePath("recovery.json"),&read)); QCOMPARE(read.document,s.data());
        QVERIFY(s.selectLayer(id)); ImageExportOptions options; options.scope=ImageExportScope::SelectedLayer; QVERIFY(!s.exportImage(dir.filePath("isolated.png"),options));
        options.scope=ImageExportScope::Composite; QVERIFY(s.exportImage(dir.filePath("full.png"),options)); QCOMPARE(QImage(dir.filePath("full.png")).convertToFormat(QImage::Format_ARGB32_Premultiplied),s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
        QVERIFY(s.exportImage(dir.filePath("full.jpg"),options)); QVERIFY(!QImage(dir.filePath("full.jpg")).isNull());
        QVERIFY(s.selectGroup(group)); options.scope=ImageExportScope::SelectedGroup; QVERIFY(s.exportImage(dir.filePath("group.png"),options));
        const auto snapshot=s.exportSnapshot(); QCOMPARE(QImage(dir.filePath("group.png")).convertToFormat(QImage::Format_ARGB32_Premultiplied),ImageDocumentRenderer::selectedGroup(snapshot.document,snapshot.source_image,snapshot.raster_images,group).convertToFormat(QImage::Format_ARGB32_Premultiplied));
        QVERIFY(!ImageDocumentRenderer::layerThumbnail(s.data(),s.exportSnapshot().source_image,s.exportSnapshot().raster_images,*findLayer(s.data(),id),{32,32}).isNull());
    }
    void persistenceValidation() {
        QTemporaryDir dir; const auto path=dir.filePath("exposure.cimg"); ImageDocumentSession s; QVERIFY(s.createCanvas({8,8},Qt::red));
        const auto id=s.addExposureOffsetGammaAdjustment(); QVERIFY(s.setExposureOffsetGamma(id,{1.25,-.0123,1.4})); QVERIFY(s.saveDocument(path));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto json=QJsonDocument::fromJson(file.readAll()).object(); file.close(); QCOMPARE(json.value("version").toInt(),ImageDocumentStore::kCurrentDocumentVersion);
        ImageDocumentSession loaded; QVERIFY(loaded.openDocument(path)); QCOMPARE(loaded.data(),s.data());
        for (int bad=0; bad<18; ++bad) {
            auto root=json; auto layers=root.value("layers").toArray(); auto layer=layers.last().toObject(); auto a=layer.value("adjustment").toObject();
            switch(bad) {
            case 0: root.insert("version",21); break;
            case 1: root.insert("version",ImageDocumentStore::kCurrentDocumentVersion+1); break;
            case 2: a.insert("exposure",20.01); break;
            case 3: a.insert("offset",-.5001); break;
            case 4: a.insert("gamma",0); break;
            case 5: a.insert("gamma",10); break;
            case 6: a.insert("exposure","1.25"); break;
            case 7: a.insert("offset",true); break;
            case 8: a.insert("gamma",QJsonValue::Null); break;
            case 9: a.remove("exposure"); break;
            case 10: a.remove("offset"); break;
            case 11: a.remove("gamma"); break;
            case 12: a.insert("type","future"); break;
            case 13: layer.insert("kind","background"); break;
            case 14: a.insert("stops",QJsonArray{}); break;
            case 15: a.insert("brightness",0); break;
            case 16: layer.insert("kind","raster"); break;
            case 17: a.insert("contrast",0); break;
            }
            layer.insert("adjustment",a); layers[layers.size()-1]=layer; root.insert("layers",layers);
            QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate)); QVERIFY(file.write(QJsonDocument(root).toJson())>0); file.close();
            const auto before=loaded.data(); QVERIFY2(!loaded.openDocument(path),qPrintable(QString::number(bad))); QCOMPARE(loaded.data(),before);
        }
        // Actual old adjustment formats remain readable and save through the current writer.
        for (int version : {20,21}) {
            auto root=json; root.insert("version",version); auto layers=root.value("layers").toArray(); auto layer=layers.last().toObject();
            layer.insert("adjustment",version==20 ? QJsonObject{{"type","gradient_map"},{"stops",QJsonArray{QJsonObject{{"position",0},{"color","#000000"}},QJsonObject{{"position",1},{"color","#ffffff"}}}}} : QJsonObject{{"type","brightness_contrast"},{"brightness",25},{"contrast",-30}});
            layers[layers.size()-1]=layer; root.insert("layers",layers); QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate)); file.write(QJsonDocument(root).toJson()); file.close();
            QVERIFY(loaded.openDocument(path)); QVERIFY(loaded.saveDocument(path)); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value("version").toInt(),ImageDocumentStore::kCurrentDocumentVersion); file.close();
        }
    }
    void reference1080p() {
        ImageDocumentSession s; QVERIFY(s.createCanvas({1920,1080},Qt::transparent)); QStringList ids;
        for(int i=0;i<5;++i) { if(i) QVERIFY(!s.addLayer().isEmpty()); ids.append(s.selectedLayerId()); QVERIFY(s.applyPaintStroke({{200.,300.+i*30},{1600.,300.+i*30}},QColor(30*i,80,180,190),220)); }
        QVERIFY(!s.groupLayers(ids).isEmpty()); const auto id=s.addExposureOffsetGammaAdjustment(); QVERIFY(s.setExposureOffsetGamma(id,{1.25,-.0123,1.4}));
        ImageInteractiveRasterCache cache; QElapsedTimer timer; timer.start();
        const auto render=[&] { const auto result=ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"exposure-1080p")); QVERIFY(!result.isNull()); };
        render(); const auto cold=timer.elapsed(); const auto work=cache.reconstructedPixels(); timer.restart(); render(); const auto warm=timer.elapsed();
        QVERIFY(s.setExposureOffsetGamma(id,{.5,-.0123,1.4})); timer.restart(); render(); const auto exposure=timer.elapsed();
        QVERIFY(s.setExposureOffsetGamma(id,{.5,.01,1.4})); timer.restart(); render(); const auto offset=timer.elapsed();
        QVERIFY(s.setExposureOffsetGamma(id,{.5,.01,.8})); timer.restart(); render(); const auto gamma=timer.elapsed(); QCOMPARE(cache.reconstructedPixels(),work);
        QVERIFY(cache.retainedBytes()<=64ULL*1024*1024);
        qInfo()<<"exposure_1080p cold_ms"<<cold<<"warm_ms"<<warm<<"exposure_ms"<<exposure<<"offset_ms"<<offset<<"gamma_ms"<<gamma<<"cache_bytes"<<cache.retainedBytes();
    }
};
QTEST_MAIN(ExposureTest)
#include "image_editor_exposure_test.moc"
