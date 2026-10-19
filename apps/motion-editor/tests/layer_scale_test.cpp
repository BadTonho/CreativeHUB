#include "model/layer_transform_hierarchy.h"
#include "model/composition_interchange.h"
#include "persistence/motion_document_store.h"
#include "application/history/composition_history.h"
#include "rendering/composition_frame_renderer.h"
#include "rendering/layer_content_renderer.h"
#include "rendering/layer_effect_processor.h"
#include "export/motion_video_export.h"
#include "diagnostics/performance_metrics.h"
#include "ui/main_window/main_window.h"
#include "ui/timeline/timeline_navigator.h"
#include "ui/viewer/composition_viewer.h"
#include <creative_suite/composition_exchange/raster.h>
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
#include <QPushButton>
#include <QMouseEvent>
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
#include <numbers>
#include <stdexcept>

namespace {
using namespace motion::model;
using namespace motion::ui;
using namespace creative_suite::animation;
namespace c=creative_suite::composition;
namespace media=creative_suite::media;
using Store=motion::persistence::MotionDocumentStore;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& f){bool thrown=false;try{f();}catch(const std::exception&){thrown=true;}check(thrown,"invalid input rejected");}
bool near(double a,double b){return std::abs(a-b)<1e-8;}
std::filesystem::path path(const QString& s){const auto b=s.toUtf8();const auto* p=reinterpret_cast<const char8_t*>(b.constData());return std::filesystem::path(std::u8string(p,p+b.size()));}
CompositionLayer shape(LayerId id){CompositionLayer l{id,LayerKind::Shape,"Shape"};l.duration_frames=60;auto content=defaultShapeLayerContent({64,32});content.width=12;content.height=8;content.stroke_width_pixels=0;l.content=content;return l;}
CompositionLayer control(LayerId id){CompositionLayer l{id,LayerKind::Null,"Control"};l.duration_frames=600;return l;}
auto resolve(const CompositionDocument& d,std::int64_t frame){return LayerTransformHierarchy(d.canvasSize(),d.layers()).evaluate(frame);}
void same(const AffineTransform& a,const AffineTransform& b){check(near(a.a,b.a)&&near(a.b,b.b)&&near(a.c,b.c)&&near(a.d,b.d)&&near(a.tx,b.tx)&&near(a.ty,b.ty),"complete affine geometry preserved");}
MotionProjectData project(const CompositionDocument& d){MotionProjectData p;p.composition={d.canvasSize(),d.frameRate()};p.layers=d.layers();p.masks=d.masks();return p;}
QJsonObject read(const QString& name){QFile f(name);check(f.open(QIODevice::ReadOnly),"read JSON");return QJsonDocument::fromJson(f.readAll()).object();}
void write(const QString& name,const QJsonObject& o){QFile f(name);check(f.open(QIODevice::WriteOnly),"write JSON");const auto b=QJsonDocument(o).toJson();check(f.write(b)==b.size(),"complete JSON write");}

void model()
{
    auto l=shape(1);l.transform={.5,.5,2,90,1};l.scale_y=ScaleChannel{.5,{}};l.anchor_point={10,-6};
    CompositionDocument d(400,200,{30,1},{l});const auto w=resolve(d,0).at(1);
    check(w.affine && near(w.world.a,0)&&near(w.world.b,2)&&near(w.world.c,-.5)&&near(w.world.d,0)&&near(w.world.tx,197)&&near(w.world.ty,80),"non-square known local matrix");
    const auto pivot=resolvedAnchorPosition(w,l.anchor_point,d.canvasSize());check(near(pivot[0],.5)&&near(pivot[1],.5),"affine pivot marker");
    const auto snapshot=d.layers();check(!d.setLayerScaleAxisBase(1,ScaleAxis::Y,0)&&!d.setLayerScaleAxisBase(1,ScaleAxis::X,-1),"positive scales only");
    check(!d.setLayerScaleAxisKeyframe(1,ScaleAxis::X,0,std::numeric_limits<double>::infinity()),"finite keys only");check(d.layers()==snapshot,"rejected values leave all state intact");
    check(d.editLayerScaleAxis(1,ScaleAxis::X,3,0),"linked edit");check(near(layerScaleBase(d.layers()[0],ScaleAxis::Y),.75),"linked ratio preserved");
    auto t=d.layers()[0].transform;t.rotation_degrees=23;check(d.setLayerTransform(1,t)&&d.layers()[0].scale_y->value==.75,"rotation edit retains independent scale");
    t.scale=4;check(d.setLayerTransform(1,t)&&!d.layers()[0].scale_y,"legacy Scale edit remains uniform");

    auto extreme=shape(5);extreme.transform.scale=1e6;extreme.scale_y=ScaleChannel{1e-6,{}};extreme.anchor_point={100.125,-200.5};
    CompositionDocument very_different(640,360,{30,1},{extreme});const auto narrow=resolve(very_different,0).at(5);
    check(near(narrow.world.a,1e6)&&near(narrow.world.d,1e-6),"very different positive axes retain finite inverse");
    t.position_x=.25;t.position_y=.75;t.opacity=.3;
    check(d.setLayerScaleAxisBase(1,ScaleAxis::Y,.25),"prepare independent transform audit");
    const auto audit_y=d.layers()[0].scale_y;check(d.setLayerTransform(1,t)&&d.layers()[0].scale_y==audit_y,"Position and Opacity preserve independent base");
    check(d.setLayerKeyframe(1,TransformProperty::PositionX,5,.2)&&d.setLayerKeyframe(1,TransformProperty::Rotation,5,45)&&
        d.setLayerKeyframe(1,TransformProperty::Opacity,5,.8)&&d.layers()[0].scale_y==audit_y,"other property keys preserve independent scale");

    CompositionDocument uniform_edit(640,360,{30,1},{shape(4)});
    check(uniform_edit.editLayerScaleAxis(4,ScaleAxis::X,2,0)&&!uniform_edit.layers()[0].scale_y,"linked legacy edit retains implicit Y");
    auto animated=shape(1);animated.keyframes.scale={{0,1,InterpolationMode::CubicBezier,{.42,0,1,1}},{20,3}};
    CompositionDocument animation(400,200,{30,1},{animated});const LayerTransformHierarchy frozen(animation.canvasSize(),animation.layers());
    check(animation.setLayerScaleAxisKeyframe(1,ScaleAxis::X,10,2.5),"independent X key");
    check(animation.layers()[0].scale_y->keyframes==animated.keyframes.scale,"Y materialized before X changes");
    check(!frozen.matches(animation.canvasSize(),animation.layers()),"Y and scale curves invalidate snapshot");
    check(!animation.editLayerScaleAxis(1,ScaleAxis::X,4,10),"linked edit blocked when Y has no current key");
    check(animation.setLayerScaleAxisKeyframe(1,ScaleAxis::Y,10,1.25),"independent Y key");
    check(animation.editLayerScaleAxis(1,ScaleAxis::X,5,10)&&near(evaluateLayerScale(animation.layers()[0],10)[1],2.5),"linked edit updates keyed axes together");
    const auto x_keys=animation.layers()[0].keyframes.scale;
    check(!animation.moveLayerScaleAxisKeyframe(1,ScaleAxis::Y,10,20),"axis collision rejected");
    check(animation.moveLayerScaleAxisKeyframe(1,ScaleAxis::Y,10,12),"axis key move");
    check(animation.layers()[0].keyframes.scale==x_keys,"moving Y does not move X");
    check(animation.setLayerScaleAxisKeyframeInterpolation(1,ScaleAxis::Y,0,InterpolationMode::Linear,{}),"independent interpolation");
    check(animation.removeLayerScaleAxisKeyframe(1,ScaleAxis::Y,12),"remove Y key");
    check(!animation.replaceLayerScaleAxisKeyframes(1,ScaleAxis::Y,{{0,1},{0,2}}),"duplicate frames rejected");
    check(animation.replaceLayerKeyframes(1,TransformProperty::Scale,{{0,.75},{20,1.25}})&&!animation.layers()[0].scale_y,"uniform audio-style replacement updates both axes");

    auto parent=control(10);parent.transform={0,0,2,0,1};parent.scale_y=ScaleChannel{1,{}};parent.visible=false;
    parent.keyframes.position_x={{0,0},{10,.1}};parent.keyframes.position_y={{0,0},{10,.2}};
    parent.keyframes.rotation={{0,0},{10,60}};parent.anchor_keyframes[0]={{0,0},{10,4}};
    auto child=shape(20);child.parent_id=10;child.transform.rotation_degrees=45;child.anchor_point={.25,-.5};
    auto grandchild=shape(21);grandchild.parent_id=20;grandchild.scale_y=ScaleChannel{.25,{}};
    auto offline=CompositionLayer{30,LayerKind::Image,"Offline"};offline.source_path="missing.png";offline.duration_frames=1;offline.timeline_start_frame=20;
    offline.transform={.1,.2,1.3,-23,.1};offline.scale_y=ScaleChannel{.4,{{0,.4},{10,.8}}};offline.anchor_point={4,7};
    CompositionDocument hierarchy(400,200,{30,1},{grandchild,child,offline,parent});const auto original=resolve(hierarchy,5);
    check(std::abs(original.at(20).world.a*original.at(20).world.c+original.at(20).world.b*original.at(20).world.d)>1,"rotated child inherits shear");
    check(hierarchy.repositionLayerAnchorKeepingPose(10,{10.5,-4.25},5),"compensate anisotropic parent anchor");
    same(original.at(20).world,resolve(hierarchy,5).at(20).world);same(original.at(21).world,resolve(hierarchy,5).at(21).world);
    const auto& compensated_parent=hierarchy.layers().back();
    check(compensated_parent.keyframes.position_x.size()==3&&compensated_parent.keyframes.position_y.size()==3&&
        compensated_parent.anchor_keyframes[0].size()==3&&compensated_parent.scale_linked&&
        compensated_parent.parent_offset==ParentTransformOffset{}&&!compensated_parent.parent_affine,
        "anisotropic Anchor compensation inserts current keys and preserves bindings and link");
    check(hierarchy.setLayerParent(20,30,5),"reparent to anisotropic offline parent before start");
    same(original.at(20).world,resolve(hierarchy,5).at(20).world);same(original.at(21).world,resolve(hierarchy,5).at(21).world);
    check(hierarchy.setLayerParent(20,0,5),"unlink complete affine geometry");same(original.at(21).world,resolve(hierarchy,5).at(21).world);
    check(hierarchy.setLayerParent(20,10,5),"relink complete affine geometry");
    CompositionHistory history;history.recordBeforeEdit({hierarchy,10});check(hierarchy.removeLayer(10,5),"delete affine parent");same(original.at(21).world,resolve(hierarchy,5).at(21).world);
    const auto undo=history.undo({hierarchy,20});check(undo.has_value(),"compound delete undo");const auto redo=history.redo(*undo);check(redo&&redo->document.layers()==hierarchy.layers(),"compound delete redo");
    const auto before_order=resolve(hierarchy,5);check(hierarchy.moveLayer(20,0),"reorder");same(before_order.at(21).world,resolve(hierarchy,5).at(21).world);
    auto huge=shape(8);huge.anchor_point={1e100,0};CompositionDocument limits(64,32,{30,1},{huge});const auto kept=limits.layers();
    rejects([&]{(void)limits.setLayerScaleAxisBase(8,ScaleAxis::X,1e300);});check(limits.layers()==kept,"numerical failure atomic");
}

void persistence(const QString& root)
{
    auto parent=control(2);parent.transform.position_x=0;parent.transform.position_y=0;parent.scale_y=ScaleChannel{.5,{}};
    auto l=shape(1);l.transform.rotation_degrees=33;l.scale_y=ScaleChannel{.75,{{0,.75,InterpolationMode::CubicBezier,{.42,0,1,1}},{20,1.5}}};l.scale_linked=false;
    CompositionDocument d(64,32,{30,1},{l,parent});check(d.setLayerParent(1,2,7),"persist affine binding fixture");auto p=project(d);
    const auto name=root+"/scale.motion";Store::save(path(name),p);check(read(name).value("version").toInt()==Store::current_format_version,"current-format writer");check(Store::load(path(name))==p,"scale and binding round trip");
    Store::saveRecovery(path(root+"/recovery"),path(name),"scale",p);check(Store::loadRecovery(path(root+"/recovery")).document==p,"recovery wrapper preserves scale");
    auto bad=read(name);bad.insert("version",11);write(root+"/bad.motion",bad);rejects([&]{(void)Store::load(path(root+"/bad.motion"));});
    for(const auto field:{"scale_y","parent_affine"}) {
        bad=read(name);auto layers=bad.value("layers").toArray();auto layer=layers[0].toObject();auto object=layer.value(field).toObject();object.insert("unknown",1);layer.insert(field,object);layers[0]=layer;bad.insert("layers",layers);write(root+"/bad.motion",bad);
        rejects([&]{(void)Store::load(path(root+"/bad.motion"));});check(read(root+"/bad.motion")==bad,"invalid file not rewritten");
    }
    bad=read(name);auto layers=bad.value("layers").toArray();auto item=layers[0].toObject();item.insert("parent_offset",QJsonObject{{"translation_x_pixels",0},{"translation_y_pixels",0},{"scale",1},{"rotation_degrees",0}});layers[0]=item;bad.insert("layers",layers);write(root+"/bad.motion",bad);rejects([&]{(void)Store::load(path(root+"/bad.motion"));});
    for(const auto code:{0,1,2,3,4,5}) {
        bad=read(name);auto cases=bad.value("layers").toArray();auto record=cases[0].toObject();
        if(code<=2){auto channel=record.value("scale_y").toObject();if(code<2)channel.insert("value",code==0?0:-.5);else channel.insert("keyframes",QJsonArray{QJsonObject{{"frame","0"},{"value",0}}});record.insert("scale_y",channel);}
        else if(code==3)record.insert("scale_linked",1);
        else if(code==4){auto matrix=record.value("parent_affine").toObject();matrix.insert("a",0);matrix.insert("b",0);record.insert("parent_affine",matrix);}
        else bad.insert("version",Store::current_format_version+1);
        cases[0]=record;bad.insert("layers",cases);write(root+"/bad.motion",bad);rejects([&]{(void)Store::load(path(root+"/bad.motion"));});
    }
    auto legacy=project(CompositionDocument(64,32,{30,1},{shape(1)}));Store::save(path(root+"/legacy.motion"),legacy);const auto neutral=read(root+"/legacy.motion");
    for(int v=1;v<=11;++v){auto old=neutral;old.insert("version",v);write(root+"/legacy.motion",old);const auto loaded=Store::load(path(root+"/legacy.motion"));check(!loaded.layers[0].scale_y&&loaded.layers[0].scale_linked,"legacy follows X without resampling");}
    auto background=defaultShapeLayerContent({64,32});background.width=64;background.height=32;background.stroke_width_pixels=0;legacy.layers[0].content=background;
    legacy.layers[0].scale_y=ScaleChannel{1,{}};legacy.layers[0].scale_linked=false;check(exportComposition(legacy).layers.size()==1,"equal axes export uniformly");
    legacy.layers[0].scale_y->value=.5;rejects([&]{(void)exportComposition(legacy);});legacy.layers[0].scale_y=ScaleChannel{1,{{0,1}}};rejects([&]{(void)exportComposition(legacy);});
    legacy.layers[0].keyframes.scale={{0,1}};check(exportComposition(legacy).layers.size()==1,"identical axis curves export uniformly");
}

// Independent reference math: neither the hierarchy nor the shared affine
// primitive/rasterizer is used to calculate these samples.
struct Matrix {double a,b,c,d,x,y;};
Matrix multiply(Matrix l,Matrix r){return {l.a*r.a+l.c*r.b,l.b*r.a+l.d*r.b,l.a*r.c+l.c*r.d,l.b*r.c+l.d*r.d,l.a*r.x+l.c*r.y+l.x,l.b*r.x+l.d*r.y+l.y};}
Matrix localReference(const CompositionLayer& l,CanvasSize canvas,std::int64_t frame) {
    const auto f=std::max<std::int64_t>(0,frame-l.timeline_start_frame);const auto t=evaluateTransform(l.transform,l.keyframes,f);
    const auto sy=l.scale_y?evaluateScalar(l.scale_y->value,l.scale_y->keyframes,f):t.scale;
    const double angle=t.rotation_degrees*std::numbers::pi/180,c=std::cos(angle),s=std::sin(angle);
    const double ax=evaluateScalar(l.anchor_point.x_pixels,l.anchor_keyframes[0],f),ay=evaluateScalar(l.anchor_point.y_pixels,l.anchor_keyframes[1],f);
    return {c*t.scale,s*t.scale,-s*sy,c*sy,t.position_x*canvas.width-c*t.scale*ax+s*sy*ay,t.position_y*canvas.height-s*t.scale*ax-c*sy*ay};
}
media::RgbaFramePtr pixels(){auto f=std::make_shared<media::RgbaFrame>();f->width=4;f->height=2;f->stride=16;for(int i=0;i<8;++i){f->rgba_pixels.insert(f->rgba_pixels.end(),{std::uint8_t(i%2?220:30),80,150,255});}return f;}
PreviewRequest request(const CompositionDocument& d,std::int64_t frame,bool hierarchy=true) {
    PreviewRequest r;r.canvas_size=d.canvasSize();r.frame_rate=d.frameRate();r.composition_frame=frame;r.black_canvas_when_empty=true;
    r.masks=std::make_shared<const std::vector<Mask>>(d.masks());if(hierarchy)r.hierarchy=std::make_shared<const LayerTransformHierarchy>(d.canvasSize(),d.layers());
    for(const auto& l:d.layers())if(l.kind!=LayerKind::Null&&l.visible&&frame>=l.timeline_start_frame&&frame-l.timeline_start_frame<l.duration_frames) {
        PreviewLayerSnapshot s;s.id=l.id;s.kind=l.kind;s.local_frame=frame-l.timeline_start_frame;s.transform=l.transform;s.keyframes=l.keyframes;s.scale_y=l.scale_y;
        s.anchor_point=l.anchor_point;s.anchor_keyframes=l.anchor_keyframes;s.content=l.content;s.effects=l.effects;s.source_mask=l.source_mask;s.transform_mask=l.transform_mask;
        s.source_path=l.source_path;s.source_frame_count=l.source_frame_count;s.source_frame_rate=l.source_frame_rate;s.source_start_frame=l.source_start_frame;
        if(l.kind==LayerKind::Image)s.still_frame=pixels();r.layers.push_back(s);
    }return r;
}
media::RgbaFrame reference(const CompositionLayer& layer,const std::vector<Mask>& masks,CanvasSize canvas,std::int64_t frame,Matrix world,media::RgbaFrame source) {
    c::ShapeMaskRasterCache cache;
    const auto coverage=[&](MaskId id,int w,int h){const auto it=std::find_if(masks.begin(),masks.end(),[id](const auto& m){return m.id==id;});check(it!=masks.end(),"reference mask");return cache.coverage(it->kind,c::evaluateShapeMask(it->parameters,it->keyframes,frame),w,h);};
    if(layer.source_mask)check(c::applyMask(source,*coverage(layer.source_mask,source.width,source.height)),"reference source mask");
    for(const auto& effect:layer.effects){const auto original=source;check(applyLayerEffects(source,{effect},{}),"reference effect");const auto mask=std::visit([](const auto& e){return e.mask;},effect);if(mask)check(c::maskedBlend(original,source,*coverage(mask,source.width,source.height)),"reference effect mask");}
    const bool intrinsic=layer.kind==LayerKind::Text||layer.kind==LayerKind::Shape;
    const auto fit=intrinsic?1.:std::min(double(canvas.width)/source.width,double(canvas.height)/source.height);
    world=multiply(world,{fit,0,0,fit,-source.width*.5*fit,-source.height*.5*fit});
    const auto det=world.a*world.d-world.b*world.c;
    media::RgbaFrame out;out.width=canvas.width;out.height=canvas.height;out.stride=out.width*4;out.rgba_pixels.resize(std::size_t(out.stride)*out.height,0);
    for(int y=0;y<out.height;++y)for(int x=0;x<out.width;++x){const double dx=x+.5-world.x,dy=y+.5-world.y;
        const double sx=(world.d*dx-world.c*dy)/det,sy=(-world.b*dx+world.a*dy)/det;
        if(sx<0||sy<0||sx>=source.width||sy>=source.height)continue;
        const auto* in=source.rgba_pixels.data()+std::size_t(int(std::floor(sy)))*source.stride+std::size_t(int(std::floor(sx)))*4;
        auto* dest=out.rgba_pixels.data()+std::size_t(y)*out.stride+x*4;std::copy_n(in,4,dest);dest[3]=std::uint8_t(std::lround(in[3]*evaluateTransform(layer.transform,layer.keyframes,frame-layer.timeline_start_frame).opacity));}
    if(layer.transform_mask){auto original=creative_suite::composition_exchange::transformFrame(source,canvas.width,canvas.height,{},creative_suite::composition_exchange::Profile::MotionLayer,intrinsic);check(c::maskedBlend(original,out,*coverage(layer.transform_mask,out.width,out.height)),"reference Transform mask");}
    const auto composed=c::FrameCompositor::compose(canvas.width,canvas.height,{{&out,{}}});check(composed.has_value(),"reference compose");return *composed;
}
void rendering(const QString& root) {
    CompositionFrameRenderer renderer(false);
    for(const auto kind:{LayerKind::Shape,LayerKind::Text,LayerKind::Image}) {
        auto l=shape(1);l.kind=kind;l.transform={.5,.5,1.75,27.5,.8};l.scale_y=ScaleChannel{.625,{}};l.anchor_point={3.25,-2.5};
        if(kind==LayerKind::Text){auto t=defaultTextLayerContent({64,32});t.text="Scale";t.font_size_pixels=9;t.box_width=32;t.box_height=16;l.content=t;}
        if(kind==LayerKind::Image){l.content={};l.source_path="virtual.png";}
        CompositionDocument d(64,32,{30,1},{l});const auto source=kind==LayerKind::Image?*pixels():*rasterizeLayerContent(l.content);
        const auto expected=reference(l,{},d.canvasSize(),0,localReference(l,d.canvasSize(),0),source);
        const auto actual=renderer.render(request(d,0));check(actual&&actual->rgba_pixels==expected.rgba_pixels,"fitted media/text/shape independently sampled");
        check(renderer.render(request(d,0,false))->rgba_pixels==actual->rgba_pixels,"snapshot-only scale rendering");
    }
    auto l=shape(1);l.duration_frames=4;l.parent_id=2;l.transform.rotation_degrees=35.5;l.anchor_point={1.25,-2.5};l.scale_y=ScaleChannel{.8,{{0,.8},{3,1.4}}};
    l.effects={GaussianBlurEffect{true,1},ColorAdjustmentEffect{true,8}};
    auto p=control(2);p.visible=false;p.transform={0,0,1.5,17.25,1};p.keyframes.scale={{0,1.5},{3,1.8}};p.scale_y=ScaleChannel{.75,{}};p.anchor_point={2,-1};
    CompositionDocument d(64,32,{30,1},{l,p});check(d.createMask(1,-2,c::ShapeMaskKind::Rectangle)!=0,"source mask");check(d.createMask(1,0,c::ShapeMaskKind::Ellipse)!=0,"effect mask");check(d.createMask(1,-1,c::ShapeMaskKind::Ellipse)!=0,"Transform mask");l=d.layers()[0];
    const auto expected_path=path(root+"/reference.mkv");media::VideoEncoder encoder({expected_path,"matroska","ffv1",64,32,30,1,1});
    for(int f=0;f<4;++f){const auto matrix=multiply(localReference(p,d.canvasSize(),f),localReference(l,d.canvasSize(),f));const auto expected=reference(l,d.masks(),d.canvasSize(),f,matrix,*rasterizeLayerContent(l.content));
        const auto actual=renderer.render(request(d,f));check(actual&&actual->rgba_pixels==expected.rgba_pixels,"animated shear and stage masks match independent reference");encoder.writeVideo(expected,f);}
    encoder.finish();std::atomic_bool cancel=false;MotionExportPerformanceSummary summary;
    MotionExportSettings settings{path(root+"/actual.mkv"),"matroska","ffv1",64,32,{30,1},1};MotionVideoExporter::exportVideo({d.canvasSize(),d.frameRate(),d.layers(),{},d.masks()},settings,cancel,{},&summary);
    check(summary.frames_rendered==4,"Nulls do not extend affine export");auto actual=media::VideoPlaybackSession::open(settings.output_path),expected=media::VideoPlaybackSession::open(expected_path);int count=0;
    while(const auto a=actual->decode_next_frame()){const auto b=expected->decode_next_frame();check(b&&(*a)->rgba_pixels==(*b)->rgba_pixels,"export matches independent encoded reference");++count;}check(count==4,"export frame count");
    auto neutral=shape(1);CompositionDocument legacy(64,32,{30,1},{neutral});const auto old=renderer.render(request(legacy,0));check(legacy.setLayerScaleLinked(1,false),"unlink state only");check(renderer.render(request(legacy,0))->rgba_pixels==old->rgba_pixels,"link toggle preserves exact legacy pixels");
    auto& metrics=motion::diagnostics::PerformanceMetrics::instance();metrics.setEnabled(true);metrics.reset();CompositionFrameRenderer cached;
    check(cached.render(request(legacy,0))!=nullptr,"warm content raster");(void)metrics.takeSnapshotAndReset();check(legacy.setLayerScaleAxisBase(1,ScaleAxis::Y,.7),"scale-only edit");check(cached.render(request(legacy,0))!=nullptr,"reuse content raster");const auto counters=metrics.takeSnapshotAndReset();check(counters&&counters->timings[std::size_t(motion::diagnostics::PreviewTimingStage::TextShapeRasterization)].count==0,"scale edit does not rerasterize");metrics.setEnabled(false);
    metrics.setEnabled(true);metrics.reset();
    auto video=CompositionLayer{7,LayerKind::Video,"Cached video"};video.source_path=expected_path;
    video.duration_frames=video.source_frame_count=video.maximum_timeline_duration_frames=4;video.source_frame_rate=30;
    video.transform.rotation_degrees=13.5;video.scale_y=ScaleChannel{.65,{}};
    CompositionDocument video_doc(64,32,{30,1},{video});CompositionFrameRenderer video_cached;
    check(video_cached.render(request(video_doc,0))!=nullptr,"decode affine video fixture");
    const auto warm=metrics.takeSnapshotAndReset();check(warm&&warm->timings[std::size_t(motion::diagnostics::PreviewTimingStage::Decode)].count>0,"initial video decode measured");
    check(video_doc.setLayerScaleAxisBase(7,ScaleAxis::X,1.125),"scale changes cached video");
    check(video_cached.render(request(video_doc,0))!=nullptr,"reuse decoded video at same frame");
    const auto reuse=metrics.takeSnapshotAndReset();check(reuse&&reuse->timestamp_seek_attempts==0&&reuse->forward_decode_attempts==0,"scale edit does not decode again");
    CompositionFrameRenderer seek_renderer(false);const auto playback=video_cached.render(request(video_doc,1),{},false,PreviewRequestMode::Playback),interactive=seek_renderer.render(request(video_doc,1));
    check(playback&&interactive&&playback->rgba_pixels==interactive->rgba_pixels,"affine playback and seek use the same geometry");metrics.setEnabled(false);
}
template<class T>T* widget(QObject& o,const char* name){auto* value=o.findChild<T*>(QString::fromLatin1(name));check(value,name);return value;}
void finish(QDoubleSpinBox* field){check(QMetaObject::invokeMethod(field,"editingFinished"),"finish value edit");}
void drag(QWidget* rows,int from,int to,int y){QMouseEvent p(QEvent::MouseButtonPress,QPointF(from,y),QPointF(from,y),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(rows,&p);QMouseEvent m(QEvent::MouseMove,QPointF(to,y),QPointF(to,y),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(rows,&m);QMouseEvent r(QEvent::MouseButtonRelease,QPointF(to,y),QPointF(to,y),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(rows,&r);}
void ui(const QString& root) {
    MainWindow w(nullptr,path(root+"/ui-recovery"),"scale-ui");w.show();QTimer::singleShot(0,[]{auto* d=QApplication::activeModalWidget();check(d,"composition dialog");widget<QLineEdit>(*d,"motion-canvas-width")->setText("640");widget<QLineEdit>(*d,"motion-canvas-height")->setText("360");widget<QComboBox>(*d,"motion-frame-rate")->setCurrentIndex(4);widget<QDialogButtonBox>(*d,"motion-new-composition-buttons")->button(QDialogButtonBox::Ok)->click();});
    widget<QAction>(w,"motion-new-composition-action")->trigger();widget<QAction>(w,"motion-new-rectangle-layer-action")->trigger();
    auto* x=widget<QDoubleSpinBox>(w,"motion-transform-scale");auto* y=widget<QDoubleSpinBox>(w,"motion-scale-y");auto* link=widget<QToolButton>(w,"motion-scale-link");
    check(x->value()==100&&y->value()==100&&link->isChecked(),"percentage defaults");link->click();x->setValue(200);finish(x);check(y->value()==100,"unlinked value changes X only");
    const auto before_link=w.compositionDocument()->layers();link->click();check(w.compositionDocument()->layers()[0].transform==before_link[0].transform&&w.compositionDocument()->layers()[0].scale_y==before_link[0].scale_y,"link toggle changes no geometry or curves");
    x->setValue(250);x->setValue(300);finish(x);check(y->value()==150,"linked ratio UI");widget<QAction>(w,"motion-undo-action")->trigger();check(x->value()==200&&y->value()==100,"one Undo for pair");widget<QAction>(w,"motion-redo-action")->trigger();check(x->value()==300&&y->value()==150,"paired Redo");
    auto* timeline=w.findChild<TimelineNavigator*>();check(timeline,"timeline");auto* x_key=widget<QToolButton>(w,"motion-transform-keyframe-scale");auto* y_key=widget<QToolButton>(w,"motion-scale-keyframe-y");
    x_key->click();y_key->click();timeline->setCurrentFrame(30);x_key->click();y_key->click();x->setValue(600);finish(x);timeline->setCurrentFrame(15);check(x->isReadOnly()&&y->isReadOnly()&&near(x->value(),450)&&near(y->value(),225),"interpolated percentage fields read-only");
    x_key->click();check(x->isReadOnly()&&y->isReadOnly(),"link blocks editing while only Y lacks key");link->click();check(!x->isReadOnly()&&y->isReadOnly(),"unlink permits editable axis");y_key->click();
    widget<QSlider>(w,"motion-timeline-zoom-slider")->setValue(widget<QSlider>(w,"motion-timeline-zoom-slider")->maximum());auto* rows=widget<QWidget>(w,"motion-timeline-layer-rows");
    const int destination_x=timeline->frameToViewportX(24);const auto destination=timeline->frameAtViewportX(destination_x);drag(rows,timeline->frameToViewportX(15),destination_x,5*34+17);
    const auto keys=motion::model::layerScaleKeys(w.compositionDocument()->layers()[0],ScaleAxis::Y);check(std::any_of(keys.begin(),keys.end(),[destination](const auto& k){return k.frame==destination;}),"timeline drags Scale Y");
    widget<QComboBox>(w,"motion-curve-editor-preset")->setCurrentIndex(1);const auto& eased=motion::model::layerScaleKeys(w.compositionDocument()->layers()[0],ScaleAxis::Y);check(std::any_of(eased.begin(),eased.end(),[](const auto& k){return k.interpolation==InterpolationMode::CubicBezier;}),"Graph Editor changes Y easing");
    widget<QAction>(w,"motion-new-null-layer-action")->trigger();y->setValue(50);finish(y);const auto& n=w.compositionDocument()->layers().back();check(n.kind==LayerKind::Null&&near(evaluateLayerScale(n,0)[0],.5)&&near(evaluateLayerScale(n,0)[1],.5),"linked Null scale fields");
    QTimer::singleShot(0,[]{auto* d=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());if(d)d->button(QMessageBox::Discard)->click();});w.close();
}
} // namespace
int main(int argc,char** argv){QApplication app(argc,argv);app.setOrganizationName("CreativeSuiteTests");app.setApplicationName("MotionScaleAxes");QStandardPaths::setTestModeEnabled(true);QTemporaryDir root;if(!root.isValid())return 1;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path());try{check(creative_suite::diagnostics::Logger::instance().initialize(path(root.path()+"/logs")),"logger");std::cout<<"model\n"<<std::flush;model();std::cout<<"persistence\n"<<std::flush;persistence(root.path());std::cout<<"rendering\n"<<std::flush;rendering(root.path());std::cout<<"ui\n"<<std::flush;ui(root.path());std::cout<<"Scale axes tests passed.\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
