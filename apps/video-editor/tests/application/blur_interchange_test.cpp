#include "composition_interchange_test_support.h"
#include "workspaces/fusion/ui/fusion_workspace.h"
#include "ui/effects/effects_catalog.h"
#include "application/project_open_service.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSettings>
#include <QFile>
#include <QJsonArray>

namespace {
void imageNear(const creative_suite::media::RgbaFramePtr& a,
               const creative_suite::media::RgbaFramePtr& b,const char* message) {
    check(a && b && a->width==b->width && a->height==b->height,"frame geometry");
    int maximum=0;for(std::size_t i=0;i<a->rgba_pixels.size();++i)
        maximum=std::max(maximum,std::abs(int(a->rgba_pixels[i])-int(b->rgba_pixels[i])));
    std::cout<<message<<": maximum channel difference "<<maximum<<'\n';check(maximum<=2,message);
}
void contract(const x::Document& d,const std::filesystem::path& dir) {
    const auto path=dir/"blur.cscomp";check(x::save(path,d),"publish v3");
    auto loaded=x::load(path);check(loaded && loaded->layers[0].effects.size()==3,"read v3 order");
    auto json=x::toJson(d,path);check(json["version"]==3,"Blur selects v3");
    auto layer=json["layers"].toArray()[0].toObject();check(layer.contains("effects")&&!layer.contains("colors")&&!layer.contains("color_masks"),"v3 typed effects only");
    for(int old:{1,2,4}) {auto invalid=json;invalid["version"]=old;rejected([&]{(void)x::fromJson(invalid,path);},"version/representation rejection");}
    auto invalid=json;layer["colors"]=QJsonArray{};invalid["layers"]=QJsonArray{layer};
    rejected([&]{(void)x::fromJson(invalid,path);},"conflicting representations rejected");
    layer.remove("colors");auto effects=layer["effects"].toArray();auto blur=effects[1].toObject();
    for(const auto* field:{"parameters","enabled"}) {auto bad=blur;bad.remove(field);auto list=effects;list[1]=bad;auto item=layer;item["effects"]=list;invalid["layers"]=QJsonArray{item};rejected([&]{(void)x::fromJson(invalid,path);},"missing Blur properties");}
    for(double radius:{-1.,100.1}) {auto bad=d;std::get<x::BlurEffect>(bad.layers[0].effects[1]).radius_pixels=radius;check(!x::validate(bad).empty(),"invalid radius");}
    auto bad=d;std::get<x::BlurEffect>(bad.layers[0].effects[1]).mask=999;check(!x::validate(bad).empty(),"dangling Blur mask");
    auto no_mask=d;no_mask.masks.clear();for(auto& e:no_mask.layers[0].effects)std::visit([](auto& v){v.mask=0;},e);
    std::get<x::BlurEffect>(no_mask.layers[0].effects[1]).enabled=false;
    check(x::toJson(no_mask,path)["version"]==3,"disabled Blur still requires v3");
    no_mask.layers[0].effects.erase(no_mask.layers[0].effects.begin()+1);
    check(x::toJson(no_mask,path)["version"]==1,"no Blur or masks remains v1");
    auto legacy=x::toJson(no_mask,path);legacy["version"]=2;
    auto legacy_layer=legacy["layers"].toArray()[0].toObject();legacy_layer["color_masks"]=QJsonArray{};legacy["layers"]=QJsonArray{legacy_layer};
    check(x::fromJson(legacy,path).layers[0].effects.size()==2,"legacy v2 empty binding arrays retain their prior meaning");
    check(!x::save(path,d,[]{return true;}) && x::load(path)->layers[0].effects.size()==3,"cancel preserves file");
    check(!x::load(path,[]{return true;}),"cancel read");
}
void nativeAndEditing(x::Document d,const std::filesystem::path& dir) {
    // Generated content avoids making persistence depend on external media discovery.
    d.layers[0].kind=x::Kind::Text;d.layers[0].source.clear();d.layers[0].presentation.profile=x::Profile::FusionCanvas;
    d.layers[0].text.content="Blur";d.layers[0].text.size=22;
    auto project=motion::model::importComposition(d);
    const auto motion_path=dir/"blur.motion";motion::persistence::MotionDocumentStore::save(motion_path,project);
    check(motion::persistence::MotionDocumentStore::load(motion_path)==project,"Motion v8 roundtrip");
    motion::persistence::MotionDocumentStore::saveRecovery(dir/"recovery.motion",{},"blur",project);
    check(motion::persistence::MotionDocumentStore::loadRecovery(dir/"recovery.motion").document==project,"Motion recovery");
    application::EditorSession session;application::TimelineCommandService commands(session);
    application::AddFusionCompositionCommand add{session.timeline().tracks()[0].track_id,0,d.duration};add.graph=application::importComposition(d);
    auto result=commands.execute(std::move(add));check(result.changed(),"insert Blur graph");const auto id=*result.selection.active_clip_id;
    const auto original=*session.timeline().tracks()[0].clips[0].node_graph;
    check(commands.execute(application::DuplicateFusionCompositionCommand{id}).changed(),"duplicate Blur");
    const auto duplicate=session.timeline().tracks()[0].clips[1].clip_id;auto changed=original;
    auto blur=std::find_if(changed.nodes.begin(),changed.nodes.end(),[](const auto& n){return n.type==fusion::nodes::NodeType::GaussianBlur;});blur->gaussian_blur.radius_pixels=25;
    check(commands.execute(application::SetClipNodeGraphCommand{duplicate,changed}).changed() && *session.timeline().tracks()[0].clips[0].node_graph==original,"independent duplicate edits");
    check(commands.undo().changed() && commands.redo().changed() && commands.undo().changed() && commands.undo().changed(),"graph history");
    check(commands.execute(application::SplitClipCommand{id,10}).changed(),"split Blur graph");
    const auto right=application::exportComposition(session.timeline().tracks()[0].clips[1],d.width,d.height,{30,1});
    for(int frame:{0,6,19})imageNear(fusionFrame(d,frame+10),fusionFrame(right,frame),"mask/Blur continuity after split");
    check(commands.undo().changed(),"undo split");
    check(commands.execute(application::TrimClipEdgeCommand{id,timeline::ClipEdge::Left,7,timeline::ClipEdgeEditMode::Individual}).changed(),"trim Blur graph");
    const auto trimmed=application::exportComposition(session.timeline().tracks()[0].clips[0],d.width,d.height,{30,1});
    imageNear(fusionFrame(d,15),fusionFrame(trimmed,8),"mask/Blur continuity after trim");
    check(commands.execute(application::SetProjectSettingsCommand{1920,1080,{60,1}}).changed(),"FPS conversion");
    const auto& scaled=session.timeline().tracks()[0].clips[0];
    check(std::find_if(scaled.node_graph->nodes.begin(),scaled.node_graph->nodes.end(),[](const auto& n){return n.type==fusion::nodes::NodeType::GaussianBlur;})->gaussian_blur.radius_pixels==std::get<x::BlurEffect>(d.layers[0].effects[1]).radius_pixels,"FPS preserves pixel radius");
    const auto document=application::ProjectDocumentMapper::toDocument(session,{});const auto path=dir/"blur.csp";project::save(path,document);
    check(project::load(path)==document,"CSP v34 roundtrip");
    std::atomic_bool cancel{false};check(application::ProjectOpenService{}.prepare(path,path,{},cancel).prepared.has_value(),"prepare Blur recovery/open");
    project::AutosaveManager autosave(dir/"autosave","blur");autosave.saveSnapshot(document,5);
    check(project::load(autosave.unsavedSnapshots()[0].path)==document,"Blur autosave");
    QFile file(QString::fromStdU16String(path.u16string()));check(file.open(QIODevice::ReadOnly),"open native fixture");auto root=QJsonDocument::fromJson(file.readAll()).object();file.close();check(root["version"]==34,"native version");
    for(int version:{30,34}) {root["version"]=version;check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"write old/future fixture");file.write(QJsonDocument(root).toJson());file.close();rejected([&]{(void)project::load(path);},"CSP legacy/future Blur refusal");}
    ui::FusionWorkspace workspace;QWidget host;workspace.createPanels(&host);
    timeline::TimelineClip clip;clip.clip_id=101;clip.kind=timeline::ClipKind::FusionComposition;clip.timeline_duration_frames=30;clip.node_graph=fusion::nodes::makeCompositionGraph();
    workspace.setSelection(&clip,{});workspace.setActive(true);int edits=0;
    QObject::connect(&workspace,&ui::FusionWorkspace::graphEditRequested,[&](auto,const auto& graph){clip.node_graph=graph;++edits;});
    check(workspace.addCatalogNode("video.gaussian_blur"),"Fusion catalog Blur");
    const auto* field=workspace.inspectorPanel()->findChild<QDoubleSpinBox*>("fusionBlurRadius");
    check(field && field->value()==10 && field->minimum()==0 && field->maximum()==100 && field->decimals()==1 && field->singleStep()==.5,"Blur Inspector defaults");
    auto* radius=const_cast<QDoubleSpinBox*>(field);radius->setValue(7.5);workspace.inspectorPanel()->findChild<QCheckBox*>("fusionBlurEnabled")->setChecked(false);
    workspace.inspectorPanel()->findChild<QPushButton*>("fusionApplyNodeSettingsButton")->click();
    check(edits==2 && clip.node_graph->nodes.back().gaussian_blur==fusion::nodes::GaussianBlurParameters{7.5,false},"Blur Inspector commit");
    workspace.setSelection(nullptr,{});check(!workspace.addCatalogNode("video.gaussian_blur"),"selection required");
}
}
int main(int argc,char** argv) try {
    QApplication app(argc,argv);QTemporaryDir temp;check(temp.isValid(),"temporary directory");
    const auto dir=std::filesystem::path(temp.path().toStdU16String());
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());QCoreApplication::setOrganizationName("CreativeSuiteTests");QCoreApplication::setApplicationName("blur-interchange");
    check(effects::isAvailable("video.gaussian_blur",effects::Workspace::Fusion) && !effects::isAvailable("video.gaussian_blur",effects::Workspace::Edit),"workspace availability");
    check(effects::setFavorite("video.gaussian_blur",true) && effects::favoriteIds().contains("video.gaussian_blur"),"Blur favorites");
    x::Document d;d.width=160;d.height=90;d.duration=30;
    QImage image(73,41,QImage::Format_RGBA8888);
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)image.setPixelColor(x,y,QColor((x*37)%256,(y*59)%256,140,(x*11+y*23)%256));
    check(image.save(QString::fromStdU16String((dir/"image.png").u16string())),"synthetic input");
    x::Layer layer;layer.id=1;layer.kind=x::Kind::Image;layer.source=dir/"image.png";layer.presentation.duration=30;
    layer.transform={.57,.45,.87,13,.65};layer.effects={x::ColorEffect{{12,110,75}},x::BlurEffect{2.5,true,17},x::ColorEffect{{-7,125,120}}};
    d.layers={layer};x::Mask mask;mask.id=17;mask.kind=creative_suite::composition::ShapeMaskKind::Ellipse;mask.parameters={.45,.55,.7,.6,27,.15,false};
    mask.keyframes[0]={{0,.2,creative_suite::animation::InterpolationMode::CubicBezier,{.2,.1,.8,.9}},{29,.8}};d.masks={mask};
    for(auto profile:{x::Profile::FusionCanvas,x::Profile::MotionLayer})for(double radius:{0.,.4,.5,2.5,10.,100.}) {
        d.layers[0].presentation.profile=profile;std::get<x::BlurEffect>(d.layers[0].effects[1]).radius_pixels=radius;
        auto native=motion::model::importComposition(d);const auto returned=motion::model::exportComposition(native);
        check(returned.layers[0].effects.size()==3 && std::get<x::BlurEffect>(returned.layers[0].effects[1]).radius_pixels==radius,"effect order and fractional radius");
        for(int frame:{0,13,29})compare(returned,native,frame,"Blur/Color order and animated effect mask");
    }
    d.layers[0].presentation.profile=x::Profile::FusionCanvas;std::get<x::BlurEffect>(d.layers[0].effects[1]).radius_pixels=2.5;
    contract(d,dir);nativeAndEditing(d,dir);
    auto shared=d;shared.layers[0].source_mask=17;std::get<x::ColorEffect>(shared.layers[0].effects[0]).mask=17;
    auto shared_native=motion::model::importComposition(shared);
    motion::model::CompositionDocument shared_model(160,90,{30,1},shared_native.layers,shared_native.masks);
    check(shared_model.maskUseCount(17)==3,"mask identity shared across source, color and Blur");
    compare(shared,shared_native,13,"shared Blur/source/color mask");
    auto reordered=shared_native.layers[0].effects;std::reverse(reordered.begin(),reordered.end());
    check(shared_model.setLayerEffects(1,reordered) && shared_model.maskUseCount(17)==3,"effect reordering retains associations");
    for(bool invert:{false,true}) {
        auto degenerate=d;degenerate.masks[0].kind=creative_suite::composition::ShapeMaskKind::Rectangle;
        degenerate.masks[0].parameters.width=0;degenerate.masks[0].parameters.invert=invert;
        compare(degenerate,motion::model::importComposition(degenerate),13,"zero-dimension Rectangle Blur mask/inversion");
    }
    imageNear(fusionFrame(d,13),fusionFrame(d,13,1),"Fusion GPU Color/Blur/Color and rotation");
    bool mixed_used=false;const auto mixed_native=motion::model::importComposition(d);
    imageNear(motionFrame(mixed_native,13),motionFrame(mixed_native,13,true,&mixed_used),"Motion GPU Color/Blur/Color and rotation");
    if(QOpenGLContext::supportsThreadedOpenGL())check(mixed_used,"Motion GPU must execute ordered mixed effects");
    auto simple=d;simple.layers[0].effects={x::BlurEffect{2.5}};simple.masks.clear();simple.layers[0].transform={};
    imageNear(fusionFrame(simple,13),fusionFrame(simple,13,1),"Fusion GPU Blur");
    imageNear(fusionFrame(simple,13),fusionFrame(simple,13,2),"Fusion CPU resource recovery");
    if(QOpenGLContext::supportsThreadedOpenGL()) {
        using namespace creative_suite::composition;
        QOffscreenSurface surface;QSurfaceFormat format;format.setVersion(3,2);format.setProfile(QSurfaceFormat::CoreProfile);surface.setFormat(format);surface.create();
        OpenGlFrameCompositor backend(&surface);std::vector<OpenGlTextureFramePtr> occupied;
        auto fixture=x::transparent(160,90);OpenGlImageOperation copy;copy.background={&fixture};
        for(int i=0;i<3;++i){auto value=backend.processImage(copy);check(value.status==OpenGlCompositionStatus::Complete && value.frame,"GPU pressure fixture");occupied.push_back(std::move(value.frame));}
        auto retry_graph=application::importComposition(simple);fusion::nodes::InputFrames inputs;
        inputs[retry_graph.nodes.front().id]=std::make_shared<const creative_suite::media::RgbaFrame>(creative_suite::media::StillImageDecoder{}.decode_first_frame(simple.layers[0].source));
        rendering::prepareGraphFrames(retry_graph,inputs,160,90,true);
        fusion::nodes::EvaluationContext ctx;ctx.canvas_width=160;ctx.canvas_height=90;ctx.local_frame=13;ctx.gpu=&backend;bool resumed=false;ctx.gpu_used=&resumed;
        auto recovered=fusion::nodes::evaluateFrame(retry_graph,inputs,ctx);
        check(recovered && recovered->rgba && !resumed,"occupied GPU Blur graph recovers completely on CPU");
        occupied.clear();auto retry=fusion::nodes::evaluateFrame(retry_graph,inputs,ctx);
        check(retry && retry->texture && resumed,"same GPU backend resumes Blur after resources are released");
        auto read=backend.readback(retry->texture);check(bool(read.frame),"retry readback");
        imageNear(std::make_shared<const creative_suite::media::RgbaFrame>(*recovered->rgba),std::make_shared<const creative_suite::media::RgbaFrame>(*read.frame),"GPU Blur retry pixels");
        retry.reset();
    }
    auto masked=d;masked.layers[0].effects={x::BlurEffect{2.5,true,17}};masked.layers[0].transform={};
    imageNear(fusionFrame(masked,13),fusionFrame(masked,13,1),"Fusion GPU masked Blur");
    auto native=motion::model::importComposition(masked);bool used=false;
    imageNear(motionFrame(native,13),motionFrame(native,13,true,&used),"Motion GPU masked Blur");if(QOpenGLContext::supportsThreadedOpenGL())check(used,"Motion GPU must execute Blur");
    auto text=d;text.layers[0].kind=x::Kind::Text;text.layers[0].source.clear();text.layers[0].text={"Blur\nText","Sans Serif",24.5,1,{210,70,170,135}};
    compare(text,motion::model::importComposition(text),13,"Blur translucent text");
    auto disabled=simple;std::get<x::BlurEffect>(disabled.layers[0].effects[0]).enabled=false;auto identity=simple;identity.layers[0].effects.clear();
    check(fusionFrame(disabled,5)->rgba_pixels==fusionFrame(identity,5)->rgba_pixels,"disabled Blur exact identity");
    auto graph=application::importComposition(d);fusion::nodes::EvaluationContext context;context.canvas_width=160;context.canvas_height=90;context.should_cancel=[]{return true;};
    check(!fusion::nodes::evaluate(graph,{},context),"cancel graph");
    auto disconnected=graph;fusion::nodes::Node extra;extra.id=disconnected.next_id++;extra.type=fusion::nodes::NodeType::GaussianBlur;disconnected.nodes.push_back(extra);
    timeline::TimelineClip bad;bad.kind=timeline::ClipKind::FusionComposition;bad.timeline_duration_frames=30;bad.node_graph=disconnected;
    rejected([&]{(void)application::exportComposition(bad,160,90,{30,1});},"disconnected Blur refused");
    check(argc>1,"reference video argument");auto video=d;auto& source=video.layers[0];source.kind=x::Kind::Video;source.source=std::filesystem::absolute(argv[1]);
    const auto metadata=creative_suite::media::VideoProbe{}.probe(source.source);source.source_rate=metadata.frame_rate.value_or(30);source.source_count=metadata.frame_count.value_or(120);source.presentation.start=3;source.presentation.duration=20;source.presentation.offset=5;
    for(auto profile:{x::Profile::FusionCanvas,x::Profile::MotionLayer}) {source.presentation.profile=profile;for(int frame:{0,3,17,29})compare(video,motion::model::importComposition(video),frame,"Blur video source windows");}
    std::cout<<"Gaussian Blur integration passed\n";return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
