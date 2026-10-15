#include "application/timeline_command_service.h"
#include "application/project_document_mapper.h"
#include "application/project_open_service.h"
#include "project/project_file.h"
#include "project/autosave_manager.h"
#include "playback/playback_worker.h"
#include "rendering/graph_generators.h"
#include "rendering/offline_export_renderer.h"
#include "workspaces/fusion/ui/fusion_workspace.h"
#include "workspaces/edit/ui/edit_workspace.h"
#include "workspaces/edit/controllers/edit_workspace_controller.h"
#include "settings/shortcut_manager.h"
#include "timeline/timeline_widget.h"
#include "ui/media_browser/media_drag_mime.h"
#include "logging/logger.h"
#include "media/video_playback.h"
#include <creative_suite/media/video_encoder.h>
#include "../../../../cmake/test_support/test_check.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QComboBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QDoubleSpinBox>
#include <QAction>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <cmath>
#include <iostream>

namespace {
using namespace fusion::nodes;
using namespace application;
std::filesystem::path path(const QString& value) { return QFileInfo(value).filesystemFilePath(); }
NodeGraph coloredGraph(std::array<std::uint8_t, 4> rgba) {
    auto graph = makeCompositionGraph(); graph.nodes[0].background_color = rgba; return graph;
}
media::VideoFrame renderGraph(const NodeGraph& graph, int w = 320, int h = 180) {
    InputFrames inputs; rendering::prepareGraphFrames(graph, inputs, w, h, true);
    EvaluationContext context; context.canvas_width = w; context.canvas_height = h;
    const auto frame = evaluate(graph, inputs, context); CS_TEST_CHECK(frame); return *frame;
}
void generators() {
    auto graph = makeCompositionGraph();
    CS_TEST_CHECK(validate(graph));
    auto blank = renderGraph(graph);
    for (auto byte : blank.rgba_pixels) CS_TEST_CHECK(byte == 0);
    graph.nodes[0].background_color = {70, 80, 90, 128};
    auto color = renderGraph(graph);
    CS_TEST_CHECK(color.rgba_pixels[0] == 70 && color.rgba_pixels[3] == 128);
    graph.connections.clear();
    auto disconnected = renderGraph(graph);
    CS_TEST_CHECK(disconnected.rgba_pixels == blank.rgba_pixels);
    graph.nodes.erase(graph.nodes.begin());
    CS_TEST_CHECK(validate(graph)); CS_TEST_CHECK(renderGraph(graph).rgba_pixels == blank.rgba_pixels);
    graph = makeCompositionGraph(); graph.nodes[0].type = NodeType::Text;
    graph.nodes[0].text.content = "Text"; graph.nodes[0].text.font_size_pixels = 20;
    auto text = renderGraph(graph);
    int min_x = 320, max_x = 0, min_y = 180, max_y = 0;
    for (int y = 0; y < 180; ++y) for (int x = 0; x < 320; ++x)
        if (text.rgba_pixels[std::size_t(y) * text.stride + x * 4 + 3]) {
            min_x = std::min(min_x, x); max_x = std::max(max_x, x);
            min_y = std::min(min_y, y); max_y = std::max(max_y, y);
        }
    CS_TEST_CHECK(max_x > min_x && max_x - min_x < 110 && max_y - min_y < 35);
    graph.nodes[0].text.font_family = "Unavailable Fusion Test Font";
    CS_TEST_CHECK(!renderGraph(graph).rgba_pixels.empty());
    graph.nodes[0].text.font_size_pixels = -1; CS_TEST_CHECK(!validate(graph));
    graph.nodes[0].text.font_size_pixels = 20;
    InputFrames inputs; EvaluationContext canceled; canceled.canvas_width = 320; canceled.canvas_height = 180;
    canceled.should_cancel = [] { return true; }; CS_TEST_CHECK(!evaluate(graph, inputs, canceled));
    // Fit a differently shaped input into a transparent canvas before Merge.
    auto merged = coloredGraph({70, 80, 90, 128});
    Node media; media.id = 3; Node merge; merge.id = 4; merge.type = NodeType::Merge;
    merged.nodes.push_back(media); merged.nodes.push_back(merge); merged.next_id = 5;
    merged.connections = {{1, 4, 0}, {3, 4, 1}, {4, 2, 0}};
    auto narrow = std::make_shared<media::VideoFrame>(media::VideoFrame{8, 32, 32, std::vector<std::uint8_t>(8 * 32 * 4)});
    for (std::size_t i = 0; i < narrow->rgba_pixels.size(); i += 4) { narrow->rgba_pixels[i + 2] = 255; narrow->rgba_pixels[i + 3] = 255; }
    InputFrames fitted{{3, narrow}}; rendering::prepareGraphFrames(merged, fitted, 320, 180, true);
    EvaluationContext explicit_canvas; explicit_canvas.canvas_width = 320; explicit_canvas.canvas_height = 180;
    const auto result = evaluate(merged, fitted, explicit_canvas); CS_TEST_CHECK(result);
    CS_TEST_CHECK(result->rgba_pixels[0] == 70 && result->rgba_pixels[3] == 128);
    CS_TEST_CHECK(result->rgba_pixels[std::size_t(90) * result->stride + 160 * 4 + 2] == 255);
    // Generators are also usable in an ordinary media graph with legacy canvas inference.
    inputs[1] = std::make_shared<const media::VideoFrame>(text);
    CS_TEST_CHECK(evaluate(graph, inputs));
}
void editing(const QString& directory) {
    EditorSession session; TimelineCommandService commands(session);
    const auto track = session.timeline().tracks()[0].track_id;
    const auto add = commands.execute(AddFusionCompositionCommand{track, 30, 150}); CS_TEST_CHECK(add.changed());
    const auto id = *add.selection.active_clip_id;
    CS_TEST_CHECK(!commands.execute(AddFusionCompositionCommand{track, 50, 20}).changed());
    CS_TEST_CHECK(!commands.execute(AddTextClipCommand{track, 50, 20, 30}).changed());
    CS_TEST_CHECK(!commands.execute(AddFusionCompositionCommand{session.timeline().tracks()[1].track_id, 0, 150}).changed());
    CS_TEST_CHECK(!commands.execute(SetClipAudioCommand{id, 0.5, true}).changed());
    CS_TEST_CHECK(!commands.execute(SetClipNodeGraphCommand{id, std::nullopt}).changed());
    auto graph = coloredGraph({60, 80, 100, 200});
    Node transform; transform.id = 3; transform.type = NodeType::Transform;
    transform.transform_keyframes.scale = {{10, 1, creative_suite::animation::InterpolationMode::CubicBezier, {0.1, 0.6, 0.8, 0.95}}, {140, 2}};
    graph.nodes.push_back(transform); graph.next_id = 4; graph.connections = {{1, 3, 0}, {3, 2, 0}};
    CS_TEST_CHECK(commands.execute(SetClipNodeGraphCommand{id, graph}).changed());
    CS_TEST_CHECK(commands.execute(ToggleTransformKeyframeCommand{id, timeline::TransformProperty::Scale, 0}).changed());
    CS_TEST_CHECK(commands.execute(ToggleTransformKeyframeCommand{id, timeline::TransformProperty::Scale, 149}).changed());
    CS_TEST_CHECK(commands.execute(SetTransformPropertyCommand{id, timeline::TransformProperty::Scale, 149, 2}).changed());
    const auto original = session.timeline().tracks()[0].clips[0];
    CS_TEST_CHECK(!commands.execute(AddFusionCompositionCommand{track, INT64_MAX, 1}).changed());
    CS_TEST_CHECK(commands.execute(AddTrackCommand{"Fusion upper"}).changed());
    const auto upper_track = session.timeline().tracks()[0].track_id;
    CS_TEST_CHECK(commands.execute(MoveClipCommand{id, upper_track, 20}).changed());
    CS_TEST_CHECK(commands.execute(MoveTrackCommand{upper_track, 1}).changed());
    CS_TEST_CHECK(commands.undo().changed()); CS_TEST_CHECK(commands.undo().changed()); CS_TEST_CHECK(commands.undo().changed());
    auto duplicate = commands.execute(DuplicateFusionCompositionCommand{id}); CS_TEST_CHECK(duplicate.changed());
    const auto duplicate_id = *duplicate.selection.active_clip_id;
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[1].node_graph == original.node_graph);
    auto changed = graph; changed.nodes[0].background_color[0] = 200;
    CS_TEST_CHECK(commands.execute(SetClipNodeGraphCommand{duplicate_id, changed}).changed());
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].node_graph == original.node_graph);
    CS_TEST_CHECK(commands.undo().changed());
    CS_TEST_CHECK(commands.execute(AddTransitionCommand{track, id, duplicate_id, timeline::TransitionKind::CrossDissolve, 10}).changed());
    CS_TEST_CHECK(commands.undo().changed()); CS_TEST_CHECK(commands.undo().changed());
    CS_TEST_CHECK(commands.execute(SplitClipCommand{id, 75}).changed());
    const auto right = session.timeline().tracks()[0].clips[1];
    CS_TEST_CHECK(right.source_start_frame == 0 && right.composition_input_offset_frames == 75);
    for (int frame = 0; frame < 150; ++frame) {
        const auto& part = session.timeline().tracks()[0].clips[frame < 75 ? 0 : 1];
        const auto actual = timeline::evaluateTransform(part.node_graph->nodes[2].transform, part.node_graph->nodes[2].transform_keyframes, frame % 75);
        const auto expected = timeline::evaluateTransform(transform.transform, transform.transform_keyframes, frame);
        CS_TEST_CHECK(std::abs(actual.scale - expected.scale) < 1e-5);
    }
    CS_TEST_CHECK(commands.undo().changed());
    CS_TEST_CHECK(commands.execute(TrimClipEdgeCommand{id, timeline::ClipEdge::Left, 40, timeline::ClipEdgeEditMode::Individual}).changed());
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].composition_input_offset_frames == 10);
    CS_TEST_CHECK(commands.execute(TrimClipEdgeCommand{id, timeline::ClipEdge::Right, 600, timeline::ClipEdgeEditMode::Individual}).changed());
    CS_TEST_CHECK(commands.execute(TrimClipEdgeCommand{id, timeline::ClipEdge::Left, 0, timeline::ClipEdgeEditMode::Individual}).changed());
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].composition_input_offset_frames == -30);
    CS_TEST_CHECK(commands.execute(SetProjectSettingsCommand{1920, 1080, {60, 1}}).changed());
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].composition_input_offset_frames == -60);
    CS_TEST_CHECK(commands.undo().changed()); CS_TEST_CHECK(commands.undo().changed());
    CS_TEST_CHECK(commands.execute(SetProjectSettingsCommand{1920, 1080, {60, 1}}).changed());
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].composition_input_offset_frames == 20);
    auto document = ProjectDocumentMapper::toDocument(session, {});
    const auto filename = path(directory + "/fusion.csp"); project::save(filename, document);
    CS_TEST_CHECK(project::load(filename) == document);
    std::atomic_bool cancel{false}; auto opened = ProjectOpenService{}.prepare(filename, filename, {}, cancel);
    CS_TEST_CHECK(opened.status == ProjectOpenStatus::Prepared && opened.prepared);
    CS_TEST_CHECK(opened.prepared->timeline.tracks[0].clips[0].node_graph == document.timeline_tracks[0].clips[0].node_graph);
    project::AutosaveManager autosave(path(directory + "/autosave"), "fusion-test"); autosave.saveSnapshot(document, 5);
    CS_TEST_CHECK(project::load(autosave.unsavedSnapshots()[0].path) == document);
    QFile file(QString::fromStdWString(filename.wstring())); CS_TEST_CHECK(file.open(QIODevice::ReadOnly));
    auto root = QJsonDocument::fromJson(file.readAll()).object(); file.close();
    const auto record = root["timeline"].toObject()["tracks"].toArray()[0].toObject()["clips"].toArray()[0].toObject();
    CS_TEST_CHECK(root["version"].toInt() == project::current_format_version && !record.contains("source") && !record.contains("text") && !record.contains("audio_gain"));
    root.insert("version", 26); CS_TEST_CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); file.write(QJsonDocument(root).toJson()); file.close();
    bool rejected = false; try { (void)project::load(filename); } catch (const project::ProjectError&) { rejected = true; } CS_TEST_CHECK(rejected);
    // New generator tags also require v27 on ordinary video clips.
    auto ordinary = document; auto& media_clip = ordinary.timeline_tracks[0].clips[0];
    media_clip.kind = timeline::ClipKind::Video; media_clip.source_path = path(directory + "/legacy-input.mp4");
    media_clip.source_duration_frames = media_clip.duration_frames; media_clip.composition_input_offset_frames = 0;
    ordinary.media.push_back({media_clip.source_path, "Legacy Input", "Unsorted", true, media::MediaKind::Video});
    const auto ordinary_path = path(directory + "/generator-on-video.csp"); project::save(ordinary_path, ordinary);
    CS_TEST_CHECK(project::load(ordinary_path) == ordinary);
    QFile old_file(QString::fromStdWString(ordinary_path.wstring())); CS_TEST_CHECK(old_file.open(QIODevice::ReadOnly));
    auto old_root = QJsonDocument::fromJson(old_file.readAll()).object(); old_file.close(); old_root.insert("version", 26);
    CS_TEST_CHECK(old_file.open(QIODevice::WriteOnly | QIODevice::Truncate)); old_file.write(QJsonDocument(old_root).toJson()); old_file.close();
    rejected = false; try { (void)project::load(ordinary_path); } catch (const project::ProjectError&) { rejected = true; } CS_TEST_CHECK(rejected);
    timeline::TimelineModel numeric;
    const auto numeric_added = numeric.addFusionComposition(0, 0, 150); CS_TEST_CHECK(numeric_added == timeline::AddClipResult::Added);
    auto boundary = numeric.snapshot(); boundary.tracks[0].clips[0].composition_input_offset_frames = INT64_MAX; numeric.restore(boundary);
    CS_TEST_CHECK(numeric.splitClip(0, 0, 75) == timeline::SplitClipResult::InvalidBoundary);
    CS_TEST_CHECK(numeric.snapshot() == boundary);
    CS_TEST_CHECK(commands.execute(DeleteClipCommand{id}).changed()); CS_TEST_CHECK(commands.undo().changed());
}
void uiWorkflow() {
    EditorSession session; TimelineCommandService commands(session); settings::ShortcutManager shortcuts;
    QWidget owner, preview(&owner); ui::EditWorkspace edit(session, commands, &preview, shortcuts, &owner, [](const QString&) {});
    edit.createPanels(&owner); auto* controller = edit.controller(); controller->setPlaybackPresentation(false, false, true);
    const auto track = session.timeline().tracks()[0].track_id;
    controller->handleTimelineEffectDrop("video.fusion_composition", track, 0);
    CS_TEST_CHECK(session.timeline().clipCount(0) == 1);
    controller->refreshInspectorPresentation(); edit.refreshCommandAvailability();
    for (auto* control : edit.ui().transform_spins) CS_TEST_CHECK(control->isEnabled());
    for (auto* control : edit.ui().transform_keys) CS_TEST_CHECK(control->isEnabled());
    controller->addEffectToSelectedClip("video.grayscale");
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].effects.size() == 1);
    controller->copySelectedClipAttributes(); controller->handleTimelineEffectDrop("video.fusion_composition", track, 200);
    timeline::ClipAttributeOptions paste; paste.effects = paste.transform = true;
    CS_TEST_CHECK(controller->applyCopiedClipAttributes(paste).changed());
    edit.refreshCommandAvailability(); QAction* duplicate = nullptr;
    for (auto* action : edit.menuActions()) if (action && action->objectName() == "edit.duplicate_fusion_composition") duplicate = action;
    CS_TEST_CHECK(duplicate && duplicate->isEnabled() && duplicate->shortcut().isEmpty()); duplicate->trigger();
    CS_TEST_CHECK(session.timeline().clipCount(0) == 3);
    ui::FusionWorkspace fusion; fusion.createPanels(&owner);
    auto selected = session.timeline().tracks()[0].clips[0]; fusion.setSelection(&selected, {}); fusion.setActive(true);
    CS_TEST_CHECK(fusion.selectedClipId() == selected.clip_id);
    NodeGraph edited = *selected.node_graph;
    QObject::connect(&fusion, &ui::FusionWorkspace::graphEditRequested, [&](auto, const auto& graph) { edited = graph; });
    CS_TEST_CHECK(fusion.addCatalogNode("text.text"));
    auto* content = fusion.inspectorPanel()->findChild<QPlainTextEdit*>("fusionTextContent");
    CS_TEST_CHECK(content); content->setPlainText("Composition title");
    fusion.inspectorPanel()->findChild<QPushButton*>("fusionApplyText")->click();
    CS_TEST_CHECK(edited.nodes.back().text.content == "Composition title");
    CS_TEST_CHECK(fusion.addCatalogNode("fusion.input"));
    auto* source = fusion.inspectorPanel()->findChild<QComboBox*>("fusionInputMediaSource");
    CS_TEST_CHECK(source && source->itemText(0) == "Transparent");
    timeline::TimelineWidget timeline; timeline.resize(1000, 350); timeline.setFrameRate({30, 1});
    timeline::TimelineModel empty; timeline.setTracks(empty.tracks()); timeline.setZoomFactor(100); timeline.show(); QApplication::processEvents();
    int drops = 0; QObject::connect(&timeline, &timeline::TimelineWidget::effectDropRequested, [&](auto id, auto, auto) { if (id == "video.fusion_composition") ++drops; });
    const QPointF position(timeline.contentXForFrame(30), timeline.trackBounds(0).center().y());
    QMimeData mime; mime.setData(ui::kEffectIdMimeType, "video.fusion_composition");
    QDragEnterEvent enter(position.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(&timeline, &enter);
    QDragMoveEvent move(position.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(&timeline, &move);
    QDropEvent drop(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(&timeline, &drop);
    CS_TEST_CHECK(drop.isAccepted() && drops == 1);
}
void workerAndExport(const QString& directory) {
    CS_TEST_CHECK(logging::Logger::instance().initialize(path(directory + "/logs")));
    const auto source_path = path(directory + "/input.mkv");
    creative_suite::media::VideoEncodingSettings encoding; encoding.output_path = source_path;
    encoding.width = 32; encoding.height = 16; encoding.frame_rate_numerator = 30;
    encoding.container_name = "matroska"; encoding.video_encoder_name = "ffv1";
    { creative_suite::media::VideoEncoder encoder(encoding);
      for (int index = 0; index < 12; ++index) {
        media::VideoFrame frame{32, 16, 128, std::vector<std::uint8_t>(32 * 16 * 4)};
        for (std::size_t offset = 0; offset < frame.rgba_pixels.size(); offset += 4) {
            frame.rgba_pixels[offset] = frame.rgba_pixels[offset + 1] = frame.rgba_pixels[offset + 2] = static_cast<std::uint8_t>(30 + index * 10);
            frame.rgba_pixels[offset + 3] = 255;
        }
        encoder.writeVideo(frame, index);
      } encoder.finish(); }
    playback::PlaybackWorker worker; media::VideoFramePtr output; int errors = 0;
    QObject::connect(&worker, &playback::PlaybackWorker::frameReady, [&](auto frame, auto, auto, auto) { output = frame; });
    QObject::connect(&worker, &playback::PlaybackWorker::playbackError, [&](auto, auto, auto) { ++errors; });
    worker.setCompositionCanvasSize(1920, 1080); worker.setPreviewQuality(playback::PreviewQuality::Quarter);
    playback::CompositionLayerSpec spec; spec.kind = timeline::ClipKind::FusionComposition; spec.segment_frame_count = 4;
    spec.track_index = 0; spec.clip_index = 0; spec.clip_id = 1; spec.track_id = 1; spec.timeline_frame_rate = {30, 1};
    spec.node_graph = coloredGraph({180, 40, 20, 255});
    rendering::RenderJob job; job.settings.output_path = directory + "/fusion.mkv";
    job.settings.width = 480; job.settings.height = 270; job.settings.frame_rate = 30; job.settings.export_audio = false;
    job.settings.container_name = "matroska"; job.settings.video_encoder_name = "ffv1";
    project::ProjectClip clip; clip.kind = spec.kind; clip.clip_id = 1; clip.duration_frames = 4; clip.node_graph = spec.node_graph;
    project::ProjectTrack track; track.track_id = 1; track.name = "Fusion"; track.clips = {clip}; job.project_snapshot.timeline_tracks = {track};
    std::atomic_bool cancel{false}; int prepared = 0;
    QVector<playback::CompositionLayerSpec> extra;
    QVector<playback::CompositionTransitionSpec> transitions;
    auto compare = [&](int revision) {
        auto layers = extra; layers.push_back(spec);
        output.reset(); worker.setComposition(layers, transitions, revision);
        rendering::OfflineExportOptions options;
        options.prepared_frame_callback = [&](const auto& frame, std::int64_t index) {
            output.reset(); worker.renderCompositionFrame(index, index, revision);
            if (!output || output->rgba_pixels!=frame.rgba_pixels) {
                std::cerr<<"Preview/export mismatch: revision="<<revision<<" frame="<<index<<" export="<<frame.width<<"x"<<frame.height;
                if (output) {
                    std::cerr<<" preview="<<output->width<<"x"<<output->height;
                    for (std::size_t byte=0;byte<std::min(output->rgba_pixels.size(),frame.rgba_pixels.size());++byte)
                        if (output->rgba_pixels[byte]!=frame.rgba_pixels[byte]) {std::cerr<<" byte="<<byte<<" preview_value="<<int(output->rgba_pixels[byte])<<" export_value="<<int(frame.rgba_pixels[byte]);break;}
                }
                std::cerr<<'\n';
            }
            CS_TEST_CHECK(output && output->rgba_pixels == frame.rgba_pixels); ++prepared;
        };
        rendering::OfflineExportRenderer::render(job, cancel, {}, options);
    };
    compare(1); CS_TEST_CHECK(prepared == 4 && output->rgba_pixels[0] == 180);
    spec.node_graph->nodes[0].type = NodeType::Text; spec.node_graph->nodes[0].text.content = "Title";
    job.project_snapshot.timeline_tracks[0].clips[0].node_graph = spec.node_graph; compare(2);
    // Masked graph output must match worker Preview and export before encoding.
    for (const auto type : {NodeType::RectangleMask,NodeType::EllipseMask}) {
        spec.node_graph=coloredGraph({180,40,20,180});
        Node mask; mask.id=3; mask.type=type; mask.mask.rotation=33; mask.mask.soft_edge=0.15;
        mask.mask_keyframes[0]={{0,0.3},{3,0.7}}; spec.node_graph->nodes.push_back(mask);
        spec.node_graph->next_id=4;spec.node_graph->connections.push_back({3,2,2});
        job.project_snapshot.timeline_tracks[0].clips[0].node_graph=spec.node_graph;
        compare(type==NodeType::RectangleMask ? 3 : 4);
    }
    // Videos keep their sampled time after a split/trim; negative times and EOF are transparent.
    auto input_graph = makePassthroughGraph(); input_graph.nodes[0].source_path = source_path;
    input_graph.nodes[0].source_frame_rate = 30; input_graph.nodes[0].source_frame_count = 12;
    spec.node_graph = input_graph; spec.composition_input_offset_frames = 3;
    job.project_snapshot.media.push_back({source_path, "Input", "Unsorted", false, media::MediaKind::Video});
    auto& persisted = job.project_snapshot.timeline_tracks[0].clips[0]; persisted.node_graph = input_graph; persisted.composition_input_offset_frames = 3;
    compare(5); CS_TEST_CHECK(output->rgba_pixels[std::size_t(135) * output->stride + 240 * 4] >= 85);
    spec.composition_input_offset_frames = -4; persisted.composition_input_offset_frames = -4; compare(6);
    CS_TEST_CHECK(output->rgba_pixels[std::size_t(135) * output->stride + 240 * 4] == 0);
    spec.composition_input_offset_frames = 12; persisted.composition_input_offset_frames = 12; compare(7);
    CS_TEST_CHECK(output->rgba_pixels[std::size_t(135) * output->stride + 240 * 4] == 0);
    // Gaussian Blur after Text, with its existing animated effect mask.
    auto blur_graph=coloredGraph({0,0,0,0});blur_graph.nodes[0].type=NodeType::Text;
    blur_graph.nodes[0].text.content="Blur";blur_graph.nodes[0].text.font_size_pixels=60;
    Node blur;blur.id=3;blur.type=NodeType::GaussianBlur;blur.gaussian_blur.radius_pixels=2.5;
    Node blur_mask;blur_mask.id=4;blur_mask.type=NodeType::EllipseMask;
    blur_mask.mask.soft_edge=.15;blur_mask.mask_keyframes[0]={{0,.2},{3,.8}};
    blur_graph.nodes.push_back(blur);blur_graph.nodes.push_back(blur_mask);blur_graph.next_id=5;
    blur_graph.connections={{1,3,0},{3,2,0},{4,3,2}};
    spec.node_graph=blur_graph;spec.composition_input_offset_frames=0;job.project_snapshot.timeline_tracks[0].clips[0].node_graph=blur_graph;
    job.project_snapshot.timeline_tracks[0].clips[0].composition_input_offset_frames=0;compare(8);
    // The Edit generator uses the same complete Preview/export pipeline.
    EditorSession generated_session;TimelineCommandService generated_commands(generated_session);
    CS_TEST_CHECK(generated_commands.execute(AddBackgroundGeneratorCommand{generated_session.timeline().tracks()[0].track_id,0,4}).changed());
    auto generated_document=ProjectDocumentMapper::toDocument(generated_session,{});
    spec.node_graph=generated_document.timeline_tracks[0].clips[0].node_graph;
    spec.composition_input_offset_frames=0;
    job.project_snapshot.timeline_tracks=generated_document.timeline_tracks;
    compare(9);CS_TEST_CHECK(output->rgba_pixels[0]==255 && output->rgba_pixels[1]==255 && output->rgba_pixels[2]==255);
    auto generated_lower=spec;generated_lower.track_index=1;generated_lower.clip_id=2;generated_lower.track_id=2;
    generated_lower.node_graph=coloredGraph({0,0,255,255});extra={generated_lower};
    auto lower_record=generated_document.timeline_tracks[0];lower_record.track_id=2;lower_record.name="Lower";
    lower_record.clips[0].clip_id=2;lower_record.clips[0].node_graph=generated_lower.node_graph;
    job.project_snapshot.timeline_tracks.push_back(lower_record);
    spec.node_graph->nodes[0].background_color={180,40,20,128};
    job.project_snapshot.timeline_tracks[0].clips[0].node_graph=spec.node_graph;
    compare(10);CS_TEST_CHECK(output->rgba_pixels[0]>0 && output->rgba_pixels[2]>100);
    spec.node_graph->nodes[0].background_color[3]=0;
    job.project_snapshot.timeline_tracks[0].clips[0].node_graph=spec.node_graph;
    compare(11);CS_TEST_CHECK(output->rgba_pixels[0]==0 && output->rgba_pixels[2]==255);extra.clear();
    // Transparent Fusion output blends with lower tracks and an upper adjustment.
    spec.node_graph = coloredGraph({180, 40, 20, 128}); spec.composition_input_offset_frames = 0; spec.track_index = 1;
    auto adjusted = spec; adjusted.kind = timeline::ClipKind::Adjustment; adjusted.track_index = 0; adjusted.clip_id = 2;
    adjusted.node_graph.reset(); adjusted.effects = {creative_suite::effects::makeDefaultInstance("video.grayscale")};
    auto lower = spec; lower.track_index = 2; lower.clip_id = 3; lower.node_graph = coloredGraph({0, 0, 255, 255});
    extra = {adjusted, lower};
    auto main_clip = clip; main_clip.node_graph = spec.node_graph;
    auto lower_clip = main_clip; lower_clip.clip_id = 3; lower_clip.node_graph = lower.node_graph;
    project::ProjectClip adjustment_clip; adjustment_clip.kind = timeline::ClipKind::Adjustment;
    adjustment_clip.clip_id = 2; adjustment_clip.duration_frames = 4; adjustment_clip.effects = adjusted.effects;
    job.project_snapshot.timeline_tracks = {{"Adjustment", 1, false, {adjustment_clip}, {}, 2},
        {"Fusion", 1, false, {main_clip}, {}, 1}, {"Lower", 1, false, {lower_clip}, {}, 3}};
    compare(20); CS_TEST_CHECK(output->rgba_pixels[0] == output->rgba_pixels[1]);
    // Existing overlap transitions operate on two independent graph outputs.
    extra.clear(); spec.track_index = 0; spec.node_graph = coloredGraph({255, 0, 0, 255});
    auto incoming = spec; incoming.clip_id = 2; incoming.clip_index = 1; incoming.timeline_start_frame = 2;
    incoming.node_graph = coloredGraph({0, 0, 255, 255}); extra = {incoming};
    auto outgoing_clip = clip; outgoing_clip.node_graph = spec.node_graph;
    auto incoming_clip = clip; incoming_clip.clip_id = 2; incoming_clip.timeline_start_frame = 2; incoming_clip.node_graph = incoming.node_graph;
    project::ProjectTrack transition_track; transition_track.track_id = 1; transition_track.name = "Transition";
    transition_track.clips = {outgoing_clip, incoming_clip};
    transition_track.transitions = {{0, 1, timeline::TransitionKind::CrossDissolve, 2}};
    job.project_snapshot.timeline_tracks = {transition_track}; transitions = {{0, 0, 1, 4, 2, timeline::TransitionKind::CrossDissolve}};
    compare(21); extra.clear(); transitions.clear();
    // Fusion Viewer targets work without a selected media file.
    spec.node_graph = coloredGraph({90, 40, 10, 128}); spec.fusion_preview_node_id = 1;
    worker.setComposition({spec}, {}, 22); output.reset(); worker.renderCompositionFrame(1, 1, 22);
    CS_TEST_CHECK(output && errors == 0);
    cancel.store(true); bool canceled = false;
    try { rendering::OfflineExportRenderer::render(job, cancel); } catch (const rendering::ExportCanceled&) { canceled = true; }
    CS_TEST_CHECK(canceled && QFileInfo::exists(job.settings.output_path));
}
} // namespace
int main(int argc, char** argv) {
    QApplication app(argc, argv); QStandardPaths::setTestModeEnabled(true); QTemporaryDir directory;
    try { CS_TEST_CHECK(directory.isValid()); generators(); editing(directory.path()); uiWorkflow(); workerAndExport(directory.path()); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
