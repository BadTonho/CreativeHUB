#include "image_document_utils.h"
#include "image_document_session.h"
#include "rendering/image_document_renderer.h"
#include "rendering/image_interactive_raster_cache.h"
#include "../src/ui/rendering/image_render_controller.h"
#include "../src/ui/canvas/image_gpu_presenter.h"
#include <QApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QPainter>
#include <QSignalSpy>
#include <QSurfaceFormat>
#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QEventLoop>
#include <QTimer>

using namespace image_editor;
using namespace creative_suite::composition;
class InteractiveTest final : public QObject {
    Q_OBJECT
private:
    static QImage baseline(const ImageDocumentSession& session) {
        const auto snapshot = session.exportSnapshot();
        return ImageDocumentRenderer::composite(snapshot.document, snapshot.source_image, snapshot.raster_images)
            .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
private slots:
    void incrementalPixelsAndHistory() {
        ImageDocumentSession session; QVERIFY(session.createCanvas({320,240}, Qt::transparent));
        const auto layer = session.addLayer(); QVERIFY(!layer.isEmpty());
        ImageInteractiveRasterCache cache;
        int step=0;
        const auto verify = [&] {
            const auto prepared = cache.prepare(session.exportSnapshot(), "document");
            ++step;
            const auto actual=ImageInteractiveRasterCache::composite(prepared), expected=baseline(session);
            if (actual!=expected) {
                for(int y=0;y<expected.height();++y) for(int x=0;x<expected.width();++x) if(actual.pixel(x,y)!=expected.pixel(x,y)) {
                    QFAIL(qPrintable(QString("Step %1 pixel %2,%3: %4 / %5").arg(step).arg(x).arg(y).arg(actual.pixel(x,y),0,16).arg(expected.pixel(x,y),0,16)));
                }
            }
            QVERIFY(cache.retainedBytes() <= cache.maximum_bytes);
        };
        verify();
        QPainterPath clip; clip.addEllipse(QRectF(0,0,160,160));
        QVERIFY(session.applyPaintStroke({{4.5,4.5},{50.3,40.6},{8,10}}, QColor(230,40,80,121), 17, nullptr, clip));
        const auto before = cache.reconstructedPixels(); verify();
        QVERIFY(cache.reconstructedPixels()-before < 320ULL*240);
        QVERIFY(session.applyPaintStroke({{30,30},{100,60}}, QColor(10,200,90,80), 11)); verify();
        QVERIFY(session.applyEraseStroke({{15,15},{60,35}}, 7)); verify();
        QVERIFY(session.undo()); verify(); QVERIFY(session.redo()); verify();
        QVERIFY(session.addLayerMask(layer)); verify();
        QVERIFY(session.setLayerOutline(layer,ImageLayerOutlineData{})); verify();
        QVERIFY(session.applyLayerMaskStroke({{10,10},{80,70}}, QColor(128,128,128), 20)); verify();
        QVERIFY(session.applyLayerMaskEraseStroke({{20,20}}, 7)); verify();
        QVERIFY(session.setLayerOpacity(layer,47)); verify();
        QVERIFY(session.setLayerMaskEnabled(layer,false)); verify();
        QVERIFY(session.setLayerMaskEnabled(layer,true)); verify();
        session.rotateRight(); verify(); session.flipHorizontal(); verify();
        QVERIFY(session.resizeCanvas({360,260})); verify();
        QVERIFY(session.createCanvas({64,64}, Qt::white)); verify();
    }
    void objectsResourcesAndGlobalPreviews() {
        ImageDocumentSession session; QVERIFY(session.createCanvas({128,128},Qt::transparent));
        QVERIFY(!session.addLayer().isEmpty()); QVERIFY(session.applyPaintStroke({{20,20},{95,80}},QColor(240,40,80,160),25));
        ImageInteractiveRasterCache cache;
        const auto verify=[&] { QCOMPARE(ImageInteractiveRasterCache::composite(cache.prepare(session.exportSnapshot(),"objects")),baseline(session)); };
        verify();
        const auto preview=[&](const ImagePreview& value,const QImage& expected) {
            QCOMPARE(ImageInteractiveRasterCache::composite(cache.prepare(session.interactiveSnapshot(value),"objects",true)),
                expected.convertToFormat(QImage::Format_ARGB32_Premultiplied));
        };
        ImagePreview erase; erase.kind=ImagePreviewKind::Erase; erase.points={{25,25},{40,40}}; erase.diameter=9;
        preview(erase,session.renderedImageWithEraseStroke(erase.points,erase.diameter)); verify();
        ImagePreview gradient; gradient.kind=ImagePreviewKind::Gradient; gradient.start={10,10}; gradient.end={100,100}; gradient.color=QColor(30,170,220,140);
        preview(gradient,session.renderedImageWithLinearGradient(gradient.start,gradient.end,gradient.color));
        QVERIFY(session.applyLinearGradient(gradient.start,gradient.end,gradient.color)); verify();
        ImagePreview blur; blur.kind=ImagePreviewKind::Blur; blur.points={{30,30},{60,60}}; blur.diameter=20; blur.radius=3;
        preview(blur,session.renderedImageWithBlurStroke(blur.points,blur.diameter,blur.radius));
        QVERIFY(session.applyBlurStroke(blur.points,blur.diameter,blur.radius)); verify();
        ImageShapeData shape; shape.start={10,15}; shape.end={55,70}; shape.fill_color=Qt::green;
        QVERIFY(!session.addShape(shape).isEmpty()); verify();
        auto objects=session.visibleObjects();
        auto object=*std::find_if(objects.begin(),objects.end(),[](const auto& item){return item.operation.kind==OperationKind::Shape;});
        object.operation.shape.start+=QPointF(8,5); object.operation.shape.end+=QPointF(8,5);
        ImagePreview moved; moved.kind=ImagePreviewKind::Objects; moved.objects={object};
        preview(moved,session.renderedImageWithObjects({object})); QVERIFY(session.updateObjectsRendered({object})); verify();
        ImagePreview excluded; excluded.kind=ImagePreviewKind::ExcludeObjects; excluded.excluded={object.operation.shape.id};
        preview(excluded,session.renderedImageWithoutObjects(excluded.excluded)); verify();
        QTemporaryDir directory; QVERIFY(directory.isValid()); QImage image(20,16,QImage::Format_ARGB32_Premultiplied); image.fill(QColor(70,90,220,130));
        const auto path=QDir(directory.path()).filePath("raster.png"); QVERIFY(image.save(path));
        QVERIFY(session.importRasterImages({{path,image}})); verify();
        const auto raster=session.visibleObjects().first(); QVERIFY(raster.operation.kind==OperationKind::RasterImage);
        image.fill(QColor(200,130,20,90)); QVERIFY(image.save(path)); QVERIFY(session.relinkRaster(raster.operation.raster.id,{path,image})); verify();
        session.rotateRight(); verify(); session.flipVertical(); verify(); QVERIFY(session.undo()); verify(); QVERIFY(session.redo()); verify();
    }
    void transientDoesNotChangeConfirmedCache() {
        ImageDocumentSession session; QVERIFY(session.createCanvas({128,128}, Qt::transparent));
        const auto layer = session.addLayer(); QVERIFY(session.applyPaintStroke({{20,20},{80,80}}, Qt::red,30));
        QVERIFY(session.addLayerMask(layer));
        ImageInteractiveRasterCache cache;
        const auto original = session.exportSnapshot();
        auto prepared = cache.prepare(original,"document"); const auto bytes = cache.retainedBytes();
        const auto before = ImageInteractiveRasterCache::composite(prepared);
        ImagePreview preview; preview.kind = ImagePreviewKind::MaskPaint; preview.mask = true;
        preview.points = {{20,20},{70,70}}; preview.color = Qt::black; preview.diameter = 14;
        auto snapshot = session.interactiveSnapshot(preview);
        prepared = cache.prepare(snapshot,"document",true);
        QCOMPARE(ImageInteractiveRasterCache::composite(prepared),
            ImageDocumentRenderer::composite(snapshot.document,snapshot.source_image,snapshot.raster_images).convertToFormat(QImage::Format_ARGB32_Premultiplied));
        QCOMPARE(cache.retainedBytes(),bytes); QCOMPARE(session.data(),original.document);
        QCOMPARE(ImageInteractiveRasterCache::composite(cache.prepare(original,"document")),before);
    }
    void clippingWorkerThumbnails() {
        ImageDocumentSession session; QVERIFY(session.createCanvas({96,72},Qt::transparent));
        const auto base=session.selectedLayerId(); QVERIFY(session.applyPaintStroke({{20,25},{70,25}},Qt::red,18));
        const auto child=session.addLayer(); QVERIFY(session.applyPaintStroke({{5,25},{85,25}},Qt::blue,40));
        QVERIFY(session.setLayerClipping(child,true));
        ImageRenderController controller; controller.setAutomatic(false);
        InteractiveFrame frame; int count=0;
        connect(&controller,&ImageRenderController::frameReady,this,[&](const InteractiveFrame& value){frame=value;++count;});
        const auto verify=[&] {
            const auto snapshot=session.exportSnapshot(); const int expected=count+1;
            controller.request(snapshot,"clipping-thumbnails"); QTRY_COMPARE_WITH_TIMEOUT(count,expected,10000);
            QCOMPARE(frame.image,baseline(session));
            const auto* layer=findLayer(snapshot.document,child); QVERIFY(layer);
            QCOMPARE(frame.thumbnails.value(child),ImageDocumentRenderer::layerThumbnail(snapshot.document,
                snapshot.source_image,snapshot.raster_images,*layer,{48,36}));
        };
        verify(); verify();
        QVERIFY(session.selectLayer(base)); QVERIFY(session.addLayerMask(base));
        QVERIFY(session.applyLayerMaskEraseStroke({{40,25}},20)); verify();
        QVERIFY(session.setLayerVisible(base,false)); verify();
        QVERIFY(session.setLayerVisible(base,true)); QVERIFY(session.setLayerOpacity(base,40)); verify();
        QVERIFY(session.setLayerClipping(child,false)); verify();
        QVERIFY(session.undo()); verify();
    }
    void latestRequestAndCancellation() {
        ImageDocumentSession session; QVERIFY(session.createCanvas({128,128},Qt::white));
        ImageRenderController controller; controller.setAutomatic(false);
        int delivered = 0; QString document; std::uint64_t sequence = 0;
        connect(&controller,&ImageRenderController::frameReady,this,[&](const InteractiveFrame& frame) {
            ++delivered; document=frame.document_id; sequence=frame.sequence;
            QVERIFY(!frame.image.isNull()); QCOMPARE(frame.document_revision,frame.sequence); QCOMPARE(frame.gesture,std::uint64_t(0));
            QVERIFY(frame.cpu_cache_bytes<=ImageInteractiveRasterCache::maximum_bytes);
        });
        for (int i=0;i<40;++i) controller.request(session.exportSnapshot(),QString::number(i));
        QVERIFY(controller.busy()); QVERIFY(controller.hasPendingRequest());
        QTRY_COMPARE_WITH_TIMEOUT(delivered,1,10000);
        QCOMPARE(document,QString("39")); QCOMPARE(sequence,controller.sequence());
        controller.request(session.exportSnapshot(),"cancelled"); controller.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),10000); QCOMPARE(delivered,1);
        controller.request(session.exportSnapshot(),"closed");
        // Destruction cancels and joins the worker before surface teardown.
    }
    void gestureIdentities() {
        ImageDocumentSession session; QVERIFY(session.createCanvas({64,64},Qt::transparent));
        ImageRenderController controller; controller.setAutomatic(false);
        InteractiveFrame frame; int count=0;
        connect(&controller,&ImageRenderController::frameReady,this,[&](const InteractiveFrame& value){frame=value;++count;});
        controller.request(session.exportSnapshot(),"gesture"); QTRY_COMPARE(count,1); const auto revision=frame.document_revision;
        controller.request(session.exportSnapshot(),"gesture",true); QTRY_COMPARE(count,2); const auto gesture=frame.gesture;
        QVERIFY(gesture>0); QCOMPARE(frame.document_revision,revision);
        controller.request(session.exportSnapshot(),"gesture",true); QTRY_COMPARE(count,3); QCOMPARE(frame.gesture,gesture);
        controller.cancel(); controller.request(session.exportSnapshot(),"gesture"); QTRY_COMPARE(count,4);
        QCOMPARE(frame.gesture,std::uint64_t(0)); QVERIFY(frame.document_revision>revision);
    }
    void failureRecoveryAndSessionDisable() {
        for (const auto& cause : {QStringLiteral("Injected GPU allocation failure."), QStringLiteral("Injected context loss.")}) {
            ImageDocumentSession session; QVERIFY(session.createCanvas({80,80},Qt::transparent));
            QVERIFY(!session.addLayer().isEmpty()); QVERIFY(session.applyPaintStroke({{10,10},{60,60}},Qt::red,12));
            ImageRenderController controller; controller.setAutomatic(true);
            QSignalSpy failures(&controller,&ImageRenderController::renderFailed);
            int count=0; InteractiveFrame frame;
            connect(&controller,&ImageRenderController::frameReady,this,[&](const InteractiveFrame& value){frame=value;++count;});
            controller.injectGpuFailureForTesting(cause);
            controller.request(session.exportSnapshot(),"failure"); QTRY_COMPARE(count,1);
            QCOMPARE(failures.count(),1); QVERIFY(failures.first().at(1).toString().contains(cause));
            QCOMPARE(frame.image,baseline(session)); QVERIFY(!frame.texture);
            controller.request(session.exportSnapshot(),"failure"); QTRY_COMPARE(count,2);
            QCOMPARE(failures.count(),1); QVERIFY(!frame.texture);
            controller.cancel(); QTest::qWait(20); QCOMPARE(failures.count(),1);
        }
    }
    void gpuBudgetsGeometryAndCoverage() {
        if (QGuiApplication::platformName()=="offscreen") QSKIP("Native GPU validation is registered separately.");
        auto surface=OpenGlFrameCompositor::createSurface(); QVERIFY(surface);
        const QSize size(256,16); const std::uint64_t frame_bytes=256*16*4;
        auto budget=std::make_shared<OpenGlTexturePoolBudget>(frame_bytes*5,3);
        {
            OpenGlFrameCompositor gpu(surface.get(),OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),budget);
            QImage pixels(size,QImage::Format_ARGB32_Premultiplied); pixels.fill(Qt::white);
            QImage mask(size,QImage::Format_Grayscale8);
            for(int y=0;y<size.height();++y) for(int x=0;x<size.width();++x) mask.scanLine(y)[x]=uchar(x);
            OpenGlRasterRequest request{size,{{{{{"content",pixels},{"mask",mask},100}}, {},100,false}}};
            auto frame=gpu.composeRasterTexture(request); QVERIFY2(frame.status==OpenGlCompositionStatus::Complete,frame.cause.c_str());
            const auto read=gpu.readback(frame.frame); QVERIFY(read.frame);
            // Exact coverage for all 256 levels; no geometry or mask tolerance.
            for(int y=0;y<size.height();++y) for(int x=0;x<size.width();++x) {
                const auto* pixel=read.frame->rgba_pixels.data()+y*read.frame->stride+x*4;
                QCOMPARE(pixel[0],uchar(x)); QCOMPARE(pixel[1],uchar(x)); QCOMPARE(pixel[2],uchar(x)); QCOMPARE(pixel[3],uchar(x));
            }
            auto second=gpu.composeRasterTexture(request); QVERIFY(second.frame);
            auto third=gpu.composeRasterTexture(request); QVERIFY(third.frame);
            const auto busy=gpu.composeRasterTexture(request); QCOMPARE(busy.status,OpenGlCompositionStatus::Busy); QVERIFY(!busy.frame);
            frame.frame.reset(); second.frame.reset(); third.frame.reset();
            auto invalid=request; invalid.nodes[0].transforms.push_back({});
            QCOMPARE(gpu.composeRasterTexture(invalid).status,OpenGlCompositionStatus::Unsupported);
            QVERIFY(budget->bytes()<=frame_bytes*5);
        }
        QCOMPARE(budget->bytes(),std::uint64_t(0)); QCOMPARE(budget->targets(),0U);
        {
            auto small=std::make_shared<OpenGlTexturePoolBudget>(frame_bytes,3);
            OpenGlFrameCompositor gpu(surface.get(),OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),small);
            QImage pixels(size,QImage::Format_ARGB32_Premultiplied); pixels.fill(Qt::red);
            OpenGlRasterRequest request{size,{{{{{"content",pixels},{},100}}, {},100,false}}};
            const auto result=gpu.composeRasterTexture(request);
            QCOMPARE(result.status,OpenGlCompositionStatus::Unsupported); QVERIFY(!result.frame); QVERIFY(small->bytes()<=frame_bytes);
        }
        ImageDocumentSession session; QVERIFY(session.createCanvas({2300,1700},Qt::transparent));
        for(int i=0;i<5;++i) {QVERIFY(!session.addLayer().isEmpty()); QVERIFY(session.applyPaintStroke({{20,20},{100,100}},Qt::red,20));}
        ImageInteractiveRasterCache cache; auto composition=cache.prepare(session.exportSnapshot(),"eviction");
        QHash<QString,qint64> revisions;
        for(const auto& node:composition.nodes) for(const auto& layer:node.layers) revisions.insert(layer.content.identity,layer.content.revision);
        composition=cache.prepare(session.exportSnapshot(),"eviction");
        for(const auto& node:composition.nodes) for(const auto& layer:node.layers) QCOMPARE(layer.content.revision,revisions.value(layer.content.identity));
        QVERIFY(cache.retainedBytes()<=cache.maximum_bytes); QCOMPARE(ImageInteractiveRasterCache::composite(composition),baseline(session));
    }
    void gpuReleasedTargetReuse() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Native GPU validation is registered separately.");
        auto surface=OpenGlFrameCompositor::createSurface(); QVERIFY(surface);
        const QSize size(1920,1080); const auto bytes=std::uint64_t(size.width())*size.height()*4;
        auto budget=std::make_shared<OpenGlTexturePoolBudget>(bytes*3,2);
        OpenGlFrameCompositor gpu(surface.get(),OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),budget);
        QImage pixels(size,QImage::Format_ARGB32_Premultiplied); pixels.fill(QColor(255,0,0,128));
        OpenGlRasterNode node;
        for(int i=0;i<6;++i) node.layers.push_back({{"shared-source",pixels},{},100});
        OpenGlRasterRequest request{size,{node}};
        QWidget window; window.resize(960,540);
        ImageGpuPresenter presenter(&window); presenter.resize(window.size());
        presenter.targetRect=[] {return QRectF(0,0,960,540);};
        QString failure; presenter.failed=[&](const QString& cause){failure=cause;};
        QEventLoop loop; QTimer timeout; timeout.setSingleShot(true);
        connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
        bool presented=false;
        connect(&presenter,&QOpenGLWidget::frameSwapped,&loop,[&]{presented=true;loop.quit();});
        window.show(); presenter.show();
        // Windows process runners can hide the first ShowWindow through startup
        // information. A subsequent explicit show exposes the test surface.
        window.hide(); window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        for(int i=0;i<60;++i) {
            auto result=gpu.composeRasterTexture(request);
            QVERIFY2(result.status==OpenGlCompositionStatus::Complete,result.cause.c_str());
            presented=false; presenter.setFrame(result.frame); result.frame.reset();
            timeout.start(5000); loop.exec(); timeout.stop();
            QVERIFY(presented); QVERIFY2(failure.isEmpty(),qPrintable(failure));
            QVERIFY(budget->bytes()<=bytes*3); QVERIFY(budget->targets()<=2);
        }
        presenter.setFrame({});
    }
    void gpuRasterBlendBudgetsAndUploads() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Native GPU validation is registered separately.");
        auto surface=OpenGlFrameCompositor::createSurface(); QVERIFY(surface);
        const QSize size(32,24); const std::uint64_t plane=32*24*4;
        auto budget=std::make_shared<OpenGlTexturePoolBudget>(plane*16,3);
        OpenGlFrameCompositor gpu(surface.get(),OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),budget);
        ImageDocumentSession session; QVERIFY(session.createCanvas(size,QColor(80,140,190,128)));
        const auto id=session.selectedLayerId(); QVERIFY(session.applyPaintStroke({{3,10},{28,10}},QColor(190,80,50,170),12));
        ImageInteractiveRasterCache cache;
        auto comp=cache.prepare(session.exportSnapshot(),"blend-budget");
        auto first=gpu.composeRasterTexture(makeGpuRasterRequest(comp)); QVERIFY(first.frame); first.frame.reset();
        const auto work=cache.reconstructedPixels();
        for(int mode=1;mode<6;++mode) {
            QVERIFY(session.setStackBlendMode(id,false,static_cast<BlendMode>(mode)));
            comp=cache.prepare(session.exportSnapshot(),"blend-budget"); QCOMPARE(cache.reconstructedPixels(),work);
            OpenGlCompositionTimings timing;auto result=gpu.composeRasterTexture(makeGpuRasterRequest(comp),{},&timing);
            QVERIFY2(result.status==OpenGlCompositionStatus::Complete,result.cause.c_str()); QCOMPARE(timing.uploaded_bytes,std::uint64_t(0));
            auto read=gpu.readback(result.frame);QVERIFY(read.frame);
            QImage actual(read.frame->rgba_pixels.data(),read.frame->width,read.frame->height,read.frame->stride,QImage::Format_RGBA8888_Premultiplied);
            const auto expected=baseline(session);const auto pixels=actual.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){auto a=pixels.pixel(x,y),b=expected.pixel(x,y);QVERIFY(std::abs(qRed(a)-qRed(b))<=2 && std::abs(qGreen(a)-qGreen(b))<=2 && std::abs(qBlue(a)-qBlue(b))<=2 && std::abs(qAlpha(a)-qAlpha(b))<=2);}
        }
        auto invalid=makeGpuRasterRequest(comp); invalid.nodes.back().layers.back().blend_mode=static_cast<BlendMode>(255);
        QCOMPARE(gpu.composeRasterTexture(invalid).status,OpenGlCompositionStatus::Unsupported);
        std::atomic_bool cancel{true};QCOMPARE(gpu.composeRasterTexture(makeGpuRasterRequest(comp),[&]{return cancel.load();}).status,OpenGlCompositionStatus::Cancelled);
        auto small=std::make_shared<OpenGlTexturePoolBudget>(plane*3,3);
        OpenGlFrameCompositor limited(surface.get(),OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),small);
        QCOMPARE(limited.composeRasterTexture(makeGpuRasterRequest(comp)).status,OpenGlCompositionStatus::Unsupported);
        QVERIFY(session.setStackBlendMode(id,false,BlendMode::Normal));comp=cache.prepare(session.exportSnapshot(),"blend-budget");
        const auto recovered=limited.composeRasterTexture(makeGpuRasterRequest(comp));QVERIFY(recovered.status==OpenGlCompositionStatus::Complete);
    }
    void capacityRecoveryIsReported() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Native GPU validation is registered separately.");
        ImageDocumentSession session;QVERIFY(session.createCanvas({64,48},Qt::transparent));
        const auto id=session.selectedLayerId();QVERIFY(session.applyPaintStroke({{10,20},{50,20}},Qt::red,20));
        QVERIFY(session.setStackBlendMode(id,false,BlendMode::Multiply));
        ImageRenderController controller;controller.setAutomatic(true);controller.setGpuByteLimitForTesting(64*48*4*3);
        QSignalSpy recoveries(&controller,&ImageRenderController::cpuRecoveryUsed),failures(&controller,&ImageRenderController::renderFailed);
        int count=0;InteractiveFrame frame;connect(&controller,&ImageRenderController::frameReady,this,[&](const auto& value){frame=value;++count;});
        controller.request(session.exportSnapshot(),"capacity");QTRY_COMPARE(count,1);
        QCOMPARE(recoveries.count(),1);QCOMPARE(failures.count(),0);QCOMPARE(frame.image,baseline(session));QVERIFY(!frame.texture);
        QVERIFY(recoveries.first()[1].toString().contains("GPU byte limit"));
        controller.request(session.exportSnapshot(),"capacity");QTRY_COMPARE(count,2);QCOMPARE(recoveries.count(),1);
        controller.request(session.exportSnapshot(),"cancelled-capacity");controller.cancel();
        controller.request(session.exportSnapshot(),"capacity");QTRY_COMPARE(count,3);QCOMPARE(recoveries.count(),1);
        QVERIFY(session.setStackBlendMode(id,false,BlendMode::Normal));controller.request(session.exportSnapshot(),"capacity");QTRY_COMPARE(count,4);QVERIFY(frame.texture);
        frame.texture.reset();QVERIFY(session.setStackBlendMode(id,false,BlendMode::Multiply));
        controller.request(session.exportSnapshot(),"capacity");QTRY_COMPARE(count,5);QCOMPARE(recoveries.count(),2);
    }
    void gpuBlendReference1080p() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Native GPU validation is registered separately.");
        auto surface=OpenGlFrameCompositor::createSurface();QVERIFY(surface);
        auto budget=std::make_shared<OpenGlTexturePoolBudget>(256ULL*1024*1024,3);
        OpenGlFrameCompositor gpu(surface.get(),OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),budget);
        ImageDocumentSession session;QVERIFY(session.createCanvas({1920,1080},Qt::transparent));QStringList ids;
        for(int i=0;i<5;++i){if(i)QVERIFY(!session.addLayer().isEmpty());ids.append(session.selectedLayerId());QVERIFY(session.applyPaintStroke({{200.,300.+i*30},{1600.,300.+i*30}},QColor(30*i,80,180,190),220));QVERIFY(session.setStackBlendMode(ids.back(),false,static_cast<BlendMode>(1+i)));}
        const auto group=session.groupLayers(ids);QVERIFY(!group.isEmpty());ImageInteractiveRasterCache cache;
        OpenGlTextureFramePtr lease;QElapsedTimer timer;
        const auto measure=[&](const char* label,bool no_upload) {
            timer.restart();const auto comp=cache.prepare(session.exportSnapshot(),"blend-1080p");const auto prepare_ms=timer.elapsed();
            OpenGlCompositionTimings timing;auto next=gpu.composeRasterTexture(makeGpuRasterRequest(comp),{},&timing);
            QVERIFY2(next.status==OpenGlCompositionStatus::Complete,next.cause.c_str());
            lease=next.frame;QVERIFY(gpu.readback(lease).frame);
            if(no_upload)QCOMPARE(timing.uploaded_bytes,std::uint64_t(0));
            qInfo()<<"blend_gpu_1080p"<<label<<"prepare_ms"<<prepare_ms<<"prepare_compose_readback_ms"<<timer.elapsed()<<"uploaded_bytes"<<timing.uploaded_bytes<<"gpu_bytes"<<gpu.resourceUsage().texture_bytes<<"cpu_cache_bytes"<<cache.retainedBytes();
        };
        measure("cold",false);const auto work=cache.reconstructedPixels();measure("warm",true);QCOMPARE(cache.reconstructedPixels(),work);
        QVERIFY(session.setStackBlendMode(ids.front(),false,BlendMode::Difference));measure("mode_change",true);QCOMPARE(cache.reconstructedPixels(),work);
        QVERIFY(session.selectLayer(ids.front()));QVERIFY(session.applyPaintStroke({{800,450}},Qt::yellow,30));measure("base_content",false);QVERIFY(cache.reconstructedPixels()>work);
        auto constrained=std::make_shared<OpenGlTexturePoolBudget>(64ULL*1024*1024,3);
        OpenGlFrameCompositor limited(surface.get(),OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),constrained);
        const auto composition=cache.prepare(session.exportSnapshot(),"blend-1080p");timer.restart();
        const auto refused=limited.composeRasterTexture(makeGpuRasterRequest(composition));
        QCOMPARE(refused.status,OpenGlCompositionStatus::Unsupported);QVERIFY(!refused.frame);
        const auto refusal_ms=timer.elapsed();timer.restart();const auto recovered=ImageInteractiveRasterCache::composite(composition);
        QVERIFY(!recovered.isNull());
        qInfo()<<"blend_gpu_1080p constrained_capacity"<<"refusal_ms"<<refusal_ms<<"cpu_recovery_ms"<<timer.elapsed()<<"operation"<<refused.operation.c_str()<<"cause"<<refused.cause.c_str()<<"gpu_bytes"<<limited.resourceUsage().texture_bytes;
        QCOMPARE(recovered,baseline(session));
    }
    void gpuGradientMaps() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Native GPU validation is registered separately.");
        auto surface=OpenGlFrameCompositor::createSurface();QVERIFY(surface);auto budget=std::make_shared<OpenGlTexturePoolBudget>(256ULL*1024*1024,3);
        OpenGlFrameCompositor gpu(surface.get(),OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),budget);
        ImageDocumentSession s;QVERIFY(s.createCanvas({160,100},QColor(40,80,120,150)));const auto base=s.selectedLayerId();
        QVERIFY(s.applyPaintStroke({{5,40},{145,40}},QColor(200,70,130,190),50));
        QPainterPath selection;selection.addRect(0,0,160,100);selection.addEllipse(40,25,30,25);
        const auto id=s.addGradientMapAdjustment(selection);creative_suite::effects::GradientMapParameters points{{{0,{210,20,70}},{.4,{30,210,100}},{1,{20,70,240}}}};QVERIFY(s.setGradientMap(id,points));
        ImageInteractiveRasterCache cache;
        const auto verify=[&](bool no_upload=false,std::optional<std::uint64_t> expected_upload={}){
            const auto comp=cache.prepare(s.exportSnapshot(),"gradient-gpu");OpenGlCompositionTimings timing;auto result=gpu.composeRasterTexture(makeGpuRasterRequest(comp),{},&timing);QVERIFY2(result.status==OpenGlCompositionStatus::Complete,result.cause.c_str());if(no_upload)QCOMPARE(timing.uploaded_bytes,std::uint64_t(0));if(expected_upload)QCOMPARE(timing.uploaded_bytes,*expected_upload);
            const auto read=gpu.readback(result.frame);QVERIFY(read.frame);const auto actual=QImage(read.frame->rgba_pixels.data(),read.frame->width,read.frame->height,read.frame->stride,QImage::Format_RGBA8888_Premultiplied).convertToFormat(QImage::Format_ARGB32_Premultiplied);const auto expected=baseline(s);
            for(int y=0;y<actual.height();++y)for(int x=0;x<actual.width();++x){auto a=actual.pixel(x,y),b=expected.pixel(x,y);QVERIFY2(std::abs(qRed(a)-qRed(b))<=2 && std::abs(qGreen(a)-qGreen(b))<=2 && std::abs(qBlue(a)-qBlue(b))<=2 && qAlpha(a)==qAlpha(b),qPrintable(QString("Gradient pixel %1,%2: %3 / %4").arg(x).arg(y).arg(a,0,16).arg(b,0,16)));}
        };
        verify();const auto work=cache.reconstructedPixels();for(int mode=1;mode<6;++mode){QVERIFY(s.setStackBlendMode(id,false,static_cast<BlendMode>(mode)));verify(true);QCOMPARE(cache.reconstructedPixels(),work);}
        points.stops[1].position=.6;QVERIFY(s.setGradientMap(id,points));verify(true);QCOMPARE(cache.reconstructedPixels(),work);
        QVERIFY(s.setLayerOpacity(id,47));verify(true);QVERIFY(s.setLayerOpacity(id,0));verify(true);QVERIFY(s.setLayerOpacity(id,100));verify(false,160*100); // Re-admit the evicted Grayscale8 mask only.
        auto invalid=makeGpuRasterRequest(cache.prepare(s.exportSnapshot(),"gradient-gpu"));std::get<creative_suite::effects::GradientMapParameters>(*invalid.nodes.back().layers.back().adjustment).stops[1].position=0;
        QCOMPARE(gpu.composeRasterTexture(invalid).status,OpenGlCompositionStatus::Unsupported);
        QCOMPARE(gpu.composeRasterTexture(makeGpuRasterRequest(cache.prepare(s.exportSnapshot(),"gradient-gpu")),[]{return true;}).status,OpenGlCompositionStatus::Cancelled);
        QVERIFY(s.setLayerClipping(id,true));verify();const auto upper=s.addLayer();QVERIFY(s.applyPaintStroke({{5,40},{145,40}},QColor(40,180,210,170),16));QVERIFY(s.setLayerClipping(upper,true));verify();
        ImageLayerOutlineData outline;outline.color=Qt::green;QVERIFY(s.setLayerOutline(base,outline));verify();
        const auto group=s.groupLayers({base,id,upper});QVERIFY(!group.isEmpty());QVERIFY(s.setGroupOpacity(group,65));s.rotateRight();verify();
        static_cast<void>(s.selectGroup(group));QVERIFY(s.setStackBlendMode(group,true,BlendMode::Overlay));const auto top=s.addGradientMapAdjustment();QVERIFY(s.setGradientMap(top,points));verify();
        auto dense=points;dense.stops.insert(dense.stops.begin()+2,{.601,{240,20,220}});QVERIFY(s.setGradientMap(top,dense));verify();
        // Reference workload: five raster layers, one isolated group and one adjustment.
        QVERIFY(s.createCanvas({1920,1080},Qt::transparent));QStringList ids;for(int i=0;i<5;++i){if(i)QVERIFY(!s.addLayer().isEmpty());ids.append(s.selectedLayerId());QVERIFY(s.applyPaintStroke({{200.,300.+i*30},{1600.,300.+i*30}},QColor(30*i,80,180,190),220));}
        QVERIFY(!s.groupLayers(ids).isEmpty());const auto adjustment=s.addGradientMapAdjustment();QVERIFY(s.setGradientMap(adjustment,points));cache.clear();
        OpenGlTextureFramePtr lease;QElapsedTimer timer;
        const auto measure=[&](const char* label,bool no_upload){timer.restart();const auto c=cache.prepare(s.exportSnapshot(),"gradient-1080p");const auto prepare=timer.elapsed();OpenGlCompositionTimings timing;auto result=gpu.composeRasterTexture(makeGpuRasterRequest(c),{},&timing);QVERIFY2(result.status==OpenGlCompositionStatus::Complete,result.cause.c_str());lease=result.frame;QVERIFY(gpu.readback(lease).frame);if(no_upload)QCOMPARE(timing.uploaded_bytes,std::uint64_t(0));qInfo()<<"gradient_gpu_1080p"<<label<<"prepare_ms"<<prepare<<"total_readback_ms"<<timer.elapsed()<<"uploaded_bytes"<<timing.uploaded_bytes<<"gpu_bytes"<<gpu.resourceUsage().texture_bytes<<"cache_bytes"<<cache.retainedBytes();};
        measure("cold",false);const auto pixels=cache.reconstructedPixels();measure("warm",true);points.stops[1].color={200,100,30};QVERIFY(s.setGradientMap(adjustment,points));measure("color",true);points.stops[1].position=.5;QVERIFY(s.setGradientMap(adjustment,points));measure("position",true);QCOMPARE(cache.reconstructedPixels(),pixels);
        QVERIFY(s.selectLayer(ids.front()));QVERIFY(s.applyPaintStroke({{800,450}},Qt::yellow,30));measure("base",false);
        auto limited_budget=std::make_shared<OpenGlTexturePoolBudget>(64ULL*1024*1024,3);OpenGlFrameCompositor limited(surface.get(),OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),limited_budget);
        const auto comp=cache.prepare(s.exportSnapshot(),"gradient-1080p");timer.restart();const auto refused=limited.composeRasterTexture(makeGpuRasterRequest(comp));QCOMPARE(refused.status,OpenGlCompositionStatus::Unsupported);const auto refusal=timer.elapsed();timer.restart();const auto recovered=ImageInteractiveRasterCache::composite(comp);QVERIFY(!recovered.isNull());qInfo()<<"gradient_capacity refusal_ms"<<refusal<<"cpu_ms"<<timer.elapsed()<<"gpu_bytes"<<limited.resourceUsage().texture_bytes;QCOMPARE(recovered,baseline(s));
    }
    void gpuBrightnessContrast() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Native GPU validation is registered separately.");
        auto surface=OpenGlFrameCompositor::createSurface();QVERIFY(surface);auto budget=std::make_shared<OpenGlTexturePoolBudget>(256ULL*1024*1024,3);
        OpenGlFrameCompositor gpu(surface.get(),OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),budget);
        ImageDocumentSession s;QVERIFY(s.createCanvas({160,100},QColor(40,80,120,150)));const auto base=s.selectedLayerId();
        QVERIFY(s.applyPaintStroke({{5,40},{145,40}},QColor(200,70,130,190),50));
        QPainterPath selection;selection.addRect(0,0,160,100);selection.addEllipse(40,25,30,25);
        const auto id=s.addBrightnessContrastAdjustment(selection);creative_suite::effects::BrightnessContrastParameters points{25,-30};QVERIFY(s.setBrightnessContrast(id,points));
        ImageInteractiveRasterCache cache;
        const auto verify=[&](bool no_upload=false,std::optional<std::uint64_t> expected_upload={}){
            const auto comp=cache.prepare(s.exportSnapshot(),"gradient-gpu");OpenGlCompositionTimings timing;auto result=gpu.composeRasterTexture(makeGpuRasterRequest(comp),{},&timing);QVERIFY2(result.status==OpenGlCompositionStatus::Complete,result.cause.c_str());if(no_upload)QCOMPARE(timing.uploaded_bytes,std::uint64_t(0));if(expected_upload)QCOMPARE(timing.uploaded_bytes,*expected_upload);
            const auto read=gpu.readback(result.frame);QVERIFY(read.frame);const auto actual=QImage(read.frame->rgba_pixels.data(),read.frame->width,read.frame->height,read.frame->stride,QImage::Format_RGBA8888_Premultiplied).convertToFormat(QImage::Format_ARGB32_Premultiplied);const auto expected=baseline(s);
            for(int y=0;y<actual.height();++y)for(int x=0;x<actual.width();++x){auto a=actual.pixel(x,y),b=expected.pixel(x,y);QVERIFY2(std::abs(qRed(a)-qRed(b))<=2 && std::abs(qGreen(a)-qGreen(b))<=2 && std::abs(qBlue(a)-qBlue(b))<=2 && qAlpha(a)==qAlpha(b),qPrintable(QString("Gradient pixel %1,%2: %3 / %4").arg(x).arg(y).arg(a,0,16).arg(b,0,16)));}
        };
        verify();const auto work=cache.reconstructedPixels();for(int mode=1;mode<6;++mode){QVERIFY(s.setStackBlendMode(id,false,static_cast<BlendMode>(mode)));verify(true);QCOMPARE(cache.reconstructedPixels(),work);}
        points.contrast=60;QVERIFY(s.setBrightnessContrast(id,points));verify(true);QCOMPARE(cache.reconstructedPixels(),work);
        QVERIFY(s.setLayerOpacity(id,47));verify(true);QVERIFY(s.setLayerOpacity(id,0));verify(true);QVERIFY(s.setLayerOpacity(id,100));verify(false,160*100); // Re-admit the evicted Grayscale8 mask only.
        auto invalid=makeGpuRasterRequest(cache.prepare(s.exportSnapshot(),"gradient-gpu"));std::get<creative_suite::effects::BrightnessContrastParameters>(*invalid.nodes.back().layers.back().adjustment).brightness=101;
        QCOMPARE(gpu.composeRasterTexture(invalid).status,OpenGlCompositionStatus::Unsupported);
        QCOMPARE(gpu.composeRasterTexture(makeGpuRasterRequest(cache.prepare(s.exportSnapshot(),"gradient-gpu")),[]{return true;}).status,OpenGlCompositionStatus::Cancelled);
        QVERIFY(s.setLayerClipping(id,true));verify();const auto upper=s.addLayer();QVERIFY(s.applyPaintStroke({{5,40},{145,40}},QColor(40,180,210,170),16));QVERIFY(s.setLayerClipping(upper,true));verify();
        ImageLayerOutlineData outline;outline.color=Qt::green;QVERIFY(s.setLayerOutline(base,outline));verify();
        const auto group=s.groupLayers({base,id,upper});QVERIFY(!group.isEmpty());QVERIFY(s.setGroupOpacity(group,65));s.rotateRight();verify();
        static_cast<void>(s.selectGroup(group));QVERIFY(s.setStackBlendMode(group,true,BlendMode::Overlay));const auto top=s.addGradientMapAdjustment();QVERIFY(!s.setGradientMap(top,creative_suite::effects::GradientMapParameters{}));verify();

        // Reference workload: five raster layers, one isolated group and one adjustment.
        QVERIFY(s.createCanvas({1920,1080},Qt::transparent));QStringList ids;for(int i=0;i<5;++i){if(i)QVERIFY(!s.addLayer().isEmpty());ids.append(s.selectedLayerId());QVERIFY(s.applyPaintStroke({{200.,300.+i*30},{1600.,300.+i*30}},QColor(30*i,80,180,190),220));}
        QVERIFY(!s.groupLayers(ids).isEmpty());const auto adjustment=s.addBrightnessContrastAdjustment();QVERIFY(s.setBrightnessContrast(adjustment,points));cache.clear();
        OpenGlTextureFramePtr lease;QElapsedTimer timer;
        const auto measure=[&](const char* label,bool no_upload){timer.restart();const auto c=cache.prepare(s.exportSnapshot(),"gradient-1080p");const auto prepare=timer.elapsed();OpenGlCompositionTimings timing;auto result=gpu.composeRasterTexture(makeGpuRasterRequest(c),{},&timing);QVERIFY2(result.status==OpenGlCompositionStatus::Complete,result.cause.c_str());lease=result.frame;QVERIFY(gpu.readback(lease).frame);if(no_upload)QCOMPARE(timing.uploaded_bytes,std::uint64_t(0));qInfo()<<"brightness_gpu_1080p"<<label<<"prepare_ms"<<prepare<<"total_readback_ms"<<timer.elapsed()<<"uploaded_bytes"<<timing.uploaded_bytes<<"gpu_bytes"<<gpu.resourceUsage().texture_bytes<<"cache_bytes"<<cache.retainedBytes();};
        measure("cold",false);const auto pixels=cache.reconstructedPixels();measure("warm",true);points.brightness=40;QVERIFY(s.setBrightnessContrast(adjustment,points));measure("brightness",true);points.contrast=-20;QVERIFY(s.setBrightnessContrast(adjustment,points));measure("contrast",true);QCOMPARE(cache.reconstructedPixels(),pixels);
        QVERIFY(s.selectLayer(ids.front()));QVERIFY(s.applyPaintStroke({{800,450}},Qt::yellow,30));measure("base",false);
        auto limited_budget=std::make_shared<OpenGlTexturePoolBudget>(64ULL*1024*1024,3);OpenGlFrameCompositor limited(surface.get(),OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),limited_budget);
        const auto comp=cache.prepare(s.exportSnapshot(),"gradient-1080p");timer.restart();const auto refused=limited.composeRasterTexture(makeGpuRasterRequest(comp));QCOMPARE(refused.status,OpenGlCompositionStatus::Unsupported);const auto refusal=timer.elapsed();timer.restart();const auto recovered=ImageInteractiveRasterCache::composite(comp);QVERIFY(!recovered.isNull());qInfo()<<"brightness_capacity refusal_ms"<<refusal<<"cpu_ms"<<timer.elapsed()<<"gpu_bytes"<<limited.resourceUsage().texture_bytes;QCOMPARE(recovered,baseline(s));
    }
    void gpuEditableBlendObjects() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Native GPU validation is registered separately.");
        auto surface=OpenGlFrameCompositor::createSurface();QVERIFY(surface);
        OpenGlFrameCompositor gpu(surface.get(),OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),
            std::make_shared<OpenGlTexturePoolBudget>(256ULL*1024*1024,3));
        QTemporaryDir dir;ImageDocumentSession s;QVERIFY(s.createCanvas({160,100},QColor(100,140,180,180)));
        ImageShapeData shape;shape.start={4,4};shape.end={140,85};shape.fill_color=QColor(220,90,50,180);
        QVERIFY(!s.addShape(shape).isEmpty());const auto shapeLayer=s.selectedLayerId();
        ImageTextData text;text.content="Blend";text.position={10,10};text.box_width=140;text.font_pixel_size=28;
        QVERIFY(!s.addText(text).isEmpty());const auto textLayer=s.selectedLayerId();
        QImage image(160,100,QImage::Format_ARGB32_Premultiplied);image.fill(QColor(40,190,240,140));
        {QPainter p(&image);p.setCompositionMode(QPainter::CompositionMode_Source);p.fillRect(QRect(35,15,20,20),Qt::transparent);}
        const auto path=dir.filePath("source.png");QVERIFY(image.save(path));QVERIFY(s.importRasterImages({{path,image}}));const auto rasterLayer=s.selectedLayerId();
        QVERIFY(s.setLayerClipping(rasterLayer,true));QVERIFY(s.addLayerMask(textLayer));QVERIFY(s.selectLayer(textLayer));
        QVERIFY(s.applyLayerMaskStroke({{60,25}},QColor(128,128,128),35));
        ImageLayerOutlineData outline;outline.position=ImageOutlinePosition::Inside;outline.color=Qt::green;
        QVERIFY(s.setLayerOutline(textLayer,outline));outline.position=ImageOutlinePosition::Outside;QVERIFY(s.setLayerOutline(shapeLayer,outline));
        ImageInteractiveRasterCache cache;
        const auto verify=[&] {
            const auto composition=cache.prepare(s.exportSnapshot(),"gpu-objects");
            auto rendered=gpu.composeRasterTexture(makeGpuRasterRequest(composition));QVERIFY2(rendered.status==OpenGlCompositionStatus::Complete,rendered.cause.c_str());
            const auto read=gpu.readback(rendered.frame);QVERIFY(read.frame);
            const auto actual=QImage(read.frame->rgba_pixels.data(),read.frame->width,read.frame->height,read.frame->stride,QImage::Format_RGBA8888_Premultiplied).convertToFormat(QImage::Format_ARGB32_Premultiplied);
            const auto expected=baseline(s);QCOMPARE(actual.size(),expected.size());
            for(int y=0;y<actual.height();++y)for(int x=0;x<actual.width();++x){const auto a=actual.pixel(x,y),b=expected.pixel(x,y);
                QVERIFY(std::abs(qRed(a)-qRed(b))<=2 && std::abs(qGreen(a)-qGreen(b))<=2 && std::abs(qBlue(a)-qBlue(b))<=2 && std::abs(qAlpha(a)-qAlpha(b))<=2);}
        };
        verify();for(int mode=1;mode<6;++mode){const auto m=static_cast<BlendMode>(mode);
            QVERIFY(s.setStackBlendMode(shapeLayer,false,m));QVERIFY(s.setStackBlendMode(textLayer,false,m));QVERIFY(s.setStackBlendMode(rasterLayer,false,m));verify();}
        const auto group=s.groupLayers({shapeLayer,textLayer,rasterLayer});QVERIFY(!group.isEmpty());
        QVERIFY(s.setStackBlendMode(group,true,BlendMode::Overlay));QVERIFY(s.setGroupOpacity(group,73));s.rotateRight();verify();
        QVERIFY(!s.addGradientMapAdjustment().isEmpty());verify();
    }
    void gpuPixelsUploadsAndPresentation() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Native GPU validation is registered separately.");
        auto surface=OpenGlFrameCompositor::createSurface(); QVERIFY(surface);
        OpenGlFrameCompositor gpu(surface.get(),OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),
            std::make_shared<OpenGlTexturePoolBudget>(256ULL*1024*1024,3));
        ImageDocumentSession session; QVERIFY(session.createCanvas({96,80},Qt::transparent));
        const auto layer=session.addLayer();
        QVERIFY(session.applyPaintStroke({{8.5,10.2},{60,55}},QColor(230,65,120,160),27));
        const auto second=session.addLayer(); QVERIFY(session.applyPaintStroke({{15,18},{78,35}},QColor(10,220,40,170),21));
        QVERIFY(session.addLayerMask(second));
        QVERIFY(session.applyLayerMaskStroke({{28,25},{60,30}},QColor(128,128,128),15));
        QVERIFY(session.setLayerOpacity(second,73));
        ImageInteractiveRasterCache cache;
        OpenGlTextureFramePtr last;
        const auto verify=[&] {
            const auto composition=cache.prepare(session.exportSnapshot(),"gpu");
            OpenGlCompositionTimings timings;
            last.reset();
            auto result=gpu.composeRasterTexture(makeGpuRasterRequest(composition),{},&timings);
            QVERIFY2(result.status==OpenGlCompositionStatus::Complete,result.cause.c_str());
            last=result.frame;
            auto read=gpu.readback(last); QVERIFY(read.frame);
            QImage actual(read.frame->rgba_pixels.data(),read.frame->width,read.frame->height,read.frame->stride,QImage::Format_RGBA8888_Premultiplied);
            const auto expected=baseline(session);
            const auto converted=actual.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            for(int y=0;y<expected.height();++y) for(int x=0;x<expected.width();++x) {
                const auto a=converted.pixel(x,y),b=expected.pixel(x,y);
                QVERIFY2(std::abs(qRed(a)-qRed(b))<=2 && std::abs(qGreen(a)-qGreen(b))<=2 &&
                    std::abs(qBlue(a)-qBlue(b))<=2 && std::abs(qAlpha(a)-qAlpha(b))<=2,
                    qPrintable(QString("Pixel mismatch at %1,%2: %3 / %4").arg(x).arg(y).arg(a,0,16).arg(b,0,16)));
            }
        };
        verify();
        auto composition=cache.prepare(session.exportSnapshot(),"gpu");
        OpenGlCompositionTimings warm; last.reset();
        auto result=gpu.composeRasterTexture(makeGpuRasterRequest(composition),{},&warm);
        QVERIFY(result.status==OpenGlCompositionStatus::Complete); QCOMPARE(warm.uploaded_bytes,std::uint64_t(0));
        result.frame.reset();
        QVERIFY(session.applyLayerMaskStroke({{25,26}},Qt::black,3));
        composition=cache.prepare(session.exportSnapshot(),"gpu");
        auto update=gpu.composeRasterTexture(makeGpuRasterRequest(composition),{},&warm);
        QVERIFY(update.status==OpenGlCompositionStatus::Complete);
        QVERIFY(warm.uploaded_bytes>0 && warm.uploaded_bytes<96ULL*80*4); update.frame.reset(); verify();
        QVERIFY(session.setLayerOutline(layer,ImageLayerOutlineData{})); verify();
        ImageLayerOutlineData inside; inside.position=ImageOutlinePosition::Inside; inside.color=QColor(50,200,90,160);
        QVERIFY(session.setLayerOutline(second,inside)); verify();
        for(int mode=1;mode<6;++mode) { QVERIFY(session.setStackBlendMode(second,false,static_cast<BlendMode>(mode))); verify(); }
        QVERIFY(session.setLayerClipping(second,true)); verify();
        for(int mode=1;mode<6;++mode) { QVERIFY(session.setStackBlendMode(second,false,static_cast<BlendMode>(mode))); verify(); }
        QVERIFY(session.setLayerOpacity(layer,45)); verify();
        const auto group=session.groupLayers({layer,second}); QVERIFY(!group.isEmpty());
        QVERIFY(session.setGroupOpacity(group,65)); verify();
        for(int mode=1;mode<6;++mode) { QVERIFY(session.setStackBlendMode(group,true,static_cast<BlendMode>(mode))); verify(); }
        session.rotateRight(); verify(); session.flipHorizontal(); verify();
        QVERIFY(session.applyCrop({5,7,70,60})); verify();
        ImageOperation bounded; bounded.kind=OperationKind::FlipHorizontal; bounded.transform_bounds={10,12,55,40};
        QVERIFY(session.applySelectedGroupTransform(bounded)); verify();
        QVERIFY(session.setGroupVisible(group,false)); verify();
        QVERIFY(session.setLayerVisible(session.data().layers.front().id,false)); verify();
        ImageGpuPresenter presenter(nullptr); presenter.resize(320,240);
        presenter.targetRect=[] { return QRectF(40,40,192,160); };
        QString failure; presenter.failed=[&](const QString& text){failure=text;};
        presenter.setFrame(last); presenter.show(); QTest::qWait(100);
        const auto rendered=presenter.grabFramebuffer(); QVERIFY(!rendered.isNull()); QVERIFY2(failure.isEmpty(),qPrintable(failure));
        QVERIFY(rendered.save("interactive-canvas-validation.png"));
        presenter.setFrame({}); last.reset();
        std::atomic_bool cancel{true};
        auto cancelled=gpu.composeRasterTexture(makeGpuRasterRequest(composition),[&]{return cancel.load();});
        QCOMPARE(cancelled.status,OpenGlCompositionStatus::Cancelled);
    }
};
int main(int argc,char** argv) {
    QSurfaceFormat format; format.setVersion(3,2); format.setProfile(QSurfaceFormat::CoreProfile); format.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(format); QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc,argv); InteractiveTest test; return QTest::qExec(&test,argc,argv);
}
#include "image_editor_interactive_test.moc"
