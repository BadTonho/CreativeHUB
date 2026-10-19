#include "model/bezier_path.h"
#include "export/motion_video_export.h"
#include "persistence/motion_recovery_store.h"
#include <creative_suite/media/video_decoder.h>
#include <creative_suite/media/video_encoder.h>
#include <QImage>
#include "model/composition_document.h"
#include "model/composition_interchange.h"
#include "model/layer_transform_hierarchy.h"
#include "persistence/motion_document_store.h"
#include "persistence/path_serialization.h"
#include "rendering/composition_frame_renderer.h"
#include "rendering/path_rasterizer.h"
#include "rendering/preview_scene_builder.h"
#include "ui/main_window/main_window.h"
#include "ui/viewer/composition_viewer.h"
#include "ui/timeline/timeline_navigator.h"
#include "ui/inspector/inspector_widget.h"
#include <creative_suite/diagnostics/logger.h>
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSlider>
#include <QFileDialog>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QListWidget>
#include <QMessageBox>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
using namespace motion::model;
using namespace motion::ui;
using Store = motion::persistence::MotionDocumentStore;
namespace a = creative_suite::animation;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double a, double b, const char *message) {
    check(std::abs(a - b) < 1e-8, message);
}
std::filesystem::path file(const QString &s) {
    return std::filesystem::path(s.toStdU16String());
}
PathGeometry rectangle(double l = -30, double t = -15, double r = 30, double b = 15) {
    return {{{{l, t}, {}, {}}, {{r, t}, {}, {}}, {{r, b}, {}, {}}, {{l, b}, {}, {}}}, true};
}
MotionProjectData project(const CompositionDocument &doc) {
    MotionProjectData p;
    p.composition = {doc.canvasSize(), doc.frameRate(), "Bezier", 0};
    p.layers = doc.layers();
    p.masks = doc.masks();
    return p;
}
PreviewRequest request(const CompositionDocument &doc, std::int64_t frame = 0) {
    PreviewRequest r;
    r.canvas_size = doc.canvasSize();
    r.frame_rate = doc.frameRate();
    r.composition_frame = frame;
    r.hierarchy = std::make_shared<const LayerTransformHierarchy>(doc.canvasSize(), doc.layers());
    r.masks = std::make_shared<const std::vector<Mask>>(doc.masks());
    preparePreviewLayers(r, doc.layers(), {});
    r.freezeScene();
    return r;
}
void pure() {
    const auto default_path = defaultShapeLayerContent({160, 90}, ShapeKind::BezierPath);
    check(default_path.path && default_path.path.valid(), "typed default Bezier content is valid");
    auto p = rectangle();
    check(validPathGeometry(p), "closed path validation");
    auto open = p;
    open.closed = false;
    open.nodes.resize(2);
    check(validPathGeometry(open) && !validPathGeometry(open, true), "open shapes, closed masks");
    auto invalid = p;
    invalid.nodes[0].incoming.x = std::numeric_limits<double>::infinity();
    check(!validPathGeometry(invalid), "reject nonfinite tangents");
    invalid = p;
    invalid.nodes.resize(4097);
    check(!validPathGeometry(invalid), "point budget");
    PathGeometry curve{{{{-20, 0}, {}, {30, 50}}, {{60, 10}, {-20, -30}, {}}}, false};
    auto split = curve;
    check(subdividePath(split, 0, .37), "curve subdivision");
    for (int i = 0; i <= 100; ++i) {
        const auto t = i / 100.;
        const auto original = pathSegmentPoint(curve, 0, t);
        const auto actual =
            t <= .37 ? pathSegmentPoint(split, 0, t / .37) : pathSegmentPoint(split, 1, (t - .37) / .63);
        near(original.x, actual.x, "subdivision x preserves curve");
        near(original.y, actual.y, "subdivision y preserves curve");
    }
    check(!subdividePath(split, 0, 0) && !subdividePath(split, 2, .5), "invalid split atomic");
    auto q = p;
    for (auto &n : q.nodes) {
        n.point.x += 40;
        n.incoming = {-2, 4};
        n.outgoing = {2, -4};
    }
    PathAnimation animation{p, {{0, p}, {20, q}}};
    check(validPathAnimation(animation), "matching path snapshots");
    const auto middle = evaluatePath(animation, 10);
    near(middle.nodes[0].point.x, -10, "linear path x");
    near(middle.nodes[0].incoming.y, 2, "relative tangent interpolation");
    check(evaluatePath(animation, -4) == p && evaluatePath(animation, 30) == q, "path endpoint clamps");
    animation.keys[0].interpolation = a::InterpolationMode::CubicBezier;
    animation.keys[0].easing = {.42, 0, 1, 1};
    const auto eased = evaluatePath(animation, 10);
    const auto fraction =
        a::evaluateScalar(0, {{0, 0, a::InterpolationMode::CubicBezier, {.42, 0, 1, 1}}, {20, 1}}, 10);
    near(eased.nodes[0].point.x, -30 + 40 * fraction, "Bezier timing reuses scalar evaluator");
    animation.keys[1].geometry.nodes.pop_back();
    check(!validPathAnimation(animation), "reject differing point counts");
    animation.keys[1].geometry = q;
    animation.keys[1].geometry.closed = false;
    check(!validPathAnimation(animation), "reject differing closure");
    PathAnimation large;
    large.base.nodes.resize(4096);
    for (int i = 0; i < 82; ++i)
        large.keys.push_back({i, large.base});
    check(!validPathAnimation(large), "weighted coordinate budget");
}
void document(const QString &root) {
    CompositionDocument doc(160, 90, {30, 1});
    LayerId id = 0;
    auto p = rectangle();
    check(doc.createPathLayer(p, 10, &id) && doc.layers()[0].duration_frames == 150, "new path timing");
    PathTarget target{id, false};
    const auto frozen = request(doc, 10);
    check(std::get<ShapeLayerContent>(frozen.sceneLayers().front().content).path.operator->() ==
              doc.pathFor(target),
          "immutable scene shares path curves without cloning");
    check(doc.setPathKeyframe(target, 10) && doc.setPathKeyframe(target, 30), "shape keys use local clock");
    check(doc.pathFor(target)->keys[0].frame == 0 && doc.pathFor(target)->keys[1].frame == 20,
          "local key frames");
    auto edited = p;
    edited.nodes[0].point.x -= 10;
    check(doc.setPathAtFrame(target, edited, 20), "insert snapshot between keys");
    check(doc.pathFor(target)->keys.size() == 3 && doc.pathFor(target)->keys[1].frame == 10,
          "snapshot insertion");
    check(!doc.setPathAtFrame(target, edited, 20), "same evaluated geometry no key/history");
    check(!doc.movePathKeyframe(target, 10, 20) && doc.movePathKeyframe(target, 10, 11),
          "path key collision rejection");
    check(doc.setPathKeyframeInterpolation(target, 0, a::InterpolationMode::CubicBezier, {.42, 0, 1, 1}),
          "path easing edit");
    auto topology = edited;
    check(subdividePath(topology, 0, .5) && !doc.setPathAtFrame(target, topology, 21),
          "topology locked when keyed");
    const auto evaluated = evaluatePath(*doc.pathFor(target), 11);
    check(doc.clearPathAnimationAtFrame(target, 21) && doc.pathFor(target)->base == evaluated,
          "remove animation retains evaluated pose");
    check(doc.setPathAtFrame(target, topology, 21), "static topology editable");
    const auto mask = doc.createPathMask(id, -2, rectangle(.25, .25, .75, .75));
    check(mask && doc.setPathKeyframe({mask, true}, 21), "mask key uses composition clock");
    check(doc.pathFor({mask, true})->keys[0].frame == 21, "mask composition frame");
    const auto source = file(root + "/paths.motion");
    Store::save(source, project(doc));
    check(Store::load(source) == project(doc), "v15 path/mask round trip");
    Store::saveRecovery(file(root + "/paths.recovery"), source, "paths", project(doc));
    check(Store::loadRecovery(file(root + "/paths.recovery")).document == project(doc),
          "recovery wrapper keeps paths");
    bool refused = false;
    try {
        (void)exportComposition(project(doc));
    } catch (const creative_suite::composition_exchange::Unsupported &e) {
        const std::string text = e.what();
        refused = text.find("Bezier mask") != std::string::npos &&
                  text.find(std::to_string(id)) != std::string::npos;
    }
    check(refused, "interchange explicit mask/layer rejection");
    for (int version = 1; version <= 14; ++version) {
        CompositionDocument legacy(160, 90, {30, 1});
        check(legacy.addContentLayer(LayerKind::Shape, "Old", 0), "legacy fixture");
        const auto legacyFile = root + QString("/v%1.motion").arg(version);
        Store::save(file(legacyFile), project(legacy));
        QFile f(legacyFile);
        check(f.open(QIODevice::ReadOnly), "read fixture");
        auto json = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        json.insert("version", version);
        check(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "rewrite legacy version");
        f.write(QJsonDocument(json).toJson());
        f.close();
        check(Store::load(file(legacyFile)).layers.size() == 1, "read supported legacy version");
    }
    const auto encoded = motion::persistence::writePathAnimation(*doc.pathFor(target));
    for (int code = 0; code < 5; ++code) {
        auto bad = encoded;
        if (code == 0)
            bad.insert("base", false);
        else if (code == 1) {
            auto base = bad.value("base").toObject();
            base.insert("closed", "true");
            bad.insert("base", base);
        } else if (code == 2)
            bad.insert("keyframes", false);
        else if (code == 3) {
            auto base = bad.value("base").toObject();
            auto nodes = base.value("nodes").toArray();
            nodes[0] = QJsonArray{1, 2};
            base.insert("nodes", nodes);
            bad.insert("base", base);
        } else {
            auto base = bad.value("base").toObject();
            auto nodes = base.value("nodes").toArray();
            auto node = nodes[0].toArray();
            node[0] = "zero";
            nodes[0] = node;
            base.insert("nodes", nodes);
            bad.insert("base", base);
        }
        bool rejected = false;
        try {
            (void)motion::persistence::readPathAnimation(bad);
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected, "invalid path JSON rejected");
    }
}
void pixels() {
    CompositionDocument doc(100, 60, {30, 1});
    LayerId id = 0;
    check(doc.createPathLayer(rectangle(-70, -10, 70, 10), 0, &id), "wide path");
    auto content = std::get<ShapeLayerContent>(doc.layers()[0].content);
    content.stroke_width_pixels = 0;
    content.fill_color = {200, 80, 40, 255};
    check(doc.setShapeLayerContent(id, content), "path style");
    CompositionFrameRenderer renderer;
    const auto frame = renderer.render(request(doc));
    check(bool(frame), "path render");
    const auto pixel = [&](int x, int y, int channel) {
        return frame->rgba_pixels[std::size_t(y) * frame->stride + x * 4 + channel];
    };
    check(pixel(0, 30, 0) == 200 && pixel(99, 30, 0) == 200 && pixel(50, 10, 0) == 0,
          "no original-area clipping, unchanged local origin");
    auto portable_layers = doc.layers();
    portable_layers.front().presentation = creative_suite::composition_exchange::Presentation{};
    CompositionDocument portable(100, 60, {30, 1}, portable_layers);
    const auto portable_frame = renderer.render(request(portable));
    check(portable_frame && portable_frame->rgba_pixels == frame->rgba_pixels,
          "portable presentation cannot refit a path's explicit pixel origin");
    const auto raster = rasterizePath(content, content.path->base);
    check(raster && raster->origin_x <= -70 && raster->frame->width > 100, "explicit raster origin/bounds");
    auto invalid_request = request(doc);
    invalid_request.path_override = PathOverride{{id, false}, {}};
    bool invalid_override = false;
    try {
        (void)renderer.render(invalid_request);
    } catch (const std::invalid_argument &) {
        invalid_override = true;
    }
    check(invalid_override, "null path override rejected before dereference");
    invalid_request.path_override =
        PathOverride{{999, false}, std::make_shared<const PathGeometry>(content.path->base)};
    invalid_override = false;
    try {
        (void)renderer.render(invalid_request);
    } catch (const std::invalid_argument &) {
        invalid_override = true;
    }
    check(invalid_override, "missing path override target rejected");
    auto pointer = request(doc);
    auto moved = content.path->base;
    for (auto &n : moved.nodes)
        n.point.y += 5;
    pointer.path_override = PathOverride{{id, false}, std::make_shared<const PathGeometry>(moved)};
    const auto preview = renderer.render(pointer);
    check(doc.setPathAtFrame({id, false}, moved, 0), "commit matching draft");
    const auto committed = renderer.render(request(doc));
    check(preview && committed && preview->rgba_pixels == committed->rgba_pixels,
          "transient/committed parity");
    auto maskId = doc.createPathMask(id, -1, rectangle(.25, .25, .75, .75));
    check(maskId, "path mask");
    const auto *mask = doc.findMask(maskId);
    MotionMaskRasterCache cache;
    const auto coverage = cache.coverage(*mask, 0, 40, 20);
    check(coverage && coverage->rgba_pixels[std::size_t(10) * coverage->stride + 20 * 4] == 255 &&
              coverage->rgba_pixels[0] == 0,
          "normalized path mask coverage");
    check(cache.coverage(*mask, 0, 40, 20) == coverage, "coverage reuse");
    auto parameters = mask->parameters;
    parameters.invert = true;
    parameters.soft_edge = .2;
    check(doc.setMaskParameters(maskId, parameters), "static invert/softness");
    const auto soft = cache.coverage(*doc.findMask(maskId), 0, 40, 20);
    check(soft && soft->rgba_pixels[0] == 255 &&
              soft->rgba_pixels[std::size_t(10) * soft->stride + 20 * 4] == 0,
          "distance feather and inversion");
    check(!cache.coverage(*doc.findMask(maskId), 0, 40, 20, [] { return true; }), "coverage cancellation");
    MotionMaskRasterCache interrupted;
    int cancel_checks = 0;
    check(!interrupted.coverage(*doc.findMask(maskId), 0, 40, 20,
                               [&] { return ++cancel_checks == 2; }),
          "subdivision cancellation cannot publish partial coverage");
    check(bool(interrupted.coverage(*doc.findMask(maskId), 0, 40, 20)),
          "coverage cache recovers after subdivision cancellation");
    creative_suite::composition::ShapeMaskRasterCache legacy, windowed;
    creative_suite::composition::ShapeMaskParameters p;
    p.rotation = 23;
    p.soft_edge = .1;
    const auto full = legacy.coverage(creative_suite::composition::ShapeMaskKind::Ellipse, p, 100, 60);
    const auto crop = windowed.coverage(creative_suite::composition::ShapeMaskKind::Ellipse, p, 30, 20, {},
                                        {100, 60, 35, 20});
    check(full && crop, "raster-space primitive coverage");
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 30; ++x)
            check(crop->rgba_pixels[std::size_t(y) * crop->stride + x * 4] ==
                      full->rgba_pixels[std::size_t(y + 20) * full->stride + (x + 35) * 4],
                  "raster window preserves logical mask coordinates");
    const auto old = doc.layers();
    check(doc.setLayerScaleLinked(id, false) && doc.setLayerScaleAtFrame(id, 2, .7, 0) &&
              doc.setLayerRotationAtFrame(id, 33, 0),
          "affine path fixture");
    const auto affine = renderer.render(request(doc));
    check(bool(affine), "affine/effects/masks pipeline");
}
void integrations(const QString &root) {
    CompositionDocument doc(160, 90, {30, 1});
    LayerId path = 0, parent = 0, source = 0;
    check(doc.createPathLayer(rectangle(-45, -20, 45, 20), 0, &path), "integrated path");
    check(doc.addContentLayer(LayerKind::Null, "Hidden inactive control", 70, &parent), "control layer");
    auto control = doc.layers().back().transform;
    control.rotation_degrees = 19;
    control.scale = 1.6;
    check(doc.setLayerTransform(parent, control) && doc.setLayerScaleAxisBase(parent, ScaleAxis::Y, .65) &&
              doc.setLayerVisible(parent, false),
          "nonuniform inactive parent");
    check(doc.setLayerParent(path, parent, 0) && doc.setLayerAnchorPoint(path, {11.25, -7.5}) &&
              doc.setLayerRotationAtFrame(path, -27, 0) && doc.setLayerScaleAtFrame(path, .85, 1.3, 0),
          "path affine inheritance");
    check(
        doc.setLayerEffects(path, {ColorAdjustmentEffect{true, 5, 120, 80}, GaussianBlurEffect{true, 1.25}}),
        "path effect stages");
    auto coverage = rectangle(.05, .1, .95, .9);
    coverage.nodes[0].outgoing = {.15, -.12};
    coverage.nodes[1].incoming = {-.15, -.12};
    const auto mask = doc.createPathMask(path, -2, coverage);
    check(mask && doc.associateMask(path, -1, mask) && doc.associateMask(path, 0, mask),
          "shared Bezier mask at all stages");
    auto parameters = doc.findMask(mask)->parameters;
    parameters.soft_edge = .03;
    check(doc.setMaskParameters(mask, parameters), "path mask feather");
    check(doc.createPathLayer(rectangle(-55, -35, 55, 35), 0, &source), "path matte source");
    auto content = std::get<ShapeLayerContent>(doc.layers().back().content);
    content.fill_color = {255, 255, 255, 200};
    content.stroke_width_pixels = 0;
    check(doc.setShapeLayerContent(source, content), "source color");
    check(doc.setLayerTrackMatte(
              path, TrackMatteBinding{source, creative_suite::composition::TrackMatteMode::Luma}) &&
              doc.setLayerBlendMode(path, creative_suite::composition::BlendMode::Screen),
          "path blend/matte integration");
    auto &metrics = motion::diagnostics::PerformanceMetrics::instance();
    metrics.setEnabled(true);
    CompositionFrameRenderer renderer;
    const auto base = renderer.render(request(doc));
    check(bool(base), "integrated path rendering");
    (void)metrics.takeSnapshotAndReset();
    check(bool(renderer.render(request(doc))), "warm integrated frame");
    const auto warm = metrics.takeSnapshotAndReset();
    check(warm && warm->timings[std::size_t(motion::diagnostics::PreviewTimingStage::TextShapeRasterization)]
                          .count == 0,
          "unchanged path rasters reused");
    LayerId child = 0;
    check(doc.addContentLayer(LayerKind::Null, "Descendant", 0, &child) && doc.setLayerParent(child, path, 0),
          "path descendant");
    const auto original_world = LayerTransformHierarchy(doc.canvasSize(), doc.layers()).evaluate(0);
    auto geometry = doc.pathFor({path, false})->base;
    geometry.nodes[0].point.x -= 12.75;
    auto preview_request = request(doc);
    preview_request.path_override =
        PathOverride{{path, false}, std::make_shared<const PathGeometry>(geometry)};
    const auto draft = renderer.render(preview_request);
    const auto work = metrics.takeSnapshotAndReset();
    check(draft && work &&
              work->timings[std::size_t(motion::diagnostics::PreviewTimingStage::TextShapeRasterization)]
                      .count == 1,
          "only edited path rerasterized");
    CompositionGpuMetrics gpu;
    CompositionFrameRenderer fallback(true, true, nullptr, &gpu);
    const auto recovered = fallback.render(preview_request);
    check(recovered && recovered->rgba_pixels == draft->rgba_pixels && gpu.fallback_frames == 1,
          "fallback retains path override");
    check(doc.setPathAtFrame({path, false}, geometry, 0), "integrated commit");
    const auto after_world = LayerTransformHierarchy(doc.canvasSize(), doc.layers()).evaluate(0);
    check(original_world.at(path).world == after_world.at(path).world &&
              original_world.at(child).world == after_world.at(child).world,
          "raster growth preserves layer and descendant transforms");
    const auto committed = renderer.render(request(doc));
    check(committed && committed->rgba_pixels == draft->rgba_pixels,
          "affine/effects/mask/matte draft parity");
    auto mask_geometry = doc.pathFor({mask, true})->base;
    mask_geometry.nodes[2].point.x = .7;
    auto mask_request = request(doc);
    mask_request.path_override =
        PathOverride{{mask, true}, std::make_shared<const PathGeometry>(mask_geometry)};
    const auto mask_draft = renderer.render(mask_request);
    check(doc.setPathAtFrame({mask, true}, mask_geometry, 0), "shared mask edit");
    const auto mask_commit = renderer.render(request(doc));
    check(mask_draft && mask_commit && mask_draft->rgba_pixels == mask_commit->rgba_pixels,
          "all shared mask consumers use override");
    MotionExportSnapshot snapshot{doc.canvasSize(), doc.frameRate(), doc.layers(), {}, doc.masks()};
    for (auto &layer : snapshot.layers)
        layer.duration_frames = 1;
    MotionExportSettings settings{file(root + "/bezier.mkv"), "matroska", "ffv1", 160, 90, {30, 1}, 10};
    std::atomic_bool cancel = false;
    MotionVideoExporter::exportVideo(snapshot, settings, cancel);
    creative_suite::media::VideoEncodingSettings encoding;
    encoding.output_path = file(root + "/bezier-reference.mkv");
    encoding.container_name = "matroska";
    encoding.video_encoder_name = "ffv1";
    encoding.width = 160;
    encoding.height = 90;
    encoding.frame_rate_numerator = 30;
    {
        creative_suite::media::VideoEncoder encoder(encoding);
        encoder.writeVideo(*mask_commit, 0);
        encoder.finish();
    }
    creative_suite::media::VideoDecoder decoder;
    check(decoder.decode_first_frame(settings.output_path).rgba_pixels ==
              decoder.decode_first_frame(encoding.output_path).rgba_pixels,
          "encoded preview/export parity");
    for (auto kind : {LayerKind::Image, LayerKind::Video, LayerKind::Text, LayerKind::Shape}) {
        auto layer = doc.layers()[0];
        layer.id = 100;
        layer.parent_id = 0;
        layer.parent_affine.reset();
        layer.parent_offset = {};
        layer.kind = kind;
        layer.track_matte.reset();
        layer.effects = {GaussianBlurEffect{true, 1}};
        std::shared_ptr<creative_suite::media::RgbaFrame> pixels =
            std::make_shared<creative_suite::media::RgbaFrame>(
                creative_suite::media::RgbaFrame{80, 40, 320, std::vector<std::uint8_t>(80 * 40 * 4, 200)});
        if (kind == LayerKind::Image || kind == LayerKind::Video) {
            layer.content = {};
            layer.source_path =
                file(root + (kind == LayerKind::Image ? "/path-image.png" : "/path-video.mkv"));
        }
        if (kind == LayerKind::Image) {
            QImage image(pixels->rgba_pixels.data(), 80, 40, 320, QImage::Format_RGBA8888);
            check(image.save(QString::fromStdU16String(layer.source_path.u16string())), "image fixture");
        }
        if (kind == LayerKind::Video) {
            auto video = encoding;
            video.output_path = layer.source_path;
            video.width = 80;
            video.height = 40;
            creative_suite::media::VideoEncoder encoder(video);
            encoder.writeVideo(*pixels, 0);
            encoder.finish();
            layer.source_frame_rate = 30;
            layer.source_frame_count = 1;
            layer.maximum_timeline_duration_frames = layer.duration_frames = 1;
        }
        if (kind == LayerKind::Text) {
            auto text = defaultTextLayerContent({160, 90});
            text.text = "Bezier";
            layer.content = text;
        }
        if (kind == LayerKind::Shape)
            layer.content = defaultShapeLayerContent({160, 90});
        layer.source_mask = layer.transform_mask = 0;
        CompositionDocument receiver(160, 90, {30, 1}, {layer});
        for (int stage = -2; stage <= 0; ++stage)
            check(receiver.createPathMask(100, stage, coverage) != 0, "Bezier masks on all receiver stages");
        auto r = request(receiver);
        if (kind == LayerKind::Image) {
            auto mutable_scene = std::make_shared<PreviewSceneSnapshot>(*r.scene);
            mutable_scene->layers[0].still_frame = pixels;
            r.scene = mutable_scene;
        }
        check(bool(renderer.render(r)), "Bezier masks support image/video/text/shape receivers");
    }
}
template <class T> T *widget(QObject &o, const char *name) {
    auto *value = dynamic_cast<T *>(o.findChild<QObject *>(name));
    check(value, "required UI control");
    return value;
}
void mouse(QWidget &w, QEvent::Type type, QPointF p, Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type, p, w.mapToGlobal(p.toPoint()), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&w, &event);
    QApplication::processEvents();
}
void interface(const QString &root) {
    CompositionDocument doc(400, 200, {30, 1});
    check(doc.addContentLayer(LayerKind::Shape, "Existing", 0), "UI fixture");
    const auto filename = root + "/ui.motion";
    Store::save(file(filename), project(doc));
    MainWindow window(nullptr, file(root + "/autosave"), "pen");
    window.resize(1200, 850);
    window.show();
    QApplication::processEvents();
    QTimer::singleShot(0, [filename] {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        check(dialog, "open file dialog");
        dialog->selectFile(filename);
        QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
    });
    widget<QAction>(window, "motion-open-composition-action")->trigger();
    QApplication::processEvents();
    auto *viewer = widget<CompositionViewer>(window, "motion-composition-viewer");
    auto *tool = widget<QComboBox>(window, "motion-viewer-tool");
    auto *create = widget<QPushButton>(window, "motion-pen-new");
    auto *finish = widget<QPushButton>(window, "motion-pen-finish");
    auto *undo = widget<QAction>(window, "motion-undo-action");
    auto *redo = widget<QAction>(window, "motion-redo-action");
    create->click();
    check(tool->currentIndex() == 1, "explicit Pen mode");
    const auto canvas = viewer->compositionRect();
    const auto a = canvas.center() + QPointF(-40, -30), b = canvas.center() + QPointF(50, 40);
    mouse(*viewer, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
    QKeyEvent backspace(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    QApplication::sendEvent(viewer, &backspace);
    mouse(*viewer, QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton);
    mouse(*viewer, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
    check(!viewer->pathGestureActive() && !finish->isEnabled() &&
              window.compositionDocument()->layers().size() == 1,
          "Backspace safely removes a point during capture");
    mouse(*viewer, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
    mouse(*viewer, QEvent::MouseButtonRelease, a, Qt::LeftButton, Qt::NoButton);
    const auto overlay = viewer->grab().toImage();
    const auto ratio = overlay.devicePixelRatio();
    const auto marker = overlay.pixelColor(int(a.x() * ratio), int(a.y() * ratio));
    check(marker.red() == 255 && marker.green() == 200, "draft marker is painted before document commit");
    mouse(*viewer, QEvent::MouseButtonPress, b, Qt::LeftButton, Qt::LeftButton);
    mouse(*viewer, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
    check(window.compositionDocument()->layers().size() == 1, "creation draft document isolation");
    finish->click();
    check(window.compositionDocument()->layers().size() == 2, "finish creates one shape");
    const auto created = window.compositionDocument()->layers();
    widget<QSpinBox>(window, "motion-path-point")->setValue(2);
    check(widget<QSpinBox>(window, "motion-path-point")->value() == 2,
          "Inspector selects any point by number");
    widget<QSpinBox>(window, "motion-path-point")->setValue(1);
    check(!widget<QSpinBox>(window, "motion-shape-width")->isEnabled() &&
              !widget<QSpinBox>(window, "motion-shape-height")->isEnabled(),
          "path reference dimensions are read only");
    check(std::get<ShapeLayerContent>(created.back().content).path->base.nodes.size() == 2,
          "open path points");
    undo->trigger();
    check(window.compositionDocument()->layers().size() == 1 && redo->isEnabled(), "single creation Undo");
    redo->trigger();
    check(window.compositionDocument()->layers() == created, "creation Redo");
    widget<QPushButton>(window, "motion-pen-edit")->click();
    const auto before = window.compositionDocument()->layers();
    const auto start_x = widget<QDoubleSpinBox>(window, "motion-path-coordinate-0")->value();
    mouse(*viewer, QEvent::MouseButtonPress, a + QPointF(3, 2), Qt::LeftButton, Qt::LeftButton);
    mouse(*viewer, QEvent::MouseMove, a + QPointF(5, 3), Qt::NoButton, Qt::LeftButton);
    check(widget<QDoubleSpinBox>(window, "motion-path-coordinate-0")->value() == start_x,
          "platform threshold avoids point jump");
    mouse(*viewer, QEvent::MouseMove, a + QPointF(24, 12), Qt::NoButton, Qt::LeftButton);
    check(std::abs(widget<QDoubleSpinBox>(window, "motion-path-coordinate-0")->value() -
                   (start_x + 21 * 400 / canvas.width())) < 1e-6,
          "drag preserves pointer offset and uses initial geometry");
    check(window.compositionDocument()->layers() == before, "drag document isolation");
    auto *autosave = widget<QTimer>(window, "motion-autosave-timer");
    QMetaObject::invokeMethod(autosave, "timeout", Qt::DirectConnection);
    motion::persistence::MotionRecoveryStore recovery(file(root + "/autosave"), "reader");
    const auto saved = recovery.snapshotsForProject(file(filename));
    check(!saved.empty() && Store::loadRecovery(saved.front().path).document.layers == before,
          "autosave ignores live path draft");
    check(widget<QDoubleSpinBox>(window, "motion-path-coordinate-0")->isReadOnly(),
          "point fields read only during capture");
    undo->trigger();
    check(window.compositionDocument()->layers() == before, "Undo cancels only draft");
    widget<QPushButton>(window, "motion-pen-edit")->click();
    mouse(*viewer, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
    mouse(*viewer, QEvent::MouseButtonRelease, a + QPointF(25, 10), Qt::LeftButton, Qt::NoButton);
    check(window.compositionDocument()->layers() != before, "release final coordinates commit");
    undo->trigger();
    check(window.compositionDocument()->layers() == before, "point edit single Undo");
    widget<QPushButton>(window, "motion-pen-edit")->click();
    mouse(*viewer, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
    mouse(*viewer, QEvent::MouseMove, a + QPointF(30, 20), Qt::NoButton, Qt::LeftButton);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(viewer, &escape);
    check(window.compositionDocument()->layers() == before && redo->isEnabled(),
          "cancel preserves Redo branch");
    for (int context = 0; context < 4; ++context) {
        widget<QPushButton>(window, "motion-pen-edit")->click();
        mouse(*viewer, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
        mouse(*viewer, QEvent::MouseMove, a + QPointF(25, 20), Qt::NoButton, Qt::LeftButton);
        if (context == 0) {
            QEvent deactivate(QEvent::WindowDeactivate);
            QApplication::sendEvent(&window, &deactivate);
        } else if (context == 1) {
            QEvent lost(QEvent::UngrabMouse);
            QApplication::sendEvent(viewer, &lost);
        } else if (context == 2) {
            auto *timeline = widget<TimelineNavigator>(window, "motion-timeline");
            timeline->setCurrentFrame(1);
            timeline->setCurrentFrame(0);
        } else {
            viewer->hide();
            viewer->show();
        }
        check(window.compositionDocument()->layers() == before && !viewer->pathGestureActive(),
              "context change cancels path draft");
    }
    widget<QPushButton>(window, "motion-pen-edit")->click();
    mouse(*viewer, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
    mouse(*viewer, QEvent::MouseButtonRelease, a, Qt::LeftButton, Qt::NoButton);
    check(window.compositionDocument()->layers() == before && redo->isEnabled(), "click preserves Redo");
    widget<QPushButton>(window, "motion-pen-edit")->click();
    mouse(*viewer, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
    mouse(*viewer, QEvent::MouseMove, a + QPointF(30, 20), Qt::NoButton, Qt::LeftButton);
    mouse(*viewer, QEvent::MouseButtonRelease, a, Qt::LeftButton, Qt::NoButton);
    check(window.compositionDocument()->layers() == before && redo->isEnabled(),
          "return to original preserves Redo");
    widget<QPushButton>(window, "motion-pen-edit")->click();
    mouse(*viewer, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
    mouse(*viewer, QEvent::MouseButtonRelease, {double(viewer->width() + 50), double(viewer->height() + 50)},
          Qt::LeftButton, Qt::NoButton);
    check(window.compositionDocument()->layers() != before, "release outside Viewer commits final point");
    undo->trigger();
    check(window.compositionDocument()->layers() == before, "outside drag is one Undo");
    auto *key = widget<QPushButton>(window, "motion-path-key");
    auto *timeline = widget<TimelineNavigator>(window, "motion-timeline");
    key->click();
    const auto target = PathTarget{window.compositionDocument()->layers().back().id, false};
    check(window.compositionDocument()->pathFor(target)->keys.size() == 1, "Inspector path key");
    timeline->setCurrentFrame(10);
    auto *x = widget<QDoubleSpinBox>(window, "motion-path-coordinate-0");
    const auto original_x = x->value();
    x->setValue(original_x - 2.125);
    check(window.compositionDocument()->pathFor(target)->keys.size() == 2,
          "Inspector edits insert complete keys");
    check(!widget<QPushButton>(window, "motion-path-remove")->isEnabled(), "keyed topology is disabled");
    timeline->setCurrentFrame(0);
    widget<QSlider>(window, "motion-timeline-zoom-slider")->setValue(21);
    timeline->setCurrentFrame(10);
    auto *rows = widget<QWidget>(window, "motion-timeline-layer-rows");
    const auto click = [&](QPointF p) {
        mouse(*rows, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
        mouse(*rows, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
    };
    click({39, 15});
    click({double(timeline->frameToViewportX(5)), 51});
    auto *preset = widget<QComboBox>(window, "motion-curve-editor-preset");
    check(preset->isEnabled() && preset->currentIndex() == 0,
          "timeline Path selects its progress segment");
    preset->setCurrentIndex(1);
    check(window.compositionDocument()->pathFor(target)->keys.front().interpolation ==
              a::InterpolationMode::CubicBezier,
          "Graph Editor edits path progress easing");
    undo->trigger();
    const auto keys_before_move = window.compositionDocument()->pathFor(target)->keys;
    const auto drag_key = [&](int final_frame) {
        mouse(*rows, QEvent::MouseButtonPress, {double(timeline->frameToViewportX(10)), 51},
              Qt::LeftButton, Qt::LeftButton);
        mouse(*rows, QEvent::MouseMove, {double(timeline->frameToViewportX(60)), 51},
              Qt::NoButton, Qt::LeftButton);
        mouse(*rows, QEvent::MouseButtonRelease, {double(timeline->frameToViewportX(final_frame)), 51},
              Qt::LeftButton, Qt::NoButton);
    };
    drag_key(40);
    check(window.compositionDocument()->pathFor(target)->keys.back().frame == 40,
          "timeline Path drag uses release coordinates");
    undo->trigger();
    check(window.compositionDocument()->pathFor(target)->keys == keys_before_move,
          "timeline Path move is one Undo");
    drag_key(0);
    check(window.compositionDocument()->pathFor(target)->keys == keys_before_move && redo->isEnabled(),
          "timeline Path collision preserves keys and Redo");
    timeline->setCurrentFrame(10);
    const auto evaluated = evaluatePath(*window.compositionDocument()->pathFor(target), 10);
    widget<QPushButton>(window, "motion-path-clear-animation")->click();
    check(window.compositionDocument()->pathFor(target)->keys.empty() &&
              window.compositionDocument()->pathFor(target)->base == evaluated,
          "remove animation preserves geometry");
    undo->trigger();
    check(window.compositionDocument()->pathFor(target)->keys.size() == 2, "animation removal Undo");
    undo->trigger();
    undo->trigger();
    timeline->setCurrentFrame(0);
    check(window.compositionDocument()->layers() == before,
          "path key operations have individual history entries");
    auto *destination = widget<QComboBox>(window, "motion-pen-destination");
    destination->setCurrentIndex(1);
    create->click();
    for (auto point : {canvas.center() + QPointF(-30, -20), canvas.center() + QPointF(30, -20),
                       canvas.center() + QPointF(0, 30)}) {
        mouse(*viewer, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
        mouse(*viewer, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    }
    check(window.compositionDocument()->masks().empty(), "mask creation stays transient");
    finish->click();
    check(window.compositionDocument()->masks().size() == 1 &&
              window.compositionDocument()->masks().front().path,
          "explicit mask creation");
    undo->trigger();
    check(window.compositionDocument()->masks().empty() && window.compositionDocument()->layers() == before,
          "mask creation and association are atomic");
    tool->setCurrentIndex(0);
    widget<QAction>(window, "motion-save-composition-action")->trigger();
    check(Store::load(file(filename)).layers == window.compositionDocument()->layers(),
          "UI confirmed path save");
    QFile invalid_file(filename);
    check(invalid_file.open(QIODevice::ReadOnly), "read UI path document");
    auto invalid_json = QJsonDocument::fromJson(invalid_file.readAll()).object();
    invalid_file.close();
    invalid_json.insert("version", 14);
    const auto invalid_name = root + "/invalid-path.motion";
    QFile invalid_output(invalid_name);
    check(invalid_output.open(QIODevice::WriteOnly), "write invalid UI document");
    invalid_output.write(QJsonDocument(invalid_json).toJson());
    invalid_output.close();
    QTimer dismiss;
    QObject::connect(&dismiss, &QTimer::timeout, [&] {
        if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            message->accept();
    });
    dismiss.start(1);
    QTimer::singleShot(0, [invalid_name] {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        check(dialog, "invalid document file picker");
        dialog->selectFile(invalid_name);
        QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
    });
    widget<QAction>(window, "motion-open-composition-action")->trigger();
    dismiss.stop();
    check(window.compositionDocument()->layers() == before,
          "invalid old-version path file keeps document open");
    window.close();
    QApplication::processEvents();
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("CreativeSuiteTests");
    app.setApplicationName("MotionBezier");
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir root;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.path());
    try {
        check(root.isValid(), "fixture directory");
        check(creative_suite::diagnostics::Logger::instance().initialize(file(root.path() + "/logs")),
              "log initialization");
        pure();
        document(root.path());
        pixels();
        integrations(root.path());
        interface(root.path());
        std::cout << "Motion Bezier paths passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
