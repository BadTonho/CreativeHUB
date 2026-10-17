#include "model/layer_transform_hierarchy.h"
#include "model/composition_interchange.h"
#include "persistence/motion_document_store.h"
#include "application/history/composition_history.h"
#include "rendering/composition_frame_renderer.h"
#include "export/motion_video_export.h"
#include "diagnostics/performance_metrics.h"
#include "ui/main_window/main_window.h"
#include "ui/viewer/composition_viewer.h"
#include "ui/timeline/timeline_navigator.h"

#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/media/video_playback.h>

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace motion::model;
using namespace motion::ui;
using namespace creative_suite::animation;
using Store=motion::persistence::MotionDocumentStore;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& f){bool thrown=false;try{f();}catch(const std::exception&){thrown=true;}check(thrown,"invalid operation rejected");}
bool near(double a,double b){return std::abs(a-b)<1e-9;}
void same(const Transform2D& a,const Transform2D& b){check(near(a.position_x,b.position_x)&&near(a.position_y,b.position_y)&&near(a.scale,b.scale)&&near(a.rotation_degrees,b.rotation_degrees)&&near(a.opacity,b.opacity),"preserve resolved geometry");}
std::filesystem::path path(const QString& value){const auto b=value.toUtf8();const auto* first=reinterpret_cast<const char8_t*>(b.constData());return std::filesystem::path(std::u8string(first,first+b.size()));}
CompositionLayer shape(LayerId id){CompositionLayer l{id,LayerKind::Shape,"Layer"};l.duration_frames=60;auto c=defaultShapeLayerContent({64,32});c.width=12;c.height=8;l.content=c;return l;}
CompositionLayer nullLayer(LayerId id){CompositionLayer l{id,LayerKind::Null,"Control"};l.duration_frames=600;return l;}
MotionProjectData project(const CompositionDocument& d){MotionProjectData p;p.composition={d.canvasSize(),d.frameRate()};p.layers=d.layers();p.masks=d.masks();return p;}
auto resolve(const CompositionDocument& d,std::int64_t f){return LayerTransformHierarchy(d.canvasSize(),d.layers()).evaluate(f);}
QJsonObject read(const QString& p){QFile f(p);check(f.open(QIODevice::ReadOnly),"read JSON");return QJsonDocument::fromJson(f.readAll()).object();}
void write(const QString& p,const QJsonObject& o){QFile f(p);check(f.open(QIODevice::WriteOnly),"write JSON");const auto b=QJsonDocument(o).toJson();check(f.write(b)==b.size(),"write complete JSON");}

void geometry()
{
    auto layer=shape(1);layer.transform={.5,.5,2,90,.7};layer.anchor_point={10,5};
    CompositionDocument doc(400,200,{30,1},{layer});
    auto value=resolve(doc,0).at(1);
    check(near(value.position_x,.525)&&near(value.position_y,.4),"fractional canvas coordinates from pixel pivot");
    auto pivot=resolvedAnchorPosition(value,layer.anchor_point,doc.canvasSize());
    check(near(pivot[0],.5)&&near(pivot[1],.5),"actual world pivot differs from rendered center");
    check(doc.setLayerAnchorPoint(1,{-50,-30}),"corner pivot");
    value=resolve(doc,0).at(1);check(near(value.position_x,.35)&&near(value.position_y,1),"rotated corner known geometry");
    check(doc.setLayerAnchorPoint(1,{-100.25,300.5}),"outside fractional pivot accepted");
    const auto unchanged=doc.layers();
    check(!doc.setLayerAnchorPoint(1,{std::numeric_limits<double>::infinity(),0}),"nonfinite base rejected");
    check(!doc.setLayerAnchorKeyframe(1,AnchorProperty::X,0,std::numeric_limits<double>::quiet_NaN()),"nonfinite key rejected");
    check(doc.layers()==unchanged,"invalid edit leaves state intact");

    auto control=nullLayer(10);control.transform={0,0,1,0,1};control.anchor_point={20,0};
    auto child=shape(20);child.parent_id=10;child.anchor_point={3,4};
    auto offline=CompositionLayer{30,LayerKind::Image,"Offline"};offline.source_path="missing.png";offline.duration_frames=1;
    offline.timeline_start_frame=20;offline.visible=false;offline.transform={.1,.2,.4,33,.1};offline.anchor_point={-5.25,8.5};
    offline.anchor_keyframes[1]={{0,8.5},{10,18.5}};
    CompositionDocument hierarchy(400,200,{30,1},{child,control,offline});
    const auto before=resolve(hierarchy,5);
    check(near(before.at(20).position_x,177.0/400),"Null anchor inherited by child");
    const auto binding=hierarchy.layers()[1].parent_offset;
    CompositionHistory history;history.recordBeforeEdit({hierarchy,10});
    check(hierarchy.repositionLayerAnchorKeepingPose(10,{80,-30},5),"compensate parent pivot");
    const auto after=resolve(hierarchy,5);same(before.at(10),after.at(10));same(before.at(20),after.at(20));
    check(hierarchy.layers()[1].parent_offset==binding,"compensation does not rewrite binding");
    auto undone=history.undo({hierarchy,10});check(undone.has_value(),"undo compound edit");
    same(before.at(20),resolve(undone->document,5).at(20));
    auto redone=history.redo(*undone);check(redone && redone->document.layers()==hierarchy.layers(),"redo all anchor data");
    check(hierarchy.setLayerParent(20,0,5),"detach anchored child");same(before.at(20),resolve(hierarchy,5).at(20));
    check(hierarchy.setLayerParent(20,30,5),"reparent to anchored offline ancestor before its start");same(before.at(20),resolve(hierarchy,5).at(20));
    check(hierarchy.setLayerParent(20,10,5),"rebind anchored child");same(before.at(20),resolve(hierarchy,5).at(20));
    check(hierarchy.removeLayer(10,5),"delete anchored parent");same(before.at(20),resolve(hierarchy,5).at(20));

    auto animated=shape(1);animated.transform={.2,.4,2,90,1};
    animated.keyframes.position_x={{0,.1},{20,.3}};animated.keyframes.position_y={{0,.3},{20,.5}};
    animated.anchor_keyframes[0]={{0,0},{20,20}};
    CompositionDocument anim(400,200,{30,1},{animated});const auto original=resolve(anim,10).at(1);
    check(anim.repositionLayerAnchorKeepingPose(1,{30,-4},10),"insert compensation keys between existing keys");
    same(original,resolve(anim,10).at(1));const auto& edited=anim.layers()[0];
    check(edited.keyframes.position_x.size()==3 && edited.keyframes.position_y.size()==3 && edited.anchor_keyframes[0].size()==3,"affected animated tracks get keys");
    check(near(edited.keyframes.position_x[1].value,.22)&&near(edited.keyframes.position_y[1].value,.6),"rotation and scale compensation in local canvas units");
    check(edited.keyframes.position_x.front()==animated.keyframes.position_x.front() && edited.keyframes.position_x.back()==animated.keyframes.position_x.back(),"other position keys unchanged");
    check(edited.anchor_point.y_pixels==-4 && edited.transform==animated.transform,"static axes change bases, animated axes keep bases");
    const auto compound=anim.layers();check(!anim.repositionLayerAnchorKeepingPose(1,{30,-4},10),"unchanged operation skipped");
    check(!anim.repositionLayerAnchorKeepingPose(1,{0,0},100),"compensation outside layer disabled");
    check(anim.layers()==compound,"skipped actions preserve state");
    check(!anim.moveLayerAnchorKeyframe(1,AnchorProperty::X,10,20),"key collision rejected");
    check(anim.moveLayerAnchorKeyframe(1,AnchorProperty::X,10,12),"move key");
    check(anim.setLayerAnchorKeyframeInterpolation(1,AnchorProperty::X,0,InterpolationMode::CubicBezier,{.42,0,1,1}),"anchor Bezier editing");
    check(evaluateLayerAnchor(anim.layers()[0],6).x_pixels<15,"Bezier evaluated");
    check(anim.removeLayerAnchorKeyframe(1,AnchorProperty::X,12),"remove key");
    check(!anim.replaceLayerAnchorKeyframes(1,AnchorProperty::Y,{{1,0},{1,2}}),"duplicate scalar frames rejected");

    auto bad=shape(8);bad.transform.scale=1e308;
    CompositionDocument limits(64,32,{30,1},{bad});const auto kept=limits.layers();
    rejects([&]{(void)limits.repositionLayerAnchorKeepingPose(8,{1e308,0},0);});
    check(limits.layers()==kept,"overflow compensation atomic");
    rejects([&]{(void)limits.replaceLayerAnchorKeyframes(8,AnchorProperty::X,{{0,1e308}});});
    check(limits.layers()==kept,"overflow curve replacement atomic");
    CompositionDocument cached(64,32,{30,1},{shape(1)});
    const LayerTransformHierarchy frozen(cached.canvasSize(),cached.layers());const auto frozen_pose=frozen.evaluate(0).at(1);
    check(cached.setLayerAnchorPoint(1,{1.25,2.5}) && !frozen.matches(cached.canvasSize(),cached.layers()),"base Anchor invalidates hierarchy snapshot");
    const LayerTransformHierarchy with_base(cached.canvasSize(),cached.layers());
    check(cached.setLayerAnchorKeyframe(1,AnchorProperty::Y,0,6) && !with_base.matches(cached.canvasSize(),cached.layers()),"Anchor curve invalidates hierarchy snapshot");
    same(frozen_pose,frozen.evaluate(0).at(1));
}

void persistence(const QString& root)
{
    auto control=nullLayer(2);control.anchor_point={5,-3};
    auto layer=shape(1);layer.parent_id=2;layer.anchor_point={-2.5,9.25};
    layer.anchor_keyframes[0]={{0,-2.5,InterpolationMode::CubicBezier,{.42,0,1,1}},{20,30}};
    CompositionDocument doc(64,32,{30,1},{layer,control});auto p=project(doc);
    const auto file=root+"/anchor.motion";Store::save(path(file),p);
    check(Store::load(path(file))==p && read(file).value("version").toInt()==11,"v11 full round trip");
    Store::saveRecovery(path(root+"/recovery"),path(file),"anchor",p);
    check(Store::loadRecovery(path(root+"/recovery")).document==p,"recovery wrapper preserves anchor");
    auto encoded=read(file);auto layers=encoded.value("layers").toArray();auto first=layers[0].toObject();
    auto anchor=first.value("anchor_point").toObject();anchor.insert("unknown",1);first.insert("anchor_point",anchor);layers[0]=first;encoded.insert("layers",layers);
    write(root+"/invalid.motion",encoded);rejects([&]{(void)Store::load(path(root+"/invalid.motion"));});
    check(read(root+"/invalid.motion")==encoded,"invalid source not rewritten");
    for(const auto bad:{QJsonValue("invalid"),QJsonValue(QJsonValue::Null),QJsonValue(QJsonObject{{"x_pixels",1}})}) {
        auto invalid=read(file);auto ls=invalid.value("layers").toArray();auto item=ls[0].toObject();item.insert("anchor_point",bad);ls[0]=item;invalid.insert("layers",ls);
        write(root+"/invalid.motion",invalid);rejects([&]{(void)Store::load(path(root+"/invalid.motion"));});
    }
    encoded=read(file);encoded.insert("version",10);write(root+"/old-anchor.motion",encoded);
    rejects([&]{(void)Store::load(path(root+"/old-anchor.motion"));});
    auto legacy=project(CompositionDocument(64,32,{30,1},{shape(1)}));Store::save(path(root+"/old.motion"),legacy);
    const auto neutral=read(root+"/old.motion");
    check(!neutral.value("layers").toArray()[0].toObject().contains("anchor_point"),"identity anchor omitted");
    for(int v=1;v<=10;++v){auto old=neutral;old.insert("version",v);write(root+"/old.motion",old);const auto loaded=Store::load(path(root+"/old.motion"));
        check(loaded.layers[0].anchor_point==AnchorPoint{} && loaded.layers[0].anchor_keyframes==AnchorKeyframes{},"v1-v10 identity migration");}
    auto background=defaultShapeLayerContent({64,32});background.width=64;background.height=32;background.stroke_width_pixels=0;
    legacy.layers[0].content=background;
    p=legacy;p.layers[0].anchor_point={1,0};rejects([&]{(void)exportComposition(p);});
    p=legacy;p.layers[0].anchor_keyframes[0]={{0,0}};rejects([&]{(void)exportComposition(p);});
    check(exportComposition(legacy).layers.size()==1,"neutral anchor interchange remains supported");
}

creative_suite::media::RgbaFramePtr pixels()
{
    auto f=std::make_shared<creative_suite::media::RgbaFrame>();f->width=4;f->height=2;f->stride=16;
    for(int i=0;i<8;++i){f->rgba_pixels.push_back(i%2?10:200);f->rgba_pixels.push_back(80);f->rgba_pixels.push_back(30);f->rgba_pixels.push_back(255);}return f;
}
PreviewRequest request(const CompositionDocument& d,std::int64_t frame,bool hierarchy=true)
{
    PreviewRequest r;r.canvas_size=d.canvasSize();r.frame_rate=d.frameRate();r.composition_frame=frame;r.black_canvas_when_empty=true;
    r.masks=std::make_shared<const std::vector<Mask>>(d.masks());if(hierarchy)r.hierarchy=std::make_shared<const LayerTransformHierarchy>(d.canvasSize(),d.layers());
    for(const auto& l:d.layers())if(l.kind!=LayerKind::Null && l.visible && frame>=l.timeline_start_frame && frame-l.timeline_start_frame<l.duration_frames){PreviewLayerSnapshot s;s.id=l.id;s.kind=l.kind;s.local_frame=frame-l.timeline_start_frame;
        s.transform=l.transform;s.keyframes=l.keyframes;s.content=l.content;s.effects=l.effects;s.source_mask=l.source_mask;s.transform_mask=l.transform_mask;
        s.source_path=l.source_path;s.source_frame_count=l.source_frame_count;s.source_start_frame=l.source_start_frame;s.source_frame_rate=l.source_frame_rate;s.presentation=l.presentation;
        s.anchor_point=l.anchor_point;s.anchor_keyframes=l.anchor_keyframes;s.still_frame=l.kind==LayerKind::Image?pixels():nullptr;r.layers.push_back(s);}return r;
}
void rendering(const QString& root)
{
    CompositionFrameRenderer renderer(false);
    for(const auto kind:{LayerKind::Shape,LayerKind::Image,LayerKind::Text}){
        auto l=shape(1);l.kind=kind;l.anchor_point={3,-2};l.transform.scale=2;l.transform.rotation_degrees=90;
        if(kind==LayerKind::Image){l.content={};l.source_path="virtual.png";}
        if(kind==LayerKind::Text){auto t=defaultTextLayerContent({64,32});t.text="Anchor";t.box_width=24;t.box_height=12;t.font_size_pixels=8;l.content=t;}
        CompositionDocument doc(64,32,{30,1},{l});auto expected=request(doc,0,false);expected.layers[0].anchor_point={};expected.layers[0].transform.position_x=28.0/64;expected.layers[0].transform.position_y=10.0/32;
        const auto a=renderer.render(request(doc,0)),b=renderer.render(expected);check(a&&b&&a->rgba_pixels==b->rgba_pixels,"source-fit and intrinsic pivot match explicit center");
        auto fallback=request(doc,0,false);const auto c=renderer.render(fallback);check(c&&c->rgba_pixels==a->rgba_pixels,"direct snapshot anchor path agrees");
    }
    auto l=shape(1);l.duration_frames=6;l.anchor_point={0,2};l.anchor_keyframes[0]={{0,0},{5,10}};l.parent_id=2;
    l.effects={GaussianBlurEffect{true,1},ColorAdjustmentEffect{true,10}};
    auto control=nullLayer(2);control.visible=false;control.transform.position_x=0;control.transform.position_y=0;control.anchor_point={2,0};
    CompositionDocument doc(64,32,{30,1},{l,control});check(doc.createMask(1,-1,creative_suite::composition::ShapeMaskKind::Ellipse)!=0,"transform mask fixture");
    check(doc.createMask(1,-2,creative_suite::composition::ShapeMaskKind::Rectangle)!=0,"source mask fixture");
    check(doc.createMask(1,0,creative_suite::composition::ShapeMaskKind::Ellipse)!=0,"effect mask fixture");
    auto baseline=doc.layers()[0];baseline.parent_id=0;baseline.anchor_point={};baseline.anchor_keyframes={};
    baseline.transform.position_x=30.0/64;baseline.transform.position_y=14.0/32;baseline.keyframes.position_x={{0,30.0/64},{5,20.0/64}};
    CompositionDocument reference(64,32,{30,1},{baseline},doc.masks());
    for(int frame=0;frame<6;++frame){const auto a=renderer.render(request(doc,frame)),b=renderer.render(request(reference,frame));check(a&&b&&a->rgba_pixels==b->rgba_pixels,"animated masked anchor matches independent geometry");}
    MotionExportSnapshot snapshot{doc.canvasSize(),doc.frameRate(),doc.layers(),{},doc.masks()};
    MotionExportSnapshot plain{reference.canvasSize(),reference.frameRate(),reference.layers(),{},reference.masks()};
    std::atomic_bool cancel{false};MotionExportPerformanceSummary summary;
    MotionExportSettings settings{path(root+"/anchor.mkv"),"matroska","ffv1",64,32,{30,1},1};
    MotionVideoExporter::exportVideo(snapshot,settings,cancel,{},&summary);auto other=settings;other.output_path=path(root+"/reference.mkv");MotionVideoExporter::exportVideo(plain,other,cancel);
    check(summary.frames_rendered==6,"Null duration stays excluded");
    auto a=creative_suite::media::VideoPlaybackSession::open(settings.output_path),b=creative_suite::media::VideoPlaybackSession::open(other.output_path);
    int count=0;while(const auto f=a->decode_next_frame()){const auto expected=b->decode_next_frame();check(expected && (*f)->rgba_pixels==(*expected)->rgba_pixels,"export parity after identical codec conversion");++count;}check(count==6,"decoded frame count");
    auto video=CompositionLayer{9,LayerKind::Video,"Video"};video.source_path=settings.output_path;video.duration_frames=6;
    video.source_frame_count=6;video.source_duration_frames=6;video.source_frame_rate=30;video.maximum_timeline_duration_frames=6;
    video.anchor_point={3.5,-2.25};video.transform.rotation_degrees=90;
    CompositionDocument video_doc(96,48,{30,1},{video});auto vr=request(video_doc,2),explicit_video=request(video_doc,2,false);
    explicit_video.layers[0].anchor_point={};explicit_video.layers[0].transform.position_x=45.75/96;explicit_video.layers[0].transform.position_y=20.5/48;
    const auto actual_video=renderer.render(vr),reference_video=renderer.render(explicit_video);
    check(actual_video && reference_video && actual_video->rgba_pixels==reference_video->rgba_pixels,"video fitted-space anchor matches explicit geometry");
    bool visible_video=false;for(std::size_t i=0;i<actual_video->rgba_pixels.size();++i)if(i%4!=3 && actual_video->rgba_pixels[i]>20)visible_video=true;
    check(visible_video,"video geometry reference contains visible content");
    auto neutral=CompositionDocument(64,32,{30,1},{shape(1)});
    const auto old=renderer.render(request(neutral,0,false)),current=renderer.render(request(neutral,0));check(old&&current&&old->rgba_pixels==current->rgba_pixels,"zero anchor retains exact pixels");
    auto& metrics=motion::diagnostics::PerformanceMetrics::instance();metrics.setEnabled(true);metrics.reset();CompositionFrameRenderer cached(true);
    check(cached.render(request(neutral,0))!=nullptr,"warm raster cache");(void)metrics.takeSnapshotAndReset();
    check(neutral.setLayerAnchorPoint(1,{5,0}),"edit only anchor");check(cached.render(request(neutral,0))!=nullptr,"reuse raster");
    const auto sampled=metrics.takeSnapshotAndReset();
    check(sampled.has_value(),"cache counters recorded");
    check(sampled->timings[static_cast<std::size_t>(motion::diagnostics::PreviewTimingStage::TextShapeRasterization)].count==0,"anchor does not rerasterize content");metrics.setEnabled(false);
    metrics.setEnabled(true);metrics.reset();CompositionFrameRenderer cached_video(true);
    check(cached_video.render(vr)!=nullptr,"warm video decoder");(void)metrics.takeSnapshotAndReset();
    check(video_doc.setLayerAnchorPoint(9,{-6.5,4.25}),"edit video pivot");
    check(cached_video.render(request(video_doc,2))!=nullptr,"render pivot without seeking video");
    const auto video_counters=metrics.takeSnapshotAndReset();
    check(video_counters && video_counters->timestamp_seek_attempts==0 && video_counters->forward_decode_attempts==0,"Anchor-only edit reuses current decoded frame");metrics.setEnabled(false);
}

template<class T>T* widget(QObject& o,const char* name){auto* p=o.findChild<T*>(QString::fromLatin1(name));check(p!=nullptr,name);return p;}
void finished(QDoubleSpinBox* field){check(QMetaObject::invokeMethod(field,"editingFinished"),"finish inspector edit");}
void ui(const QString& root)
{
    MainWindow w(nullptr,path(root+"/ui-recovery"),"anchor-ui");w.show();
    QTimer::singleShot(0,[]{auto* d=QApplication::activeModalWidget();check(d,"composition dialog");widget<QLineEdit>(*d,"motion-canvas-width")->setText("640");widget<QLineEdit>(*d,"motion-canvas-height")->setText("360");widget<QComboBox>(*d,"motion-frame-rate")->setCurrentIndex(4);widget<QDialogButtonBox>(*d,"motion-new-composition-buttons")->button(QDialogButtonBox::Ok)->click();});
    widget<QAction>(w,"motion-new-composition-action")->trigger();widget<QAction>(w,"motion-new-rectangle-layer-action")->trigger();
    auto* timeline=w.findChild<TimelineNavigator*>();check(timeline,"timeline");
    auto* x=widget<QDoubleSpinBox>(w,"motion-anchor-x");auto* key=widget<QToolButton>(w,"motion-anchor-keyframe-x");
    x->setValue(20);finished(x);check(w.compositionDocument()->layers()[0].anchor_point.x_pixels==20 && w.compositionDocument()->layers()[0].transform.position_x==.5,"direct editing leaves position alone");
    auto* viewer=dynamic_cast<CompositionViewer*>(w.findChild<QWidget*>(QStringLiteral("motion-composition-viewer")));check(viewer && viewer->selectedLayerAnchor()==QPointF(.5,.5),"Viewer marks pivot, not shifted content center");
    widget<QAction>(w,"motion-undo-action")->trigger();check(w.compositionDocument()->layers()[0].anchor_point.x_pixels==0,"Undo direct anchor");widget<QAction>(w,"motion-redo-action")->trigger();
    key->click();timeline->setCurrentFrame(30);key->click();x->setValue(60);finished(x);timeline->setCurrentFrame(15);
    check(x->isReadOnly()&&near(x->value(),40),"interpolated anchor is read-only");
    auto* reposition=widget<QPushButton>(w,"motion-reposition-anchor");
    const auto canceled=w.compositionDocument()->layers();
    QTimer::singleShot(0,[]{auto* d=QApplication::activeModalWidget();check(d,"reposition dialog");widget<QDoubleSpinBox>(*d,"motion-reposition-anchor-x")->setValue(100);widget<QDialogButtonBox>(*d,"motion-reposition-anchor-buttons")->button(QDialogButtonBox::Cancel)->click();});reposition->click();check(w.compositionDocument()->layers()==canceled,"cancel untouched");
    widget<QAction>(w,"motion-undo-action")->trigger();check(w.compositionDocument()->layers()[0].anchor_keyframes[0].back().value==20,"cancel added no history");widget<QAction>(w,"motion-redo-action")->trigger();
    timeline->setCurrentFrame(13);const auto precise=w.compositionDocument()->layers();
    QTimer::singleShot(0,[]{auto* d=QApplication::activeModalWidget();check(d,"unchanged dialog");widget<QDialogButtonBox>(*d,"motion-reposition-anchor-buttons")->button(QDialogButtonBox::Ok)->click();});reposition->click();
    check(w.compositionDocument()->layers()==precise,"unchanged dialog preserves interpolated precision");
    widget<QAction>(w,"motion-undo-action")->trigger();check(w.compositionDocument()->layers()[0].anchor_keyframes[0].back().value==20,"unchanged confirm added no history");widget<QAction>(w,"motion-redo-action")->trigger();timeline->setCurrentFrame(15);
    const auto before=w.compositionDocument()->layers();const auto pose=resolve(*w.compositionDocument(),15).at(before[0].id);
    widget<QPushButton>(w,"motion-timeline-play-pause")->click();
    QTimer::singleShot(0,[timeline]{check(!timeline->isPlaying(),"reposition pauses playback");auto* d=QApplication::activeModalWidget();check(d,"reposition dialog");widget<QDoubleSpinBox>(*d,"motion-reposition-anchor-x")->setValue(100);widget<QDoubleSpinBox>(*d,"motion-reposition-anchor-y")->setValue(-25);widget<QDialogButtonBox>(*d,"motion-reposition-anchor-buttons")->button(QDialogButtonBox::Ok)->click();});reposition->click();
    same(pose,resolve(*w.compositionDocument(),15).at(before[0].id));const auto after=w.compositionDocument()->layers();check(after[0].anchor_keyframes[0].size()==3,"dialog inserts anchor key");
    check(viewer->selectedLayerAnchor() && near(viewer->selectedLayerAnchor()->x(),after[0].transform.position_x) && near(viewer->selectedLayerAnchor()->y(),after[0].transform.position_y),"Viewer follows repositioned pivot");
    widget<QAction>(w,"motion-undo-action")->trigger();check(w.compositionDocument()->layers()==before,"one Undo compound edit");widget<QAction>(w,"motion-redo-action")->trigger();check(w.compositionDocument()->layers()==after,"Redo compound edit");
    widget<QSlider>(w,"motion-timeline-zoom-slider")->setValue(widget<QSlider>(w,"motion-timeline-zoom-slider")->maximum());
    auto* rows=widget<QWidget>(w,"motion-timeline-layer-rows");const int from=timeline->frameToViewportX(15),to=timeline->frameToViewportX(24),y=7*34+17;
    const auto destination=timeline->frameAtViewportX(to);
    QMouseEvent press(QEvent::MouseButtonPress,QPointF(from,y),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(rows,&press);
    QMouseEvent move(QEvent::MouseMove,QPointF(to,y),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(rows,&move);
    QMouseEvent release(QEvent::MouseButtonRelease,QPointF(to,y),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(rows,&release);
    check(std::any_of(w.compositionDocument()->layers()[0].anchor_keyframes[0].begin(),w.compositionDocument()->layers()[0].anchor_keyframes[0].end(),[destination](const auto& k){return k.frame==destination;}),"timeline drags anchor marker");
    widget<QComboBox>(w,"motion-curve-editor-preset")->setCurrentIndex(1);
    check(std::any_of(w.compositionDocument()->layers()[0].anchor_keyframes[0].begin(),w.compositionDocument()->layers()[0].anchor_keyframes[0].end(),[](const auto& k){return k.interpolation==InterpolationMode::CubicBezier;}),"Graph Editor edits anchor easing");
    timeline->setCurrentFrame(151);check(!reposition->isEnabled(),"reposition disabled outside activity");
    timeline->setCurrentFrame(0);widget<QAction>(w,"motion-new-null-layer-action")->trigger();
    x->setValue(-3.125);finished(x);const auto& null=w.compositionDocument()->layers().back();
    check(null.kind==LayerKind::Null && null.anchor_point.x_pixels==-3.125,"Inspector edits Null anchor");
    check(viewer->selectedLayerAnchor()==QPointF(.5,.5),"Viewer marks the actual Null pivot without drawing layer pixels");
    key->click();check(w.compositionDocument()->layers().back().anchor_keyframes[0].size()==1,"Null anchor supports keys");
    key->click();check(w.compositionDocument()->layers().back().anchor_keyframes[0].empty(),"Inspector removes Anchor key");
    QTimer::singleShot(0,[]{auto* d=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());if(d)d->button(QMessageBox::Discard)->click();});w.close();
}
}

int main(int argc,char** argv)
{
    QApplication app(argc,argv);app.setOrganizationName("CreativeSuiteTests");app.setApplicationName("MotionAnchor");QStandardPaths::setTestModeEnabled(true);QTemporaryDir root;
    if(!root.isValid())return 1;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path());
    try{check(creative_suite::diagnostics::Logger::instance().initialize(path(root.path()+"/logs")),"logger");geometry();persistence(root.path());rendering(root.path());ui(root.path());std::cout<<"Anchor Point tests passed.\n";return 0;}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
