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
// Independent hue-to-channel interpolation rather than the production sector table.
std::array<int,3> referenceRgb(std::array<double,3> input, const HueSaturationParameters& p) {
    const long double r=input[0], g=input[1], b=input[2];
    const auto lo=std::min({r,g,b}), hi=std::max({r,g,b}), d=hi-lo;
    long double l=(hi+lo)/2, h=0, saturation=0;
    const auto adjust=[](long double v,int a){return a<0?v*(1+a/100.L):v+(1-v)*a/100.L;};
    if(d>0){
        saturation=d/(l<=.5L?hi+lo:2-hi-lo);
        h=hi==r?(g-b)/d:hi==g?(b-r)/d+2:(r-g)/d+4;
        h=h/6+p.hue/360.L; h-=std::floor(h);
        saturation=adjust(saturation,p.saturation);
    }
    l=adjust(l,p.lightness);
    const auto q=l<.5L?l*(1+saturation):l+saturation-l*saturation;
    const auto low=2*l-q;
    const auto channel=[&](long double t){t-=std::floor(t);if(t<1.L/6)return low+(q-low)*6*t;if(t<.5L)return q;if(t<2.L/3)return low+(q-low)*(2.L/3-t)*6;return low;};
    std::array<int,3> result{};
    const std::array<long double,3> values{saturation==0?l:channel(h+1.L/3),saturation==0?l:channel(h),saturation==0?l:channel(h-1.L/3)};
    for(int i=0;i<3;++i)result[i]=int(std::lround(std::clamp(values[i],0.L,1.L)*255));
    return result;
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
class HueSaturationTest : public QObject {
    Q_OBJECT
private slots:
    void numericContract() {
        QVERIFY(validAdjustment(HueSaturationParameters{})); QVERIFY(identityAdjustment(HueSaturationParameters{}));
        for(auto p:{HueSaturationParameters{181,0,0},{-181,0,0},{0,101,0},{0,-101,0},{0,0,101},{0,0,-101}}) QVERIFY(!validAdjustment(p));
        const std::array params{HueSaturationParameters{},HueSaturationParameters{120,0,0},HueSaturationParameters{-120,0,0},
            HueSaturationParameters{-180,100,100},HueSaturationParameters{180,-100,-100},HueSaturationParameters{75,-25,10},
            HueSaturationParameters{0,100,0},HueSaturationParameters{0,-100,0},HueSaturationParameters{0,0,100},HueSaturationParameters{0,0,-100}};
        QCOMPARE(hueSaturationRgb({120,0,0},{1,0,0}),(std::array<unsigned char,3>{0,255,0}));
        QCOMPARE(hueSaturationRgb({-120,0,0},{1,0,0}),(std::array<unsigned char,3>{0,0,255}));
        QCOMPARE(hueSaturationRgb({0,-100,0},{1,0,0}),(std::array<unsigned char,3>{128,128,128}));
        for(const auto& p:params){
            QVERIFY(validAdjustment(p));
            // A deterministic RGB grid includes primaries, secondaries and near-gray samples.
            for(int r:{0,1,127,128,254,255})for(int g:{0,1,127,128,254,255})for(int b:{0,1,127,128,254,255}){
                const std::array rgb{r/255.,g/255.,b/255.};const auto actual=adjustmentRgb(p,rgb);const auto expected=referenceRgb(rgb,p);
                for(int i=0;i<3;++i)QVERIFY(std::abs(int(actual[i])-expected[i])<=1);
                QCOMPARE(hueSaturationRgb({-180,p.saturation,p.lightness},rgb),hueSaturationRgb({180,p.saturation,p.lightness},rgb));
            }
            for(int alpha:{0,1,64,128,255})for(int mask:{0,80,255})for(int opacity:{0,47,100})for(int m=0;m<6;++m){
                QImage pixels(1,1,QImage::Format_ARGB32_Premultiplied);pixels.fill(QColor(180,90,50,alpha));const auto before=pixels.pixel(0,0);
                QImage coverage(1,1,QImage::Format_Grayscale8);coverage.fill(mask);const auto mode=static_cast<BlendMode>(m);
                QVERIFY(applyLayerAdjustment(&pixels,p,coverage,mode,opacity));const auto actual=pixels.pixel(0,0);QCOMPARE(qAlpha(actual),alpha);
                if(!alpha || !mask || !opacity || (p==HueSaturationParameters{} && m==0)){QCOMPARE(actual,before);continue;}
                const std::array rgb{double(qRed(before))/alpha,double(qGreen(before))/alpha,double(qBlue(before))/alpha};
                const auto mapped=referenceRgb(rgb,p);const int output[]{qRed(actual),qGreen(actual),qBlue(actual)};
                for(int c=0;c<3;++c){const double strength=mask/255.*opacity/100.;const auto expected=int(std::lround(((1-strength)*rgb[c]+strength*blendReference(mode,rgb[c],mapped[c]/255.))*alpha));QVERIFY(std::abs(output[c]-expected)<=1);}
            }
        }
        for(int gray=0;gray<256;++gray){
            QCOMPARE(hueSaturationRgb({123,100,0},{gray/255.,gray/255.,gray/255.}),(std::array<unsigned char,3>{static_cast<unsigned char>(gray),static_cast<unsigned char>(gray),static_cast<unsigned char>(gray)}));
        }
        QImage straight(2,2,QImage::Format_ARGB32);straight.fill(QColor(40,80,120,1));const auto before=straight;
        QVERIFY(applyLayerAdjustment(&straight,HueSaturationParameters{},{},BlendMode::Normal,100));QCOMPARE(straight,before);QCOMPARE(straight.format(),before.format());
        std::atomic_bool cancelled{true};QVERIFY(!applyLayerAdjustment(&straight,HueSaturationParameters{90,0,0},{},BlendMode::Normal,100,&cancelled));QCOMPARE(straight,before);
    }
    void modelHistoryExportAndCache() {
        ImageDocumentSession s; QVERIFY(s.createCanvas({80,60},QColor(40,80,150,170))); const auto base = s.selectedLayerId();
        QVERIFY(s.applyPaintStroke({{10,25},{60,25}},Qt::red,25)); const auto raw = s.renderedImage();
        QPainterPath selection; selection.addRect(0,0,80,60); selection.addEllipse(25,15,20,20);
        const auto id = s.addHueSaturationAdjustment(selection); QVERIFY(!id.isEmpty()); QCOMPARE(s.renderedImage(),raw);
        QVERIFY(s.setLayerClipping(id,true)); QCOMPARE(s.renderedImage(),raw); QVERIFY(s.setLayerClipping(id,false));
        QVERIFY(!s.selectedLayerIsEditable()); QVERIFY(s.selectedLayerCanEditMask()); QVERIFY(!s.applyPaintStroke({{10,10}},Qt::red,10));
        QVERIFY(!s.setLayerOutline(id,ImageLayerOutlineData{})); QVERIFY(!s.setBrightnessContrast(id,{1,1})); QVERIFY(!s.setGradientMap(id,{}));
        s.beginLayerAdjustmentEdit(); QVERIFY(s.setHueSaturation(id,{30,0,0})); QVERIFY(s.setHueSaturation(id,{75,-25,10})); s.endLayerAdjustmentEdit();
        QVERIFY(s.undo()); QCOMPARE(std::get<HueSaturationParameters>(*findLayer(s.data(),id)->adjustment),HueSaturationParameters{});
        QCOMPARE(s.renderedImage(),raw); QVERIFY(!s.setHueSaturation(id,{})); QVERIFY(s.redo()); QVERIFY(!s.setHueSaturation(id,{181,0,0}));
        QVERIFY(s.setLayerClipping(id,true)); const auto gradient = s.addGradientMapAdjustment(); QVERIFY(s.setLayerClipping(gradient,true));
        const auto brightness = s.addBrightnessContrastAdjustment(); QVERIFY(s.setBrightnessContrast(brightness,{25,-30})); QVERIFY(s.setLayerClipping(brightness,true)); QVERIFY(!s.setHueSaturation(brightness,{1,0,1}));
        const auto exposure = s.addExposureOffsetGammaAdjustment(); QVERIFY(s.setExposureOffsetGamma(exposure,{1.25,-.0123,1.4})); QVERIFY(s.setLayerClipping(exposure,true)); QVERIFY(!s.setHueSaturation(exposure,{1,0,0}));
        const auto upper = s.addLayer(); QVERIFY(s.applyPaintStroke({{10,25},{60,25}},QColor(20,180,240,180),12)); QVERIFY(s.setLayerClipping(upper,true));
        ImageInteractiveRasterCache cache;
        const auto verify = [&] { QCOMPARE(ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"hue_saturation")),s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied)); };
        verify(); const auto work = cache.reconstructedPixels(); QVERIFY(s.setHueSaturation(id,{-45,20,-10})); verify(); QCOMPARE(cache.reconstructedPixels(),work);
        QVERIFY(s.selectLayer(id)); QVERIFY(s.applyLayerMaskStroke({{10,10}},Qt::black,10)); verify(); QVERIFY(s.undo()); verify(); QVERIFY(s.redo()); verify();
        const auto group = s.groupLayers({base,id,gradient,brightness,exposure,upper}); QVERIFY(!group.isEmpty()); QVERIFY(s.setGroupOpacity(group,60)); s.rotateRight(); verify();
        QTemporaryDir dir; const auto path = dir.filePath("hue_saturation.cimg"); QVERIFY(s.saveDocument(path)); ImageDocumentSession loaded; QVERIFY(loaded.openDocument(path)); QCOMPARE(loaded.data(),s.data());
        RecoveryDocumentData recovery{s.data(),path,"hue_saturation"}, read; QVERIFY(ImageDocumentStore::saveRecovery(dir.filePath("recovery.json"),recovery)); QVERIFY(ImageDocumentStore::loadRecovery(dir.filePath("recovery.json"),&read)); QCOMPARE(read.document,s.data());
        QVERIFY(s.selectLayer(id)); ImageExportOptions options; options.scope=ImageExportScope::SelectedLayer; QVERIFY(!s.exportImage(dir.filePath("isolated.png"),options));
        options.scope=ImageExportScope::Composite; QVERIFY(s.exportImage(dir.filePath("full.png"),options)); QCOMPARE(QImage(dir.filePath("full.png")).convertToFormat(QImage::Format_ARGB32_Premultiplied),s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
        QVERIFY(s.exportImage(dir.filePath("full.jpg"),options)); QVERIFY(!QImage(dir.filePath("full.jpg")).isNull());
        QVERIFY(s.selectGroup(group)); options.scope=ImageExportScope::SelectedGroup; QVERIFY(s.exportImage(dir.filePath("group.png"),options));
        const auto snapshot=s.exportSnapshot(); QCOMPARE(QImage(dir.filePath("group.png")).convertToFormat(QImage::Format_ARGB32_Premultiplied),ImageDocumentRenderer::selectedGroup(snapshot.document,snapshot.source_image,snapshot.raster_images,group).convertToFormat(QImage::Format_ARGB32_Premultiplied));
        QVERIFY(!ImageDocumentRenderer::layerThumbnail(s.data(),s.exportSnapshot().source_image,s.exportSnapshot().raster_images,*findLayer(s.data(),id),{32,32}).isNull());
    }
    void mixedAdjustmentOrder() {
        ImageDocumentSession s;QVERIFY(s.createCanvas({8,8},QColor(80,120,160)));
        const auto gradient=s.addGradientMapAdjustment();QVERIFY(s.setGradientMap(gradient,{{{0,{20,40,160}},{1,{210,180,30}}}}));
        const auto brightness=s.addBrightnessContrastAdjustment();QVERIFY(s.setBrightnessContrast(brightness,{20,-30}));
        const auto exposure=s.addExposureOffsetGammaAdjustment();QVERIFY(s.setExposureOffsetGamma(exposure,{.5,-.01,1.2}));
        const auto hue=s.addHueSaturationAdjustment();QVERIFY(s.setHueSaturation(hue,{120,20,-10}));const auto before=s.renderedImage();
        QVERIFY(s.moveStackItem(hue,false,{},2));QCOMPARE(s.data().root_stack[2].id,hue);const auto after=s.renderedImage();QVERIFY(after!=before);
        QVERIFY(s.undo());QCOMPARE(s.renderedImage(),before);QVERIFY(s.redo());QCOMPARE(s.renderedImage(),after);
    }
    void chromaticThumbnail() {
        ImageDocumentSession s;QVERIFY(s.createCanvas({8,8},Qt::transparent));const auto id=s.addHueSaturationAdjustment();
        const auto thumbnail=[&]{const auto snap=s.exportSnapshot();return ImageDocumentRenderer::layerThumbnail(s.data(),snap.source_image,snap.raster_images,*findLayer(s.data(),id),{7,3});};
        const auto neutral=thumbnail();QCOMPARE(neutral.pixelColor(0,0),QColor(Qt::red));QCOMPARE(neutral.pixelColor(2,0),QColor(Qt::green));
        QVERIFY(s.setHueSaturation(id,{120,0,0}));QCOMPARE(thumbnail().pixelColor(0,0),QColor(Qt::green));
        QVERIFY(s.setHueSaturation(id,{0,-100,0}));const auto gray=thumbnail();for(int x=0;x<gray.width();++x)QCOMPARE(gray.pixelColor(x,0),QColor(128,128,128));
    }
    void persistenceValidation() {
        QTemporaryDir dir; const auto path=dir.filePath("hue_saturation.cimg"); ImageDocumentSession s; QVERIFY(s.createCanvas({8,8},Qt::red));
        const auto id=s.addHueSaturationAdjustment(); QVERIFY(s.setHueSaturation(id,{75,-25,10})); QVERIFY(s.saveDocument(path));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto json=QJsonDocument::fromJson(file.readAll()).object(); file.close(); QCOMPARE(json.value("version").toInt(),ImageDocumentStore::kCurrentDocumentVersion);
        ImageDocumentSession loaded; QVERIFY(loaded.openDocument(path)); QCOMPARE(loaded.data(),s.data());
        for (int bad=0; bad<22; ++bad) {
            auto root=json; auto layers=root.value("layers").toArray(); auto layer=layers.last().toObject(); auto a=layer.value("adjustment").toObject();
            switch(bad) {
            case 0: root.insert("version",22); break;
            case 1: root.insert("version",ImageDocumentStore::kCurrentDocumentVersion+1); break;
            case 2: a.insert("hue",181); break;
            case 3: a.insert("saturation",-101); break;
            case 4: a.insert("lightness",101); break;
            case 5: a.insert("lightness",-101); break;
            case 6: a.insert("hue","75"); break;
            case 7: a.insert("saturation",true); break;
            case 8: a.insert("lightness",QJsonValue::Null); break;
            case 9: a.remove("hue"); break;
            case 10: a.remove("saturation"); break;
            case 11: a.remove("lightness"); break;
            case 12: a.insert("type","future"); break;
            case 13: layer.insert("kind","background"); break;
            case 14: a.insert("stops",QJsonArray{}); break;
            case 15: a.insert("brightness",0); break;
            case 16: layer.insert("kind","raster"); break;
            case 17: a.insert("contrast",0); break;
            case 18: a.insert("hue",.5); break;
            case 19: a.insert("exposure",1); break;
            case 20: a.insert("offset",0); break;
            case 21: a.insert("gamma",1); break;
            }
            layer.insert("adjustment",a); layers[layers.size()-1]=layer; root.insert("layers",layers);
            QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate)); QVERIFY(file.write(QJsonDocument(root).toJson())>0); file.close();
            const auto before=loaded.data(); QVERIFY2(!loaded.openDocument(path),qPrintable(QString::number(bad))); QCOMPARE(loaded.data(),before);
        }
        // Actual old adjustment formats remain readable and save through the current writer.
        for (int version : {20,21,22}) {
            auto root=json; root.insert("version",version); auto layers=root.value("layers").toArray(); auto layer=layers.last().toObject();
            layer.insert("adjustment",version==20 ? QJsonObject{{"type","gradient_map"},{"stops",QJsonArray{QJsonObject{{"position",0},{"color","#000000"}},QJsonObject{{"position",1},{"color","#ffffff"}}}}} : version==21 ? QJsonObject{{"type","brightness_contrast"},{"brightness",25},{"contrast",-30}} : QJsonObject{{"type","exposure_offset_gamma"},{"exposure",1.25},{"offset",-.0123},{"gamma",1.4}});
            layers[layers.size()-1]=layer; root.insert("layers",layers); QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate)); file.write(QJsonDocument(root).toJson()); file.close();
            QVERIFY(loaded.openDocument(path)); QVERIFY(loaded.saveDocument(path)); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value("version").toInt(),ImageDocumentStore::kCurrentDocumentVersion); file.close();
        }
    }
    void reference1080p() {
        ImageDocumentSession s; QVERIFY(s.createCanvas({1920,1080},Qt::transparent)); QStringList ids;
        for(int i=0;i<5;++i) { if(i) QVERIFY(!s.addLayer().isEmpty()); ids.append(s.selectedLayerId()); QVERIFY(s.applyPaintStroke({{200.,300.+i*30},{1600.,300.+i*30}},QColor(30*i,80,180,190),220)); }
        QVERIFY(!s.groupLayers(ids).isEmpty()); const auto id=s.addHueSaturationAdjustment(); QVERIFY(s.setHueSaturation(id,{75,-25,10}));
        ImageInteractiveRasterCache cache; QElapsedTimer timer; timer.start();
        const auto render=[&] { const auto result=ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"hue_saturation-1080p")); QVERIFY(!result.isNull()); };
        render(); const auto cold=timer.elapsed(); const auto work=cache.reconstructedPixels(); timer.restart(); render(); const auto warm=timer.elapsed();
        QVERIFY(s.setHueSaturation(id,{30,-25,10})); timer.restart(); render(); const auto hue_saturation=timer.elapsed();
        QVERIFY(s.setHueSaturation(id,{30,20,10})); timer.restart(); render(); const auto saturation=timer.elapsed();
        QVERIFY(s.setHueSaturation(id,{30,20,-10})); timer.restart(); render(); const auto lightness=timer.elapsed(); QCOMPARE(cache.reconstructedPixels(),work);
        QVERIFY(cache.retainedBytes()<=64ULL*1024*1024);
        qInfo()<<"hue_saturation_1080p cold_ms"<<cold<<"warm_ms"<<warm<<"hue_saturation_ms"<<hue_saturation<<"saturation_ms"<<saturation<<"lightness_ms"<<lightness<<"cache_bytes"<<cache.retainedBytes();
    }
};
QTEST_MAIN(HueSaturationTest)
#include "image_editor_hue_saturation_test.moc"
