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
#include "model/layer_scale_drag.h"
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
#include <QAbstractButton>
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

void calculation() {
    const AffineTransform axes{1.8,.3,.7,.8,0,0};const QTransform qt(axes.a,axes.b,axes.c,axes.d,0,0);
    const std::array initial{2.,1.};const auto directions=scaleDragDirections(axes);
    for(bool linked:{false,true})for(int index=0;index<3;++index) {
        const auto control=static_cast<ScaleDragControl>(index);const auto direction=directions[index];
        const std::array lever{direction[0]*100,direction[1]*100};
        ScaleDragCalculation calc(control,linked,initial,axes,lever);
        check(calc.evaluate(0,0)==initial,"initial position has no jump");
        auto result=calc.evaluate(lever[0]*.5,lever[1]*.5);
        check(near(result[0],index==1&&!linked?2.:3.)&&near(result[1],index==0&&!linked?1.:1.5),"axis/linked/diagonal factors");
        if(index<2||linked) {
            result=calc.evaluate(-lever[1]*.75,lever[0]*.75);
            check(result==initial,"transverse movement ignored without creating tiny edits");
        }
        result=calc.evaluate(-lever[0]*3,-lever[1]*3);
        check(validDragScale(result[0])&&validDragScale(result[1]),"crossing pivot clamps to positive range");
        if(linked)check(near(result[0]/result[1],2),"joint lower bound retains ratio");
        result=calc.evaluate(lever[0]*1e10,lever[1]*1e10);
        check(validDragScale(result[0])&&validDragScale(result[1]),"upper limit clamps");
        if(linked)check(result[0]/result[1]==2,"joint upper bound retains ratio");
        check(calc.evaluate(0,0)==initial,"returning from limits has no hysteresis");
        const auto point=calc.indicator(initial);check(near(point[0],lever[0])&&near(point[1],lever[1]),"initial indicator matches reference control");
    }
    const auto diag=qt.map(QPointF(31.25,31.25));ScaleDragCalculation free(ScaleDragControl::Diagonal,false,initial,axes,{diag.x(),diag.y()});
    const auto displacement=qt.map(QPointF(15.625,-7.8125));const auto value=free.evaluate(displacement.x(),displacement.y());
    check(near(value[0],3)&&near(value[1],.75),"independent inverse-affine diagonal reference");
    const auto marker=free.indicator(value);check(near(marker[0],diag.x()+displacement.x())&&near(marker[1],diag.y()+displacement.y()),"indicator follows free affine candidate");
    ScaleDragCalculation locked(ScaleDragControl::X,true,{minimum_drag_scale,maximum_drag_scale},{},{88,0});
    check(locked.evaluate(1000,0)==std::array{minimum_drag_scale,maximum_drag_scale}&&locked.evaluate(-1000,0)==locked.evaluate(0,0),"extreme linked ratio only permits current factor");
    bool rejected=false;try{(void)free.evaluate(std::numeric_limits<double>::infinity(),0);}catch(const std::exception&){rejected=true;}check(rejected,"nonfinite pointer rejected");
    rejected=false;try{ScaleDragCalculation invalid(ScaleDragControl::X,false,initial,{1,0,0,0,0,0},{88,0});}catch(const std::exception&){rejected=true;}check(rejected,"singular axes rejected");
    rejected=false;try{ScaleDragCalculation tiny(ScaleDragControl::X,false,initial,{},{1e-300,0});(void)tiny.evaluate(1e300,0);}catch(const std::exception&){rejected=true;}check(rejected,"projection overflow is not mistaken for transverse roundoff");
    for(bool linked:{false,true})for(const auto control:{ScaleDragControl::X,ScaleDragControl::Y,ScaleDragControl::Diagonal}) {
        if(control==ScaleDragControl::Diagonal&&!linked)continue;
        const std::array lever=control==ScaleDragControl::Y?std::array{0.,112.}:std::array{88.,0.};
        ScaleDragCalculation tiny_axes(control,linked,initial,{1e-308,0,0,1e-308,0,0},lever);
        const auto candidate=tiny_axes.evaluate(lever[0]*.5,lever[1]*.5);
        check(near(candidate[0],control==ScaleDragControl::Y&&!linked?2.:3.)&&near(candidate[1],control==ScaleDragControl::X&&!linked?1.:1.5),"constrained controls do not require unused local-lever arithmetic");
    }
    for(const auto large:{AffineTransform{1e308,1e308,0,1,0,0},AffineTransform{1e308,0,1e308,1,0,0}})
        for(const auto direction:scaleDragDirections(large))check(std::isfinite(direction[0])&&std::isfinite(direction[1])&&near(std::hypot(direction[0],direction[1]),1),"finite large controls normalize without overflow");
}
void modelGeometry() {
    for(bool animated:{false,true})for(int y_mode=0;y_mode<3;++y_mode)for(auto interpolation:{InterpolationMode::Linear,InterpolationMode::CubicBezier}) {
        auto parent=control(9);parent.kind=LayerKind::Image;parent.source_path="offline.png";parent.visible=false;parent.timeline_start_frame=50;parent.duration_frames=1;
        parent.transform={.1,.1,1.8,37,1};parent.scale_y=ScaleChannel{.4,{}};
        auto source=control(1);source.parent_id=9;source.timeline_start_frame=7;source.transform={.2,.3,2,-24,1};source.anchor_point={150.25,-93};source.scale_linked=false;
        source.anchor_keyframes[0]={{0,140.25},{20,160.25}};source.parent_affine=AffineTransform{1,.2,.3,1,5,-8};
        if(animated)source.keyframes.scale={{0,1.2,interpolation,{.2,.4,.7,.9}},{20,2.8}};
        if(y_mode)source.scale_y=ScaleChannel{.7,{}};
        if(y_mode==2)source.scale_y->keyframes={{0,.5,interpolation,{.3,.2,.8,.9}},{20,.9}};
        auto child=shape(2);child.parent_id=1;child.transform={.15,.2,.5,13,1};auto other=shape(3);
        CompositionDocument d(400,200,{30,1},{child,source,parent,other});const auto before=resolve(d,17);const auto original=d.layers();
        const auto old=evaluateLayerScale(source,10);const auto draft=LayerTransformHierarchy(d.canvasSize(),d.layers()).evaluate(17,{},{},LayerScaleOverride{1,3.25,old[1]});
        auto reference=source;reference.transform.scale=3.25;reference.keyframes.scale.clear();reference.scale_y=ScaleChannel{old[1],{}};
        const QTransform binding(1,.2,.3,1,5,-8);const auto world=localQt(reference,10)*binding*localQt(parent,0);
        matchesQt(draft.at(1).world,world);matchesQt(draft.at(2).world,localQt(child,17)*world);same(before.at(3).world,draft.at(3).world);
        const auto p=before.at(1).world.map(150.25,-93),q=draft.at(1).world.map(150.25,-93);check(near(p[0],q[0])&&near(p[1],q[1]),"selected pivot stays fixed");
        check(d.layers()==original&&d.setLayerScaleAtFrame(1,3.25,old[1],17),"override isolation and atomic frame edit");
        auto edited=d.layers()[1];check(edited.scale_y.has_value(),"independent edit materializes legacy Y");
        check(edited.scale_y->keyframes==layerScaleKeys(source,ScaleAxis::Y),"unchanged Y receives no extra key or resampling");
        check(layerScaleBase(edited,ScaleAxis::Y)==layerScaleBase(source,ScaleAxis::Y),"materialized Y retains its authored base");
        if(animated)check(edited.keyframes.scale.size()==3&&edited.keyframes.scale.front()==source.keyframes.scale.front()&&edited.keyframes.scale.back()==source.keyframes.scale.back(),"between-key insertion preserves easing and neighboring keys");
        else check(edited.keyframes.scale.empty()&&edited.transform.scale==3.25,"static axis updates base");
        edited.transform.scale=source.transform.scale;edited.keyframes.scale=source.keyframes.scale;edited.scale_y=source.scale_y;
        check(edited==source,"all unrelated authored layer data retained");same(resolve(d,17).at(2).world,draft.at(2).world);
        check(!d.setLayerScaleAtFrame(1,3.25,old[1],17),"unchanged operation has no edit");
        check(d.setLayerScaleAtFrame(1,4,.95,18),"both axes can update/insert together");
        const auto stable=d.layers();for(double bad:{0.,-1.,minimum_drag_scale*.5,maximum_drag_scale*2,std::numeric_limits<double>::quiet_NaN()})check(!d.setLayerScaleAtFrame(1,bad,1,18),"invalid factors rejected");
        check(!d.setLayerScaleAtFrame(1,1,1,6)&&!d.setLayerScaleAtFrame(1,1,1,100)&&!d.setLayerScaleAtFrame(99,1,1,18)&&d.layers()==stable,"frame/ID/range rejection is atomic");
    }
    auto uniform=control(1);uniform.keyframes.scale={{0,1},{20,2}};CompositionDocument compact(400,200,{30,1},{uniform});
    check(compact.setLayerScaleAtFrame(1,3,3,10)&&!compact.layers()[0].scale_y&&compact.layers()[0].keyframes.scale.size()==3,"uniform edit retains implicit Y");
    auto explicit_y=control(1);explicit_y.scale_y=ScaleChannel{1,{}};CompositionDocument explicit_doc(400,200,{30,1},{explicit_y});
    check(explicit_doc.setLayerScaleAtFrame(1,2,2,0)&&explicit_doc.layers()[0].scale_y.has_value(),"explicit Y is retained");
    auto one=shape(1);one.keyframes.scale={{0,1},{20,2}};one.keyframes.opacity.reserve(maximum_document_keyframes-2);
    for(std::size_t n=0;n<maximum_document_keyframes-2;++n)one.keyframes.opacity.push_back({static_cast<std::int64_t>(n),1});
    CompositionDocument full(400,200,{30,1},{std::move(one)});
    check(!full.setLayerScaleAtFrame(1,2,1,0)&&!full.layers()[0].scale_y,"materialized Y keys count toward document budget");
    check(!full.setLayerScaleAtFrame(1,3,3,10)&&full.layers()[0].keyframes.scale.size()==2,"inserted keys count atomically");
    check(full.setLayerScaleAtFrame(1,3,3,20)&&!full.layers()[0].scale_y,"editing existing uniform key works at full budget");
}

void pointerGesture() {
    CompositionViewer viewer;viewer.resize(700,550);viewer.setComposition({400,200},QPointF(.5,.5));
    viewer.setScaleDirections(std::array{QPointF(1,0),QPointF(0,1),QPointF(std::sqrt(.5),std::sqrt(.5))});viewer.setScaleDragEnabled(true);
    int starts=0,commits=0,cancels=0,previews=0;ScaleDragControl active=ScaleDragControl::X;QPointF lever,delta;
    viewer.setScaleDragHandlers({[&](ScaleDragControl c,QPointF l){active=c;lever=l;++starts;return true;},[&](QPointF p){delta=p;++previews;return true;},[&]{++commits;},[&]{++cancels;}});
    viewer.show();QApplication::processEvents();const auto p=screenPivot(viewer),h=p+QPointF(88,0);
    mouse(viewer,QEvent::MouseButtonPress,h,Qt::RightButton,Qt::RightButton);check(!starts,"right button ignored");
    press(viewer,h+QPointF(11,0));release(viewer,h+QPointF(11,0));check(!starts,"ten-logical-pixel hit radius");
    press(viewer,h+QPointF(8.25,0));check(starts==1&&viewer.scaleDragActive()&&active==ScaleDragControl::X,"X offset hit");
    check(near(lever.x(),88*400/canvasRect(viewer,{400,200}).width()),"fixed handle lever mapped to canvas");
    const QPointF indicator(40,-50);viewer.setScaleIndicator(indicator);
    const auto fit=canvasRect(viewer,{400,200});const auto indicator_pixel=p+QPointF(indicator.x()*fit.width()/400,indicator.y()*fit.height()/200);
    const auto indicator_position=(indicator_pixel*viewer.devicePixelRatioF()).toPoint();
    const auto indicator_color=viewer.grab().toImage().pixelColor(indicator_position);
    check(indicator_color.green()>210&&indicator_color.red()<130,"active effective indicator is drawn");
    move(viewer,h+QPointF(9,0));check(!previews,"platform threshold");release(viewer,h+QPointF(30.75,6.25));
    check(commits==1&&near(delta.x(),22.5*400/canvasRect(viewer,{400,200}).width()),"grab offset and final fractional coordinates");
    check(viewer.grab().toImage().pixelColor(indicator_position)!=indicator_color,"transient indicator clears at end");
    for(int index=1;index<3;++index){const auto point=p+(index==1?QPointF(0,112):QPointF(std::sqrt(.5),std::sqrt(.5))*136);press(viewer,point);check(active==static_cast<ScaleDragControl>(index),"Y/XY identified independently");release(viewer,point);}
    press(viewer,h);release(viewer,{-53.25,viewer.height()+72.5});check(commits==2,"capture permits outside release");
    press(viewer,h);escape(viewer);check(!viewer.scaleDragActive(),"Escape cancels");
    viewer.setScaleDirections(std::array{QPointF(1,0),QPointF(1,.001),QPointF(1,.0005)});
    for(int index=0;index<3;++index){const auto d=QPointF(1,index==1?.001:index==2?.0005:0);const auto radius=index==0?88.:index==1?112.:136.;
        const auto point=p+d/std::hypot(d.x(),d.y())*radius;press(viewer,point);check(active==static_cast<ScaleDragControl>(index),"different radii preserve hit separation under near-parallel projection");escape(viewer);}
    for(auto type:{QEvent::UngrabMouse,QEvent::WindowDeactivate,QEvent::Hide,QEvent::ParentChange}){press(viewer,h);QEvent event(type);QApplication::sendEvent(&viewer,&event);check(!viewer.scaleDragActive(),"lifecycle cancellation");}
    press(viewer,h);viewer.resize(710,560);check(!viewer.scaleDragActive(),"resize cancels");
    press(viewer,screenPivot(viewer)+QPointF(88,0));viewer.move(viewer.pos()+QPoint(7,0));check(!viewer.scaleDragActive(),"layout movement cancels");
    viewer.setComposition({400,200},QPointF(3,3));press(viewer,{500,200});check(!viewer.scaleDragActive(),"offscreen controls are not relocated");viewer.hide();
}
void rendering(const QString& root) {
    CompositionDocument null_only(400,200,{30,1},{control(1)});auto empty=request(null_only,0);empty.scale_override=LayerScaleOverride{1,1.5,.5};
    CompositionFrameRenderer empty_renderer;check(!empty_renderer.render(empty),"Null-only Scale override allocates no image");
    CompositionDocument uniform(400,200,{30,1},{shape(1)});auto uniform_draft=request(uniform,0);uniform_draft.scale_override=LayerScaleOverride{1,1.375,1.375};
    CompositionFrameRenderer uniform_renderer;const auto uniform_live=uniform_renderer.render(uniform_draft);
    check(uniform.setLayerScaleAtFrame(1,1.375,1.375,0),"uniform static edit");const auto uniform_confirmed=uniform_renderer.render(request(uniform,0));
    check(uniform_live&&uniform_confirmed&&uniform_live->rgba_pixels==uniform_confirmed->rgba_pixels,"uniform legacy rendering also consumes Scale override");
    auto independent=request(uniform,0);independent.scale_override=LayerScaleOverride{1,2,.5};const auto independent_live=uniform_renderer.render(independent);
    check(uniform.setLayerScaleAtFrame(1,2,.5,0),"materialize independent render reference");const auto independent_confirmed=uniform_renderer.render(request(uniform,0));
    check(independent_live&&independent_confirmed&&independent_live->rgba_pixels==independent_confirmed->rgba_pixels&&independent_live->rgba_pixels!=uniform_live->rgba_pixels,"implicit Y override enters affine rendering without masked-empty false positives");
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
        auto base=make(d);base.freezeScene();auto draft=base;draft.scale_override=LayerScaleOverride{1,1.25,.7};
        check(draft.scene==base.scene&&draft.layers.empty(),"draft shares heavy immutable scene");
        CompositionFrameRenderer renderer;auto& metrics=motion::diagnostics::PerformanceMetrics::instance();metrics.setEnabled(true);metrics.reset();
        check(renderer.render(base)!=nullptr,"warm sources");(void)metrics.takeSnapshotAndReset();
        const auto live=renderer.render(draft,{},false,PreviewRequestMode::Gesture);check(live!=nullptr,"live frame");
        const auto count=metrics.takeSnapshotAndReset();check(count&&count->timestamp_seek_attempts==0&&count->forward_decode_attempts==0&&
            count->timings[static_cast<std::size_t>(motion::diagnostics::PreviewTimingStage::TextShapeRasterization)].count==0,"fixed-frame drag reuses decode/raster caches");
        check(d.setLayerScaleAtFrame(1,1.25,.7,2),"reference confirmed edit");const auto confirmed=renderer.render(make(d));
        check(confirmed&&confirmed->rgba_pixels==live->rgba_pixels,"draft/confirmed pixel parity with every source kind and stage masks");
        MotionExportSnapshot snapshot{d.canvasSize(),d.frameRate(),d.layers(),{},d.masks()};
        for(auto& l:snapshot.layers)l.duration_frames=1;
        if(kind==LayerKind::Image)snapshot.still_frames[2]=pixels;
        MotionExportSettings settings{path(root+"/scale-"+QString::number(static_cast<int>(kind))+".mkv"),"matroska","ffv1",400,200,{30,1},10};
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
    std::atomic<int> calls=0;std::atomic<double> latest_scale=0;std::vector<PreviewFrame> frames;
    auto pixels=std::make_shared<creative_suite::media::RgbaFrame>();pixels->width=pixels->height=1;pixels->stride=4;pixels->rgba_pixels.resize(4,255);
    PreviewRenderer renderer(&receiver,{},[&](const PreviewRequest& r,const auto& cancelled) {
        if(r.gesture_session==4)throw std::runtime_error("Injected gesture rendering failure.");
        if(r.scale_override)latest_scale=r.scale_override->x;
        if(++calls==1){std::unique_lock lock(mutex);first=true;gate.notify_all();gate.wait(lock,[&]{return release_first;});check(!cancelled(),"same gesture does not starve slow in-flight work");}
        check(r.scene!=nullptr,"shared scene reaches worker");return pixels;
    },nullptr,false,nullptr,[&](PreviewFrame f){frames.push_back(std::move(f));});
    PreviewRequest r;r.gesture_session=1;r.freezeScene();(void)renderer.submit(r,PreviewRequestMode::Gesture);
    {std::unique_lock lock(mutex);check(gate.wait_for(lock,std::chrono::seconds(3),[&]{return first;}),"worker started");}
    for(int n=0;n<40;++n){r.scale_override=LayerScaleOverride{1,1+n*.01,.5};(void)renderer.submit(r,PreviewRequestMode::Gesture);}
    {std::lock_guard lock(mutex);release_first=true;}gate.notify_all();
    waitFor([&]{return calls==2&&!frames.empty()&&frames.back().generation==renderer.generation();});check(renderer.pendingDeliveryCount()<=1,"one delivery mailbox");
    check(near(latest_scale,1.39),"only latest pending scale survives replacement");
    check(frames.back().gesture_session==1&&renderer.canPresentFrame(frames.back()),"latest session presentable");
    auto old=frames.back();renderer.recoverPresentation(true);check(!renderer.canPresentFrame(old),"fallback invalidates old delivery");
    (void)renderer.submit(r,PreviewRequestMode::Gesture);waitFor([&]{return calls==3&&frames.back().cancellation_generation!=old.cancellation_generation;});
    check(renderer.canPresentFrame(frames.back()),"same draft survives recovery");
    r.gesture_session=2;r.scale_override.reset();r.position_override=LayerPositionOverride{1,.2,.3};(void)renderer.submit(r,PreviewRequestMode::Gesture);check(!renderer.canPresentFrame(old),"new session rejects old result");
    r.gesture_session=3;r.position_override.reset();r.rotation_override=LayerRotationOverride{1,45};(void)renderer.submit(r,PreviewRequestMode::Gesture);
    check(!renderer.canPresentFrame(old),"Rotation session rejects Scale result");
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

AffineTransform axesAt(MainWindow& w,LayerId id) {
    const auto& d=*w.compositionDocument();const auto frame=widget<TimelineNavigator>(w,"motion-timeline")->currentFrame();
    const auto& l=*std::find_if(d.layers().begin(),d.layers().end(),[id](const auto& item){return item.id==id;});
    const auto angle=evaluateTransform(l.transform,l.keyframes,frame-l.timeline_start_frame).rotation_degrees;
    return creative_suite::composition::composeAffine(outerAt(d,id,frame),affineOffset({0,0,1,angle}));
}
QPointF scaleHandle(MainWindow& w,ScaleDragControl control=ScaleDragControl::X,LayerId id=1) {
    auto* v=widget<CompositionViewer>(w,"motion-composition-viewer");const auto dirs=scaleDragDirections(axesAt(w,id));const auto index=static_cast<std::size_t>(control);
    constexpr std::array radii{88.,112.,136.};return screenPivot(*v)+QPointF(dirs[index][0],dirs[index][1])*radii[index];
}
void application(const QString& root) {
    auto parent=control(9);parent.kind=LayerKind::Image;parent.source_path=path(root+"/missing.png");parent.visible=false;parent.timeline_start_frame=30;parent.duration_frames=1;parent.transform={.05,.05,1.2,30,1};parent.scale_y=ScaleChannel{.7,{}};
    auto source=control(1);source.parent_id=9;source.transform={.2,.2,2,-24,1};source.scale_y=ScaleChannel{.7,{}};source.anchor_point={-3,7};source.parent_affine=AffineTransform{1,.1,.2,1,0,0};
    source.keyframes.scale={{0,1.2,InterpolationMode::CubicBezier,{.2,.3,.7,.8}},{20,2.8}};
    auto child=shape(2);child.parent_id=1;child.transform={.15,.1,1,10,1};auto background=shape(3);background.transform.position_x=.8;background.transform.position_y=.8;
    CompositionDocument initial(400,200,{30,1},{background,child,source,parent});const auto filename=root+"/scale.motion";Store::save(path(filename),project(initial));
    MainWindow w(nullptr,path(root+"/recovery"),"scale-drag");w.resize(1500,1000);w.show();open(w,filename);
    widget<QAction>(w,"motion-new-null-layer-action")->trigger();auto* timeline=widget<TimelineNavigator>(w,"motion-timeline");timeline->setCurrentFrame(10);
    auto* rows=widget<QWidget>(w,"motion-timeline-layer-rows");auto* area=widget<QScrollArea>(w,"motion-timeline-layer-scroll");
    const auto select=[&](LayerId id){const auto point=pointFor(w,id,120);showPoint(area,point);press(*rows,point);release(*rows,point);QApplication::processEvents();};select(1);
    auto* viewer=widget<CompositionViewer>(w,"motion-composition-viewer");auto* x=widget<QDoubleSpinBox>(w,"motion-transform-scale");auto* y=widget<QDoubleSpinBox>(w,"motion-scale-y");
    auto* key=widget<QWidget>(w,"motion-transform-keyframe-scale");auto* ykey=widget<QWidget>(w,"motion-scale-keyframe-y");
    auto* link=widget<QAbstractButton>(w,"motion-scale-link");auto* undo=widget<QAction>(w,"motion-undo-action");auto* redo=widget<QAction>(w,"motion-redo-action");
    waitFor([&]{return viewer->renderedFrame()!=nullptr;});const auto before=w.compositionDocument()->layers();
    const auto begin=[&](ScaleDragControl c=ScaleDragControl::X){const auto h=scaleHandle(w,c)+QPointF(6.25,-2.5);press(*viewer,h);check(viewer->scaleDragActive(),"Scale control captures");return h;};
    const auto initial_values=evaluateLayerScale(source,10);const auto pivot=screenPivot(*viewer);const auto lever=scaleHandle(w)-pivot;
    widget<QAction>(w,"motion-play-pause-action")->trigger();auto h=begin();check(!timeline->isPlaying()&&timeline->currentFrame()==10,"press pauses/fixes frame");
    const auto baseline=viewer->renderedFrame();move(*viewer,h+lever*.25);
    check(x->isReadOnly()&&y->isReadOnly()&&!key->isEnabled()&&!ykey->isEnabled()&&!redo->isEnabled()&&w.compositionDocument()->layers()==before,"transient read-only fields and document isolation");
    check(std::abs(x->value()-initial_values[0]*125)<1e-6&&std::abs(y->value()-initial_values[1]*125)<1e-6&&screenPivot(*viewer)==pivot,"linked local scale preview and fixed pivot");
    waitFor([&]{return viewer->renderedFrame()&&viewer->renderedFrame()!=baseline;});
    auto draft=request(*w.compositionDocument(),10);draft.scale_override=LayerScaleOverride{1,initial_values[0]*1.25,initial_values[1]*1.25};CompositionFrameRenderer renderer(false);const auto expected=renderer.render(draft);
    check(expected&&viewer->renderedFrame()->rgba_pixels==expected->rgba_pixels,"live content matches scale candidate");
    auto* autosave=widget<QTimer>(w,"motion-autosave-timer");QMetaObject::invokeMethod(autosave,"timeout",Qt::DirectConnection);
    motion::persistence::MotionRecoveryStore recovery(path(root+"/recovery"),"reader");const auto snapshots=recovery.snapshotsForProject(path(filename));
    check(!snapshots.empty()&&Store::loadRecovery(snapshots.front().path).document.layers==before,"autosave sees confirmed scales");
    move(*viewer,h-lever*4);check(viewer->scaleDragActive()&&y->value()>=.0001&&w.compositionDocument()->layers()==before,"crossing pivot clamps and keeps gesture active");
    move(*viewer,h+lever*.25);check(std::abs(y->value()-initial_values[1]*125)<1e-6,"return from minimum is immediate");
    release(*viewer,h+lever*.6);const auto after=w.compositionDocument()->layers();check(after!=before&&!x->isReadOnly()&&!y->isReadOnly(),"final coordinate commits both axes");
    auto reference=CompositionDocument(400,200,{30,1},before);check(reference.setLayerScaleAtFrame(1,initial_values[0]*1.6,initial_values[1]*1.6,10),"reference edit");
    const auto actual=resolve(*w.compositionDocument(),10),known=resolve(reference,10);same(actual.at(1).world,known.at(1).world);same(actual.at(2).world,known.at(2).world);same(actual.at(3).world,resolve(initial,10).at(3).world);
    undo->trigger();check(w.compositionDocument()->layers()==before&&redo->isEnabled(),"single Undo");
    h=begin();move(*viewer,h+lever*.3);escape(*viewer);check(w.compositionDocument()->layers()==before&&redo->isEnabled(),"Escape preserves redo branch");
    h=begin();move(*viewer,h+lever*.3);undo->trigger();check(w.compositionDocument()->layers()==before&&redo->isEnabled(),"Undo cancels only gesture");
    h=begin();const auto combination=QKeySequence(QKeySequence::Redo)[0];QKeyEvent redo_key(QEvent::KeyPress,combination.key(),combination.keyboardModifiers());QApplication::sendEvent(viewer,&redo_key);check(viewer->scaleDragActive(),"Redo unavailable during capture");escape(*viewer);
    redo->trigger();check(w.compositionDocument()->layers()==after,"Redo restores edit");
    h=begin();release(*viewer,h);undo->trigger();check(w.compositionDocument()->layers()==before,"click adds no history");redo->trigger();
    h=begin();move(*viewer,h+lever*.3);release(*viewer,h);undo->trigger();check(w.compositionDocument()->layers()==before,"return adds no history");redo->trigger();
    h=begin();move(*viewer,h+lever*.3);link->click();check(!viewer->scaleDragActive()&&!link->isChecked(),"link toggle cancels before normal action");undo->trigger();check(w.compositionDocument()->layers()==after,"only link toggle recorded");
    link->click();const auto unlinked=w.compositionDocument()->layers();const auto start=std::array{x->value()/100,y->value()/100};
    const auto diag=scaleHandle(w,ScaleDragControl::Diagonal)-pivot;const auto a=axesAt(w,1);const QTransform axes(a.a,a.b,a.c,a.d,0,0);const auto q=axes.inverted().map(diag);
    h=begin(ScaleDragControl::Diagonal);const auto delta=axes.map(QPointF(q.x()*.3,q.y()*-.3));move(*viewer,h+delta);
    check(std::abs(x->value()-start[0]*130)<1e-6&&std::abs(y->value()-start[1]*70)<1e-6&&w.compositionDocument()->layers()==unlinked,"unlinked diagonal uses complete inverse axes");
    release(*viewer,h+delta);undo->trigger();check(w.compositionDocument()->layers()==unlinked,"free diagonal atomic history");undo->trigger();check(w.compositionDocument()->layers()==after,"restore link");
    h=begin();timeline->setCurrentFrame(11);check(!viewer->scaleDragActive(),"seek cancels");timeline->setCurrentFrame(10);
    h=begin();select(3);check(!viewer->scaleDragActive()&&w.compositionDocument()->layers()==after,"selection cancels");select(1);
    h=begin();widget<QAction>(w,"motion-new-null-layer-action")->trigger();check(!viewer->scaleDragActive(),"structural edit cancels");undo->trigger();check(w.compositionDocument()->layers()==after,"structural edit remains independent");
    h=begin();move(*viewer,h+lever*.2);widget<QAction>(w,"motion-save-composition-action")->trigger();check(!viewer->scaleDragActive()&&Store::load(path(filename)).layers==after,"save cancels and persists only confirmed v12 data");
    Store::saveRecovery(path(root+"/roundtrip.recovery"),path(filename),"scale",project(*w.compositionDocument()));check(Store::loadRecovery(path(root+"/roundtrip.recovery")).document.layers==after,"recovery roundtrip");
    h=begin();move(*viewer,h+lever*.2);auto* angle=widget<QDoubleSpinBox>(w,"motion-transform-rotation");const auto angle_value=angle->value()+8.25;angle->setValue(angle_value);check(!viewer->scaleDragActive()&&near(angle->value(),angle_value),"subsequent Inspector edit retained");undo->trigger();check(w.compositionDocument()->layers()==after,"only subsequent edit recorded");
    const auto p=positionHandle(*viewer,{400,200});press(*viewer,p);move(*viewer,p+QPointF(20,10));escape(*viewer);check(w.compositionDocument()->layers()==after,"Position remains isolated");
    const auto rdirs=rotationGuideDirection(outerAt(*w.compositionDocument(),1,10),source.transform.rotation_degrees);const auto r=pivot+QPointF(rdirs[0],rdirs[1])*56;press(*viewer,r);check(viewer->rotationDragActive(),"Rotation remains available");escape(*viewer);
    h=begin();open(w,filename);check(!viewer->scaleDragActive(),"document replacement cancels");select(1);h=begin();closeWindow(w);check(!viewer->scaleDragActive(),"close cancels");
}
void compatibility(const QString& root) {
    auto layer=shape(1);auto& content=std::get<ShapeLayerContent>(layer.content);content.width=400;content.height=200;
    CompositionDocument d(400,200,{30,1},{layer});check(d.setLayerScaleAtFrame(1,2,2,0),"uniform persisted edit");
    check(importComposition(exportComposition(project(d))).layers[0].transform.scale==2,"uniform interchange remains supported");
    check(d.setLayerScaleAtFrame(1,3,1,0),"unequal persisted edit");bool rejected=false;try{(void)exportComposition(project(d));}catch(const std::exception&){rejected=true;}check(rejected,"unequal interchange explicitly refused");
    Store::save(path(root+"/axes.motion"),project(d));check(Store::load(path(root+"/axes.motion")).layers==d.layers(),"materialized Y survives native v12 roundtrip");
    auto parent=control(9);parent.transform={0,0,1e300,0,1};parent.scale_y=ScaleChannel{1,{}};
    auto source=control(1);source.parent_id=9;source.transform.position_x=source.transform.position_y=0;
    auto child=shape(2);child.parent_id=1;child.transform.position_x=1e4;child.transform.position_y=0;
    CompositionDocument extreme(400,200,{30,1},{parent,source,child});const auto stable=extreme.layers();rejected=false;
    try{(void)extreme.setLayerScaleAtFrame(1,1e6,1,0);}catch(const std::exception&){rejected=true;}
    check(rejected&&extreme.layers()==stable,"descendant overflow rejects transaction before publication");
}
void numericFailure(const QString& root) {
    auto source=shape(1),parent=control(2);parent.transform={0,0,1e-308,0,1};source.parent_id=2;source.scale_linked=false;
    CompositionDocument d(400,200,{30,1},{parent,source});const auto file=root+"/numeric.motion";Store::save(path(file),project(d));
    MainWindow w(nullptr,path(root+"/numeric-recovery"),"numeric-scale");w.show();open(w,file);
    auto* rows=widget<QWidget>(w,"motion-timeline-layer-rows");press(*rows,{120,17});release(*rows,{120,17});QApplication::processEvents();
    auto* viewer=widget<CompositionViewer>(w,"motion-composition-viewer");press(*viewer,scaleHandle(w,ScaleDragControl::Diagonal));
    check(!viewer->scaleDragActive()&&w.compositionDocument()->layers()==d.layers(),"numeric failure leaves document unchanged");
    QFile log(QString::fromStdU16String(creative_suite::diagnostics::Logger::instance().log_path().u16string()));check(log.open(QIODevice::ReadOnly),"read technical log");
    const auto text=log.readAll();check(text.contains("scale_drag_begin")&&text.contains("layer_id")&&text.contains("frame"),"technical failure logs operation/layer/frame");closeWindow(w);
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setOrganizationName("CreativeSuiteTests");app.setApplicationName("MotionScaleDrag");
    QStandardPaths::setTestModeEnabled(true);QTemporaryDir root;if(!root.isValid())return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path());
    try{check(creative_suite::diagnostics::Logger::instance().initialize(path(root.path()+"/logs")),"logger");
        const auto run=[](const char* name,const std::function<void()>& test){try{test();}catch(const std::exception& e){throw std::runtime_error(std::string(name)+": "+e.what());}};
        run("calculation",calculation);run("model geometry",modelGeometry);run("pointer gesture",pointerGesture);
        run("render/export",[&]{rendering(root.path());});run("scheduling",scheduling);
        run("application",[&]{application(root.path());});run("compatibility",[&]{compatibility(root.path());});
        run("numeric failure",[&]{numericFailure(root.path());});
        std::cout<<"Scale drag tests passed (DPR "<<app.devicePixelRatio()<<").\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
