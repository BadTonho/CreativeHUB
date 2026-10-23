#include "rendering/adjustment_composition.h"
#include "rendering/offline_export_renderer.h"
#include "playback/playback_worker.h"
#include "application/timeline_command_service.h"
#include "application/project_document_mapper.h"
#include "application/project_open_service.h"
#include "project/project_file.h"
#include "project/autosave_manager.h"
#include "workspaces/edit/controllers/edit_workspace_controller.h"
#include "workspaces/edit/ui/edit_workspace.h"
#include "settings/shortcut_manager.h"
#include <QAction>
#include <QToolButton>
#include <QPushButton>
#include <QStandardPaths>
#include "media/video_playback.h"
#include "logging/logger.h"
#include "timeline/timeline_geometry.h"
#include "timeline/timeline_widget.h"
#include "ui/effects/effects_catalog.h"
#include "ui/media_browser/media_drag_mime.h"
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QMimeData>
#include "../../../../cmake/test_support/test_check.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <chrono>
#include <cmath>
#include <iostream>

namespace {
using namespace rendering;
using namespace application;
media::VideoFrame solid(int r, int g, int b, int width = 8, int height = 8) {
    media::VideoFrame frame{width, height, width * 4, std::vector<std::uint8_t>(width * height * 4)};
    for (std::size_t i = 0; i < frame.rgba_pixels.size(); i += 4) {
        frame.rgba_pixels[i] = static_cast<std::uint8_t>(r);
        frame.rgba_pixels[i + 1] = static_cast<std::uint8_t>(g);
        frame.rgba_pixels[i + 2] = static_cast<std::uint8_t>(b);
        frame.rgba_pixels[i + 3] = 255;
    }
    return frame;
}
void pixels() {
    auto red = solid(180, 40, 20), blue = solid(0, 0, 255);
    auto grayscale = creative_suite::effects::makeDefaultInstance("video.grayscale");
    std::vector<creative_suite::effects::EffectInstance> effects{grayscale};
    CompositionLayer operation;
    operation.effect_stack = effects;
    auto expected = red;
    CS_TEST_CHECK(creative_suite::effects::applyStack(expected, effects));
    for (const auto opacity : {0.0, 0.5, 1.0}) {
        operation.transform.opacity = opacity;
        auto frame = composeAdjustmentLayers(8, 8, {{{&red}, false}, {operation, true}});
        CS_TEST_CHECK(frame);
        for (std::size_t i = 0; i < red.rgba_pixels.size(); ++i)
            CS_TEST_CHECK(frame->rgba_pixels[i] == static_cast<std::uint8_t>(std::lround(
                red.rgba_pixels[i] * (1 - opacity) + expected.rgba_pixels[i] * opacity)));
    }
    operation.transform.opacity = 1;
    auto upper = composeAdjustmentLayers(8, 8, {{{&red}, false}, {operation, true}, {{&blue}, false}});
    CS_TEST_CHECK(upper && upper->rgba_pixels == blue.rgba_pixels);
    auto empty = composeAdjustmentLayers(8, 8, {{operation, true}});
    CS_TEST_CHECK(empty && empty->rgba_pixels[0] == 0);
    operation.transform.scale = 0.5;
    auto scaled = composeAdjustmentLayers(8, 8, {{{&red}, false}, {operation, true}});
    CS_TEST_CHECK(scaled && scaled->rgba_pixels[0] == 0);
    CS_TEST_CHECK(scaled->rgba_pixels[(4 * 8 + 4) * 4] == expected.rgba_pixels[0]);
    auto brightness = creative_suite::effects::makeDefaultInstance("video.brightness");
    CS_TEST_CHECK(creative_suite::effects::setParameterValue(brightness, "amount", 10));
    std::vector<creative_suite::effects::EffectInstance> brighten{brightness};
    CompositionLayer next; next.effect_stack = brighten;
    operation.transform.scale = 1;
    auto stacked = composeAdjustmentLayers(8, 8, {{{&red}, false}, {operation, true}, {next, true}});
    CS_TEST_CHECK(creative_suite::effects::applyStack(expected, brighten));
    CS_TEST_CHECK(stacked && stacked->rgba_pixels == expected.rgba_pixels);
    effects[0].enabled = false;
    CS_TEST_CHECK(!adjustmentRequiresProcessing(operation.transform, effects));
    auto neutral = composeAdjustmentLayers(8, 8, {{{&red}, false}, {operation, true}});
    CS_TEST_CHECK(neutral && neutral->rgba_pixels == red.rgba_pixels);
    CS_TEST_CHECK(!composeAdjustmentLayers(8, 8, {{{&red}, false}}, [] { return true; }));
    int cancellation_checks = 0;
    CS_TEST_CHECK(!composeAdjustmentLayers(8, 8, {{{&red}, false}, {operation, true}},
        [&] { return ++cancellation_checks >= 3; }));
    CS_TEST_CHECK(!composeAdjustmentLayers(0, 8, {{{&red}, false}}));
}

void curveContinuity() {
    timeline::TimelineModel model;
    const auto added = model.addAdjustmentClip(0, 10, 150);
    CS_TEST_CHECK(added == timeline::AddClipResult::Added);
    auto snapshot = model.snapshot();
    auto& source = snapshot.tracks[0].clips[0];
    source.keyframes.scale = {{0, 1, creative_suite::animation::InterpolationMode::CubicBezier,
        {0.1, 0.7, 0.8, 0.95}}, {149, 2}};
    source.keyframes.opacity = {{20, 0.2}, {130, 0.8}};
    const auto original = source;
    model.restore(snapshot);
    const auto split = model.splitClip(0, 0, 75);
    CS_TEST_CHECK(split == timeline::SplitClipResult::Split);
    for (std::int64_t frame = 0; frame < 150; ++frame) {
        const auto& clip = model.tracks()[0].clips[frame < 75 ? 0 : 1];
        const auto actual = timeline::evaluateTransform(clip.transform, clip.keyframes, frame % 75);
        const auto expected = timeline::evaluateTransform(original.transform, original.keyframes, frame);
        CS_TEST_CHECK(std::abs(actual.scale - expected.scale) < 1e-5);
        CS_TEST_CHECK(std::abs(actual.opacity - expected.opacity) < 1e-5);
    }
    model.restore(snapshot);
    const auto preview = timeline::previewClipEdgeEdit(model.tracks(), {0, 0}, timeline::ClipEdge::Left,
        40, timeline::ClipEdgeEditMode::Individual);
    CS_TEST_CHECK(preview);
    for (std::int64_t frame = 0; frame < preview->clip.timeline_duration_frames; ++frame) {
        const auto actual = timeline::evaluateTransform(preview->clip.transform, preview->clip.keyframes, frame);
        const auto expected = timeline::evaluateTransform(original.transform, original.keyframes, frame + 30);
        CS_TEST_CHECK(std::abs(actual.scale - expected.scale) < 1e-5);
    }
}

void timelineUi() {
    timeline::TimelineModel model;
    timeline::TimelineWidget widget;
    widget.resize(1000, 350); widget.setFrameRate({30, 1}); widget.setTracks(model.tracks());
    widget.setZoomFactor(100); widget.show(); QApplication::processEvents();
    int drops = 0, effect_drops = 0, selections = 0;
    QObject::connect(&widget, &timeline::TimelineWidget::effectDropRequested,
        [&](QString id, timeline::TrackId track, qint64 frame) {
            if (id == "video.adjustment_clip") {
                ++drops;
                const auto added = model.addAdjustmentClip(*model.locateTrack(track), frame, 150);
                CS_TEST_CHECK(added == timeline::AddClipResult::Added);
            } else if (id == "video.grayscale") {
                ++effect_drops;
            }
        });
    QObject::connect(&widget, &timeline::TimelineWidget::clipSelected,
        [&](timeline::TrackId, timeline::ClipId) { ++selections; });
    const auto send = [&](const QByteArray& id, QPointF position) {
        QMimeData mime; mime.setData(ui::kEffectIdMimeType, id);
        QDragEnterEvent enter(position.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &enter);
        QDragMoveEvent move(position.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &move);
        QDropEvent drop(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &drop); return drop.isAccepted();
    };
    const QPointF point(widget.contentXForFrame(30), widget.trackBounds(0).center().y());
    CS_TEST_CHECK(send("video.adjustment_clip", point) && drops == 1);
    widget.setTracks(model.tracks());
    CS_TEST_CHECK(send("video.grayscale", widget.clipBounds({0, 0}).center()) && effect_drops == 1);
    CS_TEST_CHECK(!send("video.adjustment_clip", point) && drops == 1);
    CS_TEST_CHECK(!send("video.adjustment_clip", {point.x(), widget.trackBounds(1).center().y()}));
    const auto center = widget.clipBounds({0, 0}).center();
    QMouseEvent press(QEvent::MouseButtonPress, center, center, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, center, center, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &release);
    CS_TEST_CHECK(selections > 0);
    const auto adjacent = model.addAdjustmentClip(0, 180, 150); CS_TEST_CHECK(adjacent == timeline::AddClipResult::Added);
    widget.setTracks(model.tracks());
    CS_TEST_CHECK(!send("transitions.cross_dissolve", {widget.contentXForFrame(180), point.y()}));
    widget.setReadOnly(true);
    CS_TEST_CHECK(!send("video.adjustment_clip", {widget.contentXForFrame(400), point.y()}));
}

void workspaceUi() {
    EditorSession session; TimelineCommandService commands(session);
    settings::ShortcutManager shortcuts;
    QWidget owner, preview(&owner);
    ui::EditWorkspace workspace(session, commands, &preview, shortcuts, &owner, [](const QString&) {});
    workspace.createPanels(&owner);
    auto* controller = workspace.controller();
    controller->setPlaybackPresentation(false, false, true);
    int compositions = 0;
    QObject::connect(controller, &ui::EditWorkspaceController::renderCompositionFrameRequested,
        [&](qint64, qint64) { ++compositions; });
    controller->handleTimelineEffectDrop("video.adjustment_clip", session.timeline().tracks()[0].track_id, 0);
    controller->refreshInspectorPresentation(); workspace.refreshCommandAvailability();
    CS_TEST_CHECK(compositions > 0 && session.timeline().clipCount(0) == 1);
    for (auto* spin : workspace.ui().transform_spins) CS_TEST_CHECK(spin && spin->isEnabled());
    for (auto* key : workspace.ui().transform_keys) CS_TEST_CHECK(key && key->isEnabled());
    const auto adjustment_id = *session.selection().active_clip_id;
    auto correction = color::fromEffectStack(std::vector{
        creative_suite::effects::makeDefaultInstance("video.grayscale")});
    CS_TEST_CHECK(commands.execute(SetClipColorGraphCommand{adjustment_id, correction}).changed());
    controller->refreshInspectorPresentation();
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].effects.size() == 1);
    controller->refreshInspectorPresentation();
    CS_TEST_CHECK(workspace.ui().clip_effects_controls->isEnabled());
    controller->setSelectedClipEffectEnabled(0, false);
    CS_TEST_CHECK(!session.timeline().tracks()[0].clips[0].effects[0].enabled);
    controller->setSelectedClipEffectEnabled(0, true);
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].effects[0].enabled);
    workspace.refreshCommandAvailability();
    QAction* duplicate = nullptr;
    for (auto* action : workspace.menuActions())
        if (action && action->objectName() == "edit.duplicate_adjustment_clip") duplicate = action;
    CS_TEST_CHECK(duplicate && duplicate->isEnabled() && duplicate->shortcut().isEmpty());
    duplicate->trigger();
    CS_TEST_CHECK(session.timeline().clipCount(0) == 2);
    const auto undone = controller->undo(); CS_TEST_CHECK(undone.changed());
    CS_TEST_CHECK(session.timeline().clipCount(0) == 1);
}

void editingAndPersistence(const QString& directory) {
    EditorSession session;
    TimelineCommandService commands(session);
    const auto track = session.timeline().tracks()[0].track_id;
    auto add = commands.execute(AddAdjustmentClipCommand{track, 30, 150});
    CS_TEST_CHECK(add.changed() && session.timeline().clipCount(0) == 1);
    const auto id = *session.selection().active_clip_id;
    CS_TEST_CHECK(!commands.execute(AddAdjustmentClipCommand{track, 31, 5}).changed());
    CS_TEST_CHECK(!commands.execute(AddTextClipCommand{track, 50, 10, 30}).changed());
    CS_TEST_CHECK(!commands.execute(AddAdjustmentClipCommand{session.timeline().tracks()[1].track_id, 0, 5}).changed());
    CS_TEST_CHECK(commands.undo().changed() && !session.timeline().hasClip());
    CS_TEST_CHECK(commands.redo().changed() && session.timeline().locateClip(id));
    CS_TEST_CHECK(commands.execute(ToggleTransformKeyframeCommand{id, timeline::TransformProperty::Scale, 0}).changed());
    CS_TEST_CHECK(commands.execute(ToggleTransformKeyframeCommand{id, timeline::TransformProperty::Scale, 149}).changed());
    CS_TEST_CHECK(commands.execute(SetTransformPropertyCommand{id, timeline::TransformProperty::Scale, 149, 2}).changed());
    auto effect = creative_suite::effects::makeDefaultInstance("video.grayscale");
    auto grade = creative_suite::effects::makeDefaultInstance("video.primary_grade");
    CS_TEST_CHECK(creative_suite::effects::setParameterValue(grade, "red_gain", 136));
    auto graph = color::fromEffectStack(std::vector{effect, grade});
    CS_TEST_CHECK(color::addParallelBranch(graph, 1,
        creative_suite::effects::makeDefaultInstance("video.contrast")));
    CS_TEST_CHECK(commands.execute(SetClipColorGraphCommand{id, graph}).changed());
    CS_TEST_CHECK(!commands.execute(SetClipNodeGraphCommand{id, fusion::nodes::makePassthroughGraph()}).changed());
    CS_TEST_CHECK(!commands.execute(SetClipAudioCommand{id, 0.5, true}).changed());
    const auto original = session.timeline().tracks()[0].clips[0];
    CS_TEST_CHECK(session.timeline().clipAt(0, 35)->clip_index == 0);
    CS_TEST_CHECK(commands.execute(AddTrackCommand{"Adjustment Top"}).changed());
    const auto upper_track = session.timeline().tracks()[0].track_id;
    CS_TEST_CHECK(commands.execute(MoveClipCommand{id, upper_track, 30}).changed());
    CS_TEST_CHECK(commands.execute(MoveTrackCommand{upper_track, 1}).changed());
    CS_TEST_CHECK(session.timeline().locateClip(id)->track_index == 1);
    CS_TEST_CHECK(commands.undo().changed());
    CS_TEST_CHECK(commands.undo().changed());
    CS_TEST_CHECK(commands.undo().changed());
    auto duplicate = commands.execute(DuplicateAdjustmentClipCommand{id});
    CS_TEST_CHECK(duplicate.changed());
    const auto duplicate_id = *duplicate.selection.active_clip_id;
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[1].effects == original.effects);
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[1].color_graph == original.color_graph);
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[1].keyframes == original.keyframes);
    CS_TEST_CHECK(!commands.execute(AddTransitionCommand{track, id, duplicate_id,
        timeline::TransitionKind::FadeToBlack, 5}).changed());
    CS_TEST_CHECK(commands.undo().changed() && session.timeline().clipCount(0) == 1);
    CS_TEST_CHECK(commands.redo().changed() && session.timeline().clipCount(0) == 2);
    CS_TEST_CHECK(commands.undo().changed());
    const auto unchanged = session.timeline().snapshot();
    CS_TEST_CHECK(!commands.execute(AddAdjustmentClipCommand{track, INT64_MAX, 1}).changed());
    CS_TEST_CHECK(!commands.execute(TrimClipRangeCommand{id, 1, 100}).changed());
    CS_TEST_CHECK(session.timeline().snapshot() == unchanged);
    CS_TEST_CHECK(commands.execute(MoveClipCommand{id, track, 20}).changed());
    CS_TEST_CHECK(commands.undo().changed());
    CS_TEST_CHECK(commands.execute(SplitClipCommand{id, 75}).changed());
    const auto right = session.timeline().tracks()[0].clips[1];
    CS_TEST_CHECK(right.effects == original.effects);
    CS_TEST_CHECK(std::abs(timeline::evaluateTransform(right.transform, right.keyframes, 0).scale -
        timeline::evaluateTransform(original.transform, original.keyframes, 75).scale) < 1e-9);
    CS_TEST_CHECK(commands.undo().changed());
    CS_TEST_CHECK(commands.execute(TrimClipEdgeCommand{id, timeline::ClipEdge::Left, 35,
        timeline::ClipEdgeEditMode::Individual}).changed());
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].source_duration_frames == 0);
    CS_TEST_CHECK(commands.execute(TrimClipEdgeCommand{id, timeline::ClipEdge::Right, 500,
        timeline::ClipEdgeEditMode::Individual}).changed());
    CS_TEST_CHECK(commands.execute(SetProjectSettingsCommand{1920, 1080, {60, 1}}).changed());
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].timeline_start_frame == 70);
    auto document = ProjectDocumentMapper::toDocument(session, {});
    const auto path = QFileInfo(directory + "/adjustment.csp").filesystemFilePath();
    project::save(path, document);
    CS_TEST_CHECK(project::load(path) == document);
    std::atomic_bool cancel{false};
    const auto prepared = ProjectOpenService{}.prepare(path, path, {}, cancel);
    CS_TEST_CHECK(prepared.status == ProjectOpenStatus::Prepared && prepared.prepared);
    CS_TEST_CHECK(prepared.prepared->timeline.tracks[0].clips[0].effects == original.effects);
    CS_TEST_CHECK(prepared.prepared->timeline.tracks[0].clips[0].source_path.empty());
    project::AutosaveManager autosave(QFileInfo(directory + "/recovery").filesystemFilePath(), "adjustment-test");
    autosave.saveSnapshot(document, 5);
    const auto snapshots = autosave.unsavedSnapshots();
    CS_TEST_CHECK(snapshots.size() == 1 && project::load(snapshots[0].path) == document);
    QFile file(QString::fromStdWString(path.wstring()));
    CS_TEST_CHECK(file.open(QIODevice::ReadOnly));
    auto root = QJsonDocument::fromJson(file.readAll()).object(); file.close();
    CS_TEST_CHECK(root.value("version").toInt() == project::current_format_version);
    const auto record = root.value("timeline").toObject().value("tracks").toArray()[0].toObject().value("clips").toArray()[0].toObject();
    CS_TEST_CHECK(!record.contains("source") && !record.contains("text") && !record.contains("source_duration_frames"));
    root.insert("version", 25);
    CS_TEST_CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); file.write(QJsonDocument(root).toJson()); file.close();
    bool rejected = false;
    try { (void)project::load(path); } catch (const project::ProjectError&) { rejected = true; }
    CS_TEST_CHECK(rejected);
    root.insert("version", 26);
    auto clips = root["timeline"].toObject()["tracks"].toArray()[0].toObject()["clips"].toArray();
    auto invalid_clip = clips[0].toObject(); invalid_clip.insert("source", "unused.png"); clips[0] = invalid_clip;
    auto tracks = root["timeline"].toObject()["tracks"].toArray();
    auto invalid_track = tracks[0].toObject(); invalid_track.insert("clips", clips); tracks[0] = invalid_track;
    auto invalid_timeline = root["timeline"].toObject(); invalid_timeline.insert("tracks", tracks); root.insert("timeline", invalid_timeline);
    CS_TEST_CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); file.write(QJsonDocument(root).toJson()); file.close();
    rejected = false;
    try { (void)project::load(path); } catch (const project::ProjectError&) { rejected = true; }
    CS_TEST_CHECK(rejected);
    ui::EditWorkspaceController controller(session, commands);
    QDoubleSpinBox scale;
    ui::EditWorkspaceUi controls; controls.transform_spins[2] = &scale;
    controller.setUi(controls);
    controller.setPlaybackPresentation(false, false, true);
    controller.refreshInspectorPresentation();
    CS_TEST_CHECK(controller.selectedClipSupportsEffects() && scale.isEnabled());
    controller.handleTimelineEffectDrop("video.brightness", track, 80);
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[0].effects.size() == 3);
    controller.copySelectedClipAttributes();
    CS_TEST_CHECK(controller.canCopySelectedClipAttributes());
    controller.handleTimelineEffectDrop("video.adjustment_clip", track, 1000);
    CS_TEST_CHECK(session.timeline().clipCount(0) == 2);
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[1].timeline_duration_frames == 300);
    timeline::ClipAttributeOptions options; options.effects = true; options.transform = true;
    CS_TEST_CHECK(controller.applyCopiedClipAttributes(options).changed());
    CS_TEST_CHECK(session.timeline().tracks()[0].clips[1].effects.size() == 3);
    controller.deleteActiveTimelineClip();
    CS_TEST_CHECK(session.timeline().clipCount(0) == 1);
    CS_TEST_CHECK(commands.execute(DeleteClipCommand{id}).changed());
    CS_TEST_CHECK(commands.undo().changed());
}

class TestExportGpu final : public ExportGpuCompositor {
public:
    explicit TestExportGpu(int& calls) : calls_(calls) {}
    creative_suite::composition::OpenGlCompositionResult compose(int width, int height,
        const std::vector<CompositionLayer>& layers,
        const creative_suite::composition::OpenGlFrameCompositor::CancellationPredicate&,
        creative_suite::composition::OpenGlCompositionTimings*) override {
        ++calls_;
        return {creative_suite::composition::OpenGlCompositionStatus::Complete,
            FrameCompositor::compose(width, height, layers)};
    }
private:
    int& calls_;
};

void workerAndExport(const QString& directory) {
    CS_TEST_CHECK(logging::Logger::instance().initialize(QFileInfo(directory + "/logs").filesystemFilePath()));
    const auto pixels = std::make_shared<media::VideoFrame>(solid(180, 40, 20, 16, 9));
    const auto fullFrame = [](const media::VideoFrame& source) {
        return *FrameCompositor::compose(480, 270, {{&source, {}, {}}});
    };
    playback::CompositionLayerSpec image;
    image.kind = timeline::ClipKind::Image; image.still_frame = pixels;
    image.segment_frame_count = 8; image.track_index = 1; image.clip_index = 0;
    image.timeline_frame_rate = {30, 1};
    auto adjustment = image; adjustment.kind = timeline::ClipKind::Adjustment;
    adjustment.still_frame.reset(); adjustment.track_index = 0;
    adjustment.timeline_start_frame = 2; adjustment.segment_frame_count = 4;
    adjustment.effects = {creative_suite::effects::makeDefaultInstance("video.grayscale")};
    int gpu_calls = 0, errors = 0, warnings = 0;
    playback::PlaybackWorker worker(nullptr, [&](int w, int h, const auto& layers, const auto&, auto*) {
        ++gpu_calls;
        return creative_suite::composition::OpenGlCompositionResult{
            creative_suite::composition::OpenGlCompositionStatus::Complete,
            FrameCompositor::compose(w, h, layers)};
    });
    media::VideoFramePtr output;
    QObject::connect(&worker, &playback::PlaybackWorker::frameReady,
        [&](media::VideoFramePtr frame, qint64, quint64, quint64) { output = std::move(frame); });
    QObject::connect(&worker, &playback::PlaybackWorker::playbackError, [&](QString, qint64, quint64) { ++errors; });
    QObject::connect(&worker, &playback::PlaybackWorker::compositionWarning, [&](QString, qint64, quint64) { ++warnings; });
    worker.setCompositionCanvasSize(1920, 1080); worker.setPreviewQuality(playback::PreviewQuality::Quarter);
    worker.setGpuCompositionEnabled(true, nullptr);
    worker.setComposition({image, adjustment}, {}, 1);
    for (const auto frame : {0, 2, 5, 6}) {
        worker.renderCompositionFrame(frame, frame, 1);
        CS_TEST_CHECK(output);
        CS_TEST_CHECK(frame == 2 || frame == 5 ? output->rgba_pixels[0] == output->rgba_pixels[1] : output->rgba_pixels == fullFrame(*pixels).rgba_pixels);
    }
    CS_TEST_CHECK(gpu_calls == 2 && errors == 0 && warnings == 0);
    QImage png(pixels->rgba_pixels.data(), pixels->width, pixels->height, pixels->stride, QImage::Format_RGBA8888);
    const auto source = directory + "/image.png";
    CS_TEST_CHECK(png.save(source));
    RenderJob job;
    job.settings.output_path = directory + "/adjustment.mkv";
    job.settings.container_name = "matroska"; job.settings.video_encoder_name = "ffv1";
    job.settings.width = 480; job.settings.height = 270; job.settings.frame_rate = 30; job.settings.export_audio = false;
    auto& doc = job.project_snapshot;
    doc.canvas_width = 1920; doc.canvas_height = 1080;
    const auto source_path = QFileInfo(source).filesystemFilePath();
    doc.media.push_back({source_path, "image", "Unsorted", false, media::MediaKind::Image});
    project::ProjectClip media;
    media.kind = timeline::ClipKind::Image; media.source_path = source_path;
    media.clip_id = 1; media.duration_frames = media.source_duration_frames = 8;
    project::ProjectTrack lower; lower.name = "Lower"; lower.track_id = 2; lower.clips = {media};
    project::ProjectClip adjust;
    adjust.kind = timeline::ClipKind::Adjustment; adjust.clip_id = 2;
    adjust.timeline_start_frame = 2; adjust.duration_frames = 4; adjust.effects = adjustment.effects;
    project::ProjectTrack upper; upper.name = "Adjustment"; upper.track_id = 1; upper.clips = {adjust};
    doc.timeline_tracks = {upper, lower};
    std::atomic_bool cancel{false};
    OfflineExportOptions options;
    int export_gpu_calls = 0, export_warnings = 0;
    OfflineExportMetrics export_metrics;
    job.settings.gpu_composition_enabled = true;
    options.gpu_factory = [&](auto*) { return std::make_unique<TestExportGpu>(export_gpu_calls); };
    options.warning_callback = [&](const auto&, auto) { ++export_warnings; };
    options.metrics_callback = [&](const auto& metrics) { export_metrics = metrics; };
    int prepared_count = 0;
    options.prepared_frame_callback = [&](const auto& frame, std::int64_t timeline_frame) {
        worker.renderCompositionFrame(timeline_frame, timeline_frame, 1);
        CS_TEST_CHECK(output && output->rgba_pixels == frame.rgba_pixels);
        ++prepared_count;
    };
    OfflineExportRenderer::render(job, cancel, {}, options);
    CS_TEST_CHECK(prepared_count == 8);
    CS_TEST_CHECK(export_gpu_calls == 4 && export_metrics.gpu_frames == 4 && export_metrics.cpu_frames == 4);
    CS_TEST_CHECK(export_warnings == 0 && export_metrics.gpu_failures == 0);
    auto decoded = media::VideoPlaybackSession::open(QFileInfo(job.settings.output_path).filesystemFilePath());
    for (std::int64_t frame = 0; frame < 8; ++frame) {
        worker.renderCompositionFrame(frame, frame, 1);
        const auto encoded = decoded->decode_frame_at(frame);
        CS_TEST_CHECK(encoded && *encoded && output);
        CS_TEST_CHECK((*encoded)->rgba_pixels.size() == output->rgba_pixels.size());
        for (std::size_t byte = 0; byte < output->rgba_pixels.size(); ++byte)
            CS_TEST_CHECK(std::abs(int((*encoded)->rgba_pixels[byte]) - int(output->rgba_pixels[byte])) <= 6);
    }
    // Compare the complete prepared export canvas with preview on stacked,
    // transformed half-strength adjustments, cuts, Fusion and an upper layer.
    auto complex = job;
    complex.settings.output_path = directory + "/stacked.mkv";
    complex.project_snapshot.canvas_width = 1920; complex.project_snapshot.canvas_height = 1080;
    auto blue = std::make_shared<media::VideoFrame>(solid(0, 0, 255, 16, 9));
    const auto blue_path = directory + "/blue.png";
    QImage blue_png(blue->rgba_pixels.data(), blue->width, blue->height, blue->stride, QImage::Format_RGBA8888);
    CS_TEST_CHECK(blue_png.save(blue_path));
    auto red_spec = image; red_spec.track_index = 3; red_spec.segment_frame_count = 4;
    red_spec.node_graph = fusion::nodes::makePassthroughGraph();
    auto blue_spec = red_spec; blue_spec.still_frame = blue; blue_spec.timeline_start_frame = 4; blue_spec.clip_index = 1;
    auto lower_adjustment = adjustment; lower_adjustment.track_index = 2;
    lower_adjustment.transform.scale = 0.5; lower_adjustment.transform.opacity = 0.5;
    auto upper_adjustment = adjustment; upper_adjustment.track_index = 1;
    upper_adjustment.effects = {creative_suite::effects::makeDefaultInstance("video.brightness")};
    CS_TEST_CHECK(creative_suite::effects::setParameterValue(upper_adjustment.effects[0], "amount", 10));
    auto overlay_spec = blue_spec; overlay_spec.track_index = 0; overlay_spec.clip_index = 0;
    overlay_spec.timeline_start_frame = 3; overlay_spec.segment_frame_count = 1;
    worker.setComposition({red_spec, blue_spec, lower_adjustment, upper_adjustment, overlay_spec}, {}, 10);
    auto red_clip = media; red_clip.duration_frames = red_clip.source_duration_frames = 4; red_clip.node_graph = red_spec.node_graph;
    auto blue_clip = red_clip; blue_clip.clip_id = 3; blue_clip.source_path = QFileInfo(blue_path).filesystemFilePath(); blue_clip.timeline_start_frame = 4;
    auto lower_operation = adjust; lower_operation.clip_id = 4; lower_operation.transform = lower_adjustment.transform;
    auto upper_operation = adjust; upper_operation.clip_id = 5; upper_operation.effects = upper_adjustment.effects;
    auto overlay_clip = blue_clip; overlay_clip.clip_id = 6; overlay_clip.timeline_start_frame = 3; overlay_clip.duration_frames = overlay_clip.source_duration_frames = 1;
    auto& complex_doc = complex.project_snapshot;
    complex_doc.media.push_back({blue_clip.source_path, "Blue", "Unsorted", false, media::MediaKind::Image});
    complex_doc.timeline_tracks = {{"Overlay", 1.0, false, {overlay_clip}, {}, 4},
        {"Brightness", 1.0, false, {upper_operation}, {}, 3},
        {"Adjustment", 1.0, false, {lower_operation}, {}, 2},
        {"Sources", 1.0, false, {red_clip, blue_clip}, {}, 1}};
    OfflineExportOptions complex_options;
    complex.settings.gpu_composition_enabled = false;
    int complex_frames = 0;
    complex_options.prepared_frame_callback = [&](const auto& frame, std::int64_t timeline_frame) {
        worker.renderCompositionFrame(timeline_frame, timeline_frame, 10);
        CS_TEST_CHECK(output && output->rgba_pixels == frame.rgba_pixels);
        ++complex_frames;
    };
    OfflineExportRenderer::render(complex, cancel, {}, complex_options);
    CS_TEST_CHECK(complex_frames == 8);
    // A single adjustment spans a cut and a lower-track transition.
    auto incoming = image; incoming.still_frame = std::make_shared<media::VideoFrame>(solid(0, 0, 255, 16, 9));
    incoming.clip_index = 1; incoming.timeline_start_frame = 4; incoming.segment_frame_count = 4;
    auto outgoing = image; outgoing.segment_frame_count = 4;
    worker.setComposition({outgoing, incoming, adjustment}, {}, 12);
    worker.renderCompositionFrame(4, 4, 12);
    auto blue_gray = fullFrame(*incoming.still_frame);
    CS_TEST_CHECK(creative_suite::effects::applyStack(blue_gray, adjustment.effects));
    CS_TEST_CHECK(output->rgba_pixels == blue_gray.rgba_pixels);
    incoming.timeline_start_frame = 2;
    worker.setComposition({outgoing, incoming, adjustment}, {{1, 0, 1, 4, 2, timeline::TransitionKind::CrossDissolve}}, 13);
    worker.renderCompositionFrame(3, 3, 13); CS_TEST_CHECK(output && output->rgba_pixels[0] == output->rgba_pixels[1]);
    // Text and Fusion prepare their existing transparent frames before the adjustment.
    auto text = image; text.kind = timeline::ClipKind::Text; text.still_frame.reset(); text.track_index = 1;
    text.text.content = "A"; text.text.font_size_pixels = 128; text.text.color = {255, 0, 0, 255};
    worker.setComposition({text}, {}, 14); worker.renderCompositionFrame(3, 3, 14);
    auto text_gray = *output;
    CS_TEST_CHECK(creative_suite::effects::applyStack(text_gray, adjustment.effects));
    CS_TEST_CHECK(text_gray.rgba_pixels != output->rgba_pixels);
    worker.setComposition({text, adjustment}, {}, 14); worker.renderCompositionFrame(3, 3, 14);
    CS_TEST_CHECK(output && output->rgba_pixels == text_gray.rgba_pixels);
    outgoing = image; outgoing.node_graph = fusion::nodes::makePassthroughGraph();
    worker.setComposition({outgoing, adjustment}, {}, 15); worker.renderCompositionFrame(3, 3, 15);
    auto red_gray = fullFrame(*pixels); CS_TEST_CHECK(creative_suite::effects::applyStack(red_gray, adjustment.effects));
    CS_TEST_CHECK(output->rgba_pixels == red_gray.rgba_pixels);
    // A higher opaque track remains outside the adjustment; reordering changes its scope.
    incoming = image; incoming.still_frame = std::make_shared<media::VideoFrame>(solid(0, 0, 255, 16, 9));
    incoming.track_index = 0; adjustment.track_index = 1; outgoing.track_index = 2;
    worker.setComposition({outgoing, adjustment, incoming}, {}, 16); worker.renderCompositionFrame(3, 3, 16);
    CS_TEST_CHECK(output->rgba_pixels == fullFrame(*incoming.still_frame).rgba_pixels);
    incoming.track_index = 2; outgoing.track_index = 3;
    worker.setComposition({outgoing, adjustment, incoming}, {}, 17); worker.renderCompositionFrame(3, 3, 17);
    CS_TEST_CHECK(output->rgba_pixels == blue_gray.rgba_pixels);
    // Linked Motion publication reads the requested publication offset before adjustment.
    outgoing = image; outgoing.kind = timeline::ClipKind::Video; outgoing.still_frame.reset();
    outgoing.source_path = complex.settings.output_path; outgoing.linked_render_path = complex.settings.output_path;
    outgoing.linked_render_start_time_us = 66667; outgoing.track_index = 2;
    worker.setComposition({outgoing, adjustment}, {}, 18); worker.renderCompositionFrame(3, 3, 18);
    auto published = media::VideoPlaybackSession::open(QFileInfo(complex.settings.output_path).filesystemFilePath());
    const auto publication_frame = published->decode_frame_at(5);
    CS_TEST_CHECK(publication_frame && *publication_frame);
    auto publication_gray = **publication_frame;
    CS_TEST_CHECK(creative_suite::effects::applyStack(publication_gray, adjustment.effects));
    CS_TEST_CHECK(output && output->rgba_pixels == publication_gray.rgba_pixels);
    // Cancellation preserves the already published output and logs no technical error.
    cancel.store(true);
    bool export_canceled = false;
    try { OfflineExportRenderer::render(job, cancel); } catch (const ExportCanceled&) { export_canceled = true; }
    CS_TEST_CHECK(export_canceled && QFileInfo::exists(job.settings.output_path));
    // Technical source failure is logged before feedback, even under an adjustment.
    logging::Logger::instance().initialize(QFileInfo(directory + "/logs").filesystemFilePath());
    bool logged_before_error = false;
    QObject::connect(&worker, &playback::PlaybackWorker::playbackError, [&](QString, qint64, quint64) {
        QFile log(QString::fromStdWString(logging::Logger::instance().log_path().wstring()));
        logged_before_error = log.open(QIODevice::ReadOnly) && log.readAll().contains("missing.mkv");
    });
    outgoing.source_path = directory + "/missing.mkv"; outgoing.linked_render_path.clear();
    worker.setComposition({outgoing, adjustment}, {}, 19);
    CS_TEST_CHECK(errors > 0 && logged_before_error);
}

void benchmark(const std::string& size) {
    const int count = size == "small" ? 10 : size == "medium" ? 100 : 1000;
    const int width = size == "small" ? 480 : size == "medium" ? 960 : 1920;
    const int height = size == "small" ? 270 : size == "medium" ? 540 : 1080;
    const int active = size == "small" ? 2 : size == "medium" ? 4 : 8;
    const int adjustments = size == "small" ? 1 : size == "medium" ? 2 : 3;
    auto source = std::make_shared<media::VideoFrame>(solid(180, 40, 20, width, height));
    QVector<playback::CompositionLayerSpec> specs;
    for (int index = 0; index < count; ++index) {
        playback::CompositionLayerSpec spec;
        spec.kind = timeline::ClipKind::Image; spec.still_frame = source;
        spec.track_index = index % active + adjustments;
        spec.clip_index = index / active; spec.timeline_start_frame = spec.clip_index * 30;
        spec.segment_frame_count = 30; spec.timeline_frame_rate = {30, 1}; specs.push_back(spec);
    }
    for (int index = 0; index < adjustments; ++index) {
        playback::CompositionLayerSpec spec;
        spec.kind = timeline::ClipKind::Adjustment; spec.track_index = index;
        spec.segment_frame_count = (count / active + 1) * 30;
        spec.effects = {creative_suite::effects::makeDefaultInstance("video.grayscale")};
        spec.transform.opacity = 0.5; spec.timeline_frame_rate = {30, 1}; specs.push_back(spec);
    }
    playback::PlaybackWorker worker;
    media::VideoFramePtr result;
    QObject::connect(&worker, &playback::PlaybackWorker::frameReady,
        [&](media::VideoFramePtr frame, qint64, quint64, quint64) { result = std::move(frame); });
    worker.setCompositionCanvasSize(1920, 1080);
    worker.setPreviewQuality(size == "small" ? playback::PreviewQuality::Quarter :
        size == "medium" ? playback::PreviewQuality::Half : playback::PreviewQuality::Full);
    worker.setComposition(specs, {}, 1);
    const auto run = [&](bool seek) {
        const auto start = std::chrono::steady_clock::now();
        for (int index = 0; index < 12; ++index) {
            const auto frame = seek ? (index * 73) % (count / active * 30) : index;
            worker.renderCompositionFrame(frame, frame, 1); CS_TEST_CHECK(result);
        }
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 12;
    };
    const auto sequential = run(false), seek = run(true);
    std::cout << size << "," << count << "," << active << "," << adjustments << ","
              << width << "x" << height << "," << sequential << "," << seek << std::endl;
}
} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    if (argc == 3 && std::string(argv[1]) == "--benchmark") { benchmark(argv[2]); return 0; }
    QTemporaryDir directory;
    try {
        CS_TEST_CHECK(directory.isValid());
        pixels();
        curveContinuity();
        timelineUi();
        workspaceUi();
        editingAndPersistence(directory.path());
        workerAndExport(directory.path());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
