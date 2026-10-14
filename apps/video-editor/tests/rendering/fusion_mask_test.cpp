#include "application/timeline_command_service.h"
#include "application/project_document_mapper.h"
#include "application/project_open_service.h"
#include "project/project_file.h"
#include "project/autosave_manager.h"
#include "rendering/graph_generators.h"
#include "workspaces/fusion/nodes/evaluation/mask_rasterizer.h"
#include "workspaces/fusion/nodes/evaluation/node_graph_evaluator.h"
#include "workspaces/fusion/nodes/ui/node_canvas.h"
#include "workspaces/fusion/ui/fusion_workspace.h"
#include "../../../../cmake/test_support/test_check.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QMouseEvent>
#include <cmath>
#include <iostream>

namespace {
using namespace fusion::nodes;
using namespace application;
NodeGraph fixture(NodeType receiver) {
    auto g=makeCompositionGraph(); g.nodes[0].background_color={180,40,20,180};
    Node mask; mask.id=3; mask.type=NodeType::RectangleMask;
    g.nodes.push_back(mask); g.next_id=4;
    if (receiver==NodeType::Background) g.connections.push_back({3,1,2});
    else if (receiver==NodeType::Output) g.connections.push_back({3,2,2});
    else {
        Node node; node.id=4; node.type=receiver; node.color.brightness=40;
        if (receiver==NodeType::Effect) node.effect=creative_suite::effects::makeDefaultInstance("video.grayscale");
        node.transform.position_x=0.8; g.nodes.push_back(node); g.next_id=5;
        g.connections={{1,4,0},{4,2,0},{3,4,2}};
        if (receiver==NodeType::Merge) {
            Node fg; fg.id=5; fg.type=NodeType::Background; fg.background_color={20,120,230,210};
            g.nodes.push_back(fg); g.next_id=6; g.connections.push_back({5,4,1});
        }
    }
    return g;
}
media::VideoFrame render(NodeGraph g, NodeId target=2, std::int64_t frame=0) {
    InputFrames inputs; rendering::prepareGraphFrames(g,inputs,32,24,true);
    EvaluationContext c; c.canvas_width=32; c.canvas_height=24; c.local_frame=frame;
    auto result=evaluate(g,inputs,target,c); CS_TEST_CHECK(result); return *result;
}
void coverageAndPorts() {
    MaskRasterCache cache; MaskParameters p;
    auto r=cache.coverage(NodeType::RectangleMask,p,32,24); CS_TEST_CHECK(r);
    CS_TEST_CHECK(r->rgba_pixels[0]==0 && r->rgba_pixels[12*r->stride+16*4]==255);
    CS_TEST_CHECK(cache.coverage(NodeType::RectangleMask,p,32,24)==r);
    auto e=cache.coverage(NodeType::EllipseMask,p,32,24);
    CS_TEST_CHECK(e && e->rgba_pixels!=r->rgba_pixels);
    p.rotation=45; auto rotated=cache.coverage(NodeType::RectangleMask,p,32,24);
    CS_TEST_CHECK(rotated && rotated->rgba_pixels!=r->rgba_pixels);
    p.soft_edge=0.25; auto soft=cache.coverage(NodeType::RectangleMask,p,32,24);
    bool partial=false; for (std::size_t i=0;i<soft->rgba_pixels.size();i+=4)
        partial|=soft->rgba_pixels[i]>0 && soft->rgba_pixels[i]<255;
    CS_TEST_CHECK(partial); p.invert=true; auto inverse=cache.coverage(NodeType::RectangleMask,p,32,24);
    for (std::size_t i=0;i<soft->rgba_pixels.size();i+=4) CS_TEST_CHECK(soft->rgba_pixels[i]+inverse->rgba_pixels[i]==255);
    p.width=0; auto full=cache.coverage(NodeType::EllipseMask,p,32,24);
    for (auto b : full->rgba_pixels) CS_TEST_CHECK(b==255);
    p.invert=false; auto empty=cache.coverage(NodeType::RectangleMask,p,32,24);
    for (std::size_t i=0;i<empty->rgba_pixels.size();i+=4) CS_TEST_CHECK(empty->rgba_pixels[i]==0);
    p.width=0.5; p.center_x=-2; CS_TEST_CHECK(cache.coverage(NodeType::RectangleMask,p,32,24)->rgba_pixels[0]==0);
    p.width=4; p.height=4; p.center_x=0.5;
    CS_TEST_CHECK(cache.coverage(NodeType::RectangleMask,p,32,24)->rgba_pixels[0]==255);
    CS_TEST_CHECK(!cache.coverage(NodeType::RectangleMask,p,32,24,[]{return true;}));
    CS_TEST_CHECK(!cache.coverage(NodeType::RectangleMask,p,16384,16384));
    auto g=fixture(NodeType::Color);
    EvaluationContext oversized;oversized.canvas_width=16384;oversized.canvas_height=16384;
    CS_TEST_CHECK(!evaluate(g,{},3,oversized)); // rejected before a large image buffer is allocated

    CS_TEST_CHECK(validate(g) && !connect(g,3,4,0) && !connect(g,1,4,2) && !connect(g,1,3,0));
    Node second=g.nodes[2]; second.id=5; second.type=NodeType::EllipseMask;
    g.nodes.push_back(second); g.next_id=6;
    auto reconnected=connect(g,5,4,2); CS_TEST_CHECK(reconnected);
    CS_TEST_CHECK(std::count_if(reconnected->connections.begin(),reconnected->connections.end(),[](auto& c){return c.to==4 && c.input==2;})==1);
    CS_TEST_CHECK(connect(*reconnected,5,2,2)); // fanout
    g.nodes[2].mask.center_x=3.01; CS_TEST_CHECK(!validate(g));
}
void semantics() {
    for (const auto receiver : {NodeType::Background,NodeType::Output,NodeType::Transform,NodeType::Color,NodeType::Effect,NodeType::Merge}) {
        auto g=fixture(receiver); auto frame=render(g); media::VideoFrame original;
        auto unmasked=makeCompositionGraph(); unmasked.nodes[0].background_color={180,40,20,180}; original=render(unmasked);
        const bool blend=receiver!=NodeType::Background && receiver!=NodeType::Output;
        CS_TEST_CHECK(frame.rgba_pixels[3]==(blend ? 180 : 0));
        if (blend) for (int channel=0;channel<4;++channel) CS_TEST_CHECK(frame.rgba_pixels[channel]==original.rgba_pixels[channel]);
        const auto full=g; g.nodes[2].mask.width=0;
        auto zero=render(g); if (blend) CS_TEST_CHECK(zero.rgba_pixels==original.rgba_pixels);
        else for (std::size_t i=3;i<zero.rgba_pixels.size();i+=4) CS_TEST_CHECK(zero.rgba_pixels[i]==0);
        g.nodes[2].mask.invert=true;
        auto all=render(g); auto no_mask=g; no_mask.connections.erase(std::remove_if(no_mask.connections.begin(),no_mask.connections.end(),[](auto c){return c.input==2;}),no_mask.connections.end());
        CS_TEST_CHECK(all.rgba_pixels==render(no_mask).rgba_pixels);
        const auto view=render(full,3); CS_TEST_CHECK(view.rgba_pixels[0]==0 && view.rgba_pixels[3]==255);
    }
    for (const auto source : {NodeType::Input,NodeType::Text}) {
        auto g=fixture(NodeType::Background); g.nodes[0].type=source;
        InputFrames inputs;
        if (source==NodeType::Input) inputs[1]=std::make_shared<const media::VideoFrame>(media::VideoFrame{32,24,128,std::vector<std::uint8_t>(32*24*4,255)});
        rendering::prepareGraphFrames(g,inputs,32,24,true);
        EvaluationContext c; c.canvas_width=32;c.canvas_height=24;
        auto output=evaluate(g,inputs,c); CS_TEST_CHECK(output && output->rgba_pixels[3]==0);
    }
    media::VideoFrame a{1,1,4,{255,0,0,0}}, b{1,1,4,{0,0,255,255}}, mask{1,1,4,{128,128,128,255}};
    CS_TEST_CHECK(creative_suite::composition::maskedBlend(a,b,mask));
    CS_TEST_CHECK(b.rgba_pixels==std::vector<std::uint8_t>({0,0,255,128}));
    CS_TEST_CHECK(!creative_suite::composition::applyMask(b,mask,[]{return true;}));
    mask.width=2; CS_TEST_CHECK(!creative_suite::composition::maskedBlend(a,b,mask));
}
void editingAndPersistence(const QString& directory) {
    EditorSession session; TimelineCommandService commands(session); const auto track=session.timeline().tracks()[0].track_id;
    auto inserted=commands.execute(AddFusionCompositionCommand{track,0,100}); CS_TEST_CHECK(inserted.changed());
    const auto id=*inserted.selection.active_clip_id; auto g=fixture(NodeType::Color);
    for (std::size_t i=0;i<5;++i) g.nodes[2].mask_keyframes[i]={{0,i==4 ? -45.0 : 0.2,
        creative_suite::animation::InterpolationMode::CubicBezier,{0.1,0.6,0.8,0.95}},{99,i==4 ? 180.0 : 0.8}};
    CS_TEST_CHECK(commands.execute(SetClipNodeGraphCommand{id,g}).changed());
    CS_TEST_CHECK(commands.execute(DuplicateFusionCompositionCommand{id}).changed());
    const auto duplicate=session.timeline().tracks()[0].clips[1].clip_id;
    auto changed=g; changed.nodes[2].mask.invert=true;
    CS_TEST_CHECK(commands.execute(SetClipNodeGraphCommand{duplicate,changed}).changed());
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].node_graph==g);
    CS_TEST_CHECK(commands.undo().changed() && commands.undo().changed());
    CS_TEST_CHECK(commands.execute(SplitClipCommand{id,37}).changed());
    for (int f=0;f<100;++f) {
        const auto& part=session.timeline().tracks()[0].clips[f<37 ? 0 : 1].node_graph->nodes[2];
        const auto actual=evaluateMask(part.mask,part.mask_keyframes,f<37 ? f : f-37);
        const auto expected=evaluateMask(g.nodes[2].mask,g.nodes[2].mask_keyframes,f);
        for (std::size_t i=0;i<5;++i) CS_TEST_CHECK(std::abs(maskValue(actual,i)-maskValue(expected,i))<1e-6);
    }
    CS_TEST_CHECK(commands.undo().changed());
    CS_TEST_CHECK(commands.execute(TrimClipEdgeCommand{id,timeline::ClipEdge::Left,13,timeline::ClipEdgeEditMode::Individual}).changed());
    const auto trimmed=session.timeline().tracks()[0].clips[0].node_graph->nodes[2];
    for (int f=0;f<87;++f) for (std::size_t i=0;i<5;++i)
        CS_TEST_CHECK(std::abs(maskValue(evaluateMask(trimmed.mask,trimmed.mask_keyframes,f),i)-maskValue(evaluateMask(g.nodes[2].mask,g.nodes[2].mask_keyframes,f+13),i))<1e-6);
    CS_TEST_CHECK(commands.execute(SetProjectSettingsCommand{1920,1080,{60,1}}).changed());
    const auto rescaled=session.timeline().tracks()[0].clips[0].node_graph->nodes[2];
    CS_TEST_CHECK(rescaled.mask_keyframes[0].back().frame==172);
    auto document=ProjectDocumentMapper::toDocument(session,{});
    auto path=QFileInfo(directory+"/mask.csp").filesystemFilePath(); project::save(path,document);
    CS_TEST_CHECK(project::load(path)==document);
    std::atomic_bool cancel{false}; auto opened=ProjectOpenService{}.prepare(path,path,{},cancel);
    CS_TEST_CHECK(opened.prepared && opened.prepared->timeline.tracks[0].clips[0].node_graph==document.timeline_tracks[0].clips[0].node_graph);
    project::AutosaveManager autosave(QFileInfo(directory+"/autosave").filesystemFilePath(),"mask-test"); autosave.saveSnapshot(document,5);
    CS_TEST_CHECK(project::load(autosave.unsavedSnapshots()[0].path)==document);
    QFile file(QString::fromStdWString(path.wstring())); CS_TEST_CHECK(file.open(QIODevice::ReadOnly));
    auto root=QJsonDocument::fromJson(file.readAll()).object();file.close(); CS_TEST_CHECK(root["version"].toInt()==28);
    for (int version : {27,29}) {
        root.insert("version",version);CS_TEST_CHECK(file.open(QIODevice::WriteOnly|QIODevice::Truncate));file.write(QJsonDocument(root).toJson());file.close();
        bool rejected=false;try{(void)project::load(path);}catch(const project::ProjectError&){rejected=true;}CS_TEST_CHECK(rejected);
    }
    // The same curves must survive ordinary video split/trim paths.
    for (auto kind : {timeline::ClipKind::Video,timeline::ClipKind::Image}) {
        timeline::TimelineModel timeline; auto state=session.timeline().snapshot(); auto clip=state.tracks[0].clips[0];
        clip.frame_rate=60.0;clip.kind=kind;clip.timeline_start_frame=0;clip.timeline_duration_frames=100;clip.frame_count=300;
        clip.source_path=QFileInfo(directory+"/ordinary.mkv").filesystemFilePath();clip.source_start_frame=0;
        clip.source_duration_frames=100;clip.composition_input_offset_frames=0;clip.node_graph=g;
        state.tracks[0].clips={clip}; timeline.restore(state);
        CS_TEST_CHECK(timeline.splitClip(0,0,37)==timeline::SplitClipResult::Split);
        const auto& right=timeline.tracks()[0].clips[1].node_graph->nodes[2];
        for (int f=0;f<63;++f) CS_TEST_CHECK(std::abs(maskValue(evaluateMask(right.mask,right.mask_keyframes,f),0)-maskValue(evaluateMask(g.nodes[2].mask,g.nodes[2].mask_keyframes,f+37),0))<1e-6);
        for (bool edge : {false,true}) {
            timeline.restore(state);
            const int delta=kind==timeline::ClipKind::Image && !edge ? 0 : 13;
            CS_TEST_CHECK((edge ? timeline.trimClipEdge(0,0,timeline::ClipEdge::Left,13,timeline::ClipEdgeEditMode::Individual)
                : timeline.trimClip(0,0,delta,87))==timeline::TrimClipResult::Trimmed);
            const auto& trimmed=timeline.tracks()[0].clips[0].node_graph->nodes[2];
            for (int f=0;f<87;++f) CS_TEST_CHECK(std::abs(maskValue(evaluateMask(trimmed.mask,trimmed.mask_keyframes,f),0)-maskValue(evaluateMask(g.nodes[2].mask,g.nodes[2].mask_keyframes,f+delta),0))<1e-6);
        }

    }
}
void inspector() {
    QWidget owner; ui::FusionWorkspace workspace; workspace.createPanels(&owner);
    timeline::TimelineClip clip;clip.kind=timeline::ClipKind::FusionComposition;clip.clip_id=1;clip.timeline_duration_frames=100;clip.node_graph=makeCompositionGraph();
    workspace.setSelection(&clip,{});workspace.setActive(true);NodeGraph edited=*clip.node_graph;int edits=0;
    QObject::connect(&workspace,&ui::FusionWorkspace::graphEditRequested,[&](auto,const auto& g){edited=g;++edits;});
    auto* types=workspace.nodeEditorPanel()->findChild<QComboBox*>("fusionAddNodeType");
    types->setCurrentIndex(types->findData(static_cast<int>(NodeType::EllipseMask)));
    workspace.nodeEditorPanel()->findChild<QPushButton*>("fusionAddNodeButton")->click();
    auto* x=workspace.inspectorPanel()->findChild<QDoubleSpinBox*>("fusionMask0");CS_TEST_CHECK(x && x->minimum()==-200 && x->maximum()==300);x->setValue(70);
    workspace.inspectorPanel()->findChild<QCheckBox*>("fusionMaskInvert")->setChecked(true);
    workspace.inspectorPanel()->findChild<QPushButton*>("fusionApplyNodeSettingsButton")->click();
    CS_TEST_CHECK(edited.nodes.back().mask.center_x==0.7 && edited.nodes.back().mask.invert);
    workspace.inspectorPanel()->findChild<QPushButton*>("fusionMask0Keyframe")->click();
    CS_TEST_CHECK(edited.nodes.back().mask_keyframes[0].size()==1);
    workspace.inspectorPanel()->findChild<QPushButton*>("fusionMask0Keyframe")->click();CS_TEST_CHECK(edited.nodes.back().mask_keyframes[0].empty());
    auto* canvas=workspace.nodeEditorPanel()->findChild<NodeCanvas*>();const int before=edits;
    for (auto* item : canvas->scene()->items()) if (item->data(0).toULongLong()==edited.nodes.back().id && item->data(1).toInt()==4) {
        const auto pos=canvas->mapFromScene(item->sceneBoundingRect().center());
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(pos),QPointF(canvas->viewport()->mapToGlobal(pos)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(),&press); break;
    }
    CS_TEST_CHECK(edits==before && workspace.previewNodeId()==edited.nodes.back().id);
    workspace.inspectorPanel()->findChild<QPushButton*>("fusionRemoveNodeButton")->click();CS_TEST_CHECK(edited.nodes.size()==2);
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv); QTemporaryDir directory;
    try { CS_TEST_CHECK(directory.isValid());coverageAndPorts();semantics();editingAndPersistence(directory.path());inspector();return 0; }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
