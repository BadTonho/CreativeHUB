#include "composition_interchange_test_support.h"
#include "export/motion_video_export.h"
#include "ui/inspector/mask_inspector_widget.h"
#include "application/history/composition_history.h"
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QListWidget>
#include <QJsonArray>
#include <QFile>
#include <QDir>
namespace c=creative_suite::composition;
namespace a=creative_suite::animation;
int main(int argc,char** argv) try {
    QApplication app(argc,argv);QTemporaryDir temp;check(temp.isValid(),"temporary directory");
    const auto dir=std::filesystem::path(temp.path().toStdU16String());
    x::Document base;base.width=160;base.height=90;base.duration=60;
    x::Layer background;background.id=1;background.color={60,140,220,137};background.presentation.duration=60;
    background.transform={.57,.43,.82,17,.65};background.effects={x::ColorEffect{{12,130,70}},x::ColorEffect{{50,120,80},false}};base.layers={background};
    check(x::toJson(base,dir/"v1.cscomp").value("version")==1,"unmasked exports remain v1");
    auto zero_bindings=base;
    check(x::fromJson(x::toJson(zero_bindings,dir/"zero.cscomp"),dir/"zero.cscomp").masks.empty(),"zero-only bindings remain readable v1");
    for(auto kind:{c::ShapeMaskKind::Rectangle,c::ShapeMaskKind::Ellipse})for(int target:{-2,0,-1}) {
        auto d=base;x::Mask mask;mask.id=17;mask.kind=kind;mask.parameters={.45,.55,.7,.6,23,.12,false};
        check(a::setKeyframe(mask.keyframes[0],0,.2)&&a::setKeyframe(mask.keyframes[0],40,.7),"mask keys");
        mask.keyframes[0][0].interpolation=a::InterpolationMode::CubicBezier;mask.keyframes[0][0].easing={.2,.1,.8,.9};d.masks={mask};
        if(target==-2)d.layers[0].source_mask=mask.id;
        else if(target==-1)d.layers[0].transform_mask=mask.id;
        else for(auto& e:d.layers[0].effects)std::visit([&](auto& v){v.mask=mask.id;},e);
        auto motion_project=motion::model::importComposition(d);
        for(int frame:{0,13,49})compare(d,motion_project,frame,"masked Fusion/Motion parity");
        auto graph=application::importComposition(d);timeline::TimelineClip clip;
        clip.kind=timeline::ClipKind::FusionComposition;clip.timeline_duration_frames=d.duration;clip.node_graph=graph;
        auto back=application::exportComposition(clip,d.width,d.height,{30,1});
        check(back.masks.size()==1 && back.masks[0].parameters==mask.parameters && back.masks[0].keyframes==mask.keyframes,"editable Fusion mask round trip");
        auto json=x::toJson(d,dir/"mask.cscomp");check(json.value("version")==2,"masked export v2");
        auto decoded=x::fromJson(json,dir/"mask.cscomp");check(decoded.masks==d.masks,"mask JSON round trip");
        json.insert("version",1);rejected([&]{(void)x::fromJson(json,dir/"old.cscomp");},"v1 rejects masks");
        const auto path=dir/"masked.motion";motion::persistence::MotionDocumentStore::save(path,motion_project);
        check(motion::persistence::MotionDocumentStore::load(path)==motion_project,"native v8 masks");
        motion::persistence::MotionDocumentStore::saveRecovery(dir/"recovery.motion",{},"mask-test",motion_project);
        check(motion::persistence::MotionDocumentStore::loadRecovery(dir/"recovery.motion").document==motion_project,"mask recovery");
        for(auto& m:d.masks){m.parameters.width=0;m.parameters.invert=true;m.keyframes={};}
        auto empty=motion::model::importComposition(d);compare(d,empty,15,"inverted zero geometry");
    }
    auto shared=base;shared.layers[0].transform={};shared.layers[0].source_mask=8;std::get<x::ColorEffect>(shared.layers[0].effects[0]).mask=8;
    x::Mask definition;definition.id=8;shared.masks={definition};
    for(int target:{-2,0,-1}) {
        auto text=shared;text.layers[0].kind=x::Kind::Text;
        text.layers[0].text={"Shared\nMask","Sans Serif",24.5,1,{180,90,240,151}};
        text.layers[0].source_mask=0;for(auto& e:text.layers[0].effects)std::visit([](auto& v){v.mask=0;},e);text.layers[0].transform_mask=0;
        if(target==-2)text.layers[0].source_mask=8;
        else if(target==0)std::get<x::ColorEffect>(text.layers[0].effects[0]).mask=8;
        else text.layers[0].transform_mask=8;
        compare(text,motion::model::importComposition(text),13,"masked multiline text parity");
    }
    auto excessive=shared;excessive.masks.resize(4097,definition);
    const auto excessive_issues=x::validate(excessive);
    check(std::any_of(excessive_issues.begin(),excessive_issues.end(),[](const auto& issue){return issue.reason=="Too many masks.";}),"mask collection limit");
    excessive=shared;excessive.layers[0].effects.resize(16380);
    const auto stage_issues=x::validate(excessive);
    check(std::any_of(stage_issues.begin(),stage_issues.end(),[](const auto& issue){return issue.reason=="The composition exceeds the 16384-stage interchange limit.";}),"masks count toward graph stage budget");
    auto invalid_geometry=definition;invalid_geometry.parameters.soft_edge=1.01;
    check(!x::validMask(invalid_geometry),"invalid mask geometry rejected");
    auto imported=motion::model::importComposition(shared);
    motion::model::CompositionDocument doc(160,90,{30,1},imported.layers,imported.masks);
    check(doc.maskUseCount(8)==2,"shared mask identity");
    auto saved=doc;
    check(doc.setMaskKeyframe(8,0,10,.2)&&doc.setMaskKeyframe(8,0,40,.8),"model mask keys");
    check(doc.setMaskKeyframeInterpolation(8,0,10,a::InterpolationMode::CubicBezier,{.2,.1,.8,.9}),"model mask easing");
    check(doc.moveLayerInTimeline(1,20)&&doc.findMask(8)->keyframes[0][0].frame==10,"moving layer retains composition mask times");
    check(doc.associateMask(1,-2,0)&&doc.findMask(8)&&doc.maskUseCount(8)==1,"unbind retains other users");
    auto effects=doc.layers()[0].effects;std::reverse(effects.begin(),effects.end());
    check(doc.setLayerEffects(1,effects)&&doc.maskForTarget(1,1)==8,"effect reordering preserves association");
    check(doc.associateMask(1,1,0)&&doc.masks().empty(),"last association removes definition");
    check(saved.masks().size()==1,"independent document copy");
    motion::ui::CompositionHistory history;auto before=saved;
    check(saved.setMaskParameters(8,{.3,.4,.7,.8,10,.2,true}),"edit shared mask");history.recordBeforeEdit({before,1});
    auto undo=history.undo({saved,1});check(undo && undo->document.masks()==before.masks(),"mask undo");
    auto redo=history.redo(*undo);check(redo && redo->document.masks()==saved.masks(),"mask redo");
    x::Document gpu_doc=base;gpu_doc.layers[0].effects.clear();gpu_doc.layers[0].transform={};gpu_doc.layers[0].source_mask=8;gpu_doc.masks={definition};
    auto cpu=fusionFrame(gpu_doc,0),gpu=fusionFrame(gpu_doc,0,1);int error=0;
    for(std::size_t i=0;i<cpu->rgba_pixels.size();++i)error=std::max(error,std::abs(int(cpu->rgba_pixels[i])-int(gpu->rgba_pixels[i])));
    check(error<=2,"mask GPU parity");
    auto native_gpu=motion::model::importComposition(gpu_doc);bool used=false;
    const auto motion_gpu=motionFrame(native_gpu,0,true,&used);
    const auto motion_cpu=motionFrame(native_gpu,0);check(motion_gpu && motion_cpu,"Motion masked GPU frame");
    if(QOpenGLContext::supportsThreadedOpenGL())check(used,"Motion source mask must use available GPU");
    for(std::size_t i=0;i<motion_cpu->rgba_pixels.size();++i)check(std::abs(int(motion_cpu->rgba_pixels[i])-int(motion_gpu->rgba_pixels[i]))<=2,"Motion GPU mask parity");
    native_gpu.layers[0].effects={motion::model::GaussianBlurEffect{true,3,8}};
    const auto blur_cpu=motionFrame(native_gpu,0),blur_gpu=motionFrame(native_gpu,0,true,&used);
    check(blur_cpu && blur_gpu,"masked blur frames");
    if(QOpenGLContext::supportsThreadedOpenGL())check(used,"masked Gaussian Blur must use available GPU");
    for(std::size_t i=0;i<blur_cpu->rgba_pixels.size();++i)check(std::abs(int(blur_cpu->rgba_pixels[i])-int(blur_gpu->rgba_pixels[i]))<=2,"masked blur GPU parity");
    check(std::holds_alternative<x::BlurEffect>(motion::model::exportComposition(native_gpu).layers[0].effects[0]),"editable Blur interchange");
    auto export_doc=gpu_doc;export_doc.layers[0].presentation.start=2;export_doc.layers[0].presentation.duration=4;
    check(a::setKeyframe(export_doc.masks[0].keyframes[0],0,.2)&&a::setKeyframe(export_doc.masks[0].keyframes[0],5,.8),"export mask composition keys");
    auto export_project=motion::model::importComposition(export_doc);
    motion::ui::MotionExportSnapshot export_snapshot;export_snapshot.canvas_size={160,90};export_snapshot.frame_rate={30,1};export_snapshot.layers=export_project.layers;export_snapshot.masks=export_project.masks;
    const auto containers=creative_suite::media::availableVideoContainers();
    const auto container=std::find_if(containers.begin(),containers.end(),[](const auto& v){return v.name=="matroska";});
    check(container!=containers.end()&&creative_suite::media::supportsVideoEncoder(*container,"ffv1"),"FFV1 export fixture encoder");
    motion::ui::MotionExportSettings settings{dir/"masked-export.mkv","matroska","ffv1",160,90,{30,1},1.0};std::atomic_bool not_canceled{false};
    motion::ui::MotionVideoExporter::exportVideo(export_snapshot,settings,not_canceled);
    auto decoder=creative_suite::media::VideoPlaybackSession::open(settings.output_path);std::vector<creative_suite::media::VideoFramePtr> exported;
    while(auto frame=decoder->decode_next_frame())exported.push_back(*frame);
    const auto sample=std::size_t(45*160+64)*4;
    check(exported.size()==6 && exported[2]->rgba_pixels[sample]>15 && exported[5]->rgba_pixels[sample]<10,"export propagates masks and global composition time through source windows");
    auto image_doc=shared;image_doc.layers[0].presentation.profile=x::Profile::MotionLayer;
    image_doc.layers[0].kind=x::Kind::Image;image_doc.layers[0].source=dir/"source.png";image_doc.layers[0].source_mask=0;for(auto& e:image_doc.layers[0].effects)std::visit([](auto& v){v.mask=0;},e);image_doc.layers[0].transform_mask=8;
    QImage image(37,21,QImage::Format_RGBA8888);image.fill(QColor(130,70,230,111));check(image.save(QString::fromStdU16String(image_doc.layers[0].source.u16string())),"synthetic source");
    compare(image_doc,motion::model::importComposition(image_doc),20,"masked transform intrinsic/canvas geometry");
    auto invalid=shared;invalid.layers[0].source_mask=123;check(!x::validate(invalid).empty(),"unknown mask rejected");
    invalid=shared;invalid.masks.push_back(definition);check(!x::validate(invalid).empty(),"duplicate IDs rejected");
    auto graph=application::importComposition(shared);const auto mask_node=std::find_if(graph.nodes.begin(),graph.nodes.end(),[](const auto& n){return fusion::nodes::isMaskNode(n.type);});
    const auto output=std::find_if(graph.nodes.begin(),graph.nodes.end(),[](const auto& n){return n.type==fusion::nodes::NodeType::Output;});
    graph.connections.push_back({mask_node->id,output->id,2});timeline::TimelineClip incompatible;incompatible.kind=timeline::ClipKind::FusionComposition;incompatible.timeline_duration_frames=60;incompatible.node_graph=graph;
    rejected([&]{(void)application::exportComposition(incompatible,160,90,{30,1});},"Output mask refused");
    auto curve=definition.keyframes[0];a::setKeyframe(curve,0,.1);a::setKeyframe(curve,50,.9);curve[0].interpolation=a::InterpolationMode::CubicBezier;curve[0].easing={.2,.1,.8,.9};
    const auto slice=a::sliceScalarCurve(curve,13,22,.5);
    for(int f=0;f<22;++f)check(std::abs(a::evaluateScalar(.5,slice,f)-a::evaluateScalar(.5,curve,f+13))<1e-8,"shared Bezier slice");
    auto animated=shared;animated.masks[0].keyframes[0]=curve;
    application::EditorSession session;application::TimelineCommandService commands(session);
    application::AddFusionCompositionCommand insertion{session.timeline().tracks().front().track_id,10,60};insertion.graph=application::importComposition(animated);
    auto inserted=commands.execute(std::move(insertion));check(inserted.changed(),"insert animated masks");
    check(commands.execute(application::SplitClipCommand{*inserted.selection.active_clip_id,20}).changed(),"split mask composition");
    const auto right=application::exportComposition(session.timeline().tracks().front().clips.back(),160,90,{30,1});
    for(int f:{0,13,39})check(std::abs(a::evaluateScalar(right.masks[0].parameters.center_x,right.masks[0].keyframes[0],f)-a::evaluateScalar(.5,curve,f+20))<1e-8,"interchange Bezier masks after split");
    const auto rescaled=timeline::TimelineModel::rescaleSnapshotFrameRate(session.timeline().snapshot(),{60,1});check(rescaled.has_value(),"mask FPS conversion");
    const auto& resized_mask=*std::find_if(rescaled->tracks.front().clips.front().node_graph->nodes.begin(),rescaled->tracks.front().clips.front().node_graph->nodes.end(),[](const auto& n){return fusion::nodes::isMaskNode(n.type);});
    check(resized_mask.mask_keyframes[0].back().frame>=38,"mask key times rescale");
    motion::ui::PreviewRequest view;view.canvas_size={160,90};view.frame_rate={30,1};view.masks=std::make_shared<const std::vector<motion::model::Mask>>(shared.masks.begin(),shared.masks.end());view.mask_view_layer=1;view.mask_view_target=-2;
    motion::ui::PreviewLayerSnapshot inactive;inactive.id=1;inactive.kind=motion::model::LayerKind::Shape;inactive.content=motion::model::ShapeLayerContent{motion::model::ShapeKind::Rectangle,160,90};inactive.source_mask=8;view.layers={inactive};
    motion::ui::CompositionFrameRenderer view_renderer(false);
    auto viewed=view_renderer.render(view,{},true);check(viewed && viewed->rgba_pixels[0]==0 && viewed->rgba_pixels[(45*160+80)*4]==255,"VIEW renders coverage without source sampling");
    check(!view_renderer.render(view,[]{return true;},true),"cancel masked evaluation");
    QOffscreenSurface resource_surface;QSurfaceFormat resource_format;resource_format.setVersion(3,2);resource_format.setProfile(QSurfaceFormat::CoreProfile);resource_surface.setFormat(resource_format);resource_surface.create();
    motion::ui::CompositionGpuMetrics resources;
    motion::ui::CompositionFrameRenderer bounded_renderer(false,true,&resource_surface,&resources);
    auto crowded=view;crowded.mask_view_layer=0;crowded.layers.clear();
    for(std::uint64_t id=1;id<=4;++id){auto layer=inactive;layer.id=id;crowded.layers.push_back(layer);}
    const auto recovered=bounded_renderer.render(crowded,{},true);check(recovered!=nullptr,"bounded mask pool recovers on CPU");
    auto subsequent=crowded;subsequent.layers.resize(1);
    check(bounded_renderer.render(subsequent,{},true)!=nullptr,"subsequent masked frame");
    if(QOpenGLContext::supportsThreadedOpenGL())check(resources.composition_frames>0,"resource fallback must not disable subsequent GPU frames");
    bounded_renderer.shutdown();
    c::ShapeMaskRasterCache cache;check(!cache.coverage(c::ShapeMaskKind::Ellipse,{},8192,8192),"mask buffer limit");
    check(!cache.coverage(c::ShapeMaskKind::Ellipse,{},160,90,[]{return true;}),"raster cancellation");
    auto one=cache.coverage(c::ShapeMaskKind::Rectangle,{},160,90);check(one && one==cache.coverage(c::ShapeMaskKind::Rectangle,{},160,90),"reuse equal coverage");
    motion::ui::MaskInspectorWidget inspector;motion::model::CompositionDocument ui_doc(160,90,{30,1},imported.layers,imported.masks);
    int edits=0,views=0,curves=0;
    inspector.setHandlers([&](auto edit){if(edit(ui_doc))++edits;inspector.sync(&ui_doc,1,0);},[&](auto,int,bool){++views;},[&](auto,std::size_t,auto){++curves;});
    check(ui_doc.setMaskParameters(8,{.123456789,.5,.5,.5,0,0,false}),"precise imported mask geometry");
    inspector.sync(&ui_doc,1,0);
    inspector.findChild<QPushButton*>("motion-mask-apply")->click();
    check(edits==0 && ui_doc.findMask(8)->parameters.center_x==.123456789,"unchanged Inspector fields retain imported precision");
    inspector.findChild<QDoubleSpinBox*>("motion-mask-value-0")->setValue(25);
    inspector.findChild<QPushButton*>("motion-mask-apply")->click();
    check(edits==1&&ui_doc.findMask(8)->parameters.center_x==.25,"Inspector shared geometry edit");
    inspector.findChild<QPushButton*>("motion-mask-key-0")->click();check(curves==1&&!ui_doc.findMask(8)->keyframes[0].empty(),"Inspector mask keyframe");
    inspector.findChild<QPushButton*>("motion-mask-view")->click();check(views==1,"VIEW presentation only");
    if(qEnvironmentVariableIsSet("CREATIVE_SUITE_TEST_ARTIFACT_DIR")) {
        QDir directory(qEnvironmentVariable("CREATIVE_SUITE_TEST_ARTIFACT_DIR"));check(directory.mkpath("."),"UI artifact directory");
        inspector.resize(320,720);inspector.show();QApplication::processEvents();
        check(inspector.grab().save(directory.filePath("motion-mask-inspector.png")),"mask Inspector visual artifact");
    }
    auto version=QJsonDocument::fromJson([&]{QFile f(QString::fromStdU16String((dir/"masked.motion").u16string()));f.open(QIODevice::ReadOnly);return f.readAll();}()).object();
    version.insert("version",7);{QFile f(QString::fromStdU16String((dir/"old.motion").u16string()));f.open(QIODevice::WriteOnly);f.write(QJsonDocument(version).toJson());}
    rejected([&]{(void)motion::persistence::MotionDocumentStore::load(dir/"old.motion");},"native v7 rejects masks");
    std::cout<<"Mask interchange passed\n";return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
