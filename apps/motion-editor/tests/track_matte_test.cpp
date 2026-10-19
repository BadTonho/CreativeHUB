#include "rendering/preview_scene_builder.h"
#include "model/composition_document.h"
#include "model/composition_interchange.h"
#include "model/layer_transform_hierarchy.h"
#include "persistence/motion_document_store.h"
#include "persistence/motion_recovery_store.h"
#include "rendering/composition_frame_renderer.h"
#include "export/motion_video_export.h"
#include "diagnostics/performance_metrics.h"
#include "ui/main_window/main_window.h"
#include "ui/inspector/inspector_widget.h"
#include "ui/timeline/timeline_navigator.h"
#include "ui/viewer/composition_viewer.h"
#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/media/video_decoder.h>
#include <creative_suite/media/video_encoder.h>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace motion::model;
using namespace motion::ui;
using creative_suite::composition::BlendMode;
using creative_suite::composition::TrackMatteMode;
using Store=motion::persistence::MotionDocumentStore;
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
std::filesystem::path path(const QString& s) {return std::filesystem::path(s.toStdU16String());}
template<class T>T* widget(QObject& owner,const char* name) {
    auto* result=dynamic_cast<T*>(owner.findChild<QObject*>(QString::fromLatin1(name)));check(result,"required widget");return result;
}
CompositionLayer shape(LayerId id,ColorRgba color={192,192,192,255}) {
    CompositionLayer layer{id,LayerKind::Shape,"Repeated"};layer.duration_frames=30;
    auto content=defaultShapeLayerContent({400,200});content.width=400;content.height=200;
    content.stroke_width_pixels=0;content.fill_color=color;layer.content=content;return layer;
}
MotionProjectData project(const CompositionDocument& d) {
    MotionProjectData p;p.composition={d.canvasSize(),d.frameRate()};p.layers=d.layers();p.masks=d.masks();
    for(const auto& l:d.layers())if(!l.source_path.empty())p.media.push_back({l.source_path,creative_suite::media::MediaKind::Image,"Offline"});
    return p;
}
PreviewRequest request(const CompositionDocument& d,int frame=0) {
    PreviewRequest r;r.canvas_size=d.canvasSize();r.frame_rate=d.frameRate();r.composition_frame=frame;
    r.hierarchy=std::make_shared<const LayerTransformHierarchy>(d.canvasSize(),d.layers());r.masks=std::make_shared<const std::vector<Mask>>(d.masks());
    preparePreviewLayers(r,d.layers(),{});
    return r;
}
QJsonObject read(const QString& file) {QFile f(file);check(f.open(QIODevice::ReadOnly),"read fixture");return QJsonDocument::fromJson(f.readAll()).object();}
void write(const QString& file,const QJsonObject& o) {QFile f(file);check(f.open(QIODevice::WriteOnly),"write fixture");check(f.write(QJsonDocument(o).toJson())>0,"fixture bytes");}
void open(MainWindow& w,const QString& file) {
    QTimer::singleShot(0,[file]{auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());check(dialog,"file picker");dialog->selectFile(file);QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);});
    widget<QAction>(w,"motion-open-composition-action")->trigger();QApplication::processEvents();
}
void mouse(QWidget& w,QEvent::Type type,QPointF p,Qt::MouseButton button,Qt::MouseButtons buttons) {
    QMouseEvent e(type,p,w.mapToGlobal(p),button,buttons,Qt::NoModifier);QApplication::sendEvent(&w,&e);
}
void activate(QComboBox* box,int mode) {box->setCurrentIndex(mode);check(QMetaObject::invokeMethod(box,"activated",Qt::DirectConnection,Q_ARG(int,mode)),"activate mode");}
void modelAndPersistence(const QString& root) {
    auto target=shape(2);target.transform.rotation_degrees=35;target.scale_y=ScaleChannel{.4,{}};
    target.anchor_point={-20.5,30.25};target.keyframes.rotation={{0,35},{10,72}};
    CompositionDocument d(400,200,{30,1},{shape(1),target,shape(3)});const auto original=d.layers();
    check(d.setLayerTrackMatte(2,TrackMatteBinding{1,TrackMatteMode::Alpha}),"link matte");
    check(!d.layers()[0].visible&&d.layers()[1].track_matte->source_id==1,"new source hides atomically");
    auto unchanged=d.layers()[1];unchanged.track_matte.reset();check(unchanged==original[1],"matte preserves all other properties");
    check(d.setLayerVisible(1,true)&&d.setLayerTrackMatte(2,TrackMatteBinding{1,TrackMatteMode::Luma})&&d.layers()[0].visible,"mode-only change retains source visibility");
    const auto before=d.layers();
    check(!d.setLayerTrackMatte(2,TrackMatteBinding{2,TrackMatteMode::Alpha})&&!d.setLayerTrackMatte(2,TrackMatteBinding{99,TrackMatteMode::Alpha})&&
        !d.setLayerTrackMatte(2,TrackMatteBinding{1,static_cast<TrackMatteMode>(255)})&&d.layers()==before,"invalid relations are atomic");
    check(d.setLayerTrackMatte(3,TrackMatteBinding{2,TrackMatteMode::Alpha}),"chain source can have matte");
    check(!d.setLayerTrackMatte(1,TrackMatteBinding{3,TrackMatteMode::Alpha})&&d.eligibleTrackMatteSources(1).empty(),"cycle and cyclic candidates refused");
    LayerId null=0;check(d.addContentLayer(LayerKind::Null,"Null",0,&null),"Null");
    check(!d.setLayerTrackMatte(null,TrackMatteBinding{1,TrackMatteMode::Alpha})&&!d.setLayerTrackMatte(1,TrackMatteBinding{null,TrackMatteMode::Alpha}),"Null cannot consume or supply matte");
    check(d.moveLayer(1,2)&&d.layers()[0].track_matte->source_id==1,"reorder retains ID binding");
    const auto file=root+"/matte.motion";const auto saved=project(d);Store::save(path(file),saved);const auto encoded=read(file);
    check(encoded.value("version").toInt()==14&&Store::load(path(file))==saved,"v14 roundtrip");
    Store::saveRecovery(path(root+"/recovery"),path(file),"matte",saved);
    check(read(root+"/recovery").value("version").toInt()==1&&Store::loadRecovery(path(root+"/recovery")).document==saved,"recovery wrapper remains v1");
    for(int code=0;code<11;++code) {
        auto bad=encoded;auto layers=bad.value("layers").toArray();auto layer=layers[0].toObject();auto matte=layer.value("track_matte").toObject();
        if(code==0)bad.insert("version",13);
        else if(code==1)matte.insert("mode","unknown");
        else if(code==2)matte.insert("mode",42);
        else if(code==3)matte.insert("source_id","9999");
        else if(code==4)matte.insert("source_id",layer.value("id"));
        else if(code==5)matte.insert("source_id",QString::number(null));
        else if(code==6){auto source=layers[2].toObject();source.insert("track_matte",QJsonObject{{"source_id",layer.value("id")},{"mode","alpha"}});layers[2]=source;}
        else if(code==7)matte.insert("unexpected",true);
        else if(code==8)bad.insert("version",15);
        else if(code==9)matte.remove("mode");
        layer.insert("track_matte",code==10?QJsonValue(QJsonArray{}):QJsonValue(matte));layers[0]=layer;bad.insert("layers",layers);
        write(root+"/bad.motion",bad);bool rejected=false;
        try{(void)Store::load(path(root+"/bad.motion"));}catch(const motion::persistence::MotionDocumentError&){rejected=true;}
        check(rejected&&read(root+"/bad.motion")==bad,"invalid graph/record/version rejected without rewriting");
    }
    auto invalid=saved;invalid.layers[0].track_matte->source_id=999;bool rejected=false;
    try{Store::save(path(file),invalid);}catch(const motion::persistence::MotionDocumentError&){rejected=true;}
    check(rejected&&read(file)==encoded,"invalid save preserves existing document");
    rejected=false;try{(void)exportComposition(saved);}catch(const creative_suite::composition_exchange::Unsupported& e){const std::string text=e.what();rejected=text.find("Repeated")!=std::string::npos&&text.find("2")!=std::string::npos&&text.find("3")!=std::string::npos;}
    check(rejected,"interchange identifies every matte layer");
    CompositionDocument legacy(400,200,{30,1},{shape(1)});Store::save(path(file),project(legacy));const auto old=read(file);
    check(!old.value("layers").toArray()[0].toObject().contains("track_matte"),"absent matte omitted");
    for(int version=1;version<=13;++version){auto fixture=old;fixture.insert("version",version);write(file,fixture);check(!Store::load(path(file)).layers[0].track_matte,"v1-v13 have no matte");}
    auto high_source=shape(9'007'199'254'740'993ULL),high_target=shape(9'007'199'254'740'995ULL);
    CompositionDocument high(400,200,{30,1},{high_source,high_target});
    check(high.setLayerTrackMatte(high_target.id,TrackMatteBinding{high_source.id,TrackMatteMode::Alpha}),"64-bit matte identifier");
    Store::save(path(file),project(high));check(Store::load(path(file))==project(high),"matte IDs above JSON integer precision roundtrip exactly");
    auto removal=d;check(removal.removeLayer(1,8),"delete source");
    for(const auto& l:removal.layers())check(!l.track_matte||l.track_matte->source_id!=1,"delete clears direct consumers");
    check(removal.layers()[1].track_matte.has_value(),"delete retains indirect chain");
    CompositionDocument combined(400,200,{30,1},{shape(1),shape(2)});
    check(combined.setLayerParent(2,1,0)&&combined.setLayerTrackMatte(1,TrackMatteBinding{2,TrackMatteMode::Alpha}),"pixel and geometry graphs remain independent");
    check(combined.setLayerTrackMatte(1,{}),"clear parent matte");
    check(combined.setLayerTrackMatte(2,TrackMatteBinding{1,TrackMatteMode::Alpha}),"source can also be parent");
    const auto world=LayerTransformHierarchy(combined.canvasSize(),combined.layers()).evaluate(0).at(2).world;
    check(combined.removeLayer(1,0)&&!combined.layers()[0].track_matte&&!combined.layers()[0].parent_id&&
        LayerTransformHierarchy(combined.canvasSize(),combined.layers()).evaluate(0).at(2).world==world,"deletion clears matte while preserving child geometry");
    std::vector<CompositionLayer> long_chain;for(LayerId i=1;i<=4000;++i){auto l=shape(i);if(i>1)l.track_matte=TrackMatteBinding{i-1,TrackMatteMode::Alpha};long_chain.push_back(l);}
    std::reverse(long_chain.begin(),long_chain.end());TrackMatteGraph graph(long_chain);
    check(graph.order().front()==1&&graph.order().back()==4000,"long reversed chain is iterative and source-first");
}
int red(const creative_suite::media::RgbaFrame& f,int x=200,int y=100){return f.rgba_pixels[std::size_t(y)*f.stride+x*4];}
void parityExport(const QString& root,const CompositionDocument& d,const creative_suite::media::RgbaFrame& frame,
    const std::map<LayerId,creative_suite::media::RgbaFramePtr>& stills={}) {
    MotionExportSnapshot snapshot{d.canvasSize(),d.frameRate(),d.layers(),stills,d.masks()};for(auto& l:snapshot.layers)l.duration_frames=1;
    MotionExportSettings settings{path(root+"/matte.mkv"),"matroska","ffv1",400,200,{30,1},10};std::atomic_bool cancel=false;
    MotionVideoExporter::exportVideo(snapshot,settings,cancel);
    creative_suite::media::VideoEncodingSettings encoding;encoding.output_path=path(root+"/reference.mkv");encoding.container_name="matroska";encoding.video_encoder_name="ffv1";encoding.width=400;encoding.height=200;encoding.frame_rate_numerator=30;
    {creative_suite::media::VideoEncoder encoder(encoding);encoder.writeVideo(frame,0);encoder.finish();}
    creative_suite::media::VideoDecoder decoder;check(decoder.decode_first_frame(settings.output_path).rgba_pixels==decoder.decode_first_frame(encoding.output_path).rgba_pixels,"preview/export parity");
}
void rendering(const QString& root) {
    auto source=shape(1,{255,0,0,128});source.visible=false;source.transform.opacity=.5;
    auto target=shape(2,{200,100,50,255});target.track_matte=TrackMatteBinding{1,TrackMatteMode::Alpha};
    CompositionDocument d(400,200,{30,1},{target,source});CompositionFrameRenderer renderer;const int expected[]{50,150,11,189};
    for(int mode=0;mode<4;++mode) {
        if(mode)check(d.setLayerTrackMatte(2,TrackMatteBinding{1,static_cast<TrackMatteMode>(mode)}),"change mode");
        auto r=request(d);check(r.layers.size()==2&&!r.layers[1].composite,"hidden dependency is in scene");r.freezeScene();
        const auto frame=renderer.render(r);check(frame&&red(*frame)==expected[mode],"independent alpha/luma/opacity reference");parityExport(root,d,*frame);
    }
    source.timeline_start_frame=10;source.duration_frames=2;
    for(int mode=0;mode<4;++mode) {
        target.track_matte=TrackMatteBinding{1,static_cast<TrackMatteMode>(mode)};CompositionDocument timed(400,200,{30,1},{source,target});
        for(int frame:{0,12}){const auto result=renderer.render(request(timed,frame));check(result&&red(*result)==(mode%2?200:0),"inactive source and inversion");}
        const auto active=renderer.render(request(timed,10));check(active&&red(*active)==expected[mode],"source local clock");
    }
    {
        auto visible=shape(1,{200,100,50,128});visible.transform.opacity=.5031;
        auto inactive=shape(2);inactive.timeline_start_frame=100;inactive.track_matte=TrackMatteBinding{1,TrackMatteMode::Alpha};
        CompositionDocument unused(400,200,{30,1},{visible,inactive}),legacy(400,200,{30,1},{visible});
        const auto a=renderer.render(request(unused)),b=renderer.render(request(legacy));
        check(a&&b&&red(*a)>0&&a->rgba_pixels==b->rgba_pixels,
            "unused matte sources retain exact legacy pixels, including shape raster rounding");
    }
    auto square=shape(1,{255,255,255,255});square.visible=false;square.transform.position_x=.25;
    auto content=std::get<ShapeLayerContent>(square.content);content.width=100;content.height=100;square.content=content;
    target.track_matte=TrackMatteBinding{1,TrackMatteMode::Alpha};CompositionDocument geometry(400,200,{30,1},{target,square});
    const auto clipped=renderer.render(request(geometry));check(clipped&&red(*clipped,100,100)==200&&red(*clipped,300,100)==0,"independent canvas-pixel clipping");
    check(geometry.setLayerTrackMatte(2,TrackMatteBinding{1,TrackMatteMode::AlphaInverted}),"inverse");const auto inverted=renderer.render(request(geometry));
    check(inverted&&red(*inverted,100,100)==0&&red(*inverted,300,100)==200,"inversion includes outside geometry");
    auto image=source;image.kind=LayerKind::Image;image.content={};image.source_path=path(root+"/offline.png");image.timeline_start_frame=0;image.duration_frames=30;
    for(auto mode:{TrackMatteMode::Alpha,TrackMatteMode::AlphaInverted}) {
        target.track_matte=TrackMatteBinding{1,mode};CompositionDocument offline(400,200,{30,1},{target,image});const auto r=renderer.render(request(offline));
        check(r&&red(*r)==(mode==TrackMatteMode::Alpha?0:200),"offline preview uses transparent source");
        bool failed=false;try{(void)renderer.render(request(offline),{},true);}catch(const std::exception&){failed=true;}check(failed,"required offline source fails export rendering");
    }
    {
        auto shared=shape(1,{255,255,255,128});shared.visible=false;
        auto lower=shape(2,{200,0,0,255}),upper=shape(3,{0,200,0,255});
        lower.track_matte=upper.track_matte=TrackMatteBinding{1,TrackMatteMode::Alpha};
        CompositionDocument multi(400,200,{30,1},{lower,shared,upper});CompositionFrameRenderer once;
        auto& counters=motion::diagnostics::PerformanceMetrics::instance();counters.setEnabled(true);(void)counters.takeSnapshotAndReset();
        const auto result=once.render(request(multi));const auto work=counters.takeSnapshotAndReset();
        check(result&&red(*result)==50&&result->rgba_pixels[(100*400+200)*4+1]==100,"shared matte and final stack order");
        check(work&&work->timings[static_cast<std::size_t>(motion::diagnostics::PreviewTimingStage::TextShapeRasterization)].count==3,"shared matte processed once per frame");
    }
    auto a=shape(1,{255,255,255,128});a.visible=false;auto b=shape(2,{255,255,255,128});b.visible=false;b.track_matte=TrackMatteBinding{1,TrackMatteMode::Alpha};
    auto c=shape(3,{200,100,50,255});c.track_matte=TrackMatteBinding{2,TrackMatteMode::Alpha};CompositionDocument chain(400,200,{30,1},{c,b,a});
    const auto chain_frame=renderer.render(request(chain));check(chain_frame&&red(*chain_frame)==50,"chain consumes matted source");
    check(chain.setLayerBlendMode(2,BlendMode::Difference),"source mode");const auto ignored=renderer.render(request(chain));
    check(ignored&&ignored->rgba_pixels==chain_frame->rgba_pixels,"source blend excluded from isolated pixels");
    auto r=request(chain);r.freezeScene();check(chain.setLayerTrackMatte(3,TrackMatteBinding{2,TrackMatteMode::AlphaInverted}),"new graph");
    const auto frozen=renderer.render(r);check(frozen&&frozen->rgba_pixels==chain_frame->rgba_pixels,"frozen graph immutable");
    auto& metrics=motion::diagnostics::PerformanceMetrics::instance();metrics.setEnabled(true);(void)renderer.render(request(chain));(void)metrics.takeSnapshotAndReset();
    check(renderer.render(request(chain))!=nullptr,"warm graph");const auto cached=metrics.takeSnapshotAndReset();
    check(cached&&cached->timings[static_cast<std::size_t>(motion::diagnostics::PreviewTimingStage::TextShapeRasterization)].count==0,"matte edits reuse rasters");
}
void transformsAndMasks(const QString& root) {
    auto parent=shape(9);parent.kind=LayerKind::Null;parent.content={};parent.visible=false;parent.timeline_start_frame=50;parent.duration_frames=1;
    parent.transform.position_x=0;parent.transform.position_y=0;parent.transform.rotation_degrees=29;parent.scale_y=ScaleChannel{.55,{}};
    auto source=shape(1,{180,230,80,200});source.visible=false;source.parent_id=9;source.anchor_point={-12.5,16};source.transform.scale=.7;
    source.keyframes.opacity={{0,.4},{15,.9}};source.effects={ColorAdjustmentEffect{true,10,120,90},GaussianBlurEffect{true,2}};
    auto target=shape(2,{200,100,50,180});target.track_matte=TrackMatteBinding{1,TrackMatteMode::Luma};target.scale_y=ScaleChannel{.8,{}};target.blend_mode=BlendMode::Screen;
    CompositionDocument d(400,200,{30,1},{source,target,parent});
    for(auto id:{LayerId{1},LayerId{2}})for(int stage=-2;stage<=1;++stage)if(stage<0 || id==1)check(d.createMask(id,stage,creative_suite::composition::ShapeMaskKind::Ellipse)!=0,"stage mask");
    CompositionFrameRenderer renderer;auto r=request(d);r.freezeScene();const auto cpu=renderer.render(r);check(cpu!=nullptr,"affine hierarchy/effects/masks");parityExport(root,d,*cpu);
    CompositionGpuMetrics metrics;CompositionFrameRenderer fallback(true,true,nullptr,&metrics);const auto recovered=fallback.render(r);
    check(recovered&&recovered->rgba_pixels==cpu->rgba_pixels,"fallback preserves graph and stages");
    check(d.moveLayerInTimeline(9,0),"activate parent editing interval");
    for(LayerId id:{LayerId{1},LayerId{2},LayerId{9}})for(int kind=0;kind<3;++kind) {
        auto draft=request(d);draft.freezeScene();auto committed=d;
        if(kind==0){draft.position_override=LayerPositionOverride{id,.45,.6};check(committed.setLayerPositionAtFrame(id,.45,.6,0),"position candidate");}
        else if(kind==1){draft.rotation_override=LayerRotationOverride{id,68};check(committed.setLayerRotationAtFrame(id,68,0),"rotation candidate");}
        else {draft.scale_override=LayerScaleOverride{id,.6,1.2};check(committed.setLayerScaleAtFrame(id,.6,1.2,0),"scale candidate");}
        const auto a=renderer.render(draft),b=renderer.render(request(committed));check(a&&b&&a->rgba_pixels==b->rgba_pixels,"gesture equals confirmed target/source/ancestor");
    }
    auto view=request(d);view.mask_view_layer=2;view.mask_view_target=-2;const auto before=renderer.render(view);
    check(d.setLayerTrackMatte(2,TrackMatteBinding{1,TrackMatteMode::AlphaInverted}),"mask view edit");auto after=request(d);after.mask_view_layer=2;after.mask_view_target=-2;
    const auto viewed=renderer.render(after);check(before&&viewed&&before->rgba_pixels==viewed->rgba_pixels,"mask View ignores matte");
}
void sourceKinds(const QString& root) {
    auto pixels=std::make_shared<creative_suite::media::RgbaFrame>();pixels->width=96;pixels->height=64;pixels->stride=384;pixels->rgba_pixels.resize(96*64*4);
    for(std::size_t i=0;i<pixels->rgba_pixels.size();i+=4){pixels->rgba_pixels[i]=200;pixels->rgba_pixels[i+1]=100;pixels->rgba_pixels[i+2]=50;pixels->rgba_pixels[i+3]=200;}
    QImage image(pixels->rgba_pixels.data(),96,64,384,QImage::Format_RGBA8888);check(image.save(root+"/image.png"),"image fixture");
    creative_suite::media::VideoEncodingSettings encoding;encoding.output_path=path(root+"/video.mkv");encoding.container_name="matroska";encoding.video_encoder_name="ffv1";encoding.width=96;encoding.height=64;encoding.frame_rate_numerator=30;
    {creative_suite::media::VideoEncoder encoder(encoding);for(int i=0;i<3;++i)encoder.writeVideo(*pixels,i);encoder.finish();}
    for(auto kind:{LayerKind::Image,LayerKind::Video,LayerKind::Text,LayerKind::Shape}) {
        auto source=shape(1,{200,100,50,200});source.kind=kind;source.transform.rotation_degrees=12;source.transform.scale=1.2;
        source.scale_y=ScaleChannel{.8,{}};source.anchor_point={4,-2};source.effects={ColorAdjustmentEffect{true,7,100,100},GaussianBlurEffect{true,1}};
        if(kind==LayerKind::Text){auto text=defaultTextLayerContent({400,200});text.text="Blend";text.color={200,100,50,200};source.content=text;}
        if(kind==LayerKind::Image||kind==LayerKind::Video){source.content={};source.source_path=kind==LayerKind::Image?path(root+"/image.png"):encoding.output_path;}
        if(kind==LayerKind::Video){source.source_frame_rate=30;source.source_frame_count=3;source.duration_frames=source.maximum_timeline_duration_frames=3;}
        auto matte=shape(3,{255,255,255,160});matte.visible=false;
        source.track_matte=TrackMatteBinding{3,TrackMatteMode::Alpha};
        CompositionDocument d(400,200,{30,1},{shape(2,{64,64,64,255}),source,matte});
        const auto make=[&]{auto r=request(d,1);for(auto& l:r.layers)if(l.id==1){l.still_frame=kind==LayerKind::Image?pixels:nullptr;l.source_frame_rate=source.source_frame_rate;l.source_frame_count=source.source_frame_count;}return r;};
        CompositionFrameRenderer renderer;auto& metrics=motion::diagnostics::PerformanceMetrics::instance();
        check(renderer.render(make())!=nullptr,"warm media/content");(void)metrics.takeSnapshotAndReset();
        check(d.setLayerBlendMode(1,BlendMode::Screen),"mode change");const auto screen=renderer.render(make());const auto counts=metrics.takeSnapshotAndReset();
        check(screen&&counts&&counts->timestamp_seek_attempts==0&&counts->forward_decode_attempts==0&&counts->timings[static_cast<std::size_t>(motion::diagnostics::PreviewTimingStage::TextShapeRasterization)].count==0,"mode change preserves warmed decode/raster caches");
        check(d.setLayerBlendMode(1,BlendMode::Difference),"second mode");const auto different=renderer.render(make());check(different&&different->rgba_pixels!=screen->rgba_pixels,"all source kinds consume the mode");
        for(int stage=-2;stage<=1;++stage)check(d.createMask(1,stage,creative_suite::composition::ShapeMaskKind::Ellipse)!=0,"source stage mask");
        check(d.setLayerTrackMatte(2,TrackMatteBinding{1,TrackMatteMode::Luma}),"all source kinds can supply a chained matte");
        const auto masked=renderer.render(make());check(masked!=nullptr,"all source kinds and all mask stages render");
        CompositionGpuMetrics gpu_metrics;CompositionFrameRenderer fallback(true,true,nullptr,&gpu_metrics);const auto recovered=fallback.render(make());
        check(recovered&&recovered->rgba_pixels==masked->rgba_pixels&&gpu_metrics.fallback_frames==1,"GPU recovery retains mode/effects/masks");
        MotionExportSnapshot snapshot{d.canvasSize(),d.frameRate(),d.layers(),{},d.masks()};for(auto& l:snapshot.layers)l.duration_frames=1;
        if(kind==LayerKind::Image)snapshot.still_frames[1]=pixels;
        MotionExportSettings settings{path(root+"/kind-"+QString::number(static_cast<int>(kind))+".mkv"),"matroska","ffv1",400,200,{30,1},10};std::atomic_bool cancel=false;MotionVideoExporter::exportVideo(snapshot,settings,cancel);
        auto reference=encoding;reference.output_path=path(root+"/kind-reference.mkv");reference.width=400;reference.height=200;
        {creative_suite::media::VideoEncoder encoder(reference);encoder.writeVideo(*masked,0);encoder.finish();}
        creative_suite::media::VideoDecoder decoder;check(decoder.decode_first_frame(settings.output_path).rgba_pixels==decoder.decode_first_frame(reference.output_path).rgba_pixels,"source/effect/mask export parity");
    }
}
void interface(const QString& root) {
    auto first=shape(1),second=shape(2);first.name=second.name="Repeated %2";
    CompositionDocument d(400,200,{30,1},{first,second});const auto file=root+"/ui.motion";Store::save(path(file),project(d));
    MainWindow w(nullptr,path(root+"/autosave"),"matte");w.resize(1500,1000);w.show();open(w,file);
    auto* rows=widget<QWidget>(w,"motion-timeline-layer-rows");mouse(*rows,QEvent::MouseButtonPress,{120,17},Qt::LeftButton,Qt::LeftButton);mouse(*rows,QEvent::MouseButtonRelease,{120,17},Qt::LeftButton,Qt::NoButton);
    auto* source=widget<QComboBox>(w,"motion-track-matte-source");auto* mode=widget<QComboBox>(w,"motion-track-matte-mode");
    auto* undo=widget<QAction>(w,"motion-undo-action");auto* redo=widget<QAction>(w,"motion-redo-action");
    check(source->count()==2&&source->isEnabled()&&!mode->isEnabled()&&source->itemText(1).contains("#1")&&source->itemText(1).contains("Repeated %2"),"source selector excludes self and identifies duplicate names");
    activate(source,1);check(w.compositionDocument()->layers()[1].track_matte->source_id==1&&!w.compositionDocument()->layers()[0].visible&&mode->isEnabled(),"Inspector links and hides atomically");
    undo->trigger();check(!w.compositionDocument()->layers()[1].track_matte&&w.compositionDocument()->layers()[0].visible&&redo->isEnabled(),"single Undo includes source visibility");
    activate(source,0);check(redo->isEnabled(),"no-op retains Redo");redo->trigger();
    auto* viewer=widget<CompositionViewer>(w,"motion-composition-viewer");
    for(int gesture=0;gesture<4;++gesture) {
        const auto canvas=QRectF(viewer->rect()).adjusted(24,42,-24,-24);const auto pivot=canvas.center();
        const auto handle=pivot+(gesture==0?QPointF(32,0):gesture==1?QPointF(0,-56):gesture==2?QPointF(88,0):QPointF{});
        const auto confirmed=w.compositionDocument()->layers();mouse(*viewer,QEvent::MouseButtonPress,handle,Qt::LeftButton,Qt::LeftButton);
        check(viewer->positionDragActive()||viewer->rotationDragActive()||viewer->scaleDragActive()||viewer->panBehindActive(),"gesture begins");
        mouse(*viewer,QEvent::MouseMove,handle+QPointF(25,20),Qt::NoButton,Qt::LeftButton);activate(mode,(gesture+1)%4);
        check(!viewer->positionDragActive()&&!viewer->rotationDragActive()&&!viewer->scaleDragActive()&&!viewer->panBehindActive(),"matte edit cancels gesture");
        auto actual=w.compositionDocument()->layers()[1];actual.track_matte=confirmed[1].track_matte;check(actual==confirmed[1],"gesture values not committed");
    }
    auto* timeline=widget<TimelineNavigator>(w,"motion-timeline");
    mouse(*rows,QEvent::MouseButtonPress,{60,17},Qt::LeftButton,Qt::LeftButton);
    check(timeline->parentPickWhipActive(),"parent pick-whip begins");
    activate(mode,1);
    check(!timeline->parentPickWhipActive()&&w.compositionDocument()->layers()[1].track_matte->mode==TrackMatteMode::AlphaInverted&&
        !w.compositionDocument()->layers()[1].parent_id,"matte selection cancels parent pick-whip without parenting");
    auto* timer=widget<QTimer>(w,"motion-autosave-timer");QMetaObject::invokeMethod(timer,"timeout",Qt::DirectConnection);
    motion::persistence::MotionRecoveryStore recovery(path(root+"/autosave"),"reader");const auto saves=recovery.snapshotsForProject(path(file));
    check(!saves.empty()&&Store::loadRecovery(saves.front().path).document.layers==w.compositionDocument()->layers(),"autosave stores confirmed matte");
    auto* inspector=w.findChild<InspectorWidget*>();check(inspector,"Inspector");inspector->setBlendModeBusy(true);check(!source->isEnabled()&&!mode->isEnabled(),"work blocks selectors");inspector->setBlendModeBusy(false);
    widget<QAction>(w,"motion-new-null-layer-action")->trigger();check(!source->isEnabled()&&!mode->isEnabled(),"Null selectors disabled");undo->trigger();
    widget<QAction>(w,"motion-save-composition-action")->trigger();
    auto bad=read(file);auto layers=bad.value("layers").toArray();auto layer=layers[1].toObject();layer.insert("track_matte",QJsonObject{{"source_id","2"},{"mode","alpha"}});layers[1]=layer;bad.insert("layers",layers);write(root+"/invalid.motion",bad);
    QTimer dismiss;QObject::connect(&dismiss,&QTimer::timeout,[&]{if(auto* b=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){b->accept();dismiss.stop();}});dismiss.start(1);
    const auto before=w.compositionDocument()->layers();open(w,root+"/invalid.motion");dismiss.stop();check(w.compositionDocument()->layers()==before,"invalid Open keeps current document");
    w.close();QApplication::processEvents();
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setOrganizationName("CreativeSuiteTests");app.setApplicationName("MotionTrackMattes");QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir root;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path());
    try {check(root.isValid()&&creative_suite::diagnostics::Logger::instance().initialize(path(root.path()+"/logs")),"temporary fixture/log");
        modelAndPersistence(root.path());rendering(root.path());transformsAndMasks(root.path());sourceKinds(root.path());interface(root.path());std::cout<<"Motion track mattes passed.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
