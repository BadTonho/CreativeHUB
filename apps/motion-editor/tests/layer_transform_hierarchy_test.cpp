#include "model/layer_transform_hierarchy.h"
#include "model/composition_interchange.h"
#include "persistence/motion_document_store.h"
#include "rendering/composition_frame_renderer.h"
#include "export/motion_video_export.h"
#include "application/history/composition_history.h"
#include "ui/main_window/main_window.h"
#include "ui/inspector/inspector_widget.h"
#include "ui/dialogs/audio_keyframe_dialog.h"
#include "ui/timeline/timeline_navigator.h"

#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/media/video_playback.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
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
using Store = motion::persistence::MotionDocumentStore;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F&& action)
{
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, "invalid operation must throw");
}
bool near(double a, double b) { return std::abs(a-b) < 1e-9; }
void samePose(const Transform2D& a, const Transform2D& b)
{
    check(near(a.position_x,b.position_x) && near(a.position_y,b.position_y) &&
        near(a.scale,b.scale) && near(a.rotation_degrees,b.rotation_degrees) && near(a.opacity,b.opacity),
        "parent operations must preserve the current global pose");
}
CompositionLayer shape(LayerId id)
{
    CompositionLayer layer{id, LayerKind::Shape, "Same name"};
    layer.duration_frames = 6;
    auto content = defaultShapeLayerContent({64,32});
    content.width = 12; content.height = 8;
    layer.content = content;
    return layer;
}
CompositionLayer nullLayer(LayerId id)
{
    CompositionLayer layer{id, LayerKind::Null, "Same name"};
    layer.duration_frames = 150;
    return layer;
}
MotionProjectData project(const CompositionDocument& doc)
{
    MotionProjectData result;
    result.composition = {doc.canvasSize(), doc.frameRate()};
    result.layers = doc.layers(); result.masks = doc.masks();
    return result;
}
std::filesystem::path path(const QString& value)
{
    const auto utf8=value.toUtf8();const auto* first=reinterpret_cast<const char8_t*>(utf8.constData());
    return std::filesystem::path(std::u8string(first,first+utf8.size()));
}
void writeJson(const QString& name, const QJsonObject& value)
{
    QFile file(name); check(file.open(QIODevice::WriteOnly), "write test document");
    const auto bytes = QJsonDocument(value).toJson();
    check(file.write(bytes) == bytes.size(), "write complete test document");
}
QJsonObject readJson(const QString& name)
{
    QFile file(name); check(file.open(QIODevice::ReadOnly), "read test document");
    return QJsonDocument::fromJson(file.readAll()).object();
}

void testGeometryAndLifecycle()
{
    auto parent = shape(1), child = shape(2);
    parent.transform = {.5,.5,2,90,.2}; parent.visible=false;
    parent.timeline_start_frame=10; parent.duration_frames=1;
    parent.keyframes.position_x={{0,.5},{20,.75}};
    child.parent_id=1; child.transform={.125,.1,.5,10,.7};
    CompositionDocument doc(400,200,{30,1},{child,parent});
    auto pose = LayerTransformHierarchy(doc.canvasSize(),doc.layers()).evaluate(0).at(2);
    check(near(pose.position_x,.4) && near(pose.position_y,1) && near(pose.scale,1) &&
        near(pose.rotation_degrees,100) && near(pose.opacity,.7), "pixel-space rotation on a non-square canvas");
    pose=LayerTransformHierarchy(doc.canvasSize(),doc.layers()).evaluate(20).at(2);
    check(near(pose.position_x,.525), "inactive hidden parent keeps evaluating its own clock");
    check(doc.moveLayer(1,0), "reorder parent");
    samePose(pose,LayerTransformHierarchy(doc.canvasSize(),doc.layers()).evaluate(20).at(2));
    check(!doc.setLayerParent(1,2,20) && !doc.setLayerParent(2,2,20) &&
        !doc.setLayerParent(2,999,20), "cycles and invalid parent IDs rejected");

    auto offline=shape(7); offline.kind=LayerKind::Image;offline.content={};offline.source_path="missing.png";
    auto offline_child=shape(8);offline_child.parent_id=7;
    check(LayerTransformHierarchy({400,200},{offline_child,offline}).evaluate(10).contains(8),
        "ancestor evaluation does not access media");

    auto a=nullLayer(10), b=nullLayer(20), c=shape(30), grandchild=shape(40);
    a.transform={.2,.3,1.5,27,1}; a.keyframes.rotation={{0,27},{60,70}};
    b.transform={.8,.6,.7,-15,1}; b.keyframes.rotation={{0,-15},{60,45}};
    c.transform={.4,.2,.8,12,.6}; c.keyframes.position_x={{0,.4},{25,.6}};
    grandchild.parent_id=30; grandchild.transform={0,0,1,0,1};
    CompositionDocument animation(640,360,{30,1},{grandchild,c,a,b});
    const auto original_keys=animation.layers()[1].keyframes;
    const auto before=LayerTransformHierarchy(animation.canvasSize(),animation.layers()).evaluate(12);
    check(animation.setLayerParent(30,10,12),"bind animated layer");
    auto bound=LayerTransformHierarchy(animation.canvasSize(),animation.layers()).evaluate(12);
    samePose(before.at(30),bound.at(30)); samePose(before.at(40),bound.at(40));
    check(animation.layers()[1].keyframes==original_keys,"binding preserves authored curves");
    check(!near(LayerTransformHierarchy(animation.canvasSize(),animation.layers()).evaluate(45).at(30).rotation_degrees,
        before.at(30).rotation_degrees), "child follows subsequent parent animation");
    check(animation.setLayerParent(30,20,12),"change parent");
    samePose(before.at(30),LayerTransformHierarchy(animation.canvasSize(),animation.layers()).evaluate(12).at(30));
    const auto before_detach=LayerTransformHierarchy(animation.canvasSize(),animation.layers()).evaluate(31).at(30);
    check(animation.setLayerParent(30,0,31),"clear parent");
    samePose(before_detach,LayerTransformHierarchy(animation.canvasSize(),animation.layers()).evaluate(31).at(30));
    check(animation.layers()[1].parent_offset!=ParentTransformOffset{},"unparent retains compensation");
    check(animation.setLayerParent(30,10,12),"bind again");
    CompositionHistory history; history.recordBeforeEdit({animation,10});
    const auto predelete=animation.layers();
    const auto oldpose=LayerTransformHierarchy(animation.canvasSize(),animation.layers()).evaluate(31);
    check(animation.removeLayer(10,31),"delete parent");
    const auto after=LayerTransformHierarchy(animation.canvasSize(),animation.layers()).evaluate(31);
    samePose(oldpose.at(30),after.at(30)); samePose(oldpose.at(40),after.at(40));
    check(animation.layers()[1].parent_id==0 && animation.layers()[1].keyframes==original_keys,
        "deletion detaches direct child and retains keys");
    auto undone=history.undo({animation,30}); check(undone && undone->document.layers()==predelete,"one undo restores whole deletion");
    auto redone=history.redo(*undone);check(redone && redone->document.layers()==animation.layers(),"redo restores compensated children");

    auto invalid=doc.layers(); invalid[1].parent_id=999;
    rejects([&]{CompositionDocument bad(400,200,{30,1},invalid);});
    invalid=doc.layers(); invalid[0].parent_id=2; // parent 1 now points to child 2
    rejects([&]{CompositionDocument bad(400,200,{30,1},invalid);});
    auto huge=nullLayer(70); huge.transform.scale=1e308; huge.parent_offset.scale=1e308;
    CompositionDocument numerical(64,32,{30,1},{huge,shape(71)});
    const auto untouched=numerical.layers();
    rejects([&]{(void)numerical.setLayerParent(71,70,0);});
    check(numerical.layers()==untouched,"numerical failure leaves graph unchanged");

    std::vector<CompositionLayer> deep;
    for(LayerId i=1;i<=4096;++i){auto n=nullLayer(i);n.transform.position_x=0;n.transform.position_y=0;n.parent_id=i==4096?0:i+1;deep.push_back(n);}
    check(LayerTransformHierarchy({640,360},deep).evaluate(0).size()==4096,"deep forward-reference hierarchy is iterative");

    CompositionDocument nulls(64,32,{30,1});LayerId id=0;
    check(nulls.addContentLayer(LayerKind::Null,"Null",7,&id),"create Null");
    check(nulls.layers()[0].duration_frames==150 && nulls.layers()[0].timeline_start_frame==7,"Null timing defaults");
    TimelineNavigator playback;playback.setCompositionTiming({30,1});
    playback.setLayers({shape(3),nullLayer(4)});playback.setCurrentFrame(6);
    auto* play=playback.findChild<QPushButton*>("motion-timeline-play-pause");check(play,"playback control");
    play->click();check(playback.isPlaying() && playback.currentFrame()==0,"Null does not extend playback");play->click();
    auto transform=nulls.layers()[0].transform;transform.opacity=.5;
    check(!nulls.setLayerTransform(id,transform) && !nulls.setLayerKeyframe(id,TransformProperty::Opacity,0,.5) &&
        !nulls.replaceLayerKeyframes(id,TransformProperty::Opacity,{{0,.5}}) &&
        !nulls.setLayerEffects(id,{GaussianBlurEffect{}}) &&
        !nulls.createMask(id,-2,creative_suite::composition::ShapeMaskKind::Rectangle),"Null rejects visual properties");
}

void testPersistence(const QString& root)
{
    auto parent=nullLayer(1), child=shape(2);
    parent.transform.rotation_degrees=20;
    CompositionDocument doc(64,32,{30,1},{child,parent});
    check(doc.setLayerParent(2,1,0),"bind persisted child");
    auto data=project(doc);const auto file=root+"/hierarchy.motion";
    Store::save(path(file),data);check(Store::load(path(file))==data,"native v10 round trip");
    check(readJson(file).value("version").toInt()==12,"writer uses v11");
    Store::saveRecovery(path(root+"/snapshot"),path(file),"hierarchy",data);
    check(Store::loadRecovery(path(root+"/snapshot")).document==data,"recovery retains graph and compensation");
    check(doc.setLayerParent(2,0,0),"detach persisted child");data=project(doc);
    Store::save(path(file),data);check(Store::load(path(file))==data,"detached compensation persists");
    auto encoded=readJson(file);auto layers=encoded.value("layers").toArray();
    auto bad=layers[0].toObject();bad.insert("parent_id","999");layers[0]=bad;encoded.insert("layers",layers);
    writeJson(root+"/invalid.motion",encoded);
    rejects([&]{(void)Store::load(path(root+"/invalid.motion"));});
    check(readJson(root+"/invalid.motion")==encoded,"invalid input file stays intact");
    bad.insert("parent_id","1");layers[0]=bad;bad=layers[1].toObject();bad.insert("parent_id","2");layers[1]=bad;encoded.insert("layers",layers);
    writeJson(root+"/cycle.motion",encoded);rejects([&]{(void)Store::load(path(root+"/cycle.motion"));});
    auto legacy=project(CompositionDocument(64,32,{30,1},{shape(2)}));
    Store::save(path(root+"/legacy.motion"),legacy);const auto base=readJson(root+"/legacy.motion");
    for(int version=1;version<=9;++version){auto old=base;old.insert("version",version);writeJson(root+"/old.motion",old);
        const auto loaded=Store::load(path(root+"/old.motion"));check(loaded.layers[0].parent_id==0 &&
            loaded.layers[0].parent_offset==ParentTransformOffset{},"v1-v9 migrate with identity parenting");}
    auto oldNull=readJson(file);oldNull.insert("version",9);writeJson(root+"/old-null.motion",oldNull);
    rejects([&]{(void)Store::load(path(root+"/old-null.motion"));});
    rejects([&]{(void)exportComposition(data);});
    data=legacy;data.layers[0].parent_offset.translation_x_pixels=3;
    rejects([&]{(void)exportComposition(data);});
    data=legacy;data.layers[0].parent_id=7;rejects([&]{(void)exportComposition(data);});
}

PreviewRequest requestFor(const CompositionDocument& doc, std::int64_t frame, bool hierarchical)
{
    PreviewRequest request;request.canvas_size=doc.canvasSize();request.frame_rate=doc.frameRate();
    request.composition_frame=frame;request.black_canvas_when_empty=true;
    request.masks=std::make_shared<const std::vector<Mask>>(doc.masks());
    if(hierarchical)request.hierarchy=std::make_shared<const LayerTransformHierarchy>(doc.canvasSize(),doc.layers());
    for(const auto& layer:doc.layers())if(layer.kind!=LayerKind::Null){
        PreviewLayerSnapshot s;s.id=layer.id;s.kind=layer.kind;s.content=layer.content;s.transform=layer.transform;
        s.local_frame=frame-layer.timeline_start_frame;s.keyframes=layer.keyframes;s.effects=layer.effects;
        s.source_mask=layer.source_mask;s.transform_mask=layer.transform_mask;request.layers.push_back(s);}
    return request;
}
void testRenderingAndExport(const QString& root)
{
    auto parent=nullLayer(1);parent.visible=false;parent.timeline_start_frame=20;
    parent.transform={0,0,1,0,1};parent.keyframes.position_x={{0,.125},{3,.25}};
    auto child=shape(2);child.parent_id=1;child.transform={.375,.5,1,0,1};
    CompositionDocument doc(64,32,{30,1},{child,parent});
    check(doc.setLayerEffects(2,{GaussianBlurEffect{true,1},ColorAdjustmentEffect{true,10}}),"render effects");
    const auto mask=doc.createMask(2,-1,creative_suite::composition::ShapeMaskKind::Ellipse);
    check(mask!=0,"render transform mask");
    CompositionFrameRenderer renderer(false);
    for(const auto frame:{0,21,23}){
        const auto actual=renderer.render(requestFor(doc,frame,true));
        auto expectedRequest=requestFor(doc,frame,false);expectedRequest.layers[0].transform.position_x=
            .375+(frame<=20?.125:frame==21?(.125+(.125/3)):.25);
        const auto expected=renderer.render(expectedRequest);
        check(actual && expected && actual->rgba_pixels==expected->rgba_pixels,"inherited transform uses existing effect/mask stage");
    }
    auto unparented=CompositionDocument(64,32,{30,1},{shape(5)});
    const auto old=renderer.render(requestFor(unparented,0,false)), current=renderer.render(requestFor(unparented,0,true));
    check(old && current && old->rgba_pixels==current->rgba_pixels,"unparented pixels unchanged");
    auto onlyNull=requestFor(CompositionDocument(64,32,{30,1},{nullLayer(9)}),0,true);
    onlyNull.black_canvas_when_empty=false;
    check(!renderer.render(onlyNull),"Nulls create no preview frame");

    const auto containers=creative_suite::media::availableVideoContainers();
    const auto container=std::find_if(containers.begin(),containers.end(),[](const auto& c){return c.name=="matroska";});
    check(container!=containers.end(),"Matroska is available");
    const auto encoder=std::find_if(container->video_encoders.begin(),container->video_encoders.end(),[](const auto& e){return e.name=="ffv1";});
    check(encoder!=container->video_encoders.end(),"lossless FFV1 is available");
    check(doc.moveLayerInTimeline(1,0),"animate export parent from frame zero");
    MotionExportSnapshot snapshot{doc.canvasSize(),doc.frameRate(),doc.layers(),{},doc.masks()};
    const MotionExportSettings settings{path(root+"/hierarchy.mkv"),"matroska","ffv1",64,32,{30,1},1};
    std::atomic_bool cancel{false};MotionExportPerformanceSummary summary;
    MotionVideoExporter::exportVideo(snapshot,settings,cancel,{},&summary);
    check(summary.frames_rendered==6,"long Null does not extend export");
    // FFV1 is lossless in its selected YUV420P format, but RGB conversion has
    // chroma subsampling. Compare against an independently authored, unparented
    // composition through the same encoder instead of assuming lossless RGB.
    auto reference=snapshot;reference.layers.resize(1);reference.layers[0].parent_id=0;
    reference.layers[0].transform.position_x=.5;
    reference.layers[0].keyframes.position_x={{0,.5},{3,.625}};
    const CompositionDocument reference_doc(64,32,{30,1},reference.layers,reference.masks);
    auto reference_settings=settings;reference_settings.output_path=path(root+"/reference.mkv");
    MotionVideoExporter::exportVideo(reference,reference_settings,cancel);
    auto decoder=creative_suite::media::VideoPlaybackSession::open(settings.output_path);
    auto reference_decoder=creative_suite::media::VideoPlaybackSession::open(reference_settings.output_path);
    std::int64_t count=0;
    while(const auto decoded=decoder->decode_next_frame()){
        const auto preview=renderer.render(requestFor(doc,count,true));
        const auto reference_preview=renderer.render(requestFor(reference_doc,count,false));
        const auto reference_frame=reference_decoder->decode_next_frame();
        check(preview && (*decoded)->width==64 && (*decoded)->height==32,"export geometry");
        check(reference_preview && preview->rgba_pixels==reference_preview->rgba_pixels,
            "animated hierarchy preview matches explicit geometry");
        check(reference_frame && (*decoded)->rgba_pixels==(*reference_frame)->rgba_pixels,
            "hierarchy export matches independently authored geometry after encoding");
        ++count;
    }
    check(count==6,"exported frame count");
    snapshot.layers={nullLayer(99)};
    rejects([&]{MotionVideoExporter::exportVideo(snapshot,settings,cancel);});
    check(std::filesystem::is_regular_file(settings.output_path),"Null-only export preserves existing destination");
}

template<class T> T* widget(QObject& owner,const char* name)
{
    auto* value=owner.findChild<T*>(QString::fromLatin1(name));check(value!=nullptr,name);return value;
}
void testUi(const QString& root)
{
    MainWindow window(nullptr,path(root+"/recovery"),"hierarchy-ui");window.show();
    QTimer::singleShot(0,[]{auto* dialog=QApplication::activeModalWidget();check(dialog,"new composition dialog");
        widget<QLineEdit>(*dialog,"motion-canvas-width")->setText("640");
        widget<QLineEdit>(*dialog,"motion-canvas-height")->setText("360");
        widget<QComboBox>(*dialog,"motion-frame-rate")->setCurrentIndex(4);
        widget<QDialogButtonBox>(*dialog,"motion-new-composition-buttons")->button(QDialogButtonBox::Ok)->click();});
    widget<QAction>(window,"motion-new-composition-action")->trigger();
    auto* create=widget<QAction>(window,"motion-new-null-layer-action");check(create->shortcut().isEmpty(),"no default shortcut");create->trigger();
    const auto parent=window.compositionDocument()->layers()[0].id;
    check(!widget<QDoubleSpinBox>(window,"motion-transform-opacity")->isEnabled(),"Null opacity disabled");
    check(!widget<QAction>(window,"motion-export-video-action")->isEnabled(),"Null-only export disabled");
    AudioKeyframeDialog audio("Null",nullptr,false);
    check(widget<QComboBox>(audio,"motion-audio-keyframe-property")->findData(int(TransformProperty::Opacity))<0,"Null audio bake excludes opacity");
    auto* timeline=window.findChild<TimelineNavigator*>();check(timeline,"timeline exists");
    widget<QToolButton>(window,"motion-transform-keyframe-rotation")->click();
    timeline->setCurrentFrame(15);
    widget<QToolButton>(window,"motion-transform-keyframe-rotation")->click();
    widget<QDoubleSpinBox>(window,"motion-transform-rotation")->setValue(60);
    check(window.compositionDocument()->layers()[0].keyframes.rotation.size()==2,"Null rotation keyframes editable through Inspector");
    widget<QAction>(window,"motion-new-rectangle-layer-action")->trigger();
    const auto child=window.compositionDocument()->layers().back().id;
    auto* combo=widget<QComboBox>(window,"motion-parent-layer");
    check(combo->count()==2 && combo->findData(QVariant::fromValue<qulonglong>(child))<0,"selector excludes self");
    const auto index=combo->findData(QVariant::fromValue<qulonglong>(parent));check(index>0,"Null eligible as parent");
    combo->setCurrentIndex(index);check(QMetaObject::invokeMethod(combo,"activated",Q_ARG(int,index)),"activate parent selector");
    check(window.compositionDocument()->layers().back().parent_id==parent,"Inspector changes parent");
    widget<QAction>(window,"motion-undo-action")->trigger();check(window.compositionDocument()->layers().back().parent_id==0,"Undo parent selection");
    widget<QAction>(window,"motion-redo-action")->trigger();check(window.compositionDocument()->layers().back().parent_id==parent,"Redo parent selection");
    InspectorWidget inspector; inspector.syncParenting(window.compositionDocument(),parent);
    check(widget<QComboBox>(inspector,"motion-parent-layer")->count()==1,"selector excludes descendants");
    CompositionDocument duplicates(64,32,{30,1},{nullLayer(10),nullLayer(20),shape(30)});
    inspector.syncParenting(&duplicates,30);auto* duplicate_choices=widget<QComboBox>(inspector,"motion-parent-layer");
    check(duplicate_choices->count()==3 && duplicate_choices->itemText(1)!=duplicate_choices->itemText(2),"duplicate names distinguished by IDs");
    auto* rows=widget<QWidget>(window,"motion-timeline-layer-rows");
    timeline->setCurrentFrame(30);timeline->setLayerExpanded(parent,false);timeline->setLayerExpanded(child,false);
    const auto predelete=LayerTransformHierarchy({640,360},window.compositionDocument()->layers()).evaluate(30).at(child);
    QTimer::singleShot(0,[]{auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());check(menu,"layer context menu");
        widget<QAction>(*menu,"motion-context-remove-layer-action")->trigger();menu->close();});
    QContextMenuEvent context(QContextMenuEvent::Mouse,QPoint(100,51),rows->mapToGlobal(QPoint(100,51)));
    QApplication::sendEvent(rows,&context);
    check(window.compositionDocument()->layers().size()==1 && window.compositionDocument()->layers()[0].id==child &&
        window.compositionDocument()->layers()[0].parent_id==0,"context menu detaches surviving child");
    samePose(predelete,LayerTransformHierarchy({640,360},window.compositionDocument()->layers()).evaluate(30).at(child));
    widget<QAction>(window,"motion-undo-action")->trigger();
    check(window.compositionDocument()->layers().size()==2 && window.compositionDocument()->layers().back().parent_id==parent,"one UI Undo restores parent and binding");
    QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(rows,&remove);
    check(window.compositionDocument()->layers().size()==1 && window.compositionDocument()->layers()[0].id==child,"timeline deletes selected parent");
    samePose(predelete,LayerTransformHierarchy({640,360},window.compositionDocument()->layers()).evaluate(30).at(child));
    widget<QAction>(window,"motion-undo-action")->trigger();check(window.compositionDocument()->layers().size()==2,"Undo deletion restores graph");
    QTimer::singleShot(0,[]{auto* prompt=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());if(prompt)prompt->button(QMessageBox::Discard)->click();});
    window.close();
}
}

int main(int argc,char** argv)
{
    QApplication app(argc,argv);app.setOrganizationName("CreativeSuiteTests");app.setApplicationName("MotionHierarchy");
    QStandardPaths::setTestModeEnabled(true);QTemporaryDir temporary;
    if(!temporary.isValid())return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
    try {
        check(creative_suite::diagnostics::Logger::instance().initialize(path(temporary.path()+"/logs")),"initialize test log");
        testGeometryAndLifecycle();testPersistence(temporary.path());testRenderingAndExport(temporary.path());testUi(temporary.path());
        std::cout<<"Motion hierarchy tests passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
