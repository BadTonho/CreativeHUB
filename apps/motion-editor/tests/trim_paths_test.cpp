#include "model/trim_paths.h"
#include "model/composition_document.h"
#include "model/composition_interchange.h"
#include "model/layer_transform_hierarchy.h"
#include "persistence/motion_document_store.h"
#include "persistence/motion_recovery_store.h"
#include "rendering/composition_frame_renderer.h"
#include "rendering/preview_scene_builder.h"
#include "rendering/path_rasterizer.h"
#include "diagnostics/performance_metrics.h"
#include "export/motion_video_export.h"
#include "ui/main_window/main_window.h"
#include "ui/inspector/inspector_widget.h"
#include "ui/inspector/trim_paths_inspector.h"
#include "ui/timeline/timeline_navigator.h"
#include "ui/viewer/composition_viewer.h"
#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/media/video_playback.h>
#include <creative_suite/media/video_encoder.h>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace motion::model;
using namespace motion::ui;
using Store = motion::persistence::MotionDocumentStore;
namespace a = creative_suite::animation;
void check(bool condition, const char *cause) {
    if (!condition)
        throw std::runtime_error(cause);
}
void near(double l, double r, const char *cause, double tolerance = .01) {
    check(std::abs(l - r) <= tolerance, cause);
}
auto file(const QString &s) {
    return std::filesystem::path(s.toStdU16String());
}
PathGeometry rectangle() {
    return {{{{-40, -20}, {}, {}}, {{40, -20}, {}, {}}, {{40, 20}, {}, {}}, {{-40, 20}, {}, {}}}, true};
}
MotionProjectData project(const CompositionDocument &doc) {
    MotionProjectData data;
    data.composition = {doc.canvasSize(), doc.frameRate(), "Trim", 0};
    data.layers = doc.layers();
    data.masks = doc.masks();
    return data;
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
    auto range = trimPathRange({true, 80, 20, 0});
    near(range.start, .8, "range start", 1e-12);
    near(range.span, .4, "wrapped span", 1e-12);
    check(trimPathRange({true, 0, 100, 720}) == TrimPathRange{} &&
              trimPathRange({true, 100, 0, -720}) == TrimPathRange{},
          "full spans retain full turns");
    check(trimPathRange({true, 25, 25, 90}).span == 0 && trimPathRange({false, 20, 40, 50}).span == 1,
          "equal is empty, disabled is full");
    check(trimPathRange({true, 0, 50, 810}) == trimPathRange({true, 0, 50, 90}), "canonical visual turns");
    check(trimPathRange({true, 0, 50, -1e-15}) == TrimPathRange{0, .5},
          "sub-ULP negative offset wraps safely at the seam without changing stored values");
    PathGeometry line{{{{0, 0}, {}, {0, 0}}, {{100, 0}, {0, 0}, {}}}, false};
    const auto measured = measureTrimPath(line);
    check(bool(measured), "line length table");
    near(measuredPathLength(*measured), 100, "exact line length");
    auto pieces = extractTrimPath(*measured, {.25, .5});
    check(pieces && pieces->size() == 1, "line interval");
    near(pieces->front().nodes.front().point.x, 25, "nonlinear parameter uses arc length");
    near(pieces->front().nodes.back().point.x, 75, "arc end");
    pieces = extractTrimPath(*measured, {.8, .4});
    check(pieces && pieces->size() == 2, "open wrap has separate pieces");
    near((*pieces)[0].nodes.front().point.x, 80, "open wrap start");
    near((*pieces)[0].nodes.back().point.x, 100, "open end");
    near((*pieces)[1].nodes.back().point.x, 20, "open start continuation");
    const auto closed = measureTrimPath(rectangle());
    near(measuredPathLength(*closed), 240, "rectangle length");
    pieces = extractTrimPath(*closed, range);
    check(pieces && pieces->size() == 1 && !pieces->front().closed && pieces->front().nodes.size() == 4,
          "closed wrap joins seam without closing chord");
    near(pieces->front().nodes.front().point.x, -32, "closed start");
    near(pieces->front().nodes.back().point.x, 8, "closed finish");
    PathGeometry curve{{{{-20, 0}, {}, {30, 50}}, {{60, 10}, {-20, -30}, {}}}, false};
    const auto arc = measureTrimPath(curve);
    double reference = 0;
    auto last = pathSegmentPoint(curve, 0, 0);
    std::vector<std::pair<double, PathPoint>> samples;
    samples.reserve(20001);
    samples.push_back({0, last});
    for (int i = 1; i <= 20000; ++i) {
        const auto p = pathSegmentPoint(curve, 0, i / 20000.);
        reference += std::hypot(p.x - last.x, p.y - last.y);
        samples.push_back({reference, p});
        last = p;
    }
    near(measuredPathLength(*arc), reference, "independent sampled curve length");
    pieces = extractTrimPath(*arc, {.3, .4});
    const auto lookup = [&](double fraction) {
        const auto at = std::lower_bound(samples.begin(), samples.end(), fraction * reference,
                                         [](const auto &p, double n) { return p.first < n; });
        return at->second;
    };
    for (auto [actual, expected] : {std::pair{pieces->front().nodes.front().point, lookup(.3)},
                                    std::pair{pieces->front().nodes.back().point, lookup(.7)}}) {
        near(actual.x, expected.x, "independent curve cut x");
        near(actual.y, expected.y, "independent curve cut y");
    }
    PathGeometry zero{{{{0, 0}, {}, {}}, {{0, 0}, {}, {}}}, false};
    const auto degenerate = measureTrimPath(zero);
    check(measuredPathLength(*degenerate) == 0 && extractTrimPath(*degenerate, {0, .5})->empty(),
          "degenerate interval is safe");
    check(!measureTrimPath(curve, [] { return true; }) &&
              !extractTrimPath(*arc, {0, .5}, [] { return true; }),
          "length and cuts are cancellable");
    PathGeometry heavy;
    heavy.nodes.resize(4096);
    heavy.closed = true;
    for (std::size_t i = 0; i < heavy.nodes.size(); ++i)
        heavy.nodes[i] = {{i % 2 ? 100. : -100., i % 2 ? 100. : -100.}, {100, -100}, {-100, 100}};
    check(validPathRasterGeometry(heavy, 2), "bounded raster stress fixture");
    bool bounded = false;
    try {
        (void)measureTrimPath(heavy);
    } catch (const std::runtime_error &) {
        bounded = true;
    }
    check(bounded, "length resource exhaustion is explicit, without unbounded allocation");
    check(!validTrimValue(TrimPathProperty::Start, -.1) && !validTrimValue(TrimPathProperty::End, 101) &&
              !validTrimValue(TrimPathProperty::Offset, 1e9 + 1) &&
              !validTrimValue(static_cast<TrimPathProperty>(3), 0),
          "value/property limits");
}
void modelAndPersistence(const QString &root) {
    CompositionDocument doc(160, 90, {30, 1});
    LayerId id = 0;
    check(doc.createPathLayer(rectangle(), 10, &id), "model fixture");
    check(doc.layerTrimPaths(id)->isDefault() && !doc.setLayerTrimPathsEnabled(id, false),
          "disabled default and no-op");
    check(doc.setLayerTrimPathsEnabled(id, true) &&
              doc.setLayerTrimValueAtFrame(id, TrimPathProperty::Offset, 720.125, 10),
          "static trim edit preserves turns");
    check(doc.setLayerTrimKeyframe(id, TrimPathProperty::End, 10) &&
              doc.setLayerTrimKeyframe(id, TrimPathProperty::End, 30),
          "local keys");
    check(doc.setLayerTrimValueAtFrame(id, TrimPathProperty::End, 0, 10), "edit keyed value");
    check(!doc.setLayerTrimValueAtFrame(id, TrimPathProperty::End, 40, 20), "between keys is read only");
    check(doc.setLayerTrimKeyframe(id, TrimPathProperty::End, 20), "insert evaluated key");
    near(doc.layerTrimPaths(id)->get().keys[1][1].value, 50, "insert interpolated value");
    check(doc.setLayerTrimKeyframeInterpolation(id, TrimPathProperty::End, 0,
                                                a::InterpolationMode::CubicBezier, {.42, 0, 1, 1}),
          "trim easing");
    const auto expected = a::evaluateScalar(100, doc.layerTrimPaths(id)->get().keys[1], 5);
    near(evaluateTrimPaths(*doc.layerTrimPaths(id), 5).end_percent, expected, "scalar easing parity", 1e-12);
    check(!doc.moveLayerTrimKeyframe(id, TrimPathProperty::End, 10, 20) &&
              doc.moveLayerTrimKeyframe(id, TrimPathProperty::End, 10, 11),
          "move/collision");
    const auto curves = doc.layerTrimPaths(id)->get().keys;
    check(doc.setLayerTrimPathsEnabled(id, false) && doc.layerTrimPaths(id)->get().keys == curves,
          "disable retains animation");
    check(doc.setLayerTrimPathsEnabled(id, true), "reenable");
    auto primitive = doc;
    LayerId rect = 0;
    check(primitive.addContentLayer(LayerKind::Shape, "Primitive", 0, &rect) &&
              !primitive.setLayerTrimPathsEnabled(rect, true),
          "primitives cannot trim");
    LayerId null = 0;
    check(primitive.addContentLayer(LayerKind::Null, "Null", 0, &null) &&
              !primitive.setLayerTrimPathsEnabled(null, true),
          "Null cannot trim");
    const auto filename = root + "/trim.motion";
    Store::save(file(filename), project(doc));
    check(Store::load(file(filename)) == project(doc), "v16 roundtrip");
    Store::saveRecovery(file(root + "/recovery"), file(filename), "trim", project(doc));
    check(Store::loadRecovery(file(root + "/recovery")).document == project(doc), "recovery retains trim");
    bool refused = false;
    try {
        (void)exportComposition(project(doc));
    } catch (const creative_suite::composition_exchange::Unsupported &e) {
        refused = std::string(e.what()).find("Trim Paths") != std::string::npos;
    }
    check(refused, "exchange identifies Trim Paths");
    QFile input(filename);
    check(input.open(QIODevice::ReadOnly), "read native JSON");
    const auto encoded = QJsonDocument::fromJson(input.readAll()).object();
    input.close();
    for (int code = 0; code < 9; ++code) {
        auto bad = encoded;
        auto layers = bad.value("layers").toArray();
        auto layer = layers[0].toObject();
        auto content = layer.value("shape_content").toObject();
        auto trim = content.value("trim_paths").toObject();
        if (code == 0)
            bad.insert("version", 15);
        else if (code == 1)
            trim.insert("enabled", 1);
        else if (code == 2)
            trim.insert("start_percent", -1);
        else if (code == 3)
            trim.insert("end_percent", 101);
        else if (code == 4)
            trim.insert("offset_degrees", 1e10);
        else if (code == 5)
            trim.insert("keyframes", QJsonArray{});
        else if (code == 6)
            content.insert("primitive", "rectangle");
        else if (code == 7)
            layer.insert("kind", "null");
        else
            bad.insert("version", 17);
        content.insert("trim_paths", trim);
        layer.insert("shape_content", content);
        layers[0] = layer;
        bad.insert("layers", layers);
        QFile output(root + "/invalid.motion");
        check(output.open(QIODevice::WriteOnly), "write invalid fixture");
        output.write(QJsonDocument(bad).toJson());
        output.close();
        bool rejected = false;
        try {
            (void)Store::load(file(output.fileName()));
        } catch (const motion::persistence::MotionDocumentError &) {
            rejected = true;
        }
        check(rejected, "invalid Trim Paths record rejected");
    }
    for (int version = 1; version <= 15; ++version) {
        CompositionDocument old(160, 90, {30, 1});
        LayerId layer = 0;
        check(old.addContentLayer(LayerKind::Shape, "Old", 0, &layer), "migration fixture");
        const auto name = root + QString("/old-v%1.motion").arg(version);
        Store::save(file(name), project(old));
        QFile f(name);
        check(f.open(QIODevice::ReadOnly), "read old");
        auto json = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        json.insert("version", version);
        check(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "write old");
        f.write(QJsonDocument(json).toJson());
        f.close();
        check(std::get<ShapeLayerContent>(Store::load(file(name)).layers[0].content).trim_paths.isDefault(),
              "v1-v15 disabled migration");
    }
    CompositionDocument budget(160, 90, {30, 1});
    check(budget.createPathLayer(rectangle(), 0, &id), "budget fixture");
    PathAnimation bulky;
    bulky.base.nodes.resize(4096);
    for (int i = 0; i < 81; ++i)
        bulky.keys.push_back({i, bulky.base});
    check(budget.replacePathAnimation({id, false}, std::move(bulky)), "weighted path budget fixture");
    TrimPathAnimation many;
    for (int i = 0; i < 9344; ++i)
        many.keys[0].push_back({i, 0});
    check(budget.replaceLayerTrimPaths(id, many), "combined key budget boundary");
    many.keys[0].push_back({9344, 0});
    check(!budget.replaceLayerTrimPaths(id, std::move(many)), "scalar trim keys share global path budget");
}
void pixelsAndExport(const QString &root) {
    CompositionDocument doc(160, 90, {30, 1});
    LayerId id = 0;
    check(doc.createPathLayer(rectangle(), 0, &id), "raster fixture");
    auto content = std::get<ShapeLayerContent>(doc.layers()[0].content);
    content.fill_color = {120, 50, 200, 128};
    content.stroke_width_pixels = 4;
    check(doc.setShapeLayerContent(id, content), "raster style");
    const auto original = rasterizePath(content, content.path->base);
    const auto full = rasterizePath(content, content.path->base, 0, {}, trimPathRange({true, 0, 100, 810}));
    check(original && full && original->frame->rgba_pixels == full->frame->rgba_pixels,
          "full span retains exact legacy pixels");
    check(!rasterizePath(content, content.path->base, 0, {},
                         TrimPathRange{std::numeric_limits<double>::quiet_NaN(), 0}),
          "invalid raster trim rejected");
    const auto empty = rasterizePath(content, content.path->base, 0, {}, trimPathRange({true, 20, 20, 0}));
    const auto partial = rasterizePath(content, content.path->base, 0, {}, trimPathRange({true, 80, 20, 0}));
    check(empty && partial && empty->origin_x == original->origin_x &&
              partial->origin_y == original->origin_y && partial->frame->width == original->frame->width &&
              empty->space == original->space,
          "trim never changes raster origin, limits or mask reference");
    const auto center =
        std::size_t(-original->origin_y) * original->frame->stride + std::size_t(-original->origin_x) * 4;
    for (int i = 0; i < 4; ++i)
        check(empty->frame->rgba_pixels[center + i] == original->frame->rgba_pixels[center + i],
              "fill remains untouched");
    auto open = content.path->base;
    open.closed = false;
    auto nofill = content;
    nofill.fill_color = {255, 0, 0, 255};
    const auto hidden = rasterizePath(nofill, open, 0, {}, TrimPathRange{0, 0});
    check(hidden && std::all_of(hidden->frame->rgba_pixels.begin(), hidden->frame->rgba_pixels.end(),
                                [](auto b) { return b == 0; }),
          "empty open stroke is transparent");
    LayerId parent = 0, child = 0;
    check(doc.addContentLayer(LayerKind::Null, "Parent", 0, &parent) &&
              doc.addContentLayer(LayerKind::Shape, "Unrelated", 0, &child),
          "hierarchy fixture");
    check(doc.setLayerScaleAtFrame(parent, 1.1, .8, 0) && doc.setLayerRotationAtFrame(parent, 21, 0) &&
              doc.setLayerParent(id, parent, 0),
          "affine hierarchy");
    check(doc.setLayerAnchorPoint(id, {7, -3}) && doc.setLayerScaleAtFrame(id, 1.1, .75, 0) &&
              doc.setLayerRotationAtFrame(id, -13, 0),
          "affine path");
    check(doc.setLayerEffects(id, {ColorAdjustmentEffect{true, 5, 110, 90}, GaussianBlurEffect{true, 1.25}}),
          "effects fixture");
    PathGeometry coverage{{{{.1, .1}, {}, {}}, {{.9, .1}, {}, {}}, {{.9, .9}, {}, {}}, {{.1, .9}, {}, {}}},
                          true};
    const auto mask = doc.createPathMask(id, -2, coverage);
    check(mask && doc.associateMask(id, 0, mask) &&
              doc.createMask(id, -1, creative_suite::composition::ShapeMaskKind::Ellipse),
          "all mask stages");
    check(doc.setLayerBlendMode(id, creative_suite::composition::BlendMode::Screen), "blend fixture");
    LayerId matte = 0;
    check(doc.createPathLayer(rectangle(), 0, &matte) &&
              doc.setLayerTrackMatte(
                  id, TrackMatteBinding{matte, creative_suite::composition::TrackMatteMode::Alpha}),
          "matte fixture");
    const auto world = LayerTransformHierarchy(doc.canvasSize(), doc.layers()).evaluate(0).at(id).world;
    const auto frozen = request(doc);
    const auto *previous = &std::get<ShapeLayerContent>(frozen.sceneLayers()[0].content).trim_paths.get();
    check(previous == &doc.layerTrimPaths(id)->get(), "snapshots share immutable trim curves");
    check(doc.setLayerTrimPathsEnabled(id, true) &&
              doc.setLayerTrimValueAtFrame(id, TrimPathProperty::End, 60, 0),
          "trim composition");
    bool trim_diagnosed = false;
    try {
        (void)exportComposition(project(doc));
    } catch (const creative_suite::composition_exchange::Unsupported &e) {
        trim_diagnosed = std::string(e.what()).find("Trim Paths") != std::string::npos;
    }
    check(trim_diagnosed, "exchange still identifies Trim Paths when the same shape has Bezier masks");
    check(LayerTransformHierarchy(doc.canvasSize(), doc.layers()).evaluate(0).at(id).world == world,
          "trim does not change layer world geometry");
    check(!previous->base.enabled, "frozen scene remains unchanged");
    CompositionFrameRenderer renderer;
    auto &metrics = motion::diagnostics::PerformanceMetrics::instance();
    metrics.setEnabled(true);
    metrics.reset();
    auto r = request(doc);
    const auto frame = renderer.render(r);
    check(bool(frame), "effects/masks/blend/matte render");
    (void)metrics.takeSnapshotAndReset();
    const auto warmed = renderer.render(r);
    const auto cached = metrics.takeSnapshotAndReset();
    check(warmed && warmed->rgba_pixels == frame->rgba_pixels && cached &&
              cached->timings[static_cast<std::size_t>(
                                  motion::diagnostics::PreviewTimingStage::TextShapeRasterization)]
                      .count == 0,
          "warm path raster reused");
    check(doc.setLayerTrimValueAtFrame(id, TrimPathProperty::End, 65, 0), "cancel candidate");
    int cancellation_checks = 0;
    bool cancellation_threw = false;
    creative_suite::media::RgbaFramePtr cancelled_frame;
    try {
        cancelled_frame = renderer.render(
            request(doc), [&] { return ++cancellation_checks >= 8; }, true, PreviewRequestMode::Gesture);
    } catch (const std::exception &) {
        cancellation_threw = true;
    }
    check(!cancelled_frame && !cancellation_threw, "cancelled Trim work is not a technical render failure");
    r = request(doc);
    auto geometry = doc.pathFor({id, false})->base;
    geometry.nodes[0].outgoing = {20, -10};
    r.path_override = PathOverride{{id, false}, std::make_shared<const PathGeometry>(geometry)};
    const auto draft = renderer.render(r);
    check(doc.setPathAtFrame({id, false}, geometry, 0), "commit candidate geometry");
    const auto committed = renderer.render(request(doc));
    check(draft && committed && draft->rgba_pixels == committed->rgba_pixels,
          "Pen override measures candidate geometry with Trim Paths");
    auto snapshot = MotionExportSnapshot{doc.canvasSize(), doc.frameRate(), doc.layers(), {}, doc.masks()};
    for (auto &layer : snapshot.layers)
        layer.duration_frames = 1;
    MotionExportSettings settings{file(root + "/trim.mkv"), "matroska", "ffv1", 160, 90, {30, 1}, 10};
    std::atomic_bool cancelled = false;
    MotionVideoExporter::exportVideo(snapshot, settings, cancelled);
    creative_suite::media::VideoEncodingSettings reference_settings;
    reference_settings.output_path = file(root + "/reference.mkv");
    reference_settings.container_name = "matroska";
    reference_settings.video_encoder_name = "ffv1";
    reference_settings.width = 160;
    reference_settings.height = 90;
    reference_settings.frame_rate_numerator = 30;
    reference_settings.frame_rate_denominator = 1;
    {
        creative_suite::media::VideoEncoder encoder(reference_settings);
        encoder.writeVideo(*committed, 0);
        encoder.finish();
    }
    auto a = creative_suite::media::VideoPlaybackSession::open(settings.output_path),
         b = creative_suite::media::VideoPlaybackSession::open(reference_settings.output_path);
    const auto actual = a->decode_next_frame(), expected = b->decode_next_frame();
    check(actual && expected && (*actual)->rgba_pixels == (*expected)->rgba_pixels,
          "encoded export equals independent preview encoding");
}
template <class T> T *widget(QObject &owner, const char *name) {
    auto *p = dynamic_cast<T *>(owner.findChild<QObject *>(name));
    check(p, "required UI control");
    return p;
}
void mouse(QWidget &target, QEvent::Type type, QPointF point, Qt::MouseButton button,
           Qt::MouseButtons buttons) {
    QMouseEvent event(type, point, target.mapToGlobal(point.toPoint()), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&target, &event);
    QApplication::processEvents();
}
void interface(const QString &root) {
    CompositionDocument doc(400, 200, {30, 1});
    LayerId id = 0;
    check(doc.createPathLayer(rectangle(), 0, &id), "UI fixture");
    const auto filename = root + "/ui.motion";
    Store::save(file(filename), project(doc));
    MainWindow window(nullptr, file(root + "/autosave"), "trim");
    window.resize(1200, 850);
    window.show();
    QApplication::processEvents();
    QTimer::singleShot(0, [filename] {
        auto *d = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        check(d, "open picker");
        d->selectFile(filename);
        QMetaObject::invokeMethod(d, "accept", Qt::DirectConnection);
    });
    widget<QAction>(window, "motion-open-composition-action")->trigger();
    QApplication::processEvents();
    auto *rows = widget<QWidget>(window, "motion-timeline-layer-rows");
    const auto click = [&](QPointF p) {
        mouse(*rows, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
        mouse(*rows, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
    };
    click({70, 17});
    auto *enabled = widget<QCheckBox>(window, "motion-trim-enabled");
    auto *end = widget<QDoubleSpinBox>(window, "motion-trim-value-1");
    auto *key = widget<QToolButton>(window, "motion-trim-key-1");
    auto *undo = widget<QAction>(window, "motion-undo-action");
    auto *redo = widget<QAction>(window, "motion-redo-action");
    auto *timeline = widget<TimelineNavigator>(window, "motion-timeline");
    TrimPathsInspector blocked;
    blocked.sync(window.compositionDocument(), id, 0, true);
    check(!widget<QCheckBox>(blocked, "motion-trim-enabled")->isEnabled(), "jobs block Trim editing");
    check(enabled->isEnabled() && !enabled->isChecked() && end->value() == 100 && end->decimals() == 6,
          "Inspector defaults");
    enabled->setChecked(true);
    undo->trigger();
    check(!window.compositionDocument()->layerTrimPaths(id)->get().base.enabled && redo->isEnabled(),
          "one activation Undo");
    redo->trigger();
    end->setValue(80);
    key->click();
    timeline->setCurrentFrame(20);
    key->click();
    end->setValue(100);
    timeline->setCurrentFrame(10);
    check(end->isReadOnly() && end->value() == 90, "between keys read only");
    key->click();
    check(!end->isReadOnly(), "explicit key unlocks field");
    end->setValue(30);
    timeline->setCurrentFrame(0);
    widget<QSlider>(window, "motion-timeline-zoom-slider")->setValue(21);
    timeline->setCurrentFrame(10);
    click({39, 17});
    click({double(timeline->frameToViewportX(5)), 119});
    auto *preset = widget<QComboBox>(window, "motion-curve-editor-preset");
    check(preset->isEnabled(), "Trim timeline selects graph segment");
    preset->setCurrentIndex(1);
    check(window.compositionDocument()->layerTrimPaths(id)->get().keys[1][0].interpolation ==
              a::InterpolationMode::CubicBezier,
          "Trim graph easing");
    undo->trigger();
    const auto keys = window.compositionDocument()->layerTrimPaths(id)->get().keys;
    const auto drag = [&](int to) {
        mouse(*rows, QEvent::MouseButtonPress, {double(timeline->frameToViewportX(10)), 119}, Qt::LeftButton,
              Qt::LeftButton);
        mouse(*rows, QEvent::MouseMove, {double(timeline->frameToViewportX(60)), 119}, Qt::NoButton,
              Qt::LeftButton);
        mouse(*rows, QEvent::MouseButtonRelease, {double(timeline->frameToViewportX(to)), 119},
              Qt::LeftButton, Qt::NoButton);
    };
    drag(40);
    check(window.compositionDocument()->layerTrimPaths(id)->get().keys[1].back().frame == 40,
          "Trim key uses final pointer");
    undo->trigger();
    check(window.compositionDocument()->layerTrimPaths(id)->get().keys == keys, "single key-move Undo");
    drag(20);
    check(window.compositionDocument()->layerTrimPaths(id)->get().keys == keys && redo->isEnabled(),
          "key collision preserves Redo");
    timeline->setCurrentFrame(10);
    enabled->setChecked(false);
    check(window.compositionDocument()->layerTrimPaths(id)->get().keys == keys,
          "UI disable preserves curves");
    undo->trigger();
    auto *viewer = widget<CompositionViewer>(window, "motion-composition-viewer");
    widget<QPushButton>(window, "motion-pen-edit")->click();
    const auto canvas = viewer->compositionRect();
    const QPointF point = canvas.center() + QPointF(-40 * canvas.width() / 400, -20 * canvas.height() / 200);
    const auto before = window.compositionDocument()->layers();
    mouse(*viewer, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    mouse(*viewer, QEvent::MouseMove, point + QPointF(25, 15), Qt::NoButton, Qt::LeftButton);
    check(viewer->pathGestureActive() && window.compositionDocument()->layers() == before,
          "Pen draft isolates Trim document");
    auto *autosave = widget<QTimer>(window, "motion-autosave-timer");
    QMetaObject::invokeMethod(autosave, "timeout", Qt::DirectConnection);
    motion::persistence::MotionRecoveryStore recovery(file(root + "/autosave"), "reader");
    const auto snapshots = recovery.snapshotsForProject(file(filename));
    check(!snapshots.empty() && Store::loadRecovery(snapshots.front().path).document.layers == before,
          "autosave stores confirmed Trim values");
    end->setValue(25);
    check(!viewer->pathGestureActive() &&
              window.compositionDocument()->pathFor({id, false})->base == doc.pathFor({id, false})->base &&
              window.compositionDocument()->layerTrimPaths(id)->get().keys[1][1].value == 25,
          "Trim edit cancels Pen and preserves chosen value");
    undo->trigger();
    check(window.compositionDocument()->layers() == before, "Trim after draft is a single Undo");
    widget<QAction>(window, "motion-new-null-layer-action")->trigger();
    check(!enabled->isEnabled(), "Null has no Trim controls");
    undo->trigger();
    widget<QComboBox>(window, "motion-viewer-tool")->setCurrentIndex(0);
    widget<QAction>(window, "motion-save-composition-action")->trigger();
    check(Store::load(file(filename)).layers == window.compositionDocument()->layers(),
          "UI save confirmed Trim");
    window.close();
    QApplication::processEvents();
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("CreativeSuiteTests");
    app.setApplicationName("MotionTrimPaths");
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir root;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.path());
    try {
        check(root.isValid(), "temporary fixtures");
        check(creative_suite::diagnostics::Logger::instance().initialize(file(root.path() + "/logs")),
              "diagnostic log");
        pure();
        modelAndPersistence(root.path());
        pixelsAndExport(root.path());
        interface(root.path());
        std::cout << "Trim Paths tests passed\n";
        return 0;
    } catch (const motion::persistence::MotionDocumentError &e) {
        std::cerr << e.what() << " path=" << e.path().string()
                  << " error=" << e.systemError().value_or(0) << '\n';
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
