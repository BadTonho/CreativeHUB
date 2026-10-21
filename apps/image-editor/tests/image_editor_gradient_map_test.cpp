#include "image_document_session.h"
#include "image_document_utils.h"
#include "image_layer_clipping.h"
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
using namespace image_editor;
using creative_suite::composition::BlendMode;
using creative_suite::effects::GradientMapParameters;
namespace {
GradientMapParameters colored(){return {{{0,{220,20,50}},{.4,{20,210,60}},{1,{30,70,240}}}};}
void comparePixels(const QImage& a,const QImage& b,int tolerance=1){QCOMPARE(a.size(),b.size());auto p=a.convertToFormat(QImage::Format_ARGB32_Premultiplied),q=b.convertToFormat(p.format());for(int y=0;y<p.height();++y)for(int x=0;x<p.width();++x){auto c=p.pixel(x,y),d=q.pixel(x,y);QVERIFY2(std::abs(qRed(c)-qRed(d))<=tolerance && std::abs(qGreen(c)-qGreen(d))<=tolerance && std::abs(qBlue(c)-qBlue(d))<=tolerance && qAlpha(c)==qAlpha(d),qPrintable(QString("Pixel %1,%2 %3/%4").arg(x).arg(y).arg(c,0,16).arg(d,0,16)));}}
double reference(BlendMode m,double b,double s){switch(m){case BlendMode::Normal:return s;case BlendMode::Multiply:return b*s;case BlendMode::Screen:return b+s-b*s;case BlendMode::Overlay:return b<=.5?2*b*s:1-2*(1-b)*(1-s);case BlendMode::Add:return std::min(1.,b+s);case BlendMode::Difference:return std::abs(b-s);}return 0;}
}
class GradientMapTest:public QObject {
 Q_OBJECT
private slots:
 void numericContract(){
    using namespace creative_suite::effects;
    const GradientMapParameters defaults;QVERIFY(validGradientMap(defaults));QCOMPARE(gradientMapColor(defaults,0)[0],0);QCOMPARE(gradientMapColor(defaults,1)[0],255);QCOMPARE(gradientMapColor(defaults,.5)[0],128);
    auto bad=colored();bad.stops[1].position=.0005;QVERIFY(!validGradientMap(bad));bad.stops.resize(1);QVERIFY(!validGradientMap(bad));
    for(int alpha:{0,1,64,128,255})for(int mask:{0,80,255})for(int opacity:{0,47,100})for(int mode=0;mode<6;++mode){
        QImage pixels(2,2,QImage::Format_ARGB32_Premultiplied);pixels.fill(QColor(180,90,50,alpha));const QRgb original=pixels.pixel(0,0);
        QImage coverage(2,2,QImage::Format_Grayscale8);coverage.fill(mask);QVERIFY(applyLayerAdjustment(&pixels,defaults,coverage,static_cast<BlendMode>(mode),opacity));const auto actual=pixels.pixel(0,0);QCOMPARE(qAlpha(actual),alpha);
        if(!alpha){QCOMPARE(actual,original);continue;}
        const double rgb[]{double(qRed(original))/alpha,double(qGreen(original))/alpha,double(qBlue(original))/alpha};const double mapped=std::round((.2126*rgb[0]+.7152*rgb[1]+.0722*rgb[2])*255)/255.;const double strength=mask/255.*opacity/100.;
        const auto c=[&](int i){return int(std::lround(((1-strength)*rgb[i]+strength*reference(static_cast<BlendMode>(mode),rgb[i],mapped))*alpha));};
        QCOMPARE(qRed(actual),c(0));QCOMPARE(qGreen(actual),c(1));QCOMPARE(qBlue(actual),c(2));
    }
    QImage image(4,4,QImage::Format_ARGB32);image.fill(Qt::red);std::atomic_bool cancel{true};QVERIFY(!applyLayerAdjustment(&image,defaults,{},BlendMode::Normal,100,&cancel));
 }
 void modelMaskHistoryAndScope(){
    ImageDocumentSession s;QVERIFY(s.createCanvas({80,60},Qt::transparent));const auto base=s.selectedLayerId();QVERIFY(s.applyPaintStroke({{10,25},{60,25}},Qt::red,30));
    const auto child=s.addLayer();QVERIFY(s.applyPaintStroke({{10,25},{60,25}},Qt::blue,15));QVERIFY(s.setLayerClipping(child,true));QVERIFY(s.selectLayer(base));
    QPainterPath selection;selection.addRect(0,0,80,60);selection.addEllipse(25,15,20,20);const auto id=s.addGradientMapAdjustment(selection);QVERIFY(!id.isEmpty());QVERIFY(clippingChain(s.data(),base).contains(child));QCOMPARE(s.data().root_stack.back().id,id);
    QVERIFY(!s.selectedLayerIsEditable());QVERIFY(s.selectedLayerCanEditMask());QVERIFY(!s.applyPaintStroke({{10,10}},Qt::red,10));QVERIFY(!s.setLayerOutline(id,ImageLayerOutlineData{}));
    auto p=colored();s.beginLayerAdjustmentEdit();QVERIFY(s.setGradientMap(id,p));p.stops[1].position=.5;QVERIFY(s.setGradientMap(id,p));s.endLayerAdjustmentEdit();QVERIFY(s.undo());QCOMPARE(std::get<GradientMapParameters>(*findLayer(s.data(),id)->adjustment),GradientMapParameters{});
    QVERIFY(!s.setGradientMap(id,{}));QVERIFY(s.redo());QCOMPARE(std::get<GradientMapParameters>(*findLayer(s.data(),id)->adjustment),p);
    s.beginLayerAdjustmentEdit();auto before=p;p.stops[0].color={1,2,3};QVERIFY(s.setGradientMap(id,p));QVERIFY(s.setGradientMap(id,before));s.endLayerAdjustmentEdit();QVERIFY(s.undo());QCOMPARE(std::get<GradientMapParameters>(*findLayer(s.data(),id)->adjustment),GradientMapParameters{});QVERIFY(s.redo());
    QVERIFY(s.applyLayerMaskStroke({{10,10}},Qt::black,10));const auto mask=s.editableSelectionTargetAt({10,10},true);QVERIFY(mask);
    ImagePreview preview;preview.kind=ImagePreviewKind::MaskPaint;preview.mask=true;preview.points={{15,15}};preview.color=Qt::black;preview.diameter=10;const auto transient=s.interactiveSnapshot(preview);QVERIFY(transient.document!=s.data());
    auto pixels=ImageDocumentRenderer::maskPixels({80,60},*findLayer(s.data(),id)->mask);QCOMPARE(qGray(pixels.pixel(35,25)),0);QCOMPARE(qGray(pixels.pixel(70,50)),255);
    const auto seed=findLayer(s.data(),id)->mask->initial_selection;QVERIFY(s.resizeCanvas({90,70},CanvasAnchor::Center));QTransform shift;shift.translate(5,5);QCOMPARE(*findLayer(s.data(),id)->mask->initial_selection,shift.map(*seed));auto resized=ImageDocumentRenderer::maskPixels({90,70},*findLayer(s.data(),id)->mask);QCOMPARE(qGray(resized.pixel(0,0)),0);QCOMPARE(qGray(resized.pixel(75,55)),255);QVERIFY(s.undo());QCOMPARE(findLayer(s.data(),id)->mask->initial_selection,seed);
    QVERIFY(s.setLayerClipping(id,true));QVERIFY(s.setLayerClipping(id,false));QVERIFY(s.selectLayer(base));const auto group=s.groupLayers({base,child,id});QVERIFY(!group.isEmpty());QVERIFY(s.setGroupOpacity(group,55));s.rotateRight();
    ImageInteractiveRasterCache cache;comparePixels(ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"scope")),s.renderedImage());
 }
 void scenesCachesAndExports(){
    QTemporaryDir dir;ImageDocumentSession s;QVERIFY(s.createCanvas({100,70},QColor(80,120,170,160)));const auto base=s.selectedLayerId();QVERIFY(s.applyPaintStroke({{10,25},{80,25}},QColor(200,30,40,190),30));
    ImageLayerOutlineData outline;outline.color=Qt::green;QVERIFY(s.setLayerOutline(base,outline));const auto id=s.addGradientMapAdjustment();QVERIFY(s.setGradientMap(id,colored()));QVERIFY(s.setLayerClipping(id,true));
    const auto upper=s.addLayer();QVERIFY(s.applyPaintStroke({{10,25},{80,25}},QColor(40,180,210,180),14));QVERIFY(s.setLayerClipping(upper,true));ImageInteractiveRasterCache cache;
    const auto verify=[&]{comparePixels(ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"scene")),s.renderedImage());QVERIFY(cache.retainedBytes()<=cache.maximum_bytes);};
    for(int mode=0;mode<6;++mode){(void)s.setStackBlendMode(id,false,static_cast<BlendMode>(mode));verify();}
    QVERIFY(s.setLayerClipping(id,false));QVERIFY(!findLayer(s.data(),upper)->clip_to_base);verify();QVERIFY(s.undo());verify();
    QVERIFY(s.selectLayer(id));ImageExportOptions options;options.scope=ImageExportScope::SelectedLayer;QVERIFY(!s.exportImage(dir.filePath("adjustment.png"),options));QVERIFY(!QFile::exists(dir.filePath("adjustment.png")));
    QVERIFY(s.exportImage(dir.filePath("full.png")));comparePixels(QImage(dir.filePath("full.png")),s.renderedImage(),0);
    QVERIFY(s.selectLayer(base));const auto raw=s.exportSnapshot();const auto isolated=ImageDocumentRenderer::selectedLayer(raw.document,raw.source_image,raw.raster_images,base);QVERIFY(s.setGradientMap(id,{}));const auto changed=s.exportSnapshot();QCOMPARE(ImageDocumentRenderer::selectedLayer(changed.document,changed.source_image,changed.raster_images,base),isolated);
    QVERIFY(s.selectLayer(id));QVERIFY(s.setLayerClipping(id,false));verify();const auto work=cache.reconstructedPixels();QVERIFY(s.setGradientMap(id,colored()));verify();QCOMPARE(cache.reconstructedPixels(),work);
 }
 void transformedGroupSelection(){
    ImageDocumentSession s;QVERIFY(s.createCanvas({48,32},Qt::transparent));const auto base=s.selectedLayerId();QVERIFY(s.applyPaintStroke({{20,15}},Qt::red,100));const auto upper=s.addLayer();const auto group=s.groupLayers({base,upper});QVERIFY(!group.isEmpty());s.rotateRight();const auto before=s.renderedImage();QVERIFY(s.selectLayer(base));QPainterPath selection;selection.addRect(20,5,6,8);const auto adjustment=s.addGradientMapAdjustment(selection);QVERIFY(!adjustment.isEmpty());const auto after=s.renderedImage();QVERIFY(after.pixelColor(23,8).red()!=before.pixelColor(23,8).red());QCOMPARE(after.pixelColor(23,8).red(),after.pixelColor(23,8).green());QCOMPARE(after.pixel(28,24),before.pixel(28,24));ImageInteractiveRasterCache cache;comparePixels(ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"group-seed")),after);
 }
 void persistenceAndRecovery(){
    QTemporaryDir dir;ImageDocumentSession s;QVERIFY(s.createCanvas({48,32},Qt::transparent));QPainterPath path;path.addEllipse(4,3,30,20);const auto id=s.addGradientMapAdjustment(path);QVERIFY(s.setGradientMap(id,colored()));
    const auto file=dir.filePath("test.cimg");QVERIFY(s.saveDocument(file));ImageDocumentSession loaded;QVERIFY(loaded.openDocument(file));QCOMPARE(loaded.data(),s.data());
    RecoveryDocumentData recovery{s.data(),file,"gradient"},read;QVERIFY(ImageDocumentStore::saveRecovery(dir.filePath("recovery.json"),recovery));QVERIFY(ImageDocumentStore::loadRecovery(dir.filePath("recovery.json"),&read));QCOMPARE(read.document,s.data());
    QFile f(file);QVERIFY(f.open(QIODevice::ReadOnly));const auto root=QJsonDocument::fromJson(f.readAll()).object();f.close();QCOMPARE(root.value("version").toInt(),ImageDocumentStore::kCurrentDocumentVersion);
    for(int bad=0;bad<5;++bad){auto json=root;auto layers=json.value("layers").toArray();auto layer=layers.last().toObject();auto adjustment=layer.value("adjustment").toObject();
        if(bad==0)json.insert("version",19);if(bad==1)adjustment.insert("type","unknown");if(bad==2)adjustment.insert("stops",QJsonArray{});if(bad==3)layer.insert("kind","raster");if(bad==4){auto mask=layer.value("mask").toObject();mask.insert("initial_selection",false);layer.insert("mask",mask);}
        layer.insert("adjustment",adjustment);layers[layers.size()-1]=layer;json.insert("layers",layers);QVERIFY(f.open(QIODevice::WriteOnly|QIODevice::Truncate));f.write(QJsonDocument(json).toJson());f.close();const auto before=loaded.data();QVERIFY(!loaded.openDocument(file));QCOMPARE(loaded.data(),before);
    }
 }
 void brightnessNumeric(){
    using namespace creative_suite::effects;
    for(int b:{-100,-31,0,27,100})for(int c:{-100,-40,0,65,100})for(int alpha:{0,1,64,128,255})for(int mask:{0,91,255})for(int opacity:{0,47,100})for(int mode=0;mode<6;++mode){
        QImage image(1,1,QImage::Format_ARGB32_Premultiplied);image.fill(QColor(180,90,50,alpha));const auto before=image.pixel(0,0);QImage coverage(1,1,QImage::Format_Grayscale8);coverage.fill(mask);
        QVERIFY(applyLayerAdjustment(&image,BrightnessContrastParameters{b,c},coverage,static_cast<BlendMode>(mode),opacity));auto actual=image.pixel(0,0);QCOMPARE(qAlpha(actual),alpha);if(!alpha)continue;
        const double w=mask/255.*opacity/100.;const int channels[]{qRed(before),qGreen(before),qBlue(before)};
        for(int channel=0;channel<3;++channel){const double v=channels[channel]/double(alpha);const double adjusted=std::round(std::clamp((v+b/100.-.5)*(1+c/100.)+.5,0.,1.)*255.)/255.;
            const auto expected=int(std::round(((1-w)*v+w*reference(static_cast<BlendMode>(mode),v,adjusted))*alpha));const int result[]{qRed(actual),qGreen(actual),qBlue(actual)};QCOMPARE(result[channel],expected);}
    }
    QImage identity(16,16,QImage::Format_ARGB32_Premultiplied);for(int y=0;y<16;++y)for(int x=0;x<16;++x)identity.setPixel(x,y,qPremultiply(qRgba(x*16,y*16,123,x*16+y)));const auto before=identity;QVERIFY(applyLayerAdjustment(&identity,BrightnessContrastParameters{}, {},BlendMode::Normal,100));QCOMPARE(identity,before);
    QVERIFY(!validBrightnessContrast({101,0}));std::atomic_bool cancel{true};QVERIFY(!applyLayerAdjustment(&identity,BrightnessContrastParameters{10,20},{},BlendMode::Normal,100,&cancel));QCOMPARE(identity,before);
 }
 void brightnessModelPersistence(){
    using creative_suite::effects::BrightnessContrastParameters;
    ImageDocumentSession s;QVERIFY(s.createCanvas({80,60},QColor(40,80,150,170)));const auto base=s.selectedLayerId();QVERIFY(s.applyPaintStroke({{10,25},{60,25}},Qt::red,25));
    const auto raw=s.renderedImage();QPainterPath selection;selection.addRect(0,0,80,60);selection.addEllipse(25,15,20,20);const auto id=s.addBrightnessContrastAdjustment(selection);QVERIFY(!id.isEmpty());comparePixels(s.renderedImage(),raw,0);QVERIFY(s.setLayerClipping(id,true));comparePixels(s.renderedImage(),raw,0);QVERIFY(s.setLayerClipping(id,false));
    QVERIFY(!s.selectedLayerIsEditable());QVERIFY(s.selectedLayerCanEditMask());QVERIFY(!s.applyPaintStroke({{10,10}},Qt::red,10));QVERIFY(!s.setLayerOutline(id,ImageLayerOutlineData{}));QVERIFY(!s.setGradientMap(id,{}));
    s.beginLayerAdjustmentEdit();QVERIFY(s.setBrightnessContrast(id,{10,20}));QVERIFY(s.setBrightnessContrast(id,{30,40}));s.endLayerAdjustmentEdit();QVERIFY(s.undo());QCOMPARE(std::get<BrightnessContrastParameters>(*findLayer(s.data(),id)->adjustment),BrightnessContrastParameters{});
    QVERIFY(!s.setBrightnessContrast(id,{}));QVERIFY(s.redo());QVERIFY(!s.setBrightnessContrast(id,{101,0}));
    QVERIFY(s.setLayerClipping(id,true));const auto gradient=s.addGradientMapAdjustment();QVERIFY(s.setGradientMap(gradient,colored()));QVERIFY(!s.setBrightnessContrast(gradient,{10,10}));QVERIFY(s.setLayerClipping(gradient,true));
    ImageLayerOutlineData outline;outline.color=Qt::green;QVERIFY(s.setLayerOutline(base,outline));const auto upper=s.addLayer();QVERIFY(s.applyPaintStroke({{10,25},{60,25}},QColor(20,180,240,180),12));QVERIFY(s.setLayerClipping(upper,true));
    ImageInteractiveRasterCache cache;const auto verify=[&]{comparePixels(ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"bc")),s.renderedImage(),0);};verify();const auto work=cache.reconstructedPixels();QVERIFY(s.setBrightnessContrast(id,{-20,65}));verify();QCOMPARE(cache.reconstructedPixels(),work);
    QVERIFY(s.setLayerClipping(id,false));QVERIFY(!findLayer(s.data(),gradient)->clip_to_base);QVERIFY(!findLayer(s.data(),upper)->clip_to_base);QVERIFY(s.undo());verify();
    const auto group=s.groupLayers({base,id,gradient,upper});QVERIFY(!group.isEmpty());QVERIFY(s.setGroupOpacity(group,60));s.rotateRight();verify();
    QTemporaryDir dir;const auto path=dir.filePath("bc.cimg");QVERIFY(s.saveDocument(path));ImageDocumentSession loaded;QVERIFY(loaded.openDocument(path));QCOMPARE(loaded.data(),s.data());
    RecoveryDocumentData recovery{s.data(),path,"brightness"},read;QVERIFY(ImageDocumentStore::saveRecovery(dir.filePath("recovery.json"),recovery));QVERIFY(ImageDocumentStore::loadRecovery(dir.filePath("recovery.json"),&read));QCOMPARE(read.document,s.data());
    // Validate a simple root fixture with the real loader, preserving the open session on failure.
    ImageDocumentSession simple;QVERIFY(simple.createCanvas({8,8},Qt::red));const auto aid=simple.addBrightnessContrastAdjustment();QVERIFY(simple.setBrightnessContrast(aid,{25,-30}));QVERIFY(simple.saveDocument(path));QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));const auto json=QJsonDocument::fromJson(file.readAll()).object();file.close();QCOMPARE(json.value("version").toInt(),21);
    for(int bad=0;bad<9;++bad){auto root=json;auto layers=root.value("layers").toArray();auto layer=layers.last().toObject();auto a=layer.value("adjustment").toObject();
        if(bad==0)root.insert("version",20);if(bad==1)a.insert("brightness",101);if(bad==2)a.insert("contrast",.5);if(bad==3)a.insert("brightness","25");if(bad==4)a.remove("contrast");if(bad==5)a.insert("type","future");if(bad==6)layer.insert("kind","background");if(bad==7)a.insert("stops",QJsonArray{});if(bad==8)root.insert("version",22);
        layer.insert("adjustment",a);layers[layers.size()-1]=layer;root.insert("layers",layers);QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));file.write(QJsonDocument(root).toJson());file.close();const auto before=loaded.data();QVERIFY(!loaded.openDocument(path));QCOMPARE(loaded.data(),before);
    }
    QVERIFY(s.selectLayer(id));ImageExportOptions options;options.scope=ImageExportScope::SelectedLayer;QVERIFY(!s.exportImage(dir.filePath("isolated.png"),options));options.scope=ImageExportScope::Composite;QVERIFY(s.exportImage(dir.filePath("full.png"),options));comparePixels(QImage(dir.filePath("full.png")),s.renderedImage(),0);QVERIFY(s.selectGroup(group));options.scope=ImageExportScope::SelectedGroup;QVERIFY(s.exportImage(dir.filePath("group.png"),options));const auto snapshot=s.exportSnapshot();comparePixels(QImage(dir.filePath("group.png")),ImageDocumentRenderer::selectedGroup(snapshot.document,snapshot.source_image,snapshot.raster_images,group),0);
 }
 void reference1080p(){
    ImageDocumentSession s;QVERIFY(s.createCanvas({1920,1080},Qt::transparent));QStringList ids;for(int i=0;i<5;++i){if(i)QVERIFY(!s.addLayer().isEmpty());ids.append(s.selectedLayerId());QVERIFY(s.applyPaintStroke({{200.,300.+i*30},{1600.,300.+i*30}},QColor(30*i,80,180,190),220));}
    const auto group=s.groupLayers(ids);QVERIFY(!group.isEmpty());const auto adjustment=s.addGradientMapAdjustment();QVERIFY(s.setGradientMap(adjustment,colored()));ImageInteractiveRasterCache cache;QElapsedTimer t;t.start();
    auto image=ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"1080"));QVERIFY(!image.isNull());const auto cold=t.elapsed();const auto work=cache.reconstructedPixels();t.restart();image=ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"1080"));const auto warm=t.elapsed();QCOMPARE(cache.reconstructedPixels(),work);
    auto points=colored();points.stops[1].color={180,50,160};QVERIFY(s.setGradientMap(adjustment,points));t.restart();image=ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"1080"));const auto edit=t.elapsed();QCOMPARE(cache.reconstructedPixels(),work);
    QVERIFY(s.selectLayer(ids.front()));QVERIFY(s.applyPaintStroke({{800,450}},Qt::yellow,30));t.restart();image=ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"1080"));
    qInfo()<<"gradient_map_1080p cold_ms"<<cold<<"warm_ms"<<warm<<"edit_ms"<<edit<<"base_ms"<<t.elapsed()<<"cache_bytes"<<cache.retainedBytes();
 }
 void brightnessReference1080p(){
    ImageDocumentSession s;QVERIFY(s.createCanvas({1920,1080},Qt::transparent));QStringList ids;for(int i=0;i<5;++i){if(i)QVERIFY(!s.addLayer().isEmpty());ids.append(s.selectedLayerId());QVERIFY(s.applyPaintStroke({{200.,300.+i*30},{1600.,300.+i*30}},QColor(30*i,80,180,190),220));}
    const auto group=s.groupLayers(ids);QVERIFY(!group.isEmpty());const auto adjustment=s.addBrightnessContrastAdjustment();QVERIFY(s.setBrightnessContrast(adjustment,{25,-30}));ImageInteractiveRasterCache cache;QElapsedTimer t;t.start();
    auto image=ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"1080"));QVERIFY(!image.isNull());const auto cold=t.elapsed();const auto work=cache.reconstructedPixels();t.restart();image=ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"1080"));const auto warm=t.elapsed();QCOMPARE(cache.reconstructedPixels(),work);
    QVERIFY(s.setBrightnessContrast(adjustment,{40,-30}));t.restart();image=ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"1080"));const auto edit=t.elapsed();QCOMPARE(cache.reconstructedPixels(),work);
    QVERIFY(s.setBrightnessContrast(adjustment,{40,50}));t.restart();image=ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"1080"));const auto contrast=t.elapsed();QCOMPARE(cache.reconstructedPixels(),work);
    QVERIFY(s.selectLayer(ids.front()));QVERIFY(s.applyPaintStroke({{800,450}},Qt::yellow,30));t.restart();image=ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"1080"));
    qInfo()<<"brightness_1080p cold_ms"<<cold<<"warm_ms"<<warm<<"brightness_ms"<<edit<<"contrast_ms"<<contrast<<"base_ms"<<t.elapsed()<<"cache_bytes"<<cache.retainedBytes();
 }
};
QTEST_MAIN(GradientMapTest)
#include "image_editor_gradient_map_test.moc"
