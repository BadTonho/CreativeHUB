#include "model/composition_document.h"
#include "model/layer_transform_hierarchy.h"
#include "persistence/motion_document_store.h"
#include "persistence/motion_recovery_store.h"
#include "rendering/composition_frame_renderer.h"
#include "diagnostics/performance_metrics.h"
#include "ui/main_window/main_window.h"
#include "ui/viewer/composition_viewer.h"
#include "model/layer_position.h"
#include <creative_suite/media/video_encoder.h>
#include <QDoubleSpinBox>
#include <QTransform>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <limits>
#include "ui/timeline/timeline_navigator.h"
#include "ui/timeline/timeline_layer_tracks.h"
#include <creative_suite/diagnostics/logger.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QHelpEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace motion::model;
using namespace motion::ui;
using namespace creative_suite::animation;
using Store=motion::persistence::MotionDocumentStore;
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
template<class T>T* widget(QObject& owner,const char* name) {
    auto* result=dynamic_cast<T*>(owner.findChild<QObject*>(QString::fromLatin1(name)));
    if(!result)throw std::runtime_error(std::string("Missing widget: ")+name);
    return result;
}
std::filesystem::path path(const QString& s) { return std::filesystem::path(s.toStdU16String()); }
CompositionLayer shape(LayerId id) {
    CompositionLayer layer{id,LayerKind::Shape,"Repeated"};layer.duration_frames=90;
    auto content=defaultShapeLayerContent({400,200});content.width=32;content.height=20;content.stroke_width_pixels=0;
    layer.content=content;return layer;
}
CompositionLayer control(LayerId id) {auto layer=shape(id);layer.kind=LayerKind::Null;layer.content={};return layer;}
MotionProjectData project(const CompositionDocument& d) {
    MotionProjectData p;p.composition={d.canvasSize(),d.frameRate()};p.layers=d.layers();p.masks=d.masks();
    for(const auto& layer:d.layers())if(!layer.source_path.empty())
        p.media.push_back({layer.source_path,creative_suite::media::MediaKind::Image,"Offline"});
    return p;
}
void waitFor(const std::function<bool()>& condition) {
    QElapsedTimer timer;timer.start();
    while(!condition()&&timer.elapsed()<8000){QApplication::processEvents();QThread::msleep(1);}
    check(condition(),"asynchronous UI completed");
}
void mouse(QWidget& w,QEvent::Type type,QPointF point,Qt::MouseButton button,Qt::MouseButtons buttons) {
    QMouseEvent event(type,point,w.mapToGlobal(point),button,buttons,Qt::NoModifier);QApplication::sendEvent(&w,&event);
}
void press(QWidget& w,QPointF p) {mouse(w,QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);}
void move(QWidget& w,QPointF p) {mouse(w,QEvent::MouseMove,p,Qt::NoButton,Qt::LeftButton);}
void release(QWidget& w,QPointF p) {mouse(w,QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton);}
void escape(QWidget& w) {QKeyEvent event(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&w,&event);}
void closeWindow(MainWindow& w) {
    QTimer::singleShot(0,[]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))box->button(QMessageBox::Discard)->click();});
    w.close();QApplication::processEvents();
}
void open(MainWindow& w,const QString& filename) {
    QTimer::singleShot(0,[filename]{auto* d=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());check(d,"file picker");
        d->selectFile(filename);QMetaObject::invokeMethod(d,"accept",Qt::DirectConnection);});
    widget<QAction>(w,"motion-open-composition-action")->trigger();waitFor([&]{return w.compositionDocument()!=nullptr;});
    QApplication::processEvents();
}
QPointF pointFor(MainWindow& w,LayerId id,double x=60) {
    const auto& layers=w.compositionDocument()->layers();
    const auto found=std::find_if(layers.rbegin(),layers.rend(),[id](const auto& layer){return layer.id==id;});
    check(found!=layers.rend(),"row ID exists");
    return {x,34.0*std::distance(layers.rbegin(),found)+17};
}
void showPoint(QScrollArea* area,QPointF point) {area->ensureVisible(qRound(point.x()),qRound(point.y()),0,4);}
auto resolve(const CompositionDocument& d,std::int64_t f) {return LayerTransformHierarchy(d.canvasSize(),d.layers()).evaluate(f);}
void same(const AffineTransform& a,const AffineTransform& b) {
    for(const auto& p:{QPointF(0,0),QPointF(100,-35),QPointF(-19,213)}) {
        const auto x=a.map(p.x(),p.y()),y=b.map(p.x(),p.y());
        check(std::abs(x[0]-y[0])<1e-8&&std::abs(x[1]-y[1])<1e-8,"full affine geometry preserved");
    }
}
PreviewRequest request(const CompositionDocument& d,std::int64_t frame) {
    PreviewRequest r;r.canvas_size=d.canvasSize();r.frame_rate=d.frameRate();r.composition_frame=frame;
    r.hierarchy=std::make_shared<const LayerTransformHierarchy>(d.canvasSize(),d.layers());
    r.masks=std::make_shared<const std::vector<Mask>>(d.masks());
    for(const auto& l:d.layers())if(l.visible&&l.kind!=LayerKind::Null&&frame>=l.timeline_start_frame&&frame-l.timeline_start_frame<l.duration_frames) {
        PreviewLayerSnapshot s;s.id=l.id;s.kind=l.kind;s.local_frame=frame-l.timeline_start_frame;s.transform=l.transform;s.keyframes=l.keyframes;
        s.anchor_point=l.anchor_point;s.anchor_keyframes=l.anchor_keyframes;s.scale_y=l.scale_y;s.content=l.content;s.effects=l.effects;
        s.source_mask=l.source_mask;s.transform_mask=l.transform_mask;r.layers.push_back(s);
    }
    return r;
}


bool near(double a,double b) {return std::abs(a-b)<1e-8;}
QRectF canvasRect(const QWidget& w,CanvasSize size) {
    const auto area=QRectF(w.rect()).adjusted(24,42,-24,-24);
    QSizeF fit(size.width,size.height);fit.scale(area.size(),Qt::KeepAspectRatio);
    return {area.center()-QPointF(fit.width()/2,fit.height()/2),fit};
}
QPointF handle(CompositionViewer& viewer,CanvasSize size) {
    const auto pivot=viewer.selectedLayerAnchor();check(pivot.has_value(),"selected pivot exists");
    const auto c=canvasRect(viewer,size);
    return c.topLeft()+QPointF(pivot->x()*c.width()+32,pivot->y()*c.height());
}
void modelGeometry() {
    for(double angle:{0.,33.,90.}) {
        auto parent=control(9);parent.visible=false;parent.timeline_start_frame=30;parent.duration_frames=1;
        parent.transform={.1,.1,2,90,1};parent.scale_y=ScaleChannel{.5,{}};
        auto source=control(1);source.parent_id=9;source.transform={.2,.3,1.7,angle,1};source.scale_y=ScaleChannel{.3,{}};
        source.anchor_point={37.25,-80};source.parent_affine=AffineTransform{1,.2,.3,1,5,-8};
        source.keyframes.position_x={{0,.1,InterpolationMode::CubicBezier,{.2,.4,.7,.9}},{20,.3}};
        if(angle==33)source.keyframes.position_y={{0,.2},{20,.4}};
        auto child=shape(2);child.parent_id=1;child.transform={.15,.2,.5,13,1};
        CompositionDocument d(400,200,{30,1},{child,source,parent});const auto before=resolve(d,10);
        // Independent Qt outer matrix: R(90)*S(2,.5)*Binding. Own R/S/Anchor
        // never transform a Position displacement.
        QTransform outer;outer.rotate(90);outer.scale(2,.5);
        const QTransform binding(1,.2,.3,1,5,-8);
        const auto qt=binding*outer;const auto local_delta=qt.inverted().map(QPointF(17.5,-9.25))-qt.inverted().map(QPointF(0,0));
        auto inverse=creative_suite::composition::requireInverseAffine(
            creative_suite::composition::composeAffine(before.at(9).world,*source.parent_affine));inverse.tx=inverse.ty=0;
        const auto evaluated=evaluateTransform(source.transform,source.keyframes,10);
        const auto value=positionFromCanvasDelta(evaluated.position_x,evaluated.position_y,d.canvasSize(),inverse,17.5,-9.25);
        check(near(value[0],evaluated.position_x+local_delta.x()/400)&&near(value[1],evaluated.position_y+local_delta.y()/200),"independent Position geometry under affine parent");
        const auto draft=LayerTransformHierarchy(d.canvasSize(),d.layers()).evaluate(10,LayerPositionOverride{1,value[0],value[1]});
        for(auto id:{LayerId(1),LayerId(2)}) {auto expected=before.at(id).world;expected.tx+=17.5;expected.ty-=9.25;same(expected,draft.at(id).world);}
        same(before.at(9).world,draft.at(9).world);
        const auto original=d.layers();check(d.setLayerPositionAtFrame(1,value[0],value[1],10),"atomic static/animated edit");
        const auto& edited=d.layers()[1];check(edited.keyframes.position_x.size()==3&&edited.keyframes.position_x[0]==source.keyframes.position_x[0],"insert between Bezier keys without rewriting neighbors");
        check(edited.scale_y==source.scale_y&&edited.anchor_point==source.anchor_point&&edited.parent_affine==source.parent_affine&&edited.transform.scale==source.transform.scale&&edited.transform.rotation_degrees==source.transform.rotation_degrees,"unrelated authored geometry retained");
        const auto after=resolve(d,10);same(after.at(1).world,draft.at(1).world);same(after.at(2).world,draft.at(2).world);
        check(!d.setLayerPositionAtFrame(1,value[0],value[1],10)&&!d.setLayerPositionAtFrame(1,1e7,0,10),"unchanged/out-of-bounds edits rejected");
        check(!d.setLayerPositionAtFrame(1,0,0,100)&&!d.setLayerPositionAtFrame(99,0,0,10),"inactive/missing layer rejected");
        const auto at11=evaluateTransform(d.layers()[1].transform,d.layers()[1].keyframes,11);
        check(d.setLayerPositionAtFrame(1,at11.position_x,value[1]+.01,11)&&d.layers()[1].keyframes.position_x.size()==3,"unchanged animated axis receives no key");
        (void)original;
    }
    bool rejected=false;try{(void)positionFromCanvasDelta(0,0,{400,200},{},std::numeric_limits<double>::infinity(),0);}catch(const std::exception&){rejected=true;}check(rejected,"nonfinite delta rejected");
    auto one=shape(1);CompositionDocument d(400,200,{30,1},{one});rejected=false;
    try{(void)LayerTransformHierarchy(d.canvasSize(),d.layers()).evaluate(0,LayerPositionOverride{99,0,0});}catch(const std::exception&){rejected=true;}check(rejected,"unknown override rejected");
    // The native document limit includes other properties, not only Position.
    one.keyframes.opacity.reserve(maximum_document_keyframes-2);
    for(std::size_t n=0;n<maximum_document_keyframes-2;++n)one.keyframes.opacity.push_back({static_cast<std::int64_t>(n),1});
    one.keyframes.position_x={{0,.5},{20,.5}};CompositionDocument full(400,200,{30,1},{std::move(one)});
    rejected=!full.setLayerPositionAtFrame(1,.6,.5,10);
    check(rejected&&full.layers()[0].keyframes.position_x.size()==2,"key limit rejects insertion atomically");
}
void pointerGesture() {
    CompositionViewer viewer;viewer.resize(600,400);viewer.setComposition({400,200},QPointF(.5,.5));viewer.setPositionDragEnabled(true);
    int starts=0,commits=0,cancels=0;QPointF delta;
    viewer.setPositionDragHandlers({[&]{++starts;return true;},[&](QPointF d){delta=d;return true;},[&]{++commits;},[&]{++cancels;}});
    viewer.show();QApplication::processEvents();const auto c=canvasRect(viewer,{400,200});const auto h=handle(viewer,{400,200});
    mouse(viewer,QEvent::MouseButtonPress,h,Qt::RightButton,Qt::RightButton);check(!starts,"right button never starts");
    press(viewer,h+QPointF(11,0));check(!starts,"hit radius is ten logical pixels");release(viewer,h+QPointF(11,0));
    press(viewer,h+QPointF(8,0));check(starts==1&&viewer.positionDragActive(),"offset hit begins Position gesture");
    move(viewer,h+QPointF(9,0));check(delta.isNull(),"sub-threshold motion has no preview");
    release(viewer,h+QPointF(28,7));check(commits==1&&near(delta.x(),20*400/c.width())&&near(delta.y(),7*200/c.height()),"no jump and final release coordinates");
    press(viewer,h);release(viewer,h);check(commits==1&&cancels==1,"click cancels without commit");
    press(viewer,h);release(viewer,{viewer.width()+50.,viewer.height()+80.});check(commits==2,"capture permits outside release");
    press(viewer,h);escape(viewer);check(!viewer.positionDragActive(),"Escape cancels");
    for(auto type:{QEvent::UngrabMouse,QEvent::WindowDeactivate,QEvent::Hide}) {
        press(viewer,h);QEvent event(type);QApplication::sendEvent(&viewer,&event);check(!viewer.positionDragActive(),"capture/window lifecycle cancels");
    }
    press(viewer,h);viewer.resize(610,410);check(!viewer.positionDragActive(),"resize cancels");
    press(viewer,handle(viewer,{400,200}));viewer.move(viewer.pos()+QPoint(10,0));check(!viewer.positionDragActive(),"layout movement cancels");
    viewer.setComposition({400,200},QPointF(3,3));press(viewer,{500,200});check(!viewer.positionDragActive(),"offscreen handle not clamped into view");
    viewer.hide();
}
void rendering(const QString& root) {
    auto pixels=std::make_shared<creative_suite::media::RgbaFrame>();pixels->width=96;pixels->height=64;pixels->stride=384;pixels->rgba_pixels.resize(96*64*4,255);
    creative_suite::media::VideoEncodingSettings encoding;encoding.output_path=path(root+"/fixed.mkv");encoding.container_name="matroska";encoding.video_encoder_name="ffv1";encoding.width=96;encoding.height=64;encoding.frame_rate_numerator=30;
    {creative_suite::media::VideoEncoder encoder(encoding);for(int n=0;n<6;++n)encoder.writeVideo(*pixels,n);encoder.finish();}
    for(auto kind:{LayerKind::Shape,LayerKind::Text,LayerKind::Image,LayerKind::Video}) {
        auto source=kind==LayerKind::Shape?shape(1):control(1);source.transform={.2,.2,1,0,1};source.parent_affine=AffineTransform{1,.2,.3,1,0,0};
        auto child=shape(2);child.parent_id=1;child.kind=kind;child.transform={.2,.2,.5,15,1};child.scale_y=ScaleChannel{.3,{}};child.anchor_point={3,-2};
        if(kind==LayerKind::Text){auto text=defaultTextLayerContent({400,200});text.text="Move";child.content=text;}
        if(kind==LayerKind::Image||kind==LayerKind::Video)child.content={};
        if(kind==LayerKind::Image)child.source_path=path(root+"/inline.png");
        if(kind==LayerKind::Video) {
            child.source_path=encoding.output_path;child.source_frame_rate=30;child.source_frame_count=6;
            child.duration_frames=child.maximum_timeline_duration_frames=6;
        }
        child.effects={ColorAdjustmentEffect{true,7,100,100},GaussianBlurEffect{true,1}};
        CompositionDocument d(400,200,{30,1},{source,child});
        for(int stage=-2;stage<=0;++stage)check(d.createMask(2,stage,creative_suite::composition::ShapeMaskKind::Ellipse)!=0,"stage mask fixture");
        const auto make=[&](const CompositionDocument& document) {
            auto r=request(document,2);for(auto& l:r.layers) {
                l.still_frame=kind==LayerKind::Image?pixels:nullptr;
                if(kind==LayerKind::Video){l.source_path=encoding.output_path;l.source_frame_rate=30;l.source_frame_count=6;}
            }return r;
        };
        auto base=make(d);base.freezeScene();auto draft=base;draft.position_override=LayerPositionOverride{1,.225,.2375};
        check(draft.scene==base.scene&&draft.layers.empty(),"draft shares heavy immutable scene");
        CompositionFrameRenderer renderer;auto& metrics=motion::diagnostics::PerformanceMetrics::instance();metrics.setEnabled(true);metrics.reset();
        check(renderer.render(base)!=nullptr,"warm sources");(void)metrics.takeSnapshotAndReset();
        const auto live=renderer.render(draft,{},false,PreviewRequestMode::Gesture);check(live!=nullptr,"live frame");
        const auto count=metrics.takeSnapshotAndReset();check(count&&count->timestamp_seek_attempts==0&&count->forward_decode_attempts==0&&
            count->timings[static_cast<std::size_t>(motion::diagnostics::PreviewTimingStage::TextShapeRasterization)].count==0,"fixed-frame drag reuses decode/raster caches");
        check(d.setLayerPositionAtFrame(1,.225,.2375,2),"reference confirmed edit");const auto confirmed=renderer.render(make(d));
        check(confirmed&&confirmed->rgba_pixels==live->rgba_pixels,"draft/confirmed pixel parity with every source kind and stage masks");
        for(int stage=-2;stage<=0;++stage) {
            auto a=draft;a.mask_view_layer=2;a.mask_view_target=stage;auto b=make(d);b.mask_view_layer=2;b.mask_view_target=stage;
            const auto x=renderer.render(a),y=renderer.render(b);check(x&&y&&x->rgba_pixels==y->rgba_pixels,"mask View parity");
        }
    }
}
void scheduling() {
    QObject receiver;std::mutex mutex;std::condition_variable gate;bool first=false,release_first=false;
    std::atomic<int> calls=0;std::vector<PreviewFrame> frames;
    auto pixels=std::make_shared<creative_suite::media::RgbaFrame>();pixels->width=pixels->height=1;pixels->stride=4;pixels->rgba_pixels.resize(4,255);
    PreviewRenderer renderer(&receiver,{},[&](const PreviewRequest& r,const auto& cancelled) {
        if(r.gesture_session==4)throw std::runtime_error("Injected gesture rendering failure.");
        if(++calls==1){std::unique_lock lock(mutex);first=true;gate.notify_all();gate.wait(lock,[&]{return release_first;});check(!cancelled(),"same gesture does not starve slow in-flight work");}
        check(r.scene!=nullptr,"shared scene reaches worker");return pixels;
    },nullptr,false,nullptr,[&](PreviewFrame f){frames.push_back(std::move(f));});
    PreviewRequest r;r.gesture_session=1;r.freezeScene();(void)renderer.submit(r,PreviewRequestMode::Gesture);
    {std::unique_lock lock(mutex);check(gate.wait_for(lock,std::chrono::seconds(3),[&]{return first;}),"worker started");}
    for(int n=0;n<40;++n){r.position_override=LayerPositionOverride{1,double(n),0};(void)renderer.submit(r,PreviewRequestMode::Gesture);}
    {std::lock_guard lock(mutex);release_first=true;}gate.notify_all();
    waitFor([&]{return calls==2&&!frames.empty()&&frames.back().generation==renderer.generation();});check(renderer.pendingDeliveryCount()<=1,"one delivery mailbox");
    check(frames.back().gesture_session==1&&renderer.canPresentFrame(frames.back()),"latest session presentable");
    auto old=frames.back();renderer.recoverPresentation(true);check(!renderer.canPresentFrame(old),"fallback invalidates old delivery");
    (void)renderer.submit(r,PreviewRequestMode::Gesture);waitFor([&]{return calls==3&&frames.back().cancellation_generation!=old.cancellation_generation;});
    check(renderer.canPresentFrame(frames.back()),"same draft survives recovery");
    r.gesture_session=2;(void)renderer.submit(r,PreviewRequestMode::Gesture);check(!renderer.canPresentFrame(old),"new session rejects old result");
    r.gesture_session=4;(void)renderer.submit(r,PreviewRequestMode::Gesture);
    waitFor([&]{return frames.back().gesture_session==4;});check(frames.back().render_failed,"render failure reaches controller with session identity");
    renderer.stopAndWait();
    CompositionViewer viewer;viewer.setRenderedFrame(pixels);PreviewFrame newer;newer.rgba=pixels;newer.generation=5;newer.gesture_session=3;viewer.setPreviewFrame(newer);
    auto other=std::make_shared<creative_suite::media::RgbaFrame>(*pixels);other->rgba_pixels[0]=3;newer.rgba=other;newer.generation=4;viewer.setPreviewFrame(newer);
    check(viewer.renderedFrame()==pixels,"out-of-order gesture result cannot replace newer content");
}
void application(const QString& root) {
    auto parent=control(9);parent.kind=LayerKind::Image;parent.source_path=path(root+"/missing.png");parent.visible=false;parent.timeline_start_frame=30;parent.duration_frames=1;parent.transform={.05,.05,1.2,30,1};parent.scale_y=ScaleChannel{.7,{}};
    auto source=control(1);source.parent_id=9;source.transform={.2,.2,1.5,-20,1};source.scale_y=ScaleChannel{.5,{}};source.anchor_point={-3,7};source.parent_affine=AffineTransform{1,.1,.2,1,0,0};
    source.keyframes.position_x={{0,.15,InterpolationMode::CubicBezier,{.2,.3,.7,.8}},{20,.25}};
    auto child=shape(2);child.parent_id=1;child.transform={.15,.1,1,10,1};
    auto background=shape(3);background.transform.position_x=.8;background.transform.position_y=.8;
    CompositionDocument initial(400,200,{30,1},{background,child,source,parent});const auto filename=root+"/drag.motion";Store::save(path(filename),project(initial));
    MainWindow w(nullptr,path(root+"/recovery"),"position-drag");w.show();open(w,filename);
    widget<QAction>(w,"motion-new-null-layer-action")->trigger();auto* timeline=widget<TimelineNavigator>(w,"motion-timeline");timeline->setCurrentFrame(10);
    auto* rows=widget<QWidget>(w,"motion-timeline-layer-rows");auto* area=widget<QScrollArea>(w,"motion-timeline-layer-scroll");const auto select=pointFor(w,1,120);showPoint(area,select);press(*rows,select);release(*rows,select);QApplication::processEvents();
    auto* viewer=widget<CompositionViewer>(w,"motion-composition-viewer");auto* x=widget<QDoubleSpinBox>(w,"motion-transform-position-x");auto* y=widget<QDoubleSpinBox>(w,"motion-transform-position-y");
    auto* undo=widget<QAction>(w,"motion-undo-action");auto* redo=widget<QAction>(w,"motion-redo-action");
    // Wait for this selection/frame, rather than any frame left by Open or the
    // earlier structural edit. Drain posted layout work before freezing the
    // gesture: layout changes intentionally cancel a captured Viewer gesture.
    CompositionFrameRenderer initial_renderer(false);
    const auto initial_frame=initial_renderer.render(request(*w.compositionDocument(),10));
    waitFor([&]{return initial_frame&&viewer->renderedFrame()&&viewer->renderedFrame()->rgba_pixels==initial_frame->rgba_pixels;});
    QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);
    QApplication::processEvents();
    const auto before=w.compositionDocument()->layers();const auto c=canvasRect(*viewer,{400,200});
    const auto initial_x=x->value(),initial_y=y->value();const auto begin=[&]{const auto h=handle(*viewer,{400,200})+QPointF(6,-2);press(*viewer,h);check(viewer->positionDragActive(),"Position handle captures");return h;};
    widget<QAction>(w,"motion-play-pause-action")->trigger();auto h=begin();check(!timeline->isPlaying()&&timeline->currentFrame()==10,"press pauses/fixes frame");
    const auto frame_before_drag=viewer->renderedFrame();
    move(*viewer,h+QPointF(21.25,-13.75));check(x->isReadOnly()&&y->isReadOnly()&&w.compositionDocument()->layers()==before,"Inspector preview/document isolation");
    check(!redo->isEnabled()&&(!near(x->value(),initial_x)||!near(y->value(),initial_y)),"live local Position and blocked redo");
    waitFor([&]{return viewer->renderedFrame()&&viewer->renderedFrame()!=frame_before_drag;});
    check(viewer->positionDragActive(),"Position gesture stays active until explicit completion/cancellation");
    const auto evaluated=evaluateTransform(source.transform,source.keyframes,10);
    auto outer=creative_suite::composition::composeAffine(resolve(*w.compositionDocument(),10).at(9).world,*source.parent_affine);outer.tx=outer.ty=0;
    const auto exact=positionFromCanvasDelta(evaluated.position_x,evaluated.position_y,{400,200},creative_suite::composition::requireInverseAffine(outer),21.25*400/c.width(),-13.75*200/c.height());
    auto draft=request(*w.compositionDocument(),10);draft.position_override=LayerPositionOverride{1,exact[0],exact[1]};CompositionFrameRenderer renderer(false);
    const auto expected=renderer.render(draft);
    if(!expected||viewer->renderedFrame()->rgba_pixels!=expected->rgba_pixels) {
        std::cerr<<"Position reference mismatch: fields="<<x->value()<<","<<y->value()<<" exact="<<exact[0]<<","<<exact[1]
            <<" frame="<<timeline->currentFrame()<<" size="<<viewer->width()<<","<<viewer->height()<<" canvas="<<c.width()<<","<<c.height()<<"\n";
        if(expected) {std::size_t differences=0;for(std::size_t n=0;n<expected->rgba_pixels.size();++n)if(expected->rgba_pixels[n]!=viewer->renderedFrame()->rgba_pixels[n]){if(differences++<4)std::cerr<<"pixelbyte "<<n<<" expected "<<int(expected->rgba_pixels[n])<<" actual "<<int(viewer->renderedFrame()->rgba_pixels[n])<<"\n";}
            std::cerr<<"different bytes="<<differences<<"\n";}
    }
    check(expected&&viewer->renderedFrame()->rgba_pixels==expected->rgba_pixels,"live preview matches candidate document geometry");
    auto* autosave=widget<QTimer>(w,"motion-autosave-timer");QMetaObject::invokeMethod(autosave,"timeout",Qt::DirectConnection);
    motion::persistence::MotionRecoveryStore recovery(path(root+"/recovery"),"reader");const auto snapshots=recovery.snapshotsForProject(path(filename));
    check(!snapshots.empty()&&Store::loadRecovery(snapshots.front().path).document.layers==before,"autosave never stores draft");
    const QPointF final_delta(31.75,17.25);release(*viewer,h+final_delta);const auto after=w.compositionDocument()->layers();check(after!=before,"release commits");
    auto original=CompositionDocument(400,200,{30,1},before);const auto worlds=resolve(original,10),edited=resolve(*w.compositionDocument(),10);
    for(auto id:{LayerId(1),LayerId(2)}){auto shifted=worlds.at(id).world;shifted.tx+=final_delta.x()*400/c.width();shifted.ty+=final_delta.y()*200/c.height();same(shifted,edited.at(id).world);}
    same(worlds.at(3).world,edited.at(3).world);check(!y->isReadOnly(),"normal static-field synchronization restored");
    undo->trigger();check(w.compositionDocument()->layers()==before&&redo->isEnabled(),"single Undo");
    h=begin();move(*viewer,h+QPointF(20,0));escape(*viewer);check(w.compositionDocument()->layers()==before&&redo->isEnabled(),"Escape keeps redo branch");
    h=begin();move(*viewer,h+QPointF(20,0));undo->trigger();check(w.compositionDocument()->layers()==before&&redo->isEnabled(),"Undo cancels only draft");
    redo->trigger();check(w.compositionDocument()->layers()==after,"Redo restores edit");
    h=begin();release(*viewer,h);undo->trigger();check(w.compositionDocument()->layers()==before,"click adds no history");redo->trigger();
    h=begin();move(*viewer,h+QPointF(20,0));release(*viewer,h);undo->trigger();check(w.compositionDocument()->layers()==before,"return to original adds no history");redo->trigger();
    h=begin();move(*viewer,h+QPointF(20,0));timeline->setCurrentFrame(11);check(!viewer->positionDragActive(),"frame change cancels");timeline->setCurrentFrame(10);
    h=begin();move(*viewer,h+QPointF(20,0));
    const auto other_row=pointFor(w,3,120);showPoint(area,other_row);press(*rows,other_row);release(*rows,other_row);
    check(!viewer->positionDragActive()&&w.compositionDocument()->layers()==after,"selection change cancels without editing");
    const auto source_row=pointFor(w,1,120);showPoint(area,source_row);press(*rows,source_row);release(*rows,source_row);
    h=begin();move(*viewer,h+QPointF(20,0));widget<QAction>(w,"motion-new-null-layer-action")->trigger();
    check(!viewer->positionDragActive(),"structural edit cancels before proceeding");undo->trigger();check(w.compositionDocument()->layers()==after,"only the following structural edit enters history");
    h=begin();move(*viewer,h+QPointF(20,0));widget<QAction>(w,"motion-save-composition-action")->trigger();check(!viewer->positionDragActive()&&Store::load(path(filename)).layers==after,"Save cancels draft and persists confirmed v12 values");
    Store::saveRecovery(path(root+"/roundtrip.recovery"),path(filename),"gesture",project(*w.compositionDocument()));check(Store::loadRecovery(path(root+"/roundtrip.recovery")).document.layers==after,"recovery roundtrip");
    h=begin();closeWindow(w);check(!viewer->positionDragActive(),"close cancels");
}
void numericFailure(const QString& root) {
    auto source=shape(1),parent=control(2);parent.transform={0,0,1e-308,0,1};source.parent_id=2;
    CompositionDocument d(400,200,{30,1},{parent,source});const auto file=root+"/numeric.motion";Store::save(path(file),project(d));
    MainWindow w(nullptr,path(root+"/numeric-recovery"),"numeric");w.show();open(w,file);
    auto* rows=widget<QWidget>(w,"motion-timeline-layer-rows");press(*rows,{120,17});release(*rows,{120,17});QApplication::processEvents();
    auto* viewer=widget<CompositionViewer>(w,"motion-composition-viewer");auto h=handle(*viewer,{400,200});press(*viewer,h);move(*viewer,h+QPointF(100,0));
    check(!viewer->positionDragActive()&&w.compositionDocument()->layers()==d.layers(),"overflow cancels atomically");
    QFile log(QString::fromStdU16String(creative_suite::diagnostics::Logger::instance().log_path().u16string()));check(log.open(QIODevice::ReadOnly),"read log");
    check(log.readAll().contains("position_drag_preview"),"technical failure logged before status message");closeWindow(w);
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setOrganizationName("CreativeSuiteTests");app.setApplicationName("MotionPositionDrag");
    QStandardPaths::setTestModeEnabled(true);QTemporaryDir root;if(!root.isValid())return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path());
    try{check(creative_suite::diagnostics::Logger::instance().initialize(path(root.path()+"/logs")),"logger");
        modelGeometry();pointerGesture();rendering(root.path());scheduling();application(root.path());numericFailure(root.path());
        std::cout<<"Position drag tests passed (DPR "<<app.devicePixelRatio()<<").\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
