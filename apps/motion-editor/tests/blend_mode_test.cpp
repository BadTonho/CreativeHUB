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
    for(const auto& l:d.layers())if(l.kind!=LayerKind::Null&&l.visible&&frame>=l.timeline_start_frame&&frame-l.timeline_start_frame<l.duration_frames) {
        PreviewLayerSnapshot s;s.id=l.id;s.kind=l.kind;s.source_path=l.source_path;s.local_frame=frame-l.timeline_start_frame;
        s.transform=l.transform;s.keyframes=l.keyframes;s.content=l.content;s.effects=l.effects;s.source_mask=l.source_mask;s.transform_mask=l.transform_mask;
        s.anchor_point=l.anchor_point;s.anchor_keyframes=l.anchor_keyframes;s.scale_y=l.scale_y;s.blend_mode=l.blend_mode;r.layers.push_back(std::move(s));
    }
    return r;
}
QJsonObject read(const QString& file) {QFile f(file);check(f.open(QIODevice::ReadOnly),"read fixture");return QJsonDocument::fromJson(f.readAll()).object();}
void write(const QString& file,const QJsonObject& o) {QFile f(file);check(f.open(QIODevice::WriteOnly),"write fixture");check(f.write(QJsonDocument(o).toJson())>0,"fixture bytes");}
void waitFor(const std::function<bool()>& condition) {
    QElapsedTimer timer;timer.start();while(!condition()&&timer.elapsed()<8000){QApplication::processEvents();QThread::msleep(1);}check(condition(),"asynchronous UI completed");
}
void open(MainWindow& w,const QString& file) {
    QTimer::singleShot(0,[file]{auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());check(dialog,"file picker");dialog->selectFile(file);QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);});
    widget<QAction>(w,"motion-open-composition-action")->trigger();QApplication::processEvents();
}
void mouse(QWidget& w,QEvent::Type type,QPointF p,Qt::MouseButton button,Qt::MouseButtons buttons) {
    QMouseEvent e(type,p,w.mapToGlobal(p),button,buttons,Qt::NoModifier);QApplication::sendEvent(&w,&e);
}
void activate(QComboBox* box,int mode) {box->setCurrentIndex(mode);check(QMetaObject::invokeMethod(box,"activated",Qt::DirectConnection,Q_ARG(int,mode)),"activate mode");}
void modelAndPersistence(const QString& root) {
    auto source=shape(1);source.visible=false;source.timeline_start_frame=10;
    source.transform.rotation_degrees=35;source.scale_y=ScaleChannel{.4,{}};source.anchor_point={-20.5,30.25};
    source.keyframes.rotation={{0,35},{10,72}};
    CompositionDocument d(400,200,{30,1},{source});const auto before=d.layers()[0];
    check(d.setLayerBlendMode(1,BlendMode::Overlay),"static mode can change outside activity/visibility");
    auto compared=d.layers()[0];compared.blend_mode=before.blend_mode;check(compared==before,"mode preserves every other property");
    const auto stable=d.layers();check(!d.setLayerBlendMode(1,BlendMode::Overlay)&&!d.setLayerBlendMode(9,BlendMode::Add)&&!d.setLayerBlendMode(1,static_cast<BlendMode>(255))&&d.layers()==stable,"invalid/no-change atomicity");
    LayerId null_id=0;check(d.addContentLayer(LayerKind::Null,"Null",0,&null_id),"Null fixture");check(!d.setLayerBlendMode(null_id,BlendMode::Screen),"Null cannot blend");
    const auto file=root+"/modes.motion";auto p=project(d);Store::save(path(file),p);
    auto encoded=read(file);check(encoded.value("version").toInt()==Store::current_format_version&&encoded.value("layers").toArray()[0].toObject().value("blend_mode")=="overlay","v13 string record");
    check(!encoded.value("layers").toArray()[1].toObject().contains("blend_mode")&&Store::load(path(file))==p,"Normal omitted and full roundtrip");
    auto invalid_project=p;invalid_project.layers[0].blend_mode=static_cast<BlendMode>(255);bool invalid_save=false;
    try{Store::save(path(file),invalid_project);}catch(const motion::persistence::MotionDocumentError&){invalid_save=true;}
    check(invalid_save&&read(file)==encoded,"invalid mode save cannot replace existing file");

    Store::saveRecovery(path(root+"/recovery"),path(file),"blend",p);check(Store::loadRecovery(path(root+"/recovery")).document==p&&read(root+"/recovery").value("version").toInt()==1,"recovery wrapper unchanged");
    for(int code=0;code<8;++code) {
        auto bad=encoded;auto layers=bad.value("layers").toArray();auto record=layers[0].toObject();
        if(code==0)bad.insert("version",12);else if(code==1)record.insert("blend_mode","unknown");else if(code==2)record.insert("blend_mode",1);
        else if(code==3){record=layers[1].toObject();record.insert("blend_mode","multiply");layers.removeLast();}
        else if(code==4)bad.insert("version",Store::current_format_version+1);
        else if(code==5){bad.insert("version",12);record.insert("blend_mode","normal");}
        else if(code==6)record.insert("blend_mode",QJsonValue());
        else record.insert("blend_mode",QJsonArray{"screen"});
        layers[0]=record;bad.insert("layers",layers);write(root+"/bad.motion",bad);bool rejected=false;
        try{(void)Store::load(path(root+"/bad.motion"));}catch(const motion::persistence::MotionDocumentError&){rejected=true;}
        check(rejected&&read(root+"/bad.motion")==bad,"bad document rejected without rewriting");
    }
    CompositionDocument legacy(400,200,{30,1},{shape(1)});Store::save(path(file),project(legacy));const auto normal=read(file);
    for(int version=1;version<=12;++version){auto old=normal;old.insert("version",version);write(file,old);const auto loaded=Store::load(path(file));check(loaded.layers[0].blend_mode==BlendMode::Normal,"v1-v12 default Normal");}
    check(importComposition(exportComposition(project(legacy))).layers[0].blend_mode==BlendMode::Normal,"Normal interchange");
    check(legacy.setLayerBlendMode(1,BlendMode::Difference),"unsupported fixture");bool rejected=false;
    try{(void)exportComposition(project(legacy));}catch(const creative_suite::composition_exchange::Unsupported& e){const std::string text=e.what();rejected=text.find("Repeated")!=std::string::npos&&text.find("1")!=std::string::npos;}
    check(rejected,"interchange names unsupported layers and IDs");
    auto multiple=project(legacy);auto hidden=multiple.layers[0];hidden.id=2;hidden.visible=false;hidden.timeline_start_frame=50;hidden.blend_mode=BlendMode::Overlay;multiple.layers.push_back(hidden);rejected=false;
    try{(void)exportComposition(multiple);}catch(const creative_suite::composition_exchange::Unsupported& e){const std::string message=e.what();rejected=message.find("1")!=std::string::npos&&message.find("2")!=std::string::npos&&message.find("Repeated")!=std::string::npos;}
    check(rejected,"interchange identifies duplicate names and hidden/inactive unsupported layers");

}
void rendering(const QString& root) {
    const int expected[]{192,48,208,96,255,128};
    CompositionDocument d(400,200,{30,1},{shape(2,{64,64,64,255}),shape(1)});CompositionFrameRenderer renderer;
    auto& metrics=motion::diagnostics::PerformanceMetrics::instance();metrics.setEnabled(true);
    for(unsigned mode=0;mode<6;++mode) {
        if(mode)check(d.setLayerBlendMode(1,static_cast<BlendMode>(mode)),"render mode edit");
        auto r=request(d);r.freezeScene();const auto frame=renderer.render(r);check(frame&&frame->rgba_pixels[(100*400+200)*4]==expected[mode],"preview uses independent blend color table");
        (void)metrics.takeSnapshotAndReset();check(renderer.render(r)!=nullptr,"cached blend frame");const auto counts=metrics.takeSnapshotAndReset();
        check(counts&&counts->timings[static_cast<std::size_t>(motion::diagnostics::PreviewTimingStage::TextShapeRasterization)].count==0,"blend edits reuse raster cache");
        MotionExportSnapshot snapshot{d.canvasSize(),d.frameRate(),d.layers(),{},d.masks()};for(auto& l:snapshot.layers)l.duration_frames=1;
        MotionExportSettings settings{path(root+"/blend-"+QString::number(mode)+".mkv"),"matroska","ffv1",400,200,{30,1},10};std::atomic_bool cancel=false;
        MotionVideoExporter::exportVideo(snapshot,settings,cancel);creative_suite::media::VideoDecoder decoder;const auto exported=decoder.decode_first_frame(settings.output_path);
        // FFV1's RGB/YUV conversion can round channels; compare the same encoded reference.
        auto reference=settings;reference.output_path=path(root+"/reference.mkv");
        creative_suite::media::VideoEncodingSettings encoding;encoding.output_path=reference.output_path;encoding.container_name="matroska";encoding.video_encoder_name="ffv1";encoding.width=400;encoding.height=200;encoding.frame_rate_numerator=30;
        {creative_suite::media::VideoEncoder encoder(encoding);encoder.writeVideo(*frame,0);encoder.finish();}
        check(exported.rgba_pixels==decoder.decode_first_frame(reference.output_path).rgba_pixels,"preview/export parity");
    }
    auto null=shape(9);null.kind=LayerKind::Null;null.content={};null.visible=false;null.timeline_start_frame=50;null.duration_frames=1;null.transform.position_x=0;null.transform.position_y=0;null.transform.rotation_degrees=27;null.scale_y=ScaleChannel{.5,{}};
    auto parented=shape(1,{200,120,50,128});parented.parent_id=9;parented.anchor_point={-15,4};parented.transform.scale=.6;parented.scale_y=ScaleChannel{.9,{}};
    parented.effects={ColorAdjustmentEffect{true,10,120,90},GaussianBlurEffect{true,2}};parented.blend_mode=BlendMode::Multiply;
    for(int stage=-2;stage<=1;++stage) {
        CompositionDocument masks(400,200,{30,1},{shape(2,{64,64,64,255}),parented,null});check(masks.createMask(1,stage,creative_suite::composition::ShapeMaskKind::Ellipse)!=0,"mask fixture");
        auto r=request(masks);const auto blended=renderer.render(r);check(blended!=nullptr,"masked/sheared/offline ancestor blend");
        check(masks.setLayerBlendMode(1,BlendMode::Normal),"reference normal");const auto normal=renderer.render(request(masks));if(!normal||normal->rgba_pixels==blended->rgba_pixels)throw std::runtime_error("Masked final merge consumes mode, stage "+std::to_string(stage));
        r.mask_view_layer=1;r.mask_view_target=stage;auto n=request(masks);n.mask_view_layer=1;n.mask_view_target=stage;
        const auto a=renderer.render(r),b=renderer.render(n);check(a&&b&&a->rgba_pixels==b->rgba_pixels,"mask View is independent of mode");
    }
    auto frozen=request(d);frozen.freezeScene();const auto old=frozen.scene;check(d.setLayerBlendMode(1,BlendMode::Screen),"change confirmed scene");
    check(old->layers.back().blend_mode==BlendMode::Difference&&request(d).layers.back().blend_mode==BlendMode::Screen,"immutable snapshot freezes mode");
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
        CompositionDocument d(400,200,{30,1},{shape(2,{64,64,64,255}),source});
        const auto make=[&]{auto r=request(d,1);for(auto& l:r.layers)if(l.id==1){l.still_frame=kind==LayerKind::Image?pixels:nullptr;l.source_frame_rate=source.source_frame_rate;l.source_frame_count=source.source_frame_count;}return r;};
        CompositionFrameRenderer renderer;auto& metrics=motion::diagnostics::PerformanceMetrics::instance();
        check(renderer.render(make())!=nullptr,"warm media/content");(void)metrics.takeSnapshotAndReset();
        check(d.setLayerBlendMode(1,BlendMode::Screen),"mode change");const auto screen=renderer.render(make());const auto counts=metrics.takeSnapshotAndReset();
        check(screen&&counts&&counts->timestamp_seek_attempts==0&&counts->forward_decode_attempts==0&&counts->timings[static_cast<std::size_t>(motion::diagnostics::PreviewTimingStage::TextShapeRasterization)].count==0,"mode change preserves warmed decode/raster caches");
        check(d.setLayerBlendMode(1,BlendMode::Difference),"second mode");const auto different=renderer.render(make());check(different&&different->rgba_pixels!=screen->rgba_pixels,"all source kinds consume the mode");
        for(int stage=-2;stage<=1;++stage)check(d.createMask(1,stage,creative_suite::composition::ShapeMaskKind::Ellipse)!=0,"source stage mask");
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
    CompositionDocument d(400,200,{30,1},{shape(1)});const auto file=root+"/ui.motion";Store::save(path(file),project(d));
    MainWindow w(nullptr,path(root+"/autosave"),"blend");w.resize(1500,1000);w.show();open(w,file);
    auto* rows=widget<QWidget>(w,"motion-timeline-layer-rows");mouse(*rows,QEvent::MouseButtonPress,{120,17},Qt::LeftButton,Qt::LeftButton);mouse(*rows,QEvent::MouseButtonRelease,{120,17},Qt::LeftButton,Qt::NoButton);
    auto* box=widget<QComboBox>(w,"motion-blend-mode");auto* undo=widget<QAction>(w,"motion-undo-action");auto* redo=widget<QAction>(w,"motion-redo-action");
    auto* viewer=widget<CompositionViewer>(w,"motion-composition-viewer");check(box->isEnabled()&&box->count()==6&&box->currentIndex()==0,"selected Inspector modes");
    activate(box,1);check(w.compositionDocument()->layers()[0].blend_mode==BlendMode::Multiply,"Inspector changes model");
    auto* autosave=widget<QTimer>(w,"motion-autosave-timer");QMetaObject::invokeMethod(autosave,"timeout",Qt::DirectConnection);
    motion::persistence::MotionRecoveryStore recovery(path(root+"/autosave"),"reader");const auto saves=recovery.snapshotsForProject(path(file));
    check(!saves.empty()&&Store::loadRecovery(saves.front().path).document.layers[0].blend_mode==BlendMode::Multiply,"autosave preserves confirmed mode");undo->trigger();check(w.compositionDocument()->layers()[0].blend_mode==BlendMode::Normal&&redo->isEnabled(),"one Undo");
    activate(box,0);check(redo->isEnabled(),"same mode preserves Redo");redo->trigger();check(box->currentIndex()==1,"Redo syncs Inspector");
    // Each draft is cancelled before the newly selected mode is applied.
    for(int gesture=0;gesture<4;++gesture) {
        const auto canvas=QRectF(viewer->rect()).adjusted(24,42,-24,-24);QSizeF fit(400,200);fit.scale(canvas.size(),Qt::KeepAspectRatio);
        const auto pivot=canvas.center();const auto handle=pivot+(gesture==0?QPointF(32,0):gesture==1?QPointF(0,-56):gesture==2?QPointF(88,0):QPointF(0,0));
        mouse(*viewer,QEvent::MouseButtonPress,handle,Qt::LeftButton,Qt::LeftButton);
        check(viewer->positionDragActive()||viewer->rotationDragActive()||viewer->scaleDragActive()||viewer->panBehindActive(),"Viewer gesture begins");
        mouse(*viewer,QEvent::MouseMove,handle+QPointF(25,20),Qt::NoButton,Qt::LeftButton);activate(box,gesture+2);
        check(!viewer->positionDragActive()&&!viewer->rotationDragActive()&&!viewer->scaleDragActive()&&!viewer->panBehindActive(),"mode edit cancels draft");
        auto actual=w.compositionDocument()->layers()[0];actual.blend_mode=BlendMode::Normal;check(actual==d.layers()[0],"draft never changes document");
        check(box->currentIndex()==gesture+2,"new selection survives draft resync");
    }
    const auto confirmed=w.compositionDocument()->layers();auto* selection_inspector=w.findChild<InspectorWidget*>();check(selection_inspector,"Inspector exists");
    check(QMetaObject::invokeMethod(selection_inspector,"blendModeChanged",Qt::DirectConnection,Q_ARG(int,255)),"inject invalid selection");
    check(w.compositionDocument()->layers()==confirmed,"invalid selection is atomic");
    QFile technical_log(QString::fromStdU16String(creative_suite::diagnostics::Logger::instance().log_path().u16string()));check(technical_log.open(QIODevice::ReadOnly),"read technical error log");
    const auto errors=technical_log.readAll();check(errors.contains("set_layer_blend_mode")&&errors.contains("blend_mode")&&errors.contains("layer_id"),"technical errors carry operation/layer/mode context");
    widget<QAction>(w,"motion-save-composition-action")->trigger();check(Store::load(path(file)).layers==w.compositionDocument()->layers(),"save confirmed mode");
    widget<QAction>(w,"motion-new-null-layer-action")->trigger();check(!box->isEnabled()&&box->currentIndex()==0,"Null fixed at Normal");undo->trigger();
    auto* inspector=w.findChild<InspectorWidget*>();check(inspector,"Inspector exists");inspector->setBlendModeBusy(true);check(!box->isEnabled(),"busy mode blocked");inspector->setBlendModeBusy(false);
    auto bad=read(file);auto layers=bad.value("layers").toArray();auto record=layers[0].toObject();record.insert("blend_mode","bad");layers[0]=record;bad.insert("layers",layers);write(root+"/invalid.motion",bad);
    QTimer dismiss;QObject::connect(&dismiss,&QTimer::timeout,[&]{if(auto* b=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){b->accept();dismiss.stop();}});dismiss.start(1);
    const auto before=w.compositionDocument()->layers();open(w,root+"/invalid.motion");dismiss.stop();check(w.compositionDocument()->layers()==before,"invalid open keeps document");
    widget<QAction>(w,"motion-save-composition-action")->trigger();w.close();QApplication::processEvents();
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setOrganizationName("CreativeSuiteTests");app.setApplicationName("MotionBlendModes");QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir root;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path());
    try {check(root.isValid()&&creative_suite::diagnostics::Logger::instance().initialize(path(root.path()+"/logs")),"temporary fixture/log");
        modelAndPersistence(root.path());rendering(root.path());sourceKinds(root.path());interface(root.path());std::cout<<"Motion blend modes passed.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
