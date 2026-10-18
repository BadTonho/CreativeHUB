#include "model/composition_document.h"
#include "model/layer_transform_hierarchy.h"
#include "persistence/motion_document_store.h"
#include "persistence/motion_recovery_store.h"
#include "rendering/composition_frame_renderer.h"
#include "diagnostics/performance_metrics.h"
#include "ui/main_window/main_window.h"
#include "ui/viewer/composition_viewer.h"
#include "model/layer_position.h"
#include "model/layer_rotation.h"
#include "model/composition_interchange.h"
#include "export/motion_video_export.h"
#include <creative_suite/media/video_decoder.h>
#include <numbers>
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
#include <QKeySequence>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
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
    auto* action=widget<QAction>(w,"motion-open-composition-action");action->trigger();
    waitFor([&]{return w.compositionDocument()!=nullptr&&action->isEnabled();});
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
QPointF positionHandle(CompositionViewer& viewer,CanvasSize size) {
    const auto pivot=viewer.selectedLayerAnchor();check(pivot.has_value(),"selected pivot exists");
    const auto c=canvasRect(viewer,size);
    return c.topLeft()+QPointF(pivot->x()*c.width()+32,pivot->y()*c.height());
}

QPointF screenPivot(CompositionViewer& v,CanvasSize size={400,200}) {
    const auto p=v.selectedLayerAnchor();check(p.has_value(),"pivot available");
    const auto c=canvasRect(v,size);return c.topLeft()+QPointF(p->x()*c.width(),p->y()*c.height());
}
QPointF toCanvas(CompositionViewer& v,QPointF point) {
    const auto c=canvasRect(v,{400,200});return {(point.x()-c.left())*400/c.width(),(point.y()-c.top())*200/c.height()};
}
QPointF circle(double degrees,double radius=56) {
    const auto a=degrees*std::numbers::pi/180.;return {std::cos(a)*radius,std::sin(a)*radius};
}
QTransform localQt(const CompositionLayer& l,std::int64_t frame) {
    const auto t=evaluateTransform(l.transform,l.keyframes,frame);const auto a=evaluateLayerAnchor(l,frame);const auto s=evaluateLayerScale(l,frame);
    QTransform result;result.translate(t.position_x*400,t.position_y*200);result.rotate(t.rotation_degrees);
    result.scale(s[0],s[1]);result.translate(-a.x_pixels,-a.y_pixels);return result;
}
void matchesQt(const AffineTransform& actual,const QTransform& expected) {
    for(const auto point:{QPointF(0,0),QPointF(53,-91),QPointF(-18,213)}) {
        const auto a=actual.map(point.x(),point.y());const auto b=expected.map(point);
        check(near(a[0],b.x())&&near(a[1],b.y()),"independent affine reference");
    }
}
void angularCalculation() {
    const AffineTransform outer{2,.3,.7,.8,0,0};const auto inverse=creative_suite::composition::requireInverseAffine(outer);
    const QTransform qt(2,.3,.7,.8,0,0);const QPointF pivot(123.25,-19.5);
    const auto point=[&](double a){return pivot+qt.map(circle(a,50));};
    auto p=point(171);RotationDragCalculation calc(42.25,inverse,pivot.x(),pivot.y(),p.x(),p.y(),2);
    double value=0;for(int n=1;n<=80;++n){p=point(171+n*10);value=calc.update(p.x(),p.y());}
    check(near(value,842.25),"unwrapped positive turns through boundary with shear");
    for(int n=79;n>=0;--n){p=point(171+n*10);value=calc.update(p.x(),p.y());}
    check(value==42.25,"return to original clears numerical residue");
    for(int n=1;n<=72;++n){p=point(171-n*10);value=calc.update(p.x(),p.y());}
    check(near(value,-677.75),"negative full turns preserved");
    RotationDragCalculation tie(0,{},0,0,10,0,1);check(tie.update(-10,0)==180,"exact 180 chooses positive");
    check(tie.update(0,0)==180,"neutral zone retains last value");
    check(tie.update(0,-10)==180&&tie.update(10,0)==270,"exit rebases without jump");
    const auto guide=rotationGuideDirection(outer,33.5);const auto expected=qt.map(circle(33.5-90));
    check(near(guide[0],expected.x()/std::hypot(expected.x(),expected.y()))&&near(guide[1],expected.y()/std::hypot(expected.x(),expected.y())),"guide projects local -Y without decomposition");
    bool rejected=false;try{(void)tie.update(std::numeric_limits<double>::infinity(),0);}catch(const std::exception&){rejected=true;}check(rejected,"nonfinite pointer rejected");
    rejected=false;try{RotationDragCalculation invalid(0,{1,0,0,0,0,0},0,0,10,0,1);}catch(const std::exception&){rejected=true;}check(rejected,"singular outer rejected");
}
void modelGeometry() {
    for(bool animated:{false,true})for(auto interpolation:{InterpolationMode::Linear,InterpolationMode::CubicBezier}) {
        auto parent=control(9);parent.kind=LayerKind::Image;parent.source_path="offline.png";parent.visible=false;
        parent.timeline_start_frame=50;parent.duration_frames=1;parent.transform={.1,.1,2,30,1};parent.scale_y=ScaleChannel{.4,{}};
        parent.keyframes.rotation={{0,30},{20,60}};
        auto source=control(1);source.parent_id=9;source.timeline_start_frame=7;source.transform={.2,.3,1.7,-35,1};source.scale_y=ScaleChannel{.3,{{0,.2},{20,.4}}};
        source.anchor_point={130.25,-83.75};source.anchor_keyframes[0]={{0,120.25},{20,140.25}};
        source.parent_affine=AffineTransform{1,.2,.3,1,5,-8};source.scale_linked=false;
        if(animated)source.keyframes.rotation={{0,-35,interpolation,{.2,.4,.7,.9}},{20,45}};
        auto child=shape(2);child.parent_id=1;child.transform={.15,.2,.5,13,1};auto other=shape(3);
        CompositionDocument d(400,200,{30,1},{child,source,parent,other});const auto before=resolve(d,17);const auto original=d.layers();
        const LayerTransformHierarchy hierarchy(d.canvasSize(),d.layers());
        const auto draft=hierarchy.evaluate(17,{},LayerRotationOverride{1,725.75});
        auto expected_source=source;expected_source.transform.rotation_degrees=725.75;expected_source.keyframes.rotation.clear();
        const QTransform binding(1,.2,.3,1,5,-8);const auto world=localQt(expected_source,10)*binding*localQt(parent,0);
        matchesQt(draft.at(1).world,world);matchesQt(draft.at(2).world,localQt(child,17)*world);
        same(draft.at(3).world,before.at(3).world);
        const auto initial_pivot=before.at(1).world.map(130.25,-83.75),next_pivot=draft.at(1).world.map(130.25,-83.75);
        check(near(initial_pivot[0],next_pivot[0])&&near(initial_pivot[1],next_pivot[1]),"Anchor pivot stays fixed");
        check(d.layers()==original,"override never changes authored state");check(d.setLayerRotationAtFrame(1,725.75,17),"atomic local Rotation");
        auto edited=d.layers()[1];if(animated){check(edited.keyframes.rotation.size()==3&&edited.keyframes.rotation.front()==source.keyframes.rotation.front()&&edited.keyframes.rotation.back()==source.keyframes.rotation.back(),"insert preserves neighboring easing and keys");edited.keyframes.rotation=source.keyframes.rotation;}
        else {check(edited.keyframes.rotation.empty(),"static rotation stays static");edited.transform.rotation_degrees=source.transform.rotation_degrees;}
        check(edited==source,"all other layer values retained");same(resolve(d,17).at(2).world,draft.at(2).world);
        check(!d.setLayerRotationAtFrame(1,725.75,17),"unchanged value has no edit");
        if(animated)check(d.setLayerRotationAtFrame(1,-721,17)&&d.layers()[1].keyframes.rotation.size()==3,"edit existing key");
        const auto stable=d.layers();
        for(double invalid:{1e9+1,-1e9-1,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})check(!d.setLayerRotationAtFrame(1,invalid,17),"invalid angle rejected");
        check(!d.setLayerRotationAtFrame(1,0,6)&&!d.setLayerRotationAtFrame(1,0,100)&&!d.setLayerRotationAtFrame(99,0,17)&&d.layers()==stable,"invalid frame/ID is atomic");
        bool rejected=false;try{(void)hierarchy.evaluate(17,{},LayerRotationOverride{99,0});}catch(const std::exception&){rejected=true;}check(rejected,"invalid override ID rejected");
    }
    auto one=shape(1);one.keyframes.rotation={{0,0},{20,20}};one.keyframes.opacity.reserve(maximum_document_keyframes-2);
    for(std::size_t n=0;n<maximum_document_keyframes-2;++n)one.keyframes.opacity.push_back({static_cast<std::int64_t>(n),1});
    CompositionDocument full(400,200,{30,1},{std::move(one)});
    check(!full.setLayerRotationAtFrame(1,40,10)&&full.layers()[0].keyframes.rotation.size()==2,"key budget rejects insertion atomically");
    check(full.setLayerRotationAtFrame(1,30,20),"full budget still permits editing existing key");
    auto full_canvas=shape(7);auto& background=std::get<ShapeLayerContent>(full_canvas.content);background.width=400;background.height=200;
    CompositionDocument uniform(400,200,{30,1},{full_canvas});
    check(uniform.setLayerRotationAtFrame(7,720,0),"uniform full-turn edit");
    check(importComposition(exportComposition(project(uniform))).layers[0].transform.rotation_degrees==720,"interchange retains representable full turns");
    bool rejected=false;try{(void)exportComposition(project(CompositionDocument(400,200,{30,1},{control(7)})));}catch(const std::exception&){rejected=true;}
    check(rejected,"existing Null interchange refusal retained");
    auto huge=control(9);huge.transform={0,0,1e300,0,1};huge.scale_y=ScaleChannel{1,{}};
    auto source=control(1);source.parent_id=9;source.transform.position_x=source.transform.position_y=0;
    auto descendant=shape(2);descendant.parent_id=1;descendant.transform.position_x=0;descendant.transform.position_y=1e6;
    CompositionDocument extreme(400,200,{30,1},{huge,source,descendant});(void)resolve(extreme,0);const auto unchanged=extreme.layers();
    rejected=false;try{(void)extreme.setLayerRotationAtFrame(1,90,0);}catch(const std::exception&){rejected=true;}
    check(rejected&&extreme.layers()==unchanged,"descendant geometry overflow rejects entire edit before publication");
}

void pointerGesture() {
    CompositionViewer viewer;viewer.resize(600,400);viewer.setComposition({400,200},QPointF(.5,.5));
    viewer.setRotationDirection(QPointF(0,-1));viewer.setRotationDragEnabled(true);
    int starts=0,commits=0,cancels=0,previews=0;QPointF initial,latest;double scale=0;
    viewer.setRotationDragHandlers({[&](QPointF p,double s){initial=p;scale=s;++starts;return true;},[&](QPointF p){latest=p;++previews;return true;},[&]{++commits;},[&]{++cancels;}});
    viewer.show();QApplication::processEvents();auto h=screenPivot(viewer)+QPointF(0,-56);
    mouse(viewer,QEvent::MouseButtonPress,h,Qt::RightButton,Qt::RightButton);check(!starts,"right button ignored");
    press(viewer,h+QPointF(11,0));release(viewer,h+QPointF(11,0));check(!starts,"ten logical pixel hit radius");
    press(viewer,h+QPointF(8.25,0));check(starts==1&&viewer.rotationDragActive(),"offset hit captures");
    check(near(initial.x(),toCanvas(viewer,h+QPointF(8.25,0)).x()),"fractional grab position retained");
    move(viewer,h+QPointF(9,0));check(previews==0,"platform threshold before preview");
    release(viewer,h+QPointF(30.75,6.25));check(commits==1&&near(latest.x(),toCanvas(viewer,h+QPointF(30.75,6.25)).x())&&near(scale,400/canvasRect(viewer,{400,200}).width()),"final coordinates and frozen logical mapping");
    press(viewer,h);release(viewer,h);check(commits==1&&cancels==1,"click adds no commit");
    press(viewer,h);release(viewer,{-53.25,viewer.height()+72.5});check(commits==2&&latest==toCanvas(viewer,{-53.25,viewer.height()+72.5}),"captured outside release");
    press(viewer,h);escape(viewer);check(!viewer.rotationDragActive(),"Escape cancels");
    for(auto type:{QEvent::UngrabMouse,QEvent::WindowDeactivate,QEvent::Hide,QEvent::ParentChange}) {press(viewer,h);QEvent event(type);QApplication::sendEvent(&viewer,&event);check(!viewer.rotationDragActive(),"capture/window cancellation");}
    press(viewer,h);viewer.resize(620,410);check(!viewer.rotationDragActive(),"resize cancels");
    h=screenPivot(viewer)+QPointF(0,-56);press(viewer,h);viewer.move(viewer.pos()+QPoint(7,0));check(!viewer.rotationDragActive(),"layout movement cancels");
    viewer.setComposition({400,200},QPointF(3,3));press(viewer,{500,200});check(!viewer.rotationDragActive(),"offscreen handle is not relocated");viewer.hide();
}
void rendering(const QString& root) {
    CompositionDocument null_only(400,200,{30,1},{control(1)});auto empty=request(null_only,0);empty.rotation_override=LayerRotationOverride{1,90};
    CompositionFrameRenderer empty_renderer;check(!empty_renderer.render(empty),"Null-only rotation allocates no image");
    CompositionDocument uniform(400,200,{30,1},{shape(1)});auto uniform_draft=request(uniform,0);uniform_draft.rotation_override=LayerRotationOverride{1,137.75};
    CompositionFrameRenderer uniform_renderer;const auto uniform_live=uniform_renderer.render(uniform_draft);
    check(uniform.setLayerRotationAtFrame(1,137.75,0),"uniform static edit");const auto uniform_confirmed=uniform_renderer.render(request(uniform,0));
    check(uniform_live&&uniform_confirmed&&uniform_live->rgba_pixels==uniform_confirmed->rgba_pixels,"uniform legacy rendering also consumes Rotation override");
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
        for(int stage=-2;stage<=1;++stage)check(d.createMask(2,stage,creative_suite::composition::ShapeMaskKind::Ellipse)!=0,"stage mask fixture");
        const auto make=[&](const CompositionDocument& document) {
            auto r=request(document,2);for(auto& l:r.layers) {
                l.still_frame=kind==LayerKind::Image?pixels:nullptr;
                if(kind==LayerKind::Video){l.source_path=encoding.output_path;l.source_frame_rate=30;l.source_frame_count=6;}
            }return r;
        };
        auto base=make(d);base.freezeScene();auto draft=base;draft.rotation_override=LayerRotationOverride{1,37.25};
        check(draft.scene==base.scene&&draft.layers.empty(),"draft shares heavy immutable scene");
        CompositionFrameRenderer renderer;auto& metrics=motion::diagnostics::PerformanceMetrics::instance();metrics.setEnabled(true);metrics.reset();
        check(renderer.render(base)!=nullptr,"warm sources");(void)metrics.takeSnapshotAndReset();
        const auto live=renderer.render(draft,{},false,PreviewRequestMode::Gesture);check(live!=nullptr,"live frame");
        const auto count=metrics.takeSnapshotAndReset();check(count&&count->timestamp_seek_attempts==0&&count->forward_decode_attempts==0&&
            count->timings[static_cast<std::size_t>(motion::diagnostics::PreviewTimingStage::TextShapeRasterization)].count==0,"fixed-frame drag reuses decode/raster caches");
        check(d.setLayerRotationAtFrame(1,37.25,2),"reference confirmed edit");const auto confirmed=renderer.render(make(d));
        check(confirmed&&confirmed->rgba_pixels==live->rgba_pixels,"draft/confirmed pixel parity with every source kind and stage masks");
        MotionExportSnapshot snapshot{d.canvasSize(),d.frameRate(),d.layers(),{},d.masks()};
        for(auto& l:snapshot.layers)l.duration_frames=1;
        if(kind==LayerKind::Image)snapshot.still_frames[2]=pixels;
        MotionExportSettings settings{path(root+"/rotation-"+QString::number(static_cast<int>(kind))+".mkv"),"matroska","ffv1",400,200,{30,1},10};
        std::atomic_bool cancel=false;MotionVideoExporter::exportVideo(snapshot,settings,cancel);
        auto expected_encoding=encoding;expected_encoding.output_path=path(root+"/reference-"+QString::number(static_cast<int>(kind))+".mkv");expected_encoding.width=400;expected_encoding.height=200;
        {creative_suite::media::VideoEncoder encoder(expected_encoding);encoder.writeVideo(*live,0);encoder.finish();}
        creative_suite::media::VideoDecoder decoder;
        const auto exported=decoder.decode_first_frame(settings.output_path),reference=decoder.decode_first_frame(expected_encoding.output_path);
        check(exported.rgba_pixels==reference.rgba_pixels,"export matches independently encoded preview reference");
        for(int stage=-2;stage<=1;++stage) {
            auto a=draft;a.mask_view_layer=2;a.mask_view_target=stage;auto b=make(d);b.mask_view_layer=2;b.mask_view_target=stage;
            const auto x=renderer.render(a),y=renderer.render(b);check(x&&y&&x->rgba_pixels==y->rgba_pixels,"mask View parity");
        }
    }
}
void scheduling() {
    QObject receiver;std::mutex mutex;std::condition_variable gate;bool first=false,release_first=false;
    std::atomic<int> calls=0;std::atomic<double> latest_angle=0;std::vector<PreviewFrame> frames;
    auto pixels=std::make_shared<creative_suite::media::RgbaFrame>();pixels->width=pixels->height=1;pixels->stride=4;pixels->rgba_pixels.resize(4,255);
    PreviewRenderer renderer(&receiver,{},[&](const PreviewRequest& r,const auto& cancelled) {
        if(r.gesture_session==4)throw std::runtime_error("Injected gesture rendering failure.");
        if(r.rotation_override)latest_angle=r.rotation_override->degrees;
        if(++calls==1){std::unique_lock lock(mutex);first=true;gate.notify_all();gate.wait(lock,[&]{return release_first;});check(!cancelled(),"same gesture does not starve slow in-flight work");}
        check(r.scene!=nullptr,"shared scene reaches worker");return pixels;
    },nullptr,false,nullptr,[&](PreviewFrame f){frames.push_back(std::move(f));});
    PreviewRequest r;r.gesture_session=1;r.freezeScene();(void)renderer.submit(r,PreviewRequestMode::Gesture);
    {std::unique_lock lock(mutex);check(gate.wait_for(lock,std::chrono::seconds(3),[&]{return first;}),"worker started");}
    for(int n=0;n<40;++n){r.rotation_override=LayerRotationOverride{1,double(n)};(void)renderer.submit(r,PreviewRequestMode::Gesture);}
    {std::lock_guard lock(mutex);release_first=true;}gate.notify_all();
    waitFor([&]{return calls==2&&!frames.empty()&&frames.back().generation==renderer.generation();});check(renderer.pendingDeliveryCount()<=1,"one delivery mailbox");
    check(latest_angle==39,"only latest pending rotation survives replacement");
    check(frames.back().gesture_session==1&&renderer.canPresentFrame(frames.back()),"latest session presentable");
    auto old=frames.back();renderer.recoverPresentation(true);check(!renderer.canPresentFrame(old),"fallback invalidates old delivery");
    (void)renderer.submit(r,PreviewRequestMode::Gesture);waitFor([&]{return calls==3&&frames.back().cancellation_generation!=old.cancellation_generation;});
    check(renderer.canPresentFrame(frames.back()),"same draft survives recovery");
    r.gesture_session=2;r.rotation_override.reset();r.position_override=LayerPositionOverride{1,.2,.3};(void)renderer.submit(r,PreviewRequestMode::Gesture);check(!renderer.canPresentFrame(old),"new session rejects old result");
    r.gesture_session=4;(void)renderer.submit(r,PreviewRequestMode::Gesture);
    waitFor([&]{return frames.back().gesture_session==4;});check(frames.back().render_failed,"render failure reaches controller with session identity");
    renderer.stopAndWait();
    CompositionViewer viewer;viewer.setRenderedFrame(pixels);PreviewFrame newer;newer.rgba=pixels;newer.generation=5;newer.gesture_session=3;viewer.setPreviewFrame(newer);
    auto other=std::make_shared<creative_suite::media::RgbaFrame>(*pixels);other->rgba_pixels[0]=3;newer.rgba=other;newer.generation=4;viewer.setPreviewFrame(newer);
    check(viewer.renderedFrame()==pixels,"out-of-order gesture result cannot replace newer content");
}

AffineTransform outerAt(const CompositionDocument& d,LayerId id,std::int64_t frame) {
    const auto& l=*std::find_if(d.layers().begin(),d.layers().end(),[id](const auto& item){return item.id==id;});
    const auto resolved=resolve(d,frame);auto c=creative_suite::composition::composeAffine(l.parent_id?resolved.at(l.parent_id).world:AffineTransform{},l.parent_affine.value_or(affineOffset(l.parent_offset)));
    c.tx=c.ty=0;return c;
}
QPointF rotationHandle(MainWindow& w,LayerId id=1) {
    auto* v=widget<CompositionViewer>(w,"motion-composition-viewer");auto* t=widget<TimelineNavigator>(w,"motion-timeline");
    const auto& d=*w.compositionDocument();const auto& l=*std::find_if(d.layers().begin(),d.layers().end(),[id](const auto& item){return item.id==id;});
    const auto angle=evaluateTransform(l.transform,l.keyframes,t->currentFrame()-l.timeline_start_frame).rotation_degrees;
    const auto direction=rotationGuideDirection(outerAt(d,id,t->currentFrame()),angle);
    return screenPivot(*v)+QPointF(direction[0],direction[1])*56;
}
// Rotate a pointer in the pre-own-rotation space using Qt as the independent reference.
QPointF around(MainWindow& w,QPointF initial,double delta,LayerId id=1) {
    auto* v=widget<CompositionViewer>(w,"motion-composition-viewer");const auto p=screenPivot(*v);
    const auto c=outerAt(*w.compositionDocument(),id,widget<TimelineNavigator>(w,"motion-timeline")->currentFrame());
    const QTransform outer(c.a,c.b,c.c,c.d,0,0);QTransform turn;turn.rotate(delta);
    return p+outer.map(turn.map(outer.inverted().map(initial-p)));
}
void application(const QString& root) {
    auto parent=control(9);parent.kind=LayerKind::Image;parent.source_path=path(root+"/missing.png");parent.visible=false;parent.timeline_start_frame=30;parent.duration_frames=1;parent.transform={.05,.05,1.2,30,1};parent.scale_y=ScaleChannel{.7,{}};
    auto source=control(1);source.parent_id=9;source.transform={.2,.2,1.5,-20,1};source.scale_y=ScaleChannel{.5,{}};source.anchor_point={-3,7};source.parent_affine=AffineTransform{1,.1,.2,1,0,0};
    source.keyframes.rotation={{0,-20,InterpolationMode::CubicBezier,{.2,.3,.7,.8}},{20,40}};
    auto child=shape(2);child.parent_id=1;child.transform={.15,.1,1,10,1};auto background=shape(3);background.transform.position_x=.8;background.transform.position_y=.8;
    CompositionDocument initial(400,200,{30,1},{background,child,source,parent});const auto filename=root+"/rotation.motion";Store::save(path(filename),project(initial));
    MainWindow w(nullptr,path(root+"/recovery"),"rotation-drag");w.show();open(w,filename);
    widget<QAction>(w,"motion-new-null-layer-action")->trigger();auto* timeline=widget<TimelineNavigator>(w,"motion-timeline");timeline->setCurrentFrame(10);
    auto* rows=widget<QWidget>(w,"motion-timeline-layer-rows");auto* area=widget<QScrollArea>(w,"motion-timeline-layer-scroll");
    const auto select=[&](LayerId id){const auto point=pointFor(w,id,120);showPoint(area,point);press(*rows,point);release(*rows,point);QApplication::processEvents();};select(1);
    auto* viewer=widget<CompositionViewer>(w,"motion-composition-viewer");auto* angle=widget<QDoubleSpinBox>(w,"motion-transform-rotation");
    auto* key=widget<QWidget>(w,"motion-transform-keyframe-rotation");auto* undo=widget<QAction>(w,"motion-undo-action");auto* redo=widget<QAction>(w,"motion-redo-action");
    waitFor([&]{return viewer->renderedFrame()!=nullptr;});const auto before=w.compositionDocument()->layers();
    const auto begin=[&]{const auto h=rotationHandle(w)+QPointF(6.25,-2.5);press(*viewer,h);check(viewer->rotationDragActive(),"Rotation handle captures");return h;};
    widget<QAction>(w,"motion-play-pause-action")->trigger();auto h=begin();check(!timeline->isPlaying()&&timeline->currentFrame()==10,"press pauses and fixes frame");
    const auto pivot=screenPivot(*viewer);const auto base_angle=evaluateTransform(source.transform,source.keyframes,10).rotation_degrees;
    const auto baseline=viewer->renderedFrame();move(*viewer,around(w,h,37.25));
    check(angle->isReadOnly()&&!key->isEnabled()&&!redo->isEnabled()&&w.compositionDocument()->layers()==before,"Inspector read-only with document/redo isolation");
    check(std::abs(angle->value()-(base_angle+37.25))<1e-6&&screenPivot(*viewer)==pivot,"local Rotation preview with fixed pivot");
    waitFor([&]{return viewer->renderedFrame()&&viewer->renderedFrame()!=baseline;});
    auto draft=request(*w.compositionDocument(),10);draft.rotation_override=LayerRotationOverride{1,base_angle+37.25};CompositionFrameRenderer renderer(false);const auto expected=renderer.render(draft);
    check(expected&&expected->rgba_pixels==viewer->renderedFrame()->rgba_pixels,"live rotation pixels match candidate geometry");
    auto* autosave=widget<QTimer>(w,"motion-autosave-timer");QMetaObject::invokeMethod(autosave,"timeout",Qt::DirectConnection);
    motion::persistence::MotionRecoveryStore recovery(path(root+"/recovery"),"reader");const auto snapshots=recovery.snapshotsForProject(path(filename));
    check(!snapshots.empty()&&Store::loadRecovery(snapshots.front().path).document.layers==before,"autosave stores confirmed data only");
    release(*viewer,around(w,h,63.5));const auto after=w.compositionDocument()->layers();check(after!=before&&!angle->isReadOnly(),"release commits at final coordinates");
    auto reference=CompositionDocument(400,200,{30,1},before);check(reference.setLayerRotationAtFrame(1,base_angle+63.5,10),"reference commit");
    const auto actual_world=resolve(*w.compositionDocument(),10),expected_world=resolve(reference,10);
    same(actual_world.at(1).world,expected_world.at(1).world);same(actual_world.at(2).world,expected_world.at(2).world);same(actual_world.at(3).world,resolve(initial,10).at(3).world);
    undo->trigger();check(w.compositionDocument()->layers()==before&&redo->isEnabled(),"one Undo restores original keys");
    h=begin();move(*viewer,around(w,h,30));escape(*viewer);check(w.compositionDocument()->layers()==before&&redo->isEnabled(),"Escape preserves redo");
    h=begin();move(*viewer,around(w,h,30));undo->trigger();check(w.compositionDocument()->layers()==before&&redo->isEnabled(),"Undo cancels gesture only");
    h=begin();const auto redo_combination=QKeySequence(QKeySequence::Redo)[0];
    QKeyEvent redo_key(QEvent::KeyPress,redo_combination.key(),redo_combination.keyboardModifiers());QApplication::sendEvent(viewer,&redo_key);
    check(viewer->rotationDragActive()&&w.compositionDocument()->layers()==before,"Redo cannot change document during capture");escape(*viewer);
    redo->trigger();check(w.compositionDocument()->layers()==after,"Redo restores single atomic edit");
    h=begin();release(*viewer,h);undo->trigger();check(w.compositionDocument()->layers()==before,"click adds no history");redo->trigger();
    h=begin();move(*viewer,around(w,h,35));release(*viewer,h);undo->trigger();check(w.compositionDocument()->layers()==before,"return to original adds no history");redo->trigger();
    const auto previous=angle->value();h=begin();for(int step=1;step<=48;++step)move(*viewer,around(w,h,step*15.));
    release(*viewer,around(w,h,720));check(std::abs(angle->value()-previous-720)<1e-6,"two complete turns persist without normalization");undo->trigger();check(w.compositionDocument()->layers()==after,"full turns are one Undo");
    h=begin();move(*viewer,around(w,h,25));move(*viewer,pivot);move(*viewer,pivot+QPointF(2,1));
    const auto held=angle->value();move(*viewer,pivot+QPointF(30,0));check(angle->value()==held,"neutral-zone exit has no angular jump");release(*viewer,pivot);undo->trigger();check(w.compositionDocument()->layers()==after,"neutral-zone release commits last candidate");
    h=begin();move(*viewer,around(w,h,30));timeline->setCurrentFrame(11);check(!viewer->rotationDragActive(),"seek cancels");timeline->setCurrentFrame(10);
    h=begin();move(*viewer,around(w,h,30));select(3);check(!viewer->rotationDragActive()&&w.compositionDocument()->layers()==after,"selection cancels");select(1);
    h=begin();move(*viewer,around(w,h,30));widget<QAction>(w,"motion-new-null-layer-action")->trigger();check(!viewer->rotationDragActive(),"structural edit cancels");undo->trigger();check(w.compositionDocument()->layers()==after,"only structural edit entered history");
    h=begin();move(*viewer,around(w,h,30));widget<QAction>(w,"motion-save-composition-action")->trigger();check(!viewer->rotationDragActive()&&Store::load(path(filename)).layers==after,"save cancels preview and writes v12 confirmed data");
    Store::saveRecovery(path(root+"/roundtrip.recovery"),path(filename),"rotation",project(*w.compositionDocument()));check(Store::loadRecovery(path(root+"/roundtrip.recovery")).document.layers==after,"recovery roundtrip");
    // Switch gesture kinds through the same renderer without accepting a stale session.
    const auto p=positionHandle(*viewer,{400,200});press(*viewer,p);check(viewer->positionDragActive(),"Position remains available");move(*viewer,p+QPointF(20,10));escape(*viewer);
    h=begin();move(*viewer,around(w,h,45));escape(*viewer);check(w.compositionDocument()->layers()==after,"Position/Rotation sessions remain isolated");
    for(const auto* name:{"motion-transform-position-x","motion-scale-y","motion-anchor-y"}) {
        auto* field=widget<QDoubleSpinBox>(w,name);const auto value=field->value()+.125;
        h=begin();move(*viewer,around(w,h,20));field->setValue(value);
        check(!viewer->rotationDragActive()&&w.compositionDocument()->layers()!=after&&near(field->value(),value),"other transform edit cancels draft and retains requested value");
        undo->trigger();check(w.compositionDocument()->layers()==after,"only subsequent field edit enters history");
    }
    select(2);h=rotationHandle(w,2);press(*viewer,h);move(*viewer,around(w,h,20,2));
    auto* width=widget<QSpinBox>(w,"motion-shape-width");const auto new_width=width->value()+3;width->setValue(new_width);
    check(!viewer->rotationDragActive()&&width->value()==new_width&&std::get<ShapeLayerContent>(w.compositionDocument()->layers()[1].content).width==new_width,"content edit survives draft cancellation");
    undo->trigger();check(w.compositionDocument()->layers()==after,"content edit has its own history only");select(1);
    angle->setValue(1e9-5);const auto boundary=w.compositionDocument()->layers();h=begin();move(*viewer,around(w,h,30));
    check(!viewer->rotationDragActive()&&w.compositionDocument()->layers()==boundary,"out-of-range candidate cancels without publishing");undo->trigger();check(w.compositionDocument()->layers()==after,"invalid gesture adds no history");
    h=begin();open(w,filename);check(!viewer->rotationDragActive(),"document replacement cancels");select(1);
    h=begin();closeWindow(w);check(!viewer->rotationDragActive(),"close cancels");
}

void numericFailure(const QString& root) {
    auto source=shape(1),parent=control(2);parent.transform={0,0,1e-308,0,1};source.parent_id=2;source.transform.rotation_degrees=180;
    CompositionDocument d(400,200,{30,1},{parent,source});const auto file=root+"/numeric.motion";Store::save(path(file),project(d));
    MainWindow w(nullptr,path(root+"/numeric-recovery"),"numeric");w.show();open(w,file);
    auto* rows=widget<QWidget>(w,"motion-timeline-layer-rows");press(*rows,{120,17});release(*rows,{120,17});QApplication::processEvents();
    auto* viewer=widget<CompositionViewer>(w,"motion-composition-viewer");const auto h=rotationHandle(w);press(*viewer,h);
    check(!viewer->rotationDragActive()&&w.compositionDocument()->layers()==d.layers(),"numeric overflow leaves document intact");
    QFile log(QString::fromStdU16String(creative_suite::diagnostics::Logger::instance().log_path().u16string()));check(log.open(QIODevice::ReadOnly),"read technical log");
    const auto text=log.readAll();check(text.contains("rotation_drag_begin")&&text.contains("layer_id")&&text.contains("frame"),"technical error logged with operation/layer/frame");closeWindow(w);
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setOrganizationName("CreativeSuiteTests");app.setApplicationName("MotionRotationDrag");
    QStandardPaths::setTestModeEnabled(true);QTemporaryDir root;if(!root.isValid())return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path());
    try{check(creative_suite::diagnostics::Logger::instance().initialize(path(root.path()+"/logs")),"logger");
        const auto run=[](const char* name,const std::function<void()>& test){try{test();}catch(const std::exception& e){throw std::runtime_error(std::string(name)+": "+e.what());}};
        run("angular calculation",angularCalculation);run("model geometry",modelGeometry);run("pointer gesture",pointerGesture);
        run("render/export",[&]{rendering(root.path());});run("scheduling",scheduling);
        run("application",[&]{application(root.path());});run("numeric failure",[&]{numericFailure(root.path());});
        std::cout<<"Rotation drag tests passed (DPR "<<app.devicePixelRatio()<<").\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
