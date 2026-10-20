#include "image_document_session.h"
#include "image_layer_clipping.h"
#include "editing/image_layer_stack_editor.h"
#include "rendering/image_document_renderer.h"
#include "rendering/image_interactive_raster_cache.h"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>

using namespace image_editor;
class ClippingTest final : public QObject {
    Q_OBJECT
private slots:
    void pixelsAndOpacity() {
        ImageDocumentSession session; QVERIFY(session.createCanvas({24,20},Qt::transparent));
        auto snapshot = session.exportSnapshot(); auto& doc = snapshot.document;
        const auto baseId = doc.layers.back().id;
        ImageLayerData child; child.id="child"; child.name="Child"; child.clip_to_base=true;
        ImageOperation baseOp; baseOp.kind=OperationKind::RasterImage; baseOp.raster.id="base-pixels";
        baseOp.raster.source_size={24,20};
        ImageOperation childOp=baseOp; childOp.raster.id="child-pixels";
        doc.layers.back().operations={baseOp}; child.operations={childOp};
        doc.layers.append(child); doc.root_stack.append({child.id,false});
        QImage base(24,20,QImage::Format_ARGB32_Premultiplied); base.fill(Qt::transparent);
        { QPainter p(&base); p.fillRect(4,4,16,12,QColor(255,0,0,128)); p.setCompositionMode(QPainter::CompositionMode_Clear); p.fillRect(10,8,4,4,Qt::transparent); }
        QImage fill(base.size(),base.format()); fill.fill(Qt::blue);
        snapshot.raster_images.insert(baseOp.raster.id,base); snapshot.raster_images.insert(childOp.raster.id,fill);
        const auto render=[&] { return ImageDocumentRenderer::composite(doc,snapshot.source_image,snapshot.raster_images).convertToFormat(QImage::Format_ARGB32_Premultiplied); };
        auto result=render(); QCOMPARE(result.pixelColor(6,6).alpha(),128);
        QCOMPARE(result.pixelColor(6,6).blue(),255); QCOMPARE(result.pixelColor(11,9).alpha(),0);
        QCOMPARE(result.pixelColor(0,0).alpha(),0);
        doc.layers[1].opacity=50; result=render(); QVERIFY(qAbs(result.pixelColor(6,6).alpha()-64)<=1);
        doc.layers[1].opacity=100; doc.layers[2].opacity=50; result=render();
        QVERIFY(qAbs(result.pixelColor(6,6).red()-128)<=2); QCOMPARE(result.pixelColor(6,6).alpha(),128);
        doc.layers[2].opacity=100;
        // Base outlines remain their own color, even on the inside of the matte.
        for (auto position : {ImageOutlinePosition::Inside,ImageOutlinePosition::Outside}) {
            base.fill(Qt::transparent); { QPainter p(&base); p.fillRect(4,4,16,12,Qt::red); }
            snapshot.raster_images[baseOp.raster.id]=base;
            ImageLayerOutlineData outline; outline.color=Qt::green; outline.position=position;
            doc.layers[1].outline=outline; result=render();
            QCOMPARE(result.pixelColor(12,10),QColor(Qt::blue));
            QCOMPARE(result.pixelColor(position==ImageOutlinePosition::Inside?4:3,10),QColor(Qt::green));
        }
        doc.layers[1].visible=false; result=render(); QCOMPARE(result.pixelColor(12,10).alpha(),0);
        doc.layers[1].visible=true; doc.layers[1].opacity=0; result=render(); QCOMPARE(result.pixelColor(12,10).alpha(),0);
        doc.layers[1].opacity=100; doc.layers[1].outline.reset();
        const auto isolated=ImageDocumentRenderer::selectedLayer(doc,snapshot.source_image,snapshot.raster_images,child.id);
        QCOMPARE(isolated.pixelColor(4,8),QColor(Qt::blue)); QCOMPARE(isolated.pixelColor(0,0).alpha(),0);
        const auto onlyBase=ImageDocumentRenderer::selectedLayer(doc,snapshot.source_image,snapshot.raster_images,baseId);
        QCOMPARE(onlyBase.pixelColor(4,8),QColor(Qt::red));
        ImageInteractiveRasterCache cache;
        QCOMPARE(ImageInteractiveRasterCache::composite(cache.prepare(snapshot,"pixels")),render());
        const auto work=cache.reconstructedPixels(); const auto unchanged=cache.prepare(snapshot,"pixels"); QVERIFY(!unchanged.size.isEmpty()); QCOMPARE(cache.reconstructedPixels(),work);
        snapshot.raster_images[childOp.raster.id].fill(Qt::yellow);
        QCOMPARE(ImageInteractiveRasterCache::composite(cache.prepare(snapshot,"pixels")),render());
        std::atomic_bool cancel{true};
        QVERIFY(ImageDocumentRenderer::layerThumbnail(doc,snapshot.source_image,snapshot.raster_images,doc.layers[2],{24,20},&cancel).isNull());
        QVERIFY(cache.prepare(snapshot,"pixels",false,&cancel).size.isEmpty());
        QCOMPARE(ImageInteractiveRasterCache::composite(cache.prepare(snapshot,"pixels")),render());
        cache.setByteLimit(1); QCOMPARE(ImageInteractiveRasterCache::composite(cache.prepare(snapshot,"pixels")),render());
        QVERIFY(cache.retainedBytes()<=1);
    }
    void backgroundGroupBarriersAndEmptyBases() {
        ImageDocumentSession s; QVERIFY(s.createCanvas({48,32},QColor(255,0,0,128)));
        const auto child=s.selectedLayerId(); QVERIFY(s.applyPaintStroke({{0,16},{47,16}},Qt::blue,30));
        QVERIFY(s.setLayerClipping(child,true));
        auto pixels=s.renderedImage(); QVERIFY(qAbs(pixels.pixelColor(20,16).alpha()-128)<=1);
        QCOMPARE(pixels.pixelColor(20,16).blue(),255);
        ImageInteractiveRasterCache cache;
        QCOMPARE(ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"background")),pixels.convertToFormat(QImage::Format_ARGB32_Premultiplied));
        QVERIFY(s.setLayerVisible(s.data().layers.front().id,false));
        QCOMPARE(s.renderedImage().pixelColor(20,16).alpha(),0);
        QCOMPARE(ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"background")),s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
        QVERIFY(!s.addGroup().isEmpty()); const auto orphan=s.addLayer();
        QVERIFY(!s.setLayerClipping(orphan,true)); // A group cannot be crossed or used as the base.
        auto invalid=s.data(); findLayer(invalid,orphan)->clip_to_base=true;
        QTemporaryDir dir; QVERIFY(!ImageDocumentStore::saveDocument(dir.filePath("orphan.cimg"),invalid));
        const auto empty=s.addLayer(); const auto top=s.addLayer();
        QVERIFY(s.applyPaintStroke({{10,10},{30,10}},Qt::green,10)); QVERIFY(s.setLayerClipping(top,true));
        auto snap=s.exportSnapshot();
        QCOMPARE(ImageDocumentRenderer::layerChain(snap.document,snap.source_image,snap.raster_images,empty).pixelColor(20,10).alpha(),0);
        // A shape remains editable while its image/paint overlay is clipped.
        ImageShapeData shape; shape.start={4,4}; shape.end={40,28};
        QVERIFY(!s.addShape(shape).isEmpty()); const auto shapeBase=s.selectedLayerId();
        const auto shapeChild=s.addLayer(); QVERIFY(s.applyPaintStroke({{0,15},{47,15}},Qt::blue,25));
        QVERIFY(s.setLayerClipping(shapeChild,true));
        QVERIFY(s.setLayerVisible(shapeBase,false));
        for(const auto& object:s.visibleObjects()) QVERIFY(object.layer_id!=shapeChild);
    }
    void historyStructureAndPersistence() {
        QTemporaryDir dir; ImageDocumentSession s; QVERIFY(s.createCanvas({64,48},Qt::transparent));
        const auto base=s.selectedLayerId(); QVERIFY(s.applyPaintStroke({{15,20},{40,20}},Qt::red,16));
        const auto child=s.addLayer(); QVERIFY(!child.isEmpty()); QVERIFY(s.applyPaintStroke({{8,20},{55,20}},Qt::blue,30));
        const auto before=s.data(); QVERIFY(s.setLayerClipping(child,true)); QVERIFY(!s.setLayerClipping(child,true));
        QVERIFY(!s.setLayerClipping(s.data().layers.front().id,true));
        const auto clipped=s.renderedImage(); QVERIFY(s.undo()); QCOMPARE(s.data(),before); QVERIFY(s.redo());
        const auto next=s.addLayer(); QVERIFY(s.setLayerClipping(next,true));
        QCOMPARE(clippingBases(s.data()).value(next),base);
        QVERIFY(s.setLayerClipping(child,false)); QCOMPARE(clippingBases(s.data()).value(next),child);
        QVERIFY(s.undo());
        QVERIFY(s.deleteLayer(base)); QVERIFY(!findLayer(s.data(),child)->clip_to_base); QVERIFY(!findLayer(s.data(),next)->clip_to_base);
        QVERIFY(s.undo()); QVERIFY(findLayer(s.data(),child)->clip_to_base);
        const auto group=s.groupLayers({base,child,next}); QVERIFY(!group.isEmpty());
        QVERIFY(s.setGroupOpacity(group,70));
        auto snap=s.exportSnapshot(); ImageInteractiveRasterCache cache;
        QCOMPARE(ImageInteractiveRasterCache::composite(cache.prepare(snap,"group")),s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
        // Moving a clipped member to the beginning of a group releases the orphan run atomically.
        QVERIFY(s.moveStackItem(child,false,group,0)); QVERIFY(!findLayer(s.data(),child)->clip_to_base);
        QVERIFY(s.undo()); QVERIFY(findLayer(s.data(),child)->clip_to_base);
        QVERIFY(s.ungroup(group)); QVERIFY(findLayer(s.data(),child)->clip_to_base);
        const auto path=dir.filePath("clipping.cimg"); QString error;
        QVERIFY2(s.saveDocument(path,&error),qPrintable(error)); ImageDocumentSession loaded;
        QVERIFY(loaded.openDocument(path)); QCOMPARE(loaded.data(),s.data());
        RecoveryDocumentData recovery{s.data(),path,"clipping-session"}, recovered;
        const auto recoveryPath=dir.filePath("recover.json");
        QVERIFY(ImageDocumentStore::saveRecovery(recoveryPath,recovery));
        QVERIFY(ImageDocumentStore::loadRecovery(recoveryPath,&recovered)); QCOMPARE(recovered.document,s.data());
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto root=QJsonDocument::fromJson(file.readAll()).object(); file.close();
        QCOMPARE(root.value("version").toInt(),ImageDocumentStore::kCurrentDocumentVersion);
        for(int bad=0;bad<3;++bad) {
            auto invalid=root; auto layers=invalid.value("layers").toArray(); auto layer=layers.at(2).toObject();
            if(bad==0) layer.insert("clip_to_base",1);
            if(bad==1) invalid.insert("version",17);
            if(bad==2) { auto bg=layers[0].toObject(); bg.insert("clip_to_base",true); layers[0]=bg; }
            layers[2]=layer; invalid.insert("layers",layers);
            QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate)); file.write(QJsonDocument(invalid).toJson()); file.close();
            const auto saved=loaded.data(); QVERIFY(!loaded.openDocument(path)); QCOMPARE(loaded.data(),saved);
        }
        auto legacy=root; legacy.insert("version",17); auto layers=legacy.value("layers").toArray();
        for(qsizetype i=0;i<layers.size();++i) { auto layer=layers[i].toObject(); layer.remove("clip_to_base"); layers[i]=layer; }
        legacy.insert("layers",layers); QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate)); file.write(QJsonDocument(legacy).toJson()); file.close();
        QVERIFY(loaded.openDocument(path)); for(const auto& layer:loaded.data().layers) QVERIFY(!layer.clip_to_base);
        Q_UNUSED(clipped);
    }
    void masksTextThumbnailsExportsAndCache() {
        QTemporaryDir dir; ImageDocumentSession s; QVERIFY(s.createCanvas({120,80},Qt::transparent));
        ImageTextData text; text.content="Clip"; text.font_pixel_size=40; text.position={8,8}; text.box_width=110;
        QVERIFY(!s.addText(text).isEmpty()); const auto base=s.selectedLayerId();
        const auto child=s.addLayer(); QVERIFY(s.applyPaintStroke({{10,25},{105,25}},Qt::blue,50)); QVERIFY(s.setLayerClipping(child,true));
        QVERIFY(s.addLayerMask(base)); QVERIFY(s.selectLayer(base));
        QVERIFY(s.applyLayerMaskStroke({{35,30}},QColor(128,128,128),30));
        ImageInteractiveRasterCache cache;
        const auto verify=[&] {
            QCOMPARE(ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"text")),s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
            QVERIFY(cache.retainedBytes()<=cache.maximum_bytes);
        };
        verify(); verify(); const auto before=s.renderedLayerThumbnails({120,80}).value(child);
        QVERIFY(s.applyLayerMaskEraseStroke({{55,30}},45)); verify();
        QVERIFY(s.renderedLayerThumbnails({120,80}).value(child)!=before);
        QVERIFY(s.setLayerVisible(base,false)); verify(); QVERIFY(s.setLayerVisible(base,true)); verify();
        QVERIFY(s.setLayerMaskEnabled(base,false)); verify(); QVERIFY(s.undo()); verify(); QVERIFY(s.redo()); verify();
        QVERIFY(s.selectLayer(child));
        const auto editable=s.editableSelectionTargetAt({20,25},false); QVERIFY(editable);
        QVERIFY(s.setLayerClipping(child,false)); QCOMPARE(s.editableSelectionTargetAt({20,25},false)->pixels,editable->pixels);
        QVERIFY(s.undo());
        QVERIFY(s.exportImage(dir.filePath("full.png"))); QCOMPARE(QImage(dir.filePath("full.png")).convertToFormat(QImage::Format_ARGB32_Premultiplied),s.renderedImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
        ImageExportOptions options; options.scope=ImageExportScope::SelectedLayer;
        QVERIFY(s.exportImage(dir.filePath("selected.png"),options));
        QCOMPARE(QImage(dir.filePath("selected.png")).convertToFormat(QImage::Format_ARGB32_Premultiplied),ImageDocumentRenderer::selectedLayer(s.data(),s.exportSnapshot().source_image,s.exportSnapshot().raster_images,child));
    }
    void representative1080p() {
        ImageDocumentSession s; QVERIFY(s.createCanvas({1920,1080},Qt::transparent)); QStringList ids;
        for(int i=0;i<5;++i) {
            if(i) QVERIFY(!s.addLayer().isEmpty());
            ids.append(s.selectedLayerId());
            QVERIFY(s.applyPaintStroke({{200.,300.+i*30},{1600.,300.+i*30}},QColor(20*i,80,200),200));
            if(i) QVERIFY(s.setLayerClipping(ids.back(),true));
        }
        QVERIFY(!s.groupLayers(ids).isEmpty()); ImageInteractiveRasterCache cache; QElapsedTimer timer; timer.start();
        const auto first=cache.prepare(s.exportSnapshot(),"1080p"); auto frame=ImageInteractiveRasterCache::composite(first); QVERIFY(!frame.isNull());
        const auto cold=timer.elapsed(); const auto work=cache.reconstructedPixels(); timer.restart();
        frame=ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"1080p")); const auto warm=timer.elapsed();
        QCOMPARE(cache.reconstructedPixels(),work); QVERIFY(cache.retainedBytes()<=cache.maximum_bytes);
        QVERIFY(s.setLayerOpacity(ids.front(),70)); timer.restart();
        frame=ImageInteractiveRasterCache::composite(cache.prepare(s.exportSnapshot(),"1080p")); QVERIFY(!frame.isNull());
        qInfo()<<"clipping 1080p cold_ms"<<cold<<"warm_ms"<<warm<<"base_update_ms"<<timer.elapsed()<<"retained_bytes"<<cache.retainedBytes();
    }
};
QTEST_MAIN(ClippingTest)
#include "image_editor_clipping_test.moc"
