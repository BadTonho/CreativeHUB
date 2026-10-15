#include "application/composition_interchange.h"
#include "application/editor_session.h"
#include "application/project_document_mapper.h"
#include "application/timeline_command_service.h"
#include "model/composition_interchange.h"
#include "persistence/motion_document_store.h"
#include "project/autosave_manager.h"
#include "project/project_file.h"
#include "rendering/composition_frame_renderer.h"
#include "rendering/graph_generators.h"
#include "workspaces/edit/controllers/edit_workspace_controller.h"
#include <QApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <cmath>
#include <creative_suite/composition/frame_compositor.h>
#include <creative_suite/composition_exchange/raster.h>
#include <creative_suite/media/still_image_decoder.h>
#include <creative_suite/media/media_importer.h>
#include <creative_suite/media/video_playback.h>
#include <creative_suite/media/video_probe.h>
#include <cstdlib>
#include <iostream>
#include <set>

namespace x = creative_suite::composition_exchange;
namespace {
void check(bool value, const char *message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
template <class F> void rejected(F f, const char *message) {
    bool caught = false;
    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }
    check(caught, message);
}
creative_suite::media::RgbaFramePtr fusionFrame(const x::Document &d, int frame, int gpu_mode=0) {
    auto graph = application::importComposition(d);
    std::set<std::pair<double, double>> positions;
    for (const auto &n : graph.nodes)
        check(positions.emplace(n.x, n.y).second, "node positions overlap");
    fusion::nodes::InputFrames inputs;
    for (const auto &n : graph.nodes) {
        if (n.type != fusion::nodes::NodeType::Input)
            continue;
        if (n.source_is_still)
            inputs[n.id] = std::make_shared<const creative_suite::media::RgbaFrame>(
                creative_suite::media::StillImageDecoder{}.decode_first_frame(n.source_path));
        else {
            auto index =
                x::sourceFrame(*n.presentation, frame, n.source_frame_rate, n.source_frame_count,
                               double(d.fps_numerator) / d.fps_denominator);
            if (index) {
                auto decoder = creative_suite::media::VideoPlaybackSession::open(n.source_path);
                auto decoded = decoder->decode_frame_at(*index);
                if (decoded && *decoded)
                    inputs[n.id] = *decoded;
            }
        }
    }
    rendering::prepareGraphFrames(graph, inputs, d.width, d.height, true, {}, frame);
    fusion::nodes::EvaluationContext context;
    context.local_frame = frame;
    context.canvas_width = d.width;
    context.canvas_height = d.height;
    auto result = fusion::nodes::evaluate(graph, inputs, context);
    if (gpu_mode) {
        using namespace creative_suite::composition;
        QSurfaceFormat format;format.setVersion(3,2);format.setProfile(QSurfaceFormat::CoreProfile);
        QOffscreenSurface surface;surface.setFormat(format);surface.create();
        auto budget = gpu_mode==2 ? std::make_shared<OpenGlTexturePoolBudget>(1) : std::shared_ptr<OpenGlTexturePoolBudget>{};
        OpenGlFrameCompositor backend(&surface,OpenGlPrecisionPolicy::Automatic,nullptr,budget);
        bool used=false;context.gpu=&backend;context.gpu_used=&used;
        auto evaluated=fusion::nodes::evaluateFrame(graph,inputs,context);
        check(bool(evaluated),"GPU graph/fallback frame missing");
        if(evaluated->texture){auto rgba=backend.readback(evaluated->texture);check(bool(rgba.frame),"GPU readback");result=std::move(rgba.frame);}
        else result=std::move(evaluated->rgba);
        std::cout<<"GPU graph mode "<<gpu_mode<<" used "<<used<<'\n';
        if (gpu_mode==1 && surface.isValid() && QOpenGLContext::supportsThreadedOpenGL())
            check(used,"available GPU unexpectedly fell back for a compatible graph");
        if(gpu_mode==2)check(!used,"resource-limited graph must recover on CPU");
        evaluated.reset();
    }
    check(bool(result), "Fusion frame missing");
    auto output =
        creative_suite::composition::FrameCompositor::compose(d.width, d.height, {{&*result, {}}});
    check(bool(output), "Fusion composite missing");
    return std::make_shared<const creative_suite::media::RgbaFrame>(std::move(*output));
}
creative_suite::media::RgbaFramePtr motionFrame(const motion::model::MotionProjectData &d,
                                                int frame) {
    motion::ui::PreviewRequest request;
    request.canvas_size = d.composition.canvas_size;
    request.frame_rate = d.composition.frame_rate;
    request.black_canvas_when_empty = true;
    for (const auto &l : d.layers) {
        if (!l.visible || frame < l.timeline_start_frame ||
            frame - l.timeline_start_frame >= l.duration_frames)
            continue;
        motion::ui::PreviewLayerSnapshot s;
        s.id = l.id;
        s.kind = l.kind;
        s.local_frame = frame - l.timeline_start_frame;
        s.transform = l.transform;
        s.keyframes = l.keyframes;
        s.content = l.content;
        s.effects = l.effects;
        s.presentation = l.presentation;
        s.source_path = l.source_path;
        s.source_frame_count = l.source_frame_count;
        s.source_frame_rate = l.source_frame_rate;
        s.source_start_frame = l.source_start_frame;
        if (l.kind == motion::model::LayerKind::Image)
            s.still_frame = std::make_shared<const creative_suite::media::RgbaFrame>(
                creative_suite::media::StillImageDecoder{}.decode_first_frame(l.source_path));
        request.layers.push_back(std::move(s));
    }
    motion::ui::CompositionFrameRenderer renderer(false);
    return renderer.render(request, {}, true);
}
void compare(const x::Document &d, const motion::model::MotionProjectData &project, int frame,
             const char *message) {
    auto a = fusionFrame(d, frame), b = motionFrame(project, frame);
    check(bool(b) && a->rgba_pixels.size() == b->rgba_pixels.size(), "render geometry");
    int maximum = 0;
    for (std::size_t i = 0; i < a->rgba_pixels.size(); ++i)
        maximum = std::max(maximum, std::abs(int(a->rgba_pixels[i]) - int(b->rgba_pixels[i])));
    std::cout << "frame " << frame << " maximum channel difference " << maximum << '\n';
    // Straight RGBA intermediates and final source-over composition round at
    // different boundaries in the two existing renderers.
    check(maximum <= 2, message);
}
} // namespace
int main(int argc, char **argv) try {
    std::cout << std::unitbuf;
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary directory");
    auto dir = std::filesystem::path(temporary.path().toStdU16String());
    x::Document d;
    d.name = "Editable composition";
    d.width = 320;
    d.height = 180;
    d.duration = 90;
    x::Layer bg;
    bg.id = 3;
    bg.kind = x::Kind::Background;
    bg.presentation.name = "Background";
    bg.presentation.duration = 90;
    bg.color = {70, 120, 190, 128};
    d.layers.push_back(bg);
    x::Layer text;
    text.id = 8;
    text.kind = x::Kind::Text;
    text.presentation.name = "Title";
    text.presentation.duration = 90;
    text.text.content = "Editable\nText";
    text.text.size = 24.5;
    text.transform.position_x = .42;
    check(creative_suite::animation::setKeyframe(
              text.curves, creative_suite::animation::TransformProperty::Opacity, 0, .2),
          "keyframe");
    check(creative_suite::animation::setKeyframe(
              text.curves, creative_suite::animation::TransformProperty::Opacity, 80, .9),
          "keyframe");
    text.curves.opacity.front().interpolation =
        creative_suite::animation::InterpolationMode::CubicBezier;
    text.curves.opacity.front().easing = {.2, .1, .8, .9};
    text.colors = {{10, 100, 100}, {40, 150, 60}};
    text.colors_enabled = {true, false};
    d.layers.push_back(text);
    check(x::validate(d).empty(), "valid interchange");
    const auto path = dir / "roundtrip.cscomp";
    check(x::save(path, d), "save");
    auto loaded = x::load(path);
    check(loaded && loaded->layers.size() == 2 && loaded->name == d.name, "load");
    check(!x::save(path, d, [] { return true; }), "cancel save");
    check(x::load(path)->name == d.name, "cancel preserves destination");
    check(!x::load(path, [] { return true; }), "cancel load");
    auto invalid = x::toJson(d, path);
    invalid.insert("version", 2);
    rejected([&] { (void)x::fromJson(invalid, path); }, "future interchange");
    invalid = x::toJson(d, path);
    invalid.insert("width", -1);
    rejected([&] { (void)x::fromJson(invalid, path); }, "invalid geometry");
    auto project = motion::model::importComposition(*loaded);
    check(project.layers[1].keyframes == text.curves, "editable curves");
    {
        motion::model::CompositionDocument editable(320, 180, {30, 1}, project.layers);
        auto title = std::get<motion::model::TextLayerContent>(project.layers[1].content);
        title.text = "Edited in Motion";
        title.font_size_pixels = 28.5;
        auto fill = std::get<motion::model::ShapeLayerContent>(project.layers[0].content);
        fill.fill_color = {11, 22, 33, 99};
        check(editable.setTextLayerContent(text.id, title) &&
                  editable.setShapeLayerContent(bg.id, fill) &&
                  editable.setLayerName(text.id, "Edited title") &&
                  editable.setLayerKeyframe(text.id,
                      creative_suite::animation::TransformProperty::Opacity, 30, .4),
              "normal Motion controls edit imported layers");
        auto edited_project = project;
        edited_project.layers = editable.layers();
        auto edited = motion::model::exportComposition(edited_project);
        check(edited.layers[1].text.content == title.text && edited.layers[1].text.size == 28.5 &&
                  edited.layers[1].presentation.name == "Edited title" &&
                  edited.layers[0].color == fill.fill_color &&
                  project.layers[1].keyframes == text.curves,
              "edited export and independent source composition");
    }
    auto motion_path = dir / "saved.motion";
    motion::persistence::MotionDocumentStore::save(motion_path, project);
    auto reopened = motion::persistence::MotionDocumentStore::load(motion_path);
    check(reopened == project, "Motion native roundtrip");
    auto recovery = dir / "recovery.motion";
    motion::persistence::MotionDocumentStore::saveRecovery(recovery, {}, "interchange", project);
    check(motion::persistence::MotionDocumentStore::loadRecovery(recovery).document == project,
          "Motion recovery");
    auto returned = motion::model::exportComposition(reopened);
    check(returned.name == d.name && returned.layers[1].text.content == text.text.content &&
              returned.layers[1].text.size == 24.5 && !returned.layers[1].colors_enabled.back(),
          "editable roundtrip properties");
    for (int frame : {0, 20, 60, 89})
        compare(returned, reopened, frame, "Fusion/Motion RGBA parity");

    QImage image(73, 41, QImage::Format_RGBA8888);
    image.fill(QColor(180, 30, 90, 127));
    auto image_path = dir / std::filesystem::path(u8"source-image-\u00e7\u732b.png");
    check(image.save(QString::fromStdU16String(image_path.u16string())), "source image");
    auto native_motion = d;
    x::Layer image_layer;
    image_layer.id = 12;
    image_layer.kind = x::Kind::Image;
    image_layer.source = image_path;
    image_layer.presentation.profile = x::Profile::MotionLayer;
    image_layer.presentation.start = 10;
    image_layer.presentation.duration = 60;
    image_layer.transform = {.62, .43, .73, 13, .5};
    native_motion.layers.push_back(image_layer);
    for (auto &l : native_motion.layers) {
        l.presentation.profile = x::Profile::MotionLayer;
        if (l.kind == x::Kind::Text) {
            l.presentation.text_width = 180;
            l.presentation.text_height = 100;
        }
    }
    auto unicode_file = dir / std::filesystem::path(u8"interchange-\u00e7\u732b.cscomp");
    check(x::save(unicode_file, native_motion), "Unicode save");
    auto unicode_loaded = x::load(unicode_file);
    check(unicode_loaded && unicode_loaded->layers.back().source == image_path,
          "Unicode relative media");
    auto native_project = motion::model::importComposition(native_motion);
    auto native_returned = motion::model::exportComposition(native_project);
    for (int frame : {0, 10, 40, 80})
        compare(native_returned, native_project, frame, "native Motion RGBA parity");
    auto gpu_doc=native_returned;
    // Stay within the existing three-target pool for the GPU success case.
    // The separate one-byte budget exercises complete CPU recovery.
    gpu_doc.layers.pop_back();
    for(auto& layer:gpu_doc.layers)layer.transform.rotation_degrees=0;
    const auto cpu=fusionFrame(gpu_doc,40);
    for(int mode:{1,2}){
        const auto gpu=fusionFrame(gpu_doc,40,mode);int maximum=0;
        for(std::size_t i=0;i<cpu->rgba_pixels.size();++i)maximum=std::max(maximum,std::abs(int(cpu->rgba_pixels[i])-int(gpu->rgba_pixels[i])));
        check(maximum<=2,"interchange GPU/CPU/fallback parity");
    }
    check(argc > 1, "reference video");
    auto video_path = std::filesystem::absolute(argv[1]);
    auto metadata = creative_suite::media::VideoProbe{}.probe(video_path);
    x::Layer video;
    video.id = 17;
    video.kind = x::Kind::Video;
    video.source = video_path;
    video.source_rate = metadata.frame_rate.value_or(30);
    video.source_count = metadata.frame_count.value_or(
        std::int64_t(std::ceil(metadata.duration_seconds.value_or(1) * video.source_rate)));
    video.presentation.start = 7;
    video.presentation.duration = 60;
    video.presentation.offset = 10;
    video.transform = {.53, .48, .83, 16, .7};
    for (auto profile : {x::Profile::FusionCanvas, x::Profile::MotionLayer}) {
        video.presentation.profile = profile;
        auto video_doc = native_motion;
        video_doc.layers.push_back(video);
        auto video_project = motion::model::importComposition(video_doc);
        auto toggled = video_project;
        toggled.layers.back().presentation->visible = false;
        auto visible = motionFrame(toggled, 18), baseline = motionFrame(video_project, 18);
        check(visible && baseline && visible->rgba_pixels == baseline->rgba_pixels,
              "unhidden video");
        for (int frame : {0, 7, 18, 59, 80})
            compare(video_doc, video_project, frame, "video raster/source-clock parity");
    }

    application::EditorSession session;
    application::TimelineCommandService commands(session);
    ui::EditWorkspaceController controller(session, commands);
    controller.setPlaybackPresentation(false, false, true);
    int commits = 0;
    qint64 rendered_global = -1, rendered_local = -1;
    QObject::connect(&controller, &ui::EditWorkspaceController::timelineEditCommitted,
        [&](const application::TimelineEditResult& result, bool, bool refresh, const QString&) {
            check(result.changed() && refresh, "import refresh notification");
            ++commits;
        });
    QObject::connect(&controller, &ui::EditWorkspaceController::renderCompositionFrameRequested,
        [&](qint64 global, qint64 local) { rendered_global = global; rendered_local = local; });
    auto track = session.timeline().tracks().front().track_id;
    application::AddFusionCompositionCommand add{track, 10, d.duration};
    add.graph = application::importComposition(returned);
    add.display_name = d.name;
    auto applied = controller.insertPreparedComposition(add);
    check(applied.changed() && applied.invalidate_playback &&
              applied.selection.active_clip_id.has_value(),
          "insertion result");
    check(commits == 1 && rendered_global == 10 && rendered_local == 0 &&
              session.playheadFrame() == 0 && controller.timelinePlayheadFrame() == 10,
          "import selects the clip and refreshes its global timeline position");
    check(controller.insertPreparedComposition(add).status == application::EditStatus::Rejected &&
              commits == 1, "collision must not publish a committed edit");
    check(commands.undo().changed() && session.timeline().tracks().front().clips.empty(), "undo");
    check(commands.redo().changed(), "redo");
    auto document = application::ProjectDocumentMapper::toDocument(session);
    auto csp = dir / "saved.csp";
    project::save(csp, document);
    check(project::load(csp) == document, "CSP native roundtrip");
    project::AutosaveManager autosave(dir / "autosave", "interchange");
    autosave.saveSnapshot(document, csp, 5);
    auto snapshots = autosave.validSnapshotsForProject(csp);
    check(snapshots.size() == 1 && project::load(snapshots.front().path) == document,
          "CSP autosave");
    auto fps =
        timeline::TimelineModel::rescaleSnapshotFrameRate(session.timeline().snapshot(), {60, 1});
    check(fps &&
              fps->tracks.front().clips.front().node_graph->nodes.front().presentation->duration ==
                  180,
          "FPS source windows");
    auto clip = session.timeline().tracks().front().clips.front();
    auto exported = application::exportComposition(clip, 320, 180, {30, 1});
    check(exported.layers.size() == 2 && exported.name == d.name, "Fusion reverse export");
    check(commands.execute(application::SplitClipCommand{clip.clip_id, 30}).changed(), "split");
    auto right_doc = application::exportComposition(
        session.timeline().tracks().front().clips.back(), 320, 180, {30, 1});
    check(right_doc.duration == 60 && right_doc.layers.front().presentation.start == 0 &&
              right_doc.layers.front().presentation.offset == 30,
          "split source clock");
    for (int frame : {0, 15, 59}) {
        auto sliced = creative_suite::animation::evaluateTransform(
            right_doc.layers[1].transform, right_doc.layers[1].curves, frame);
        auto original = creative_suite::animation::evaluateTransform(
            d.layers[1].transform, d.layers[1].curves, frame + 30);
        check(std::abs(sliced.opacity - original.opacity) < 1e-8, "Bezier split continuity");
    }
    {
        application::EditorSession media_session;
        application::TimelineCommandService media_commands(media_session);
        ui::EditWorkspaceController media_controller(media_session, media_commands);
        auto image_document = native_motion;
        image_document.layers = {image_layer};
        image_document.layers.front().source =
            creative_suite::media::MediaLibrary::canonicalPath(image_path);
        application::AddFusionCompositionCommand prepared{
            media_session.timeline().tracks().front().track_id, 200, image_document.duration};
        prepared.graph = application::importComposition(image_document);
        std::atomic_bool canceled{false};
        auto batch = creative_suite::media::MediaImporter{}.process({image_path}, canceled);
        check(batch.files.size() == 1 && batch.files.front().item.has_value(), "prepare image media");
        prepared.media.push_back(std::move(*batch.files.front().item));
        const auto* pixels = prepared.media.front().first_frame.rgba_pixels.data();
        check(media_controller.insertPreparedComposition(std::move(prepared)).changed() &&
                  media_session.mediaLibrary().contains(image_path) &&
                  media_session.mediaLibrary().items().front().first_frame.rgba_pixels.data() == pixels,
              "prepared import commits media without copying pixels");
        check(media_commands.undo().changed() &&
                  media_session.timeline().tracks().front().clips.empty() &&
                  media_commands.redo().changed(), "prepared media import history");
    }
    auto processed_clip = clip;
    processed_clip.transform.opacity = .5;
    bool unsupported_clip = false;
    try {
        (void)application::exportComposition(processed_clip, 320, 180, {30, 1});
    } catch (const x::Unsupported&) {
        unsupported_clip = true;
    }
    check(unsupported_clip, "clip processing is an expected incompatibility, not a technical failure");
    clip.node_graph->nodes.push_back(
        {clip.node_graph->next_id++, fusion::nodes::NodeType::RectangleMask});
    rejected([&] { (void)application::exportComposition(clip, 320, 180, {30, 1}); },
             "mask refusal");
    project.layers.front().effects.emplace_back(motion::model::GaussianBlurEffect{});
    rejected([&] { (void)motion::model::exportComposition(project); }, "blur refusal");
    x::Presentation timing;
    timing.duration = 100;
    timing.offset = -10;
    check(!x::sourceFrame(timing, 0, 30, 20, 30) && x::sourceFrame(timing, 10, 30, 20, 30) == 0,
          "source clock");
    check(!x::sourceFrame(timing, 40, 30, 20, 30), "Fusion EOF");
    timing.profile = x::Profile::MotionLayer;
    check(x::sourceFrame(timing, 40, 30, 20, 30) == 19, "Motion EOF");
    timing.duration = 0;
    check(x::sourceFrame(timing, INT64_MAX, 30, 20, 30) == 19, "numeric last-frame hold");
    check(rendering::graphFrameClock(INT64_MAX, 1) == INT64_MAX &&
              rendering::graphFrameClock(INT64_MIN, -1) == INT64_MIN,
          "numeric source clock");
    rejected([] { (void)x::transparent(32768, 32768); }, "raster resource budget");
    auto bounded = d;
    bounded.width = 4000;
    bounded.height = 4000;
    bounded.layers.clear();
    for (std::uint64_t id = 1; id <= 5; ++id) {
        auto l = bg;
        l.id = id;
        bounded.layers.push_back(l);
    }
    auto graph = application::importComposition(bounded);
    rejected(
        [&] {
            fusion::nodes::InputFrames frames;
            rendering::prepareGraphFrames(graph, frames, 4000, 4000, true);
        },
        "aggregate raster budget");
    auto stages = d;
    stages.layers.clear();
    for (std::uint64_t id = 1; id <= 17; ++id) {
        auto l = bg;
        l.id = id;
        l.colors.resize(1024);
        stages.layers.push_back(l);
    }
    check(!x::validate(stages).empty(), "aggregate stage budget");
    auto missing = d;
    missing.layers.front().kind = x::Kind::Video;
    missing.layers.front().source = dir / "missing.mkv";
    missing.layers.front().source_count = 30;
    missing.layers.front().source_rate = 30;
    check(!x::validate(missing, true).empty(), "missing media");
    std::cout << "Composition interchange passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "Composition interchange failed: " << error.what() << '\n';
    return 1;
}
