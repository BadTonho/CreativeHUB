#include "model/composition_document.h"
#include "model/layer_transform_hierarchy.h"
#include "persistence/motion_document_store.h"
#include "persistence/motion_recovery_store.h"
#include "rendering/composition_frame_renderer.h"
#include "diagnostics/performance_metrics.h"
#include "ui/main_window/main_window.h"
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
void wheel(QWidget& w,QPointF p,int pixels) {
    QWheelEvent event(p,w.mapToGlobal(p),QPoint(0,pixels),QPoint(),Qt::LeftButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(&w,&event);
}
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

void eligibility() {
    auto root=control(1),a=shape(2),b=control(3),c=shape(4),other=shape(5);
    a.parent_id=1;b.parent_id=2;c.parent_id=3;other.visible=false;other.timeline_start_frame=100;
    CompositionDocument d(400,200,{30,1},{c,other,b,root,a});
    for(const auto& child:d.layers()) {
        const auto candidates=d.eligibleLayerParents(child.id);
        for(const auto& parent:d.layers())check((std::find(candidates.begin(),candidates.end(),parent.id)!=candidates.end())==
            d.canSetLayerParent(child.id,parent.id),"bulk query agrees with document validation");
    }
    check(d.eligibleLayerParents(1)==std::vector<LayerId>{5},"all descendants excluded independent of order");
    check(d.eligibleLayerParents(999).empty(),"missing origin rejected");
    std::vector<CompositionLayer> chain;
    for(LayerId i=1;i<=256;++i){auto l=control(i);l.parent_id=i-1;chain.push_back(l);}
    CompositionDocument deep(400,200,{30,1},chain);
    check(deep.eligibleLayerParents(1).empty()&&deep.eligibleLayerParents(256).size()==255,"deep iterative candidate traversal");
}

void timelineGesture() {
    std::vector<CompositionLayer> layers;for(LayerId i=1;i<=40;++i)layers.push_back(control(i));
    layers[0].name="A long parent name %2 that must be elided in the header";
    layers.back().name="Repeated %2";layers.back().parent_id=1;
    CompositionDocument d(400,200,{30,1},layers);
    TimelineNavigator timeline;timeline.resize(800,320);timeline.setCompositionTiming({30,1});timeline.setLayers(d.layers());
    int begins=0,cancels=0,commits=0,reorders=0;std::pair<LayerId,LayerId> result{};bool busy=false;
    timeline.setParentPickWhipHandlers({[&](LayerId id)->std::optional<std::vector<LayerId>>{
        if(busy)return {}; ++begins;timeline.setSelectedLayerId(id);return d.eligibleLayerParents(id);},
        [&](LayerId child,LayerId parent){++commits;result={child,parent};},[&]{++cancels;}});
    timeline.setLayerReorderHandler([&](LayerId,std::size_t){++reorders;});timeline.show();QApplication::processEvents();
    auto* rows=widget<QWidget>(timeline,"motion-timeline-layer-rows");auto* area=widget<QScrollArea>(timeline,"motion-timeline-layer-scroll");
    auto* bar=area->verticalScrollBar();auto* timer=widget<QTimer>(timeline,"motion-parent-pick-whip-scroll-timer");
    const QPointF start(60,17);
    mouse(*rows,QEvent::MouseButtonPress,start,Qt::RightButton,Qt::RightButton);check(!timeline.parentPickWhipActive(),"right button does not begin");
    busy=true;press(*rows,start);release(*rows,{200,51});check(!begins&&!commits&&!reorders,"refused gesture never reorders");busy=false;
    press(*rows,start);release(*rows,start);check(begins==1&&cancels==1&&!commits,"click only selects and cancels");
    press(*rows,start);const auto base=rows->grab().toImage();
    move(*rows,start+QPointF(1,0));check(rows->grab().toImage()==base,"movement below platform threshold has no overlay");
    move(*rows,{300,51});const auto preview=rows->grab().toImage();check(preview!=base,"candidate highlight and connection drawn");
    release(*rows,{300,85});check(commits==1&&result==std::pair<LayerId,LayerId>{40,38},"final release location resolves persistent ID");
    press(*rows,start);move(*rows,{300,17});release(*rows,{300,17});check(commits==1,"self destination rejected");
    press(*rows,start);move(*rows,{300,51});release(*rows,{rows->width()+20.0,51});check(commits==1&&!timer->isActive(),"outside viewport cancels and stops timer");
    press(*rows,start);move(*rows,{300,51});escape(*rows);check(!timeline.parentPickWhipActive()&&commits==1,"Escape cancels");
    for(auto type:{QEvent::UngrabMouse,QEvent::WindowDeactivate,QEvent::Hide}) {
        press(*rows,start);move(*rows,{300,51});QEvent event(type);QApplication::sendEvent(rows,&event);
        check(!timeline.parentPickWhipActive()&&!timer->isActive(),"capture/window lifecycle cancels");
    }
    // Header remains independent of horizontal timing, even when clips cross its left edge.
    TimelineLayerTracks tracks;tracks.resize(800,200);
    TimelineLayerRow long_clip;long_clip.id=1;long_clip.name="Clip";long_clip.duration_frames=1000;
    tracks.setRows({long_clip});tracks.setViewState(1000,0,0,100);tracks.show();QApplication::processEvents();
    auto* clip_rows=widget<QWidget>(tracks,"motion-timeline-layer-rows");const auto header=clip_rows->grab().toImage().copy(0,0,180,34);
    tracks.setViewState(1000,300,300,100);check(header==clip_rows->grab().toImage().copy(0,0,180,34),"scrolled clips never cover parenting controls");
    tracks.hide();
    QHelpEvent help(QEvent::ToolTip,QPoint(100,17),rows->mapToGlobal(QPoint(100,17)));QApplication::sendEvent(rows,&help);
    check(QToolTip::text().contains("Repeated %2 [#40]")&&QToolTip::text().contains("A long parent name %2")&&QToolTip::text().contains("[#1]"),"full literal source and parent identities in tooltip");QToolTip::hideText();
    // Fixed physical pointer, deterministic ticks: the target changes as content scrolls.
    press(*rows,start);const auto edge=rows->mapFrom(area->viewport(),QPointF(260,area->viewport()->height()-5));move(*rows,edge);
    const auto global=rows->mapToGlobal(edge);const auto old=bar->value();check(timer->isActive(),"bottom edge starts autoscroll");
    for(int n=0;n<5;++n)QMetaObject::invokeMethod(timer,"timeout",Qt::DirectConnection);
    check(bar->value()==old+40&&begins>0,"eight logical pixels per tick");
    const auto queries=begins;const auto scrolled=rows->mapFromGlobal(global);
    const auto expected=LayerId(40-static_cast<int>(scrolled.y())/34);
    release(*rows,scrolled);check(result==std::pair<LayerId,LayerId>{40,expected}&&begins==queries&&!timer->isActive(),"target recalculated after stationary-pointer scroll");
    bar->setValue(0);press(*rows,start);move(*rows,{260,70});const auto wheel_old=bar->value();wheel(*rows,{260,70},-17);
    check(bar->value()==wheel_old+17&&timeline.parentPickWhipActive(),"wheel scroll keeps gesture alive");escape(*rows);
    bar->setValue(0);press(*rows,start);move(*rows,edge);bar->setValue(bar->maximum());const auto maximum=bar->value();
    QMetaObject::invokeMethod(timer,"timeout",Qt::DirectConnection);check(bar->value()==maximum&&!timer->isActive(),"autoscroll stops at bounds");escape(*rows);
    bar->setValue(0);timeline.setLayerExpanded(40,true);timeline.setTransformGroupExpanded(40,true);QApplication::processEvents();
    const auto prior_commits=commits;press(*rows,start);move(*rows,{260,85});release(*rows,{260,85});check(commits==prior_commits,"expanded property rows cannot be parents");
    press(*rows,start);move(*rows,{260,51});release(*rows,{260,51});check(commits==prior_commits,"expanded group rows cannot be parents");
    press(*rows,start);timeline.setLayerExpanded(40,false);check(!timeline.parentPickWhipActive(),"expansion/layout changes cancel");
    press(*rows,start);timeline.setLayers({});check(!timeline.parentPickWhipActive(),"removed targets invalidate capture");
    timeline.setLayers(d.layers());QApplication::processEvents();press(*rows,start);timeline.setCurrentFrame(5);check(!timeline.parentPickWhipActive(),"seek cancels");
    press(*rows,start);timeline.setSelectedLayerId(39);check(!timeline.parentPickWhipActive(),"selection cancels");
    press(*rows,start);timeline.resize(820,330);QApplication::processEvents();check(!timeline.parentPickWhipActive(),"viewport resize cancels");
    press(*rows,start);timeline.move(timeline.pos()+QPoint(10,10));QApplication::processEvents();
    check(!timeline.parentPickWhipActive(),"moving the timeline layout cancels");
    press(*rows,{120,17});move(*rows,{120,51});release(*rows,{120,51});check(reorders==1,"parent label area preserves layer reordering");
    auto long_name=control(39),long_source=control(40);long_name.name=long_source.name=std::string(200,'N');
    const auto candidatePixels=[&](LayerId candidate) {
        long_name.id=candidate;timeline.setLayers({long_name,long_source});QApplication::processEvents();
        press(*rows,start);move(*rows,{300,51});const auto image=rows->grab().toImage();const auto ratio=image.devicePixelRatio();
        const auto label=image.copy(qRound(300*ratio),qRound(63*ratio),qRound((rows->width()-300)*ratio),qRound(24*ratio));
        escape(*rows);return label;
    };
    check(candidatePixels(39)!=candidatePixels(38),"elided duplicate candidate names retain their distinct IDs");
    timeline.hide();
}

void application(const QString& root) {
    auto source=shape(1);source.visible=false;source.transform={.4,.3,1.5,-20,1};source.scale_y=ScaleChannel{.75,{}};
    source.anchor_point={-4,3.5};source.anchor_keyframes[0]={{0,-4},{20,6}};
    source.keyframes.position_x={{0,.3,InterpolationMode::CubicBezier,{.25,.1,.25,1}},{20,.5}};
    auto parent=control(2);parent.name="Repeated %2";parent.parent_id=4;parent.transform={.05,.1,2,35,1};parent.scale_y=ScaleChannel{.5,{}};
    auto child=shape(3);child.parent_id=1;child.transform={.1,.2,.5,10,1};
    child.effects={GaussianBlurEffect{true,1},ColorAdjustmentEffect{true,8,100,100}};
    auto offline=shape(4);offline.kind=LayerKind::Image;offline.content={};offline.source_path=path(root+"/missing.png");
    offline.visible=false;offline.timeline_start_frame=40;offline.duration_frames=1;offline.transform={-.1,-.1,.7,-12,1};offline.scale_y=ScaleChannel{1.1,{}};
    CompositionDocument initial(400,200,{30,1},{source,parent,child,offline});
    check(initial.createMask(3,-2,creative_suite::composition::ShapeMaskKind::Rectangle)!=0,"source mask fixture");
    check(initial.createMask(3,-1,creative_suite::composition::ShapeMaskKind::Ellipse)!=0,"transform mask fixture");
    check(initial.createMask(3,0,creative_suite::composition::ShapeMaskKind::Ellipse)!=0,"effect mask fixture");
    const auto filename=root+"/parenting.motion";Store::save(path(filename),project(initial));
    MainWindow w(nullptr,path(root+"/recovery"),"pick-whip");w.show();open(w,filename);
    widget<QAction>(w,"motion-new-null-layer-action")->trigger();QApplication::processEvents();
    auto* timeline=widget<TimelineNavigator>(w,"motion-timeline");timeline->setCurrentFrame(10);
    auto* rows=widget<QWidget>(w,"motion-timeline-layer-rows");auto* area=widget<QScrollArea>(w,"motion-timeline-layer-scroll");
    auto* undo=widget<QAction>(w,"motion-undo-action");auto* redo=widget<QAction>(w,"motion-redo-action");
    const auto before=w.compositionDocument()->layers();const auto worlds=resolve(*w.compositionDocument(),10);
    CompositionFrameRenderer renderer(false);const auto pixels=renderer.render(request(*w.compositionDocument(),10));check(pixels!=nullptr,"reference frame");
    const auto begin=[&]{const auto p=pointFor(w,1);showPoint(area,p);press(*rows,p);check(timeline->parentPickWhipActive(),"start from hidden source icon");};
    const auto target=[&](LayerId id){const auto p=pointFor(w,id,250);showPoint(area,p);return p;};
    widget<QAction>(w,"motion-play-pause-action")->trigger();check(timeline->isPlaying(),"playback starts");begin();
    check(!timeline->isPlaying()&&timeline->currentFrame()==10,"press freezes composition frame");
    auto& metrics=motion::diagnostics::PerformanceMetrics::instance();metrics.setEnabled(true);metrics.reset();
    move(*rows,target(2));check(w.compositionDocument()->layers()==before,"hover never changes parent/curves");
    const auto counters=metrics.takeSnapshotAndReset();check(!counters||counters->requests==0,"hover/scroll issue no render request");
    auto* autosave=widget<QTimer>(w,"motion-autosave-timer");QMetaObject::invokeMethod(autosave,"timeout",Qt::DirectConnection);
    motion::persistence::MotionRecoveryStore recovery(path(root+"/recovery"),"reader");const auto backups=recovery.snapshotsForProject(path(filename));
    check(!backups.empty()&&Store::loadRecovery(backups.front().path).document.layers==before,"autosave sees confirmed hierarchy only");
    release(*rows,target(2));const auto after=w.compositionDocument()->layers();check(after[0].parent_id==2,"release changes parent");
    auto reference=CompositionDocument(400,200,{30,1},before,w.compositionDocument()->masks());
    check(reference.setLayerParent(1,2,10)&&reference.layers()==after,"same result as Inspector document operation");
    const auto resolved=resolve(*w.compositionDocument(),10);same(worlds.at(1).world,resolved.at(1).world);same(worlds.at(3).world,resolved.at(3).world);
    check(after[0].transform==before[0].transform&&after[0].keyframes==before[0].keyframes&&after[0].anchor_keyframes==before[0].anchor_keyframes,"parenting preserves local animation exactly");
    const auto rendered=renderer.render(request(*w.compositionDocument(),10));check(rendered&&rendered->rgba_pixels==pixels->rgba_pixels,"current-frame masked descendant pixels preserved");
    auto* selector=widget<QComboBox>(w,"motion-parent-layer");check(selector->currentData().toULongLong()==2,"Inspector follows confirmed parent");
    check(selector->currentText()==QStringLiteral("Repeated %2 [#2]"),"Inspector preserves literal names and persistent IDs");
    check(selector->findData(QVariant::fromValue<qulonglong>(1))<0&&selector->findData(QVariant::fromValue<qulonglong>(3))<0,
          "confirmed source stays selected and excludes its descendants");
    undo->trigger();check(w.compositionDocument()->layers()==before&&redo->isEnabled(),"one Undo for entire binding");
    begin();check(!redo->isEnabled(),"Redo unavailable during capture");redo->trigger();move(*rows,target(2));escape(*rows);
    check(w.compositionDocument()->layers()==before&&redo->isEnabled(),"Escape preserves redo branch");
    check(selector->findData(QVariant::fromValue<qulonglong>(1))<0&&selector->findData(QVariant::fromValue<qulonglong>(3))<0,
          "cancelled source stays selected");
    begin();move(*rows,target(2));undo->trigger();check(w.compositionDocument()->layers()==before&&redo->isEnabled(),"Undo cancels only the gesture");
    begin();move(*rows,target(3));release(*rows,target(3));check(w.compositionDocument()->layers()==before&&redo->isEnabled(),"descendant destination rejected without history");
    redo->trigger();check(w.compositionDocument()->layers()==after,"Redo restores binding");
    begin();release(*rows,target(2));undo->trigger();check(w.compositionDocument()->layers()==before,"same parent adds no history");redo->trigger();
    begin();timeline->setCurrentFrame(200);check(!timeline->parentPickWhipActive(),"frame context change cancels");
    const auto inactive_worlds=resolve(*w.compositionDocument(),200);begin();release(*rows,target(4));
    const auto outside=resolve(*w.compositionDocument(),200);same(inactive_worlds.at(1).world,outside.at(1).world);same(inactive_worlds.at(3).world,outside.at(3).world);
    check(w.compositionDocument()->layers()[0].parent_id==4,"offline inactive parent and inactive origin accepted");undo->trigger();
    timeline->setCurrentFrame(10);begin();move(*rows,target(4));widget<QAction>(w,"motion-save-composition-action")->trigger();
    check(!timeline->parentPickWhipActive()&&Store::load(path(filename)).layers==after,"Save cancels draft and persists confirmed v12 hierarchy");
    Store::saveRecovery(path(root+"/saved.recovery"),path(filename),"pick",project(*w.compositionDocument()));
    check(Store::loadRecovery(path(root+"/saved.recovery")).document.layers==after,"recovery round trip");
    begin();move(*rows,target(4));closeWindow(w);check(w.compositionDocument()->layers()==after,"close cancels clean-document draft");
}

void numericFailure(const QString& root) {
    auto child=shape(1),tiny=control(9);tiny.transform={0,0,1e-308,0,1};
    CompositionDocument d(400,200,{30,1},{child,tiny});const auto filename=root+"/numeric.motion";Store::save(path(filename),project(d));
    MainWindow w(nullptr,path(root+"/numeric-recovery"),"numeric-pick");w.show();open(w,filename);
    auto* rows=widget<QWidget>(w,"motion-timeline-layer-rows");auto* area=widget<QScrollArea>(w,"motion-timeline-layer-scroll");
    showPoint(area,pointFor(w,1));press(*rows,pointFor(w,1));showPoint(area,pointFor(w,9));
    QTimer::singleShot(0,[]{auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());check(box,"technical failure reported");box->accept();});
    release(*rows,pointFor(w,9,250));check(w.compositionDocument()->layers()==d.layers(),"numeric binding failure is atomic");
    QFile log(QString::fromStdU16String(creative_suite::diagnostics::Logger::instance().log_path().u16string()));check(log.open(QIODevice::ReadOnly),"read diagnostics");
    const auto text=log.readAll();check(text.contains("set_layer_parent")&&text.contains("Layer 1, parent 9, frame 0"),"failure log includes operation/source/parent/frame");
    closeWindow(w);
}
} // namespace

int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setOrganizationName("CreativeSuiteTests");app.setApplicationName("MotionParentPickWhip");
    QStandardPaths::setTestModeEnabled(true);QTemporaryDir root;if(!root.isValid())return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path());
    try {
        check(creative_suite::diagnostics::Logger::instance().initialize(path(root.path()+"/logs")),"logger");
        eligibility();timelineGesture();application(root.path());numericFailure(root.path());
        std::cout<<"Parent pick-whip tests passed (DPR "<<app.devicePixelRatio()<<").\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
