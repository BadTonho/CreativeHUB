#include "image_document_session.h"
#include "image_layer_clipping.h"
#include "rendering/image_document_renderer.h"
#include "rendering/image_interactive_raster_cache.h"
#include "rendering/image_layer_blending.h"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace image_editor;
using creative_suite::composition::BlendMode;
namespace {
double reference(BlendMode mode,double b,double s) {
    switch(mode) {
    case BlendMode::Normal:return s;
    case BlendMode::Multiply:return b*s;
    case BlendMode::Screen:return b+s-b*s;
    case BlendMode::Overlay:return b<.5?b*(2*s):1-(1-b)*(2-2*s);
    case BlendMode::Add:return std::min(1.,b+s);
    case BlendMode::Difference:return std::max(b,s)-std::min(b,s);
    }
    return 0;
}
void closePixels(const QImage& actual,const QImage& expected,int tolerance=1) {
    QCOMPARE(actual.size(),expected.size());
    const auto a=actual.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const auto b=expected.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for(int y=0;y<a.height();++y) for(int x=0;x<a.width();++x) {
        const auto p=a.pixel(x,y),q=b.pixel(x,y);
        QVERIFY2(std::abs(qRed(p)-qRed(q))<=tolerance && std::abs(qGreen(p)-qGreen(q))<=tolerance &&
            std::abs(qBlue(p)-qBlue(q))<=tolerance && std::abs(qAlpha(p)-qAlpha(q))<=tolerance,
            qPrintable(QString("%1,%2: %3 / %4").arg(x).arg(y).arg(p,0,16).arg(q,0,16)));
    }
}
}
class BlendTest final:public QObject {
    Q_OBJECT
private slots:
    void numericPixels() {
        using namespace creative_suite::composition;
        const double expected[]{.75,.1875,.8125,.375,1.,.5};
        for(int i=0;i<6;++i) QCOMPARE(blendChannel(static_cast<BlendMode>(i),.25,.75),expected[i]);
        for(int mode=1;mode<6;++mode) for(int ab:{0,1,64,128,255}) for(int as:{1,80,128,255})
            for(int opacity:{1,47,100}) for(bool atop:{false,true}) {
                QImage back(3,2,QImage::Format_ARGB32_Premultiplied); back.fill(QColor(70,180,245,ab));
                QImage src(back.size(),back.format()); src.fill(QColor(210,130,50,as));
                const auto original=back.pixel(0,0),source=src.pixel(0,0);
                QVERIFY(blendLayerPixels(&back,src,static_cast<BlendMode>(mode),opacity,nullptr,atop));
                const double a=as/255.*opacity/100.,b=ab/255.;
                const auto channel=[&](int bp,int sp) {
                    const double cb=ab?double(bp)/ab:0,cs=double(sp)/as;
                    const double value=(1-a)*bp/255.+(atop?0:a*(1-b)*cs)+a*b*reference(static_cast<BlendMode>(mode),cb,cs);
                    return int(std::lround(value*255));
                };
                const auto pixel=back.pixel(0,0);
                QCOMPARE(qRed(pixel),channel(qRed(original),qRed(source)));
                QCOMPARE(qGreen(pixel),channel(qGreen(original),qGreen(source)));
                QCOMPARE(qBlue(pixel),channel(qBlue(original),qBlue(source)));
                QCOMPARE(qAlpha(pixel),atop?ab:int(std::lround((a+b*(1-a))*255)));
            }
        QImage p(2,2,QImage::Format_ARGB32_Premultiplied);p.fill(Qt::red);
        std::atomic_bool cancel{true}; QVERIFY(!blendLayerPixels(&p,p,BlendMode::Multiply,100,&cancel));
        QVERIFY(!blendLayerPixels(&p,p,static_cast<BlendMode>(255),100));
    }
    void historyPersistenceRecovery() {
        QTemporaryDir dir; ImageDocumentSession s; QVERIFY(s.createCanvas({64,48},Qt::transparent));
        const auto id=s.selectedLayerId(); const auto before=s.data();
        QVERIFY(s.setStackBlendMode(id,false,BlendMode::Multiply));
        QVERIFY(!s.setStackBlendMode(id,false,BlendMode::Multiply));
        QVERIFY(!s.setStackBlendMode(s.data().layers.front().id,false,BlendMode::Multiply));
        QVERIFY(!s.setStackBlendMode(id,false,static_cast<BlendMode>(255)));
        QVERIFY(s.undo()); QCOMPARE(s.data(),before); QVERIFY(!s.setStackBlendMode(id,false,BlendMode::Normal));
        QVERIFY(s.redo()); const auto second=s.addLayer(); const auto group=s.groupLayers({id,second});
        QVERIFY(!group.isEmpty()); QVERIFY(s.setStackBlendMode(group,true,BlendMode::Screen));
        const auto path=dir.filePath("blend.cimg"); QVERIFY(s.saveDocument(path));
        ImageDocumentSession loaded; QVERIFY(loaded.openDocument(path)); QCOMPARE(loaded.data(),s.data());
        RecoveryDocumentData r{s.data(),path,"blend-session"},recovered;
        QVERIFY(ImageDocumentStore::saveRecovery(dir.filePath("recovery.json"),r));
        QVERIFY(ImageDocumentStore::loadRecovery(dir.filePath("recovery.json"),&recovered)); QCOMPARE(recovered.document,s.data());
        QFile f(path); QVERIFY(f.open(QIODevice::ReadOnly)); const auto root=QJsonDocument::fromJson(f.readAll()).object();f.close();
        QCOMPARE(root.value("version").toInt(),19);
        for(int bad=0;bad<4;++bad) {
            auto json=root;auto layers=json.value("layers").toArray();auto g=layers.last().toObject();
            if(bad==0) g.insert("blend_mode","unsupported");
            if(bad==1) g.insert("blend_mode",2);
            if(bad==2) json.insert("version",18);
            if(bad==3) {auto bg=layers[0].toObject();bg.insert("blend_mode","normal");layers[0]=bg;}
            layers[layers.size()-1]=g;json.insert("layers",layers);
            QVERIFY(f.open(QIODevice::WriteOnly|QIODevice::Truncate));f.write(QJsonDocument(json).toJson());f.close();
            const auto old=loaded.data(); QVERIFY(!loaded.openDocument(path)); QCOMPARE(loaded.data(),old);
        }
        auto legacy=root;legacy.insert("version",18);auto layers=legacy.value("layers").toArray();auto g=layers.last().toObject();
        g.remove("blend_mode");auto children=g.value("children").toArray();
        for(qsizetype i=0;i<children.size();++i){auto child=children[i].toObject();child.remove("blend_mode");children[i]=child;}
        g.insert("children",children);layers[layers.size()-1]=g;legacy.insert("layers",layers);
        QVERIFY(f.open(QIODevice::WriteOnly|QIODevice::Truncate));f.write(QJsonDocument(legacy).toJson());f.close();
        QVERIFY(loaded.openDocument(path)); QCOMPARE(loaded.data().groups.front().blend_mode,BlendMode::Normal);
        QVERIFY(s.ungroup(group)); QCOMPARE(findLayer(s.data(),id)->blend_mode,BlendMode::Multiply);
    }
    void scenesCachesOutlinesAndExport() {
        QTemporaryDir dir; ImageDocumentSession s; QVERIFY(s.createCanvas({96,72},QColor(80,140,190,170)));
        const auto base=s.selectedLayerId(); QVERIFY(s.applyPaintStroke({{15,25},{65,25}},QColor(230,70,140,190),28));
        const auto child=s.addLayer(); QVERIFY(s.applyPaintStroke({{5,25},{80,25}},QColor(50,220,90,170),40));
        QVERIFY(s.addLayerMask(child)); QVERIFY(s.applyLayerMaskStroke({{40,25}},QColor(128,128,128),18));
        ImageInteractiveRasterCache cache;
        const auto verify=[&] { const auto snap=s.exportSnapshot(); closePixels(ImageInteractiveRasterCache::composite(cache.prepare(snap,"blend")),s.renderedImage()); QVERIFY(cache.retainedBytes()<=cache.maximum_bytes); };
        verify(); const auto work=cache.reconstructedPixels();
        for(int mode=1;mode<6;++mode) { QVERIFY(s.setStackBlendMode(child,false,static_cast<BlendMode>(mode))); verify(); QCOMPARE(cache.reconstructedPixels(),work); }
        QVERIFY(s.setLayerClipping(child,true));
        ImageLayerOutlineData outline;outline.color=Qt::green;
        for(auto pos:{ImageOutlinePosition::Inside,ImageOutlinePosition::Outside}) {
            outline.position=pos; QVERIFY(s.setLayerOutline(base,outline));
            for(int mode=0;mode<6;++mode) { (void)s.setStackBlendMode(child,false,static_cast<BlendMode>(mode)); verify(); }
        }
        QVERIFY(s.setStackBlendMode(child,false,BlendMode::Multiply));verify();
        const auto cachedWork=cache.reconstructedPixels();
        QVERIFY(s.setStackBlendMode(child,false,BlendMode::Overlay));verify(); QCOMPARE(cache.reconstructedPixels(),cachedWork);
        const auto baseWork=cache.reconstructedPixels();
        QVERIFY(s.setStackBlendMode(base,false,BlendMode::Screen));verify();QCOMPARE(cache.reconstructedPixels(),baseWork);
        const auto group=s.groupLayers({base,child}); QVERIFY(!group.isEmpty());
        for(int mode=0;mode<6;++mode){(void)s.setStackBlendMode(group,true,static_cast<BlendMode>(mode));verify();}
        QVERIFY(s.setGroupOpacity(group,63));verify();s.rotateRight();verify();
        QVERIFY(s.setGroupVisible(group,false));verify();QVERIFY(s.undo());verify();QVERIFY(s.redo());verify();
        QVERIFY(s.setGroupVisible(group,true));
        QVERIFY(s.exportImage(dir.filePath("full.png"))); closePixels(QImage(dir.filePath("full.png")),s.renderedImage(),0);
        QVERIFY(s.selectLayer(child)); ImageExportOptions options;options.scope=ImageExportScope::SelectedLayer;
        QVERIFY(s.exportImage(dir.filePath("isolated.png"),options)); const auto snap=s.exportSnapshot();
        closePixels(QImage(dir.filePath("isolated.png")),ImageDocumentRenderer::selectedLayer(snap.document,snap.source_image,snap.raster_images,child),0);
        const auto isolated=QImage(dir.filePath("isolated.png"));
        QVERIFY(s.setStackBlendMode(child,false,BlendMode::Screen));
        QVERIFY(s.exportImage(dir.filePath("isolated2.png"),options));QCOMPARE(QImage(dir.filePath("isolated2.png")),isolated);
        std::atomic_bool cancel{true}; QVERIFY(cache.prepare(s.exportSnapshot(),"blend",false,&cancel).size.isEmpty()); verify();
    }
    void editableTextShapesImagesAndPreviews() {
        QTemporaryDir dir;ImageDocumentSession s;QVERIFY(s.createCanvas({160,100},QColor(100,140,180,180)));
        ImageShapeData shape;shape.start={4,4};shape.end={140,85};shape.fill_color=QColor(220,90,50,180);
        QVERIFY(!s.addShape(shape).isEmpty());const auto shapeLayer=s.selectedLayerId();
        ImageTextData text;text.content="Blend";text.position={10,10};text.box_width=140;text.font_pixel_size=28;
        QVERIFY(!s.addText(text).isEmpty());const auto textLayer=s.selectedLayerId();
        QImage image(160,100,QImage::Format_ARGB32_Premultiplied);image.fill(QColor(40,190,240,140));
        const auto path=dir.filePath("source.png");QVERIFY(image.save(path));QVERIFY(s.importRasterImages({{path,image}}));const auto rasterLayer=s.selectedLayerId();
        QVERIFY(s.setLayerClipping(rasterLayer,true));QVERIFY(s.addLayerMask(textLayer));QVERIFY(s.selectLayer(textLayer));
        QVERIFY(s.applyLayerMaskStroke({{60,25}},QColor(128,128,128),35));
        ImageLayerOutlineData outline;outline.position=ImageOutlinePosition::Inside;outline.color=Qt::green;
        QVERIFY(s.setLayerOutline(textLayer,outline));ImageInteractiveRasterCache cache;
        for(int mode=1;mode<6;++mode){
            const auto m=static_cast<BlendMode>(mode);QVERIFY(s.setStackBlendMode(shapeLayer,false,m));QVERIFY(s.setStackBlendMode(textLayer,false,m));QVERIFY(s.setStackBlendMode(rasterLayer,false,m));
            const auto snapshot=s.exportSnapshot();closePixels(ImageInteractiveRasterCache::composite(cache.prepare(snapshot,"objects")),s.renderedImage());
            ImagePreview preview;preview.kind=ImagePreviewKind::MaskPaint;preview.mask=true;preview.points={{25,25},{90,30}};preview.diameter=15;preview.color=Qt::black;
            const auto transient=s.interactiveSnapshot(preview);closePixels(ImageInteractiveRasterCache::composite(cache.prepare(transient,"objects",true)),ImageDocumentRenderer::composite(transient.document,transient.source_image,transient.raster_images));
            closePixels(ImageInteractiveRasterCache::composite(cache.prepare(snapshot,"objects")),s.renderedImage());
        }
        QVERIFY(s.selectLayer(rasterLayer));const auto raw=s.editableSelectionTargetAt({70,30},false);QVERIFY(raw);
        QVERIFY(s.setStackBlendMode(rasterLayer,false,BlendMode::Normal));QCOMPARE(s.editableSelectionTargetAt({70,30},false)->pixels,raw->pixels);
        QCOMPARE(QImage(path).convertToFormat(image.format()),image);
        const auto group=s.groupLayers({shapeLayer,textLayer,rasterLayer});QVERIFY(!group.isEmpty());
        const auto snap=s.exportSnapshot();const auto inside=ImageDocumentRenderer::selectedGroup(snap.document,snap.source_image,snap.raster_images,group);
        QVERIFY(s.setStackBlendMode(group,true,BlendMode::Difference));const auto changed=s.exportSnapshot();
        QCOMPARE(ImageDocumentRenderer::selectedGroup(changed.document,changed.source_image,changed.raster_images,group),inside);
    }
    void stackAndThumbnailDependencies() {
        ImageDocumentSession s;QVERIFY(s.createCanvas({48,32},Qt::transparent));
        const auto lower=s.selectedLayerId();QVERIFY(s.applyPaintStroke({{8,16},{40,16}},QColor(190,70,130),24));
        const auto upper=s.addLayer();QVERIFY(s.applyPaintStroke({{8,16},{40,16}},QColor(50,170,220),24));
        const auto isolated=s.renderedLayerThumbnails({48,32}).value(upper);
        QVERIFY(s.setStackBlendMode(upper,false,BlendMode::Multiply));
        QCOMPARE(s.renderedLayerThumbnails({48,32}).value(upper),isolated);
        const auto before=s.renderedImage();QVERIFY(s.moveLayer(upper,-1));
        QVERIFY(s.renderedImage()!=before);QCOMPARE(findLayer(s.data(),upper)->blend_mode,BlendMode::Multiply);
        QVERIFY(s.undo());QCOMPARE(s.renderedImage(),before);
        const auto group=s.groupLayers({lower,upper});QVERIFY(!group.isEmpty());
        const auto thumbnail=s.renderedLayerThumbnails({48,32}).value(group);
        QVERIFY(s.setStackBlendMode(upper,false,BlendMode::Screen));
        QVERIFY(s.renderedLayerThumbnails({48,32}).value(group)!=thumbnail);
        QVERIFY(s.setLayerVisible(upper,false));QVERIFY(s.setStackBlendMode(upper,false,BlendMode::Difference));
        QVERIFY(s.setStackBlendMode(group,true,BlendMode::Overlay));
        QVERIFY(s.moveStackItem(upper,false,{},s.data().root_stack.size()));
        QCOMPARE(findLayer(s.data(),upper)->blend_mode,BlendMode::Difference);
        QVERIFY(s.ungroup(group));QCOMPARE(findLayer(s.data(),lower)->blend_mode,BlendMode::Normal);
        QCOMPARE(findLayer(s.data(),upper)->blend_mode,BlendMode::Difference);
    }
    void reference1080p() {
        ImageDocumentSession s;QVERIFY(s.createCanvas({1920,1080},Qt::transparent));QStringList ids;
        for(int i=0;i<5;++i){if(i) QVERIFY(!s.addLayer().isEmpty());ids.append(s.selectedLayerId());QVERIFY(s.applyPaintStroke({{200.,300.+i*30},{1600.,300.+i*30}},QColor(30*i,80,180,190),220));QVERIFY(s.setStackBlendMode(ids.back(),false,static_cast<BlendMode>(1+i)));}
        const auto group=s.groupLayers(ids);QVERIFY(!group.isEmpty());ImageInteractiveRasterCache cache;QElapsedTimer t;t.start();
        auto frame=ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"1080p"));QVERIFY(!frame.isNull());const auto cold=t.elapsed();const auto work=cache.reconstructedPixels();t.restart();
        frame=ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"1080p"));const auto warm=t.elapsed();QCOMPARE(cache.reconstructedPixels(),work);
        QVERIFY(s.setStackBlendMode(ids.front(),false,BlendMode::Difference));t.restart();frame=ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"1080p"));const auto change=t.elapsed();QCOMPARE(cache.reconstructedPixels(),work);
        QVERIFY(s.selectLayer(ids.front()));QVERIFY(s.applyPaintStroke({{800,450}},Qt::yellow,30));t.restart();frame=ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"1080p"));
        qInfo()<<"blend 1080p cold_ms"<<cold<<"warm_ms"<<warm<<"mode_change_ms"<<change<<"base_update_ms"<<t.elapsed()<<"retained_bytes"<<cache.retainedBytes();
    }
};
QTEST_MAIN(BlendTest)
#include "image_editor_blend_test.moc"
