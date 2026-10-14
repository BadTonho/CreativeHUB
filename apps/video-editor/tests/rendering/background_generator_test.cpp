#include "application/timeline_command_service.h"
#include "application/project_document_mapper.h"
#include "application/project_open_service.h"
#include "project/project_file.h"
#include "project/autosave_manager.h"
#include "workspaces/edit/ui/edit_workspace.h"
#include "workspaces/edit/controllers/edit_workspace_controller.h"
#include "settings/shortcut_manager.h"
#include "timeline/timeline_widget.h"
#include "ui/media_browser/media_drag_mime.h"
#include "workspaces/fusion/ui/fusion_workspace.h"
#include "workspaces/fusion/nodes/ui/node_canvas.h"
#include "../../../../cmake/test_support/test_check.h"
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGraphicsItem>
#include <QSettings>
#include <iostream>

namespace {
using namespace application;
using namespace fusion::nodes;
std::filesystem::path path(const QString& name) { return QFileInfo(name).filesystemFilePath(); }
void modelAndPersistence(const QString& directory) {
    EditorSession session; TimelineCommandService commands(session);
    const auto track=session.timeline().tracks()[0].track_id;
    CS_TEST_CHECK(commands.execute(AddBackgroundGeneratorCommand{track,30,150}).changed());
    const auto id=*session.selection().active_clip_id;
    const auto original=session.timeline().tracks()[0].clips[0];
    CS_TEST_CHECK(original.kind==timeline::ClipKind::FusionComposition && original.display_name=="Background");
    CS_TEST_CHECK((original.node_graph->nodes[0].background_color==std::array<std::uint8_t,4>({255,255,255,255})));
    CS_TEST_CHECK(original.source_path.empty() && original.source_start_frame==0 && original.composition_input_offset_frames==0);
    CS_TEST_CHECK(!commands.execute(AddBackgroundGeneratorCommand{track,40,150}).changed());
    CS_TEST_CHECK(!commands.execute(AddBackgroundGeneratorCommand{session.timeline().tracks()[1].track_id,0,150}).changed());
    CS_TEST_CHECK(!commands.execute(AddBackgroundGeneratorCommand{track,INT64_MAX,150}).changed());
    CS_TEST_CHECK(commands.undo().changed() && session.timeline().clipCount(0)==0);
    CS_TEST_CHECK(!commands.undo().changed()); // insertion is exactly one history entry
    CS_TEST_CHECK(commands.redo().changed());
    CS_TEST_CHECK(commands.execute(DuplicateFusionCompositionCommand{id}).changed());
    const auto duplicate=*session.selection().active_clip_id;
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[1].display_name=="Background");
    auto graph=*original.node_graph;graph.nodes[0].background_color={12,34,56,100};
    CS_TEST_CHECK(commands.execute(SetClipNodeGraphCommand{duplicate,graph}).changed());
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].node_graph==original.node_graph);
    CS_TEST_CHECK(commands.undo().changed() && commands.undo().changed());
    CS_TEST_CHECK(commands.execute(SplitClipCommand{id,60}).changed());
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].display_name=="Background" && session.timeline().tracks()[0].clips[1].display_name=="Background");
    CS_TEST_CHECK(commands.undo().changed());
    CS_TEST_CHECK(commands.execute(TrimClipEdgeCommand{id,timeline::ClipEdge::Left,40,timeline::ClipEdgeEditMode::Individual}).changed());
    CS_TEST_CHECK(commands.execute(TrimClipEdgeCommand{id,timeline::ClipEdge::Right,400,timeline::ClipEdgeEditMode::Individual}).changed());
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].display_name=="Background");
    CS_TEST_CHECK(commands.execute(SetProjectSettingsCommand{1920,1080,{60,1}}).changed());
    CS_TEST_CHECK(commands.execute(SetClipNodeGraphCommand{id,graph}).changed());
    auto document=ProjectDocumentMapper::toDocument(session,{});
    const auto filename=path(directory+"/background.csp");project::save(filename,document);
    CS_TEST_CHECK(project::load(filename)==document);
    CS_TEST_CHECK(document.timeline_tracks[0].clips[0].display_name=="Background");
    std::atomic_bool cancel{false};auto opened=ProjectOpenService{}.prepare(filename,filename,{},cancel);
    CS_TEST_CHECK(opened.prepared && opened.prepared->timeline.tracks[0].clips[0].display_name=="Background");
    CS_TEST_CHECK(opened.prepared->timeline.tracks[0].clips[0].node_graph==graph);
    project::AutosaveManager autosave(path(directory+"/autosave"),"background-test");autosave.saveSnapshot(document,5);
    CS_TEST_CHECK(project::load(autosave.unsavedSnapshots()[0].path)==document);
    QFile file(QString::fromStdWString(filename.wstring()));CS_TEST_CHECK(file.open(QIODevice::ReadOnly));
    auto root=QJsonDocument::fromJson(file.readAll()).object();file.close();CS_TEST_CHECK(root["version"].toInt()==project::current_format_version);
    for (int version : {28,project::current_format_version+1}) {
        root.insert("version",version);CS_TEST_CHECK(file.open(QIODevice::WriteOnly|QIODevice::Truncate));file.write(QJsonDocument(root).toJson());file.close();
        bool rejected=false;try{(void)project::load(filename);}catch(const project::ProjectError&){rejected=true;}CS_TEST_CHECK(rejected);
    }
    // v27/v28 source-free graphs without custom names retain their historical label.
    document.timeline_tracks[0].clips[0].display_name.clear();
    project::save(filename,document);CS_TEST_CHECK(file.open(QIODevice::ReadOnly));root=QJsonDocument::fromJson(file.readAll()).object();file.close();
    for (int version : {27,28}) {
        root.insert("version",version);CS_TEST_CHECK(file.open(QIODevice::WriteOnly|QIODevice::Truncate));file.write(QJsonDocument(root).toJson());file.close();
        auto legacy=ProjectOpenService{}.prepare(filename,filename,{},cancel);
        CS_TEST_CHECK(legacy.prepared && legacy.prepared->timeline.tracks[0].clips[0].display_name=="Fusion Composition");
    }
}
void inspectorAndFusion() {
    EditorSession session;TimelineCommandService commands(session);settings::ShortcutManager shortcuts;
    QWidget owner,preview(&owner);ui::EditWorkspace edit(session,commands,&preview,shortcuts,&owner,[](const QString&){});
    edit.createPanels(&owner);auto* controller=edit.controller();controller->setPlaybackPresentation(false,false,true);
    const auto track=session.timeline().tracks()[0].track_id;
    controller->handleTimelineEffectDrop("video.background",track,0);controller->refreshInspectorPresentation();
    const auto id=*session.selection().active_clip_id;
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].timeline_duration_frames==150);
    CS_TEST_CHECK(!edit.ui().background_controls->isHidden() && edit.ui().background_nodes->isHidden());
    const auto saved=ProjectDocumentMapper::toDocument(session,{});
    const auto color_dialog=[&](QColor color,bool accept) {
        QTimer::singleShot(0,[color,accept] {
            auto* dialog=qobject_cast<QColorDialog*>(QApplication::activeModalWidget());CS_TEST_CHECK(dialog);
            dialog->setCurrentColor(color);
            if (accept) dialog->accept();else dialog->reject();
        });
        edit.ui().background_color->click();
    };
    color_dialog(QColor(12,34,56,100),false);
    CS_TEST_CHECK(ProjectDocumentMapper::toDocument(session,{})==saved);
    color_dialog(QColor(12,34,56,100),true);
    const auto color=std::array<std::uint8_t,4>{12,34,56,100};
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].node_graph->nodes[0].background_color==color);
    CS_TEST_CHECK(edit.ui().background_sample->text().contains("100"));
    CS_TEST_CHECK(commands.undo().changed() && ProjectDocumentMapper::toDocument(session,{})==saved);
    CS_TEST_CHECK(commands.redo().changed());
    auto graph=*session.timeline().tracks()[0].clips[0].node_graph;
    Node second;second.id=graph.next_id++;second.type=NodeType::Background;second.background_color={1,2,3,4};
    graph.nodes.push_back(second);controller->applyFusionNodeGraph(id,graph);controller->refreshInspectorPresentation();
    CS_TEST_CHECK(edit.ui().background_nodes->count()==2 && !edit.ui().background_nodes->isHidden());
    const auto before_selection=ProjectDocumentMapper::toDocument(session,{});
    edit.ui().background_nodes->setCurrentIndex(1);
    CS_TEST_CHECK(ProjectDocumentMapper::toDocument(session,{})==before_selection);
    color_dialog(QColor(70,80,90,128),true);
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].node_graph->nodes.back().background_color[0]==70);
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].node_graph->nodes[0].background_color==color);
    // Actual Fusion controls see the same node color and publish back to Edit.
    ui::FusionWorkspace fusion;fusion.createPanels(&owner);
    auto selected=session.timeline().tracks()[0].clips[0];fusion.setSelection(&selected,{});fusion.setActive(true);
    QObject::connect(&fusion,&ui::FusionWorkspace::graphEditRequested,[&](auto clip_id,const auto& changed){controller->applyFusionNodeGraph(clip_id,changed);controller->refreshInspectorPresentation();});
    auto* canvas=fusion.nodeEditorPanel()->findChild<NodeCanvas*>();canvas->setSelectedNode(second.id);
    QTimer::singleShot(0,[] {
        auto* dialog=qobject_cast<QColorDialog*>(QApplication::activeModalWidget());CS_TEST_CHECK(dialog);
        CS_TEST_CHECK(dialog->currentColor()==QColor(70,80,90,128));
        dialog->setCurrentColor(QColor(150,160,170,180));dialog->accept();
    });
    fusion.inspectorPanel()->findChild<QPushButton*>("fusionGeneratorColor")->click();
    CS_TEST_CHECK(edit.ui().background_sample->text().contains("150"));
    graph=*session.timeline().tracks()[0].clips[0].node_graph;graph.nodes.pop_back();
    controller->applyFusionNodeGraph(id,graph);controller->refreshInspectorPresentation();
    CS_TEST_CHECK(edit.ui().background_nodes->count()==1 && edit.ui().background_sample->text().contains("12"));
    graph.nodes[0].type=NodeType::Text;controller->applyFusionNodeGraph(id,graph);controller->refreshInspectorPresentation();
    CS_TEST_CHECK(edit.ui().background_controls->isHidden());
    // Inspector discovery also works for ordinary video/image graphs.
    auto state=session.timeline().snapshot();
    for (auto kind : {timeline::ClipKind::Video,timeline::ClipKind::Image}) {
        auto& clip=state.tracks[0].clips[0];clip.kind=kind;clip.node_graph=makeCompositionGraph();clip.display_name="Media";
        clip.source_path="inspector-media.mp4";session.selectionForUi().selected_source_path=clip.source_path;
        session.legacyTimelineForUi().restore(state);controller->refreshInspectorPresentation();
        CS_TEST_CHECK(!edit.ui().background_controls->isHidden());
    }
    CS_TEST_CHECK(makeCompositionGraph().nodes[0].background_color[3]==0);
}
void drops() {
    timeline::TimelineWidget widget;widget.resize(1000,350);widget.setFrameRate({24,1});widget.setZoomFactor(100);
    // Deliver real drag/drop events directly; painting is covered by the
    // existing timeline/application suites and is independent of drop validation.
    timeline::TimelineModel timeline;widget.setTracks(timeline.tracks());
    int accepted=0;QObject::connect(&widget,&timeline::TimelineWidget::effectDropRequested,[&](auto id,auto,auto){if(id=="video.background")++accepted;});
    const auto drop=[&](int track) {
        const QPointF position(widget.contentXForFrame(30),widget.trackBounds(track).center().y());
        QMimeData mime;mime.setData(ui::kEffectIdMimeType,"video.background");
        QDragEnterEvent enter(position.toPoint(),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&widget,&enter);
        QDragMoveEvent move(position.toPoint(),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&widget,&move);
        QDropEvent event(position,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&widget,&event);
        return event.isAccepted();
    };
    CS_TEST_CHECK(drop(0) && accepted==1);CS_TEST_CHECK(!drop(1));
    CS_TEST_CHECK(timeline.addFusionComposition(0,0,150)==timeline::AddClipResult::Added);widget.setTracks(timeline.tracks());
    CS_TEST_CHECK(!drop(0) && accepted==1);
}
}
int main(int argc,char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);QTemporaryDir directory;
    QCoreApplication::setOrganizationName("CreativeSuiteTests");QCoreApplication::setApplicationName("background-generator");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
    try {CS_TEST_CHECK(directory.isValid());drops();modelAndPersistence(directory.path());inspectorAndFusion();return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
