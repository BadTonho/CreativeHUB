#include "image_document_session.h"
#include "rendering/image_layer_outline.h"
#include "rendering/image_interactive_raster_cache.h"
#include "rendering/image_document_renderer.h"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <algorithm>

using namespace image_editor;
class OutlineTest final : public QObject {
    Q_OBJECT
private slots:
    void pixels() {
        QImage input(17,17,QImage::Format_ARGB32_Premultiplied); input.fill(Qt::transparent);
        { QPainter p(&input); p.fillRect(4,4,9,9,Qt::red); p.setCompositionMode(QPainter::CompositionMode_Clear); p.fillRect(7,7,3,3,Qt::transparent); }
        const auto original=input;
        ImageLayerOutlineData o; o.color=Qt::blue;
        const auto outside=applyLayerOutline(input,o);
        QCOMPARE(input,original); QCOMPARE(outside.pixelColor(4,4),QColor(Qt::red));
        QCOMPARE(outside.pixelColor(3,8),QColor(Qt::blue));
        QVERIFY(outside.pixelColor(8,8).alpha()>0); // Transparent hole is outlined too.
        QCOMPARE(outside.pixelColor(0,0).alpha(),0);
        o.position=ImageOutlinePosition::Inside;
        const auto inside=applyLayerOutline(input,o);
        QCOMPARE(inside.pixelColor(4,8),QColor(Qt::blue));
        QCOMPARE(inside.pixelColor(8,8).alpha(),0);
        QCOMPARE(inside.pixelColor(3,8).alpha(),0);
        // Every grayscale alpha value survives an inside outline; no alpha threshold.
        for(int a:{1,64,128,254}) {
            QImage faint(3,3,QImage::Format_ARGB32_Premultiplied); faint.fill(QColor(255,0,0,a));
            const auto result=applyLayerOutline(faint,o);
            QCOMPARE(result.pixelColor(0,0).alpha(),a);
        }
        QImage empty(3,3,QImage::Format_ARGB32_Premultiplied); empty.fill(Qt::transparent);
        QCOMPARE(applyLayerOutline(empty,o),empty);
        std::atomic_bool cancel{true}; QVERIFY(applyLayerOutline(input,o,&cancel).isNull());
        o.enabled=false; QCOMPARE(applyLayerOutline(input,o),input);
        o.enabled=true; o.color=QColor(0,0,0,0); QCOMPARE(applyLayerOutline(input,o),input);
        o.thickness=0; QVERIFY(applyLayerOutline(input,o).isNull());
    }
    void circularCoverage() {
        // Independent brute-force reference protects the optimized sliding-window disk.
        QImage input(9,7,QImage::Format_ARGB32_Premultiplied);
        for(int y=0;y<input.height();++y) for(int x=0;x<input.width();++x)
            input.setPixelColor(x,y,QColor(0,0,0,(x*53+y*79)%256));
        for(int radius:{1,2,4,12}) for(bool inside:{false,true}) {
            ImageLayerOutlineData o; o.thickness=radius; o.color=QColor(255,255,255,149);
            o.position=inside?ImageOutlinePosition::Inside:ImageOutlinePosition::Outside;
            const auto actual=applyLayerOutline(input,o); QVERIFY(!actual.isNull());
            for(int y=0;y<input.height();++y) for(int x=0;x<input.width();++x) {
                int maximum=0;
                for(int dy=-radius;dy<=radius;++dy) for(int dx=-radius;dx<=radius;++dx) {
                    const double weight=std::clamp(radius+.5-std::hypot(double(dx),double(dy)),0.,1.);
                    const int a=input.rect().contains(x+dx,y+dy)?input.pixelColor(x+dx,y+dy).alpha():0;
                    maximum=std::max(maximum,int(std::lround((inside?255-a:a)*weight)));
                }
                const int a=input.pixelColor(x,y).alpha();
                const int ring=inside?std::max(0,a-(255-maximum)):std::max(0,maximum-a);
                const int expectedAlpha=inside?a:a+int(std::lround(ring*(149/255.)*(255-a)/255.));
                QCOMPARE(actual.pixelColor(x,y).alpha(),expectedAlpha);
                const int red=inside?int(std::lround(ring*(149/255.))):expectedAlpha-a;
                QCOMPARE(qRed(actual.pixel(x,y)),red);
            }
        }
    }
    void historyPersistenceRecovery() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        ImageDocumentSession s; QVERIFY(s.createCanvas({40,30},Qt::transparent));
        const auto id=s.selectedLayerId();
        QVERIFY(s.applyPaintStroke({{10,10},{20,10}},Qt::red,4));
        const auto before=s.data(); ImageLayerOutlineData o;
        s.beginLayerOutlineEdit();
        QVERIFY(s.setLayerOutline(id,o)); o.thickness=5; QVERIFY(s.setLayerOutline(id,o));
        s.endLayerOutlineEdit();
        QVERIFY(!s.setLayerOutline(id,o));
        QVERIFY(!s.setLayerOutline(s.data().layers.front().id,o));
        QVERIFY(s.undo()); QVERIFY(s.data()==before); QVERIFY(s.redo());
        const auto styled=s.renderedImage();
        QString error; const auto path=dir.filePath("outline.cimg"); QVERIFY2(s.saveDocument(path,&error),qPrintable(error));
        ImageDocumentSession loaded; QVERIFY(loaded.openDocument(path)); QCOMPARE(loaded.renderedImage(),styled);
        RecoveryDocumentData recovery{s.data(),path,"outline-session"};
        const auto recoveryPath=dir.filePath("recovery.json");
        QVERIFY(ImageDocumentStore::saveRecovery(recoveryPath,recovery,&error));
        RecoveryDocumentData recovered; QVERIFY(ImageDocumentStore::loadRecovery(recoveryPath,&recovered,&error));
        QVERIFY(recovered.document==s.data());
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto root=QJsonDocument::fromJson(file.readAll()).object(); file.close();
        QCOMPARE(root.value("version").toInt(),17);
        for(int version:{16,17}) for(int bad:{0,1,2,3,4}) {
            auto malformed=root; malformed.insert("version",version); auto layers=malformed.value("layers").toArray();
            auto layer=layers.last().toObject(); auto outline=layer.value("outline").toObject();
            if(bad==0) outline.insert("thickness",0);
            if(bad==1) outline.insert("thickness",1025);
            if(bad==2) outline.insert("position","center");
            if(bad==3) outline.insert("color","invalid");
            if(bad==4) outline.insert("enabled",1);
            layer.insert("outline",outline); layers[layers.size()-1]=layer; malformed.insert("layers",layers);
            QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate)); file.write(QJsonDocument(malformed).toJson()); file.close();
            ImageDocumentData invalid; QVERIFY(!ImageDocumentStore::loadDocument(path,&invalid,&error));
        }
        auto legacy=root; legacy.insert("version",16); auto layers=legacy.value("layers").toArray();
        auto layer=layers.last().toObject(); layer.remove("outline"); layers[layers.size()-1]=layer; legacy.insert("layers",layers);
        QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate)); file.write(QJsonDocument(legacy).toJson()); file.close();
        QVERIFY(loaded.openDocument(path)); QVERIFY(!loaded.data().layers.back().outline);
        o.enabled=false; QVERIFY(s.setLayerOutline(id,o)); QVERIFY(s.data().layers.back().outline->thickness==5);
        QVERIFY(s.setLayerOutline(id,{})); QVERIFY(!s.data().layers.back().outline);
    }
    void renderCachesMasksExports() {
        QTemporaryDir dir; ImageDocumentSession s; QVERIFY(s.createCanvas({96,72},Qt::transparent));
        ImageTextData text; text.content="Outline"; text.position={8,8}; text.font_pixel_size=16; text.box_width=80;
        QVERIFY(!s.addText(text).isEmpty()); const auto id=s.selectedLayerId();
        ImageLayerOutlineData o; o.color=QColor(20,80,240,180); QVERIFY(s.setLayerOutline(id,o));
        QVERIFY(s.addLayerMask(id)); QVERIFY(s.applyLayerMaskStroke({{30,20}},QColor(128,128,128),15));
        ImageInteractiveRasterCache cache;
        const auto verify=[&] {
            const auto snapshot=s.exportSnapshot();
            const auto actual=ImageInteractiveRasterCache::composite(cache.prepare(snapshot,"outline"));
            const auto expected=s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
            QCOMPARE(actual,expected); QVERIFY(cache.retainedBytes()<=cache.maximum_bytes);
        };
        verify(); verify();
        o.thickness=4; QVERIFY(s.setLayerOutline(id,o)); verify();
        QVERIFY(s.setLayerOpacity(id,47)); verify();
        QVERIFY(s.applyLayerMaskEraseStroke({{25,22}},8)); verify();
        QVERIFY(s.setLayerMaskEnabled(id,false)); verify();
        QVERIFY(s.setLayerMaskEnabled(id,true)); verify();
        const auto target=s.editableSelectionTargetAt({20,20},false); QVERIFY(target);
        const auto targetBefore=target->pixels;
        o.color=Qt::green; QVERIFY(s.setLayerOutline(id,o));
        QCOMPARE(s.editableSelectionTargetAt({20,20},false)->pixels,targetBefore);
        ImagePreview preview; preview.kind=ImagePreviewKind::MaskPaint; preview.mask=true; preview.color=Qt::black;
        preview.points={{20,20},{35,25}}; preview.diameter=10;
        const auto snap=s.interactiveSnapshot(preview);
        QCOMPARE(ImageInteractiveRasterCache::composite(cache.prepare(snap,"outline",true)),
            ImageDocumentRenderer::composite(snap.document,snap.source_image,snap.raster_images).convertToFormat(QImage::Format_ARGB32_Premultiplied));
        verify(); QVERIFY(s.undo()); verify(); QVERIFY(s.redo()); verify();
        QVERIFY(s.exportImage(dir.filePath("full.png")));
        QCOMPARE(QImage(dir.filePath("full.png")).convertToFormat(QImage::Format_ARGB32),s.renderedImage().convertToFormat(QImage::Format_ARGB32));
        ImageExportOptions options; options.scope=ImageExportScope::SelectedLayer;
        QVERIFY(s.exportImage(dir.filePath("layer.png"),options));
        const auto exported=QImage(dir.filePath("layer.png")).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const auto full=s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        for(int y=0;y<full.height();++y) for(int x=0;x<full.width();++x) {
            const auto a=exported.pixel(x,y), b=full.pixel(x,y);
            QVERIFY(std::abs(qRed(a)-qRed(b))<=1 && std::abs(qGreen(a)-qGreen(b))<=1 &&
                std::abs(qBlue(a)-qBlue(b))<=1 && qAlpha(a)==qAlpha(b));
        }
        const auto group=s.groupLayers({s.data().layers.at(1).id,id}); QVERIFY(!group.isEmpty());
        QVERIFY(s.setGroupOpacity(group,64)); s.rotateRight(); verify();
        options.scope=ImageExportScope::SelectedGroup; QVERIFY(s.exportImage(dir.filePath("group.png"),options));
        QCOMPARE(QImage(dir.filePath("group.png")).convertToFormat(QImage::Format_ARGB32),s.renderedImage().convertToFormat(QImage::Format_ARGB32));
        QVERIFY(s.resizeCanvas({100,80})); verify();
        cache.setByteLimit(0); verify();
    }
    void importedRaster() {
        QTemporaryDir dir; ImageDocumentSession s; QVERIFY(s.createCanvas({64,64},Qt::transparent));
        QImage pixels(16,16,QImage::Format_ARGB32); pixels.fill(Qt::transparent);
        { QPainter p(&pixels); p.fillRect(3,3,10,10,QColor(220,30,40,140)); }
        const auto source=dir.filePath("logo.png"); QVERIFY(pixels.save(source));
        QVERIFY(s.importRasterImages({{source,pixels}})); const auto id=s.selectedLayerId();
        const auto before=s.renderedImage(); ImageLayerOutlineData o; o.color=Qt::blue;
        QVERIFY(s.setLayerOutline(id,o)); QVERIFY(s.renderedImage()!=before);
        ImageInteractiveRasterCache cache;
        QCOMPARE(ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"raster")),
            s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
        auto objects=s.visibleObjects(); QVERIFY(!objects.isEmpty());
        for(auto& object:objects) if(object.layer_id==id) object.operation.raster.transform.scale(.5,.5);
        QVERIFY(s.updateObjectsRendered(objects)); QCOMPARE(s.data().layers.back().outline->thickness,2);
        QCOMPARE(ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"raster")),
            s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
        QCOMPARE(QImage(source),pixels);
        auto bad=s.data(); bad.layers.front().outline=o; QString error;
        QVERIFY(!ImageDocumentStore::saveDocument(dir.filePath("invalid-background.cimg"),bad,&error));
        o.thickness=1024; QVERIFY(s.setLayerOutline(id,o)); QVERIFY(!s.renderedImage().isNull());
    }
    void reference1080p() {
        ImageDocumentSession s; QVERIFY(s.createCanvas({1920,1080},Qt::transparent));
        const auto emptyLayer=s.selectedLayerId();
        QStringList ids;
        for(int i=0;i<5;++i) {
            ImageShapeData shape; shape.start={double(40+i*200),100}; shape.end={double(200+i*200),800};
            QVERIFY(!s.addShape(shape).isEmpty()); ids.append(s.selectedLayerId());
            QVERIFY(s.setLayerOutline(s.selectedLayerId(),ImageLayerOutlineData{}));
        }
        QVERIFY(s.deleteLayer(emptyLayer));
        QVERIFY(!s.groupLayers(ids).isEmpty());
        ImageInteractiveRasterCache cache; QElapsedTimer timer; timer.start();
        auto frame=cache.prepare(s.exportSnapshot(),"1080p"); QVERIFY(!ImageInteractiveRasterCache::composite(frame).isNull());
        const auto cold=timer.elapsed(); const auto reconstructed=cache.reconstructedPixels(); timer.restart();
        frame=cache.prepare(s.exportSnapshot(),"1080p"); const auto warmPrepare=timer.elapsed();
        QCOMPARE(cache.reconstructedPixels(),reconstructed);
        QVERIFY(!ImageInteractiveRasterCache::composite(frame).isNull());
        qInfo("1080p outline workload: cold %lld ms, warm %lld ms (prepare %lld ms), retained %llu bytes",cold,timer.elapsed(),warmPrepare,cache.retainedBytes());
        QVERIFY(cache.retainedBytes()<=cache.maximum_bytes);
    }
};
QTEST_MAIN(OutlineTest)
#include "image_editor_outline_test.moc"
