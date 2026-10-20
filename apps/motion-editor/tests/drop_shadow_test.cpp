#include "model/composition_document.h"
#include "model/composition_interchange.h"
#include "model/layer_transform_hierarchy.h"
#include "persistence/motion_document_store.h"
#include "rendering/composition_frame_renderer.h"
#include "rendering/preview_scene_builder.h"
#include "rendering/masked_layer_renderer.h"
#include "diagnostics/performance_metrics.h"
#include "ui/main_window/main_window.h"
#include "ui/timeline/timeline_navigator.h"
#include "ui/translation_helpers.h"
#include "export/motion_video_export.h"
#include <creative_suite/composition/vulkan_frame_compositor.h>
#include <creative_suite/effects/drop_shadow.h>
#include <creative_suite/media/video_playback.h>
#include <creative_suite/media/video_encoder.h>
#include <creative_suite/diagnostics/logger.h>
#include <QApplication>
#include <QAction>
#include <QDoubleSpinBox>
#include <QToolButton>
#include <QSlider>
#include <QPushButton>
#include <QListWidget>
#include <QComboBox>
#include <QFileDialog>
#include <QColorDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QFile>
#include <QMouseEvent>
#include <QMessageBox>
#include <QOffscreenSurface>
#include <QImage>
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
namespace e = creative_suite::effects;
namespace c = creative_suite::composition;
namespace media = creative_suite::media;
using namespace motion::model;
using namespace motion::ui;
using Store = motion::persistence::MotionDocumentStore;
namespace a = creative_suite::animation;
void check(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}
void near(double x, double y, const char *message, double error = 1e-9) {
    check(std::abs(x - y) <= error, message);
}
auto file(const QString &s) {
    return std::filesystem::path(s.toStdU16String());
}
media::RgbaFrame pixels() {
    return {2, 2, 8, {200, 100, 50, 128, 20, 40, 80, 255, 255, 0, 0, 0, 80, 100, 120, 64}};
}
void same(const media::RgbaFrame &x, const media::RgbaFrame &y, int rgb = 0) {
    check(x.width == y.width && x.height == y.height, "frame geometry parity");
    for (int r = 0; r < x.height; ++r)
        for (int k = 0; k < x.width * 4; ++k)
            if (std::abs(int(x.rgba_pixels[std::size_t(r) * x.stride + k]) -
                         int(y.rgba_pixels[std::size_t(r) * y.stride + k])) > (k % 4 == 3 ? 0 : rgb)) {
                std::cerr << "pixel mismatch row=" << r << " byte=" << k
                          << " expected=" << int(x.rgba_pixels[std::size_t(r) * x.stride + k])
                          << " actual=" << int(y.rgba_pixels[std::size_t(r) * y.stride + k]) << '\n';
                throw std::runtime_error("pixel parity");
            }
}
MotionProjectData project(const CompositionDocument &doc) {
    MotionProjectData p;
    p.composition = {doc.canvasSize(), doc.frameRate(), "Shadow", 0};
    p.layers = doc.layers();
    p.masks = doc.masks();
    return p;
}
PreviewRequest request(const CompositionDocument &doc, std::int64_t frame = 0,
                       media::RgbaFramePtr still = {}) {
    PreviewRequest r;
    r.canvas_size = doc.canvasSize();
    r.frame_rate = doc.frameRate();
    r.composition_frame = frame;
    r.hierarchy = std::make_shared<const LayerTransformHierarchy>(doc.canvasSize(), doc.layers());
    r.masks = std::make_shared<const std::vector<Mask>>(doc.masks());
    preparePreviewLayers(r, doc.layers(), [&](const auto &) { return still; });
    r.freezeScene();
    return r;
}
void numeric() {
    auto src = pixels();
    e::DropShadowParameters p;
    p.softness_pixels = 0;
    p.angle_degrees = 0;
    p.distance_pixels = 2.5;
    p.color = {10, 20, 30};
    auto result = e::applyDropShadow(src, p);
    const auto b = e::dropShadowBounds(2, 2, p);
    check(result.has_value(), "fractional shadow");
    const auto at = [&](int x, int y) {
        return result->rgba_pixels.data() + std::size_t(y + b.top) * result->stride + (x + b.left) * 4;
    };
    check(at(2, 0)[3] == 32 && at(2, 0)[0] == 10 && at(2, 0)[1] == 20,
          "independent bilinear half-alpha shadow");
    check(at(0, 0)[0] == 200 && at(0, 0)[3] == 128, "unshifted original");
    p.distance_pixels = 0;
    result = e::applyDropShadow(src, p);
    const auto both = result->rgba_pixels.data() + std::size_t(b.top) * result->stride +
                      e::dropShadowBounds(2, 2, p).left * 4;
    const double source_alpha = 128 / 255., shadow_alpha = source_alpha * .5,
                 alpha = source_alpha + shadow_alpha * (1 - source_alpha);
    check(both[3] == std::lround(alpha * 255) &&
              both[0] == std::lround((200 * source_alpha + 10 * shadow_alpha * (1 - source_alpha)) / alpha),
          "independent premultiplied source-over");
    p.opacity_percent = 0;
    same(src, *e::applyDropShadow(src, p));
    p.opacity_percent = 100;
    p.distance_pixels = 3;
    p.angle_degrees = 90;
    auto down = e::dropShadowBounds(2, 2, p);
    near(down.dx, 0, "cardinal x");
    near(down.dy, 3, "cardinal y");
    p.angle_degrees = -630;
    auto turns = e::dropShadowBounds(2, 2, p);
    near(turns.dy, down.dy, "full turns only reduced for geometry");
    p.softness_pixels = 2;
    check(!e::applyDropShadow(src, p, [] { return true; }), "cancel before allocating");
    bool bounded = false;
    try {
        (void)e::dropShadowBounds(16384, 16384, p);
    } catch (const std::runtime_error &) {
        bounded = true;
    }
    check(bounded, "bounded expanded allocation");
    p.distance_pixels = -1;
    check(!e::validDropShadow(p), "negative distance rejected");
    p.distance_pixels = 10;
    p.angle_degrees = 1e9 + 1;
    check(!e::validDropShadow(p), "angle limit");
    auto transparent = src;
    for (std::size_t i = 3; i < transparent.rgba_pixels.size(); i += 4)
        transparent.rgba_pixels[i] = 0;
    p.angle_degrees = 45;
    auto empty = e::applyDropShadow(transparent, p);
    check(empty && std::all_of(empty->rgba_pixels.begin(), empty->rgba_pixels.end(),
                               [](auto v) { return v == 0; }),
          "transparent input stays empty");
}
void model(const QString &root) {
    CompositionDocument doc(160, 90, {30, 1});
    LayerId id = 0;
    check(doc.addContentLayer(LayerKind::Shape, "Shape", 10, &id), "model fixture");
    DropShadowEffect shadow;
    shadow.parameters.angle_degrees = 720.123456789;
    check(doc.setLayerEffects(id, {shadow}), "add shadow");
    check(!doc.setLayerEffects(id, {shadow}), "same effect is no-op");
    check(doc.setEffectKeyframe(id, 0, EffectParameterProperty::ShadowOpacityPercent, 0, 0) &&
              doc.setEffectKeyframe(id, 0, EffectParameterProperty::ShadowOpacityPercent, 20, 100),
          "shadow keys");
    check(doc.setEffectKeyframeInterpolation(id, 0, EffectParameterProperty::ShadowOpacityPercent, 0,
                                             a::InterpolationMode::CubicBezier, {.42, 0, 1, 1}),
          "shadow Bezier easing");
    const auto &fx = doc.layers()[0].effects[0];
    near(evaluateEffectParameter(fx, EffectParameterProperty::ShadowOpacityPercent, 10),
         a::evaluateScalar(50, std::get<DropShadowEffect>(fx).keyframes[0], 10), "existing evaluator");
    check(!doc.moveEffectKeyframe(id, 0, EffectParameterProperty::ShadowOpacityPercent, 0, 20),
          "collision rejected");
    auto effects = doc.layers()[0].effects;
    std::get<DropShadowEffect>(effects[0]).enabled = false;
    check(doc.setLayerEffects(id, effects), "disable");
    check(std::get<DropShadowEffect>(doc.layers()[0].effects[0]).keyframes[0].size() == 2,
          "disable retains keys");
    LayerId null = 0;
    check(doc.addContentLayer(LayerKind::Null, "Null", 0, &null) && !doc.setLayerEffects(null, {shadow}),
          "Null rejects effects");
    const auto filename = root + "/shadow.motion";
    Store::save(file(filename), project(doc));
    check(Store::load(file(filename)) == project(doc), "v17 full roundtrip");
    Store::saveRecovery(file(root + "/shadow.recovery"), file(filename), "shadow", project(doc));
    check(Store::loadRecovery(file(root + "/shadow.recovery")).document == project(doc),
          "recovery wrapper remains compatible");
    bool exchange = false;
    try {
        (void)exportComposition(project(doc));
    } catch (const creative_suite::composition_exchange::Unsupported &error) {
        exchange = std::string(error.what()).find("Drop Shadow") != std::string::npos &&
                   std::string(error.what()).find("Shape") != std::string::npos;
    }
    check(exchange, "disabled effect is explicitly refused by exchange");
    QFile input(filename);
    check(input.open(QIODevice::ReadOnly), "read saved JSON");
    auto json = QJsonDocument::fromJson(input.readAll()).object();
    input.close();
    for (int code = 0; code < 9; ++code) {
        auto bad = json;
        auto layers = bad["layers"].toArray();
        auto layer = layers[0].toObject();
        auto stack = layer["effects"].toArray();
        auto value = stack[0].toObject();
        if (code == 0)
            bad["version"] = 16;
        else if (code == 1)
            bad["version"] = 18;
        else if (code == 2)
            value["opacity_percent"] = 101;
        else if (code == 3)
            value["angle_degrees"] = -1e9 - 1;
        else if (code == 4)
            value["distance_pixels"] = -1;
        else if (code == 5)
            value["softness_pixels"] = 101;
        else if (code == 6)
            value["color_rgb"] = QJsonArray{1, 2, 255.5};
        else if (code == 7)
            value["keyframes"] = QJsonObject{{"unknown", QJsonArray{}}};
        else
            value["mask"] = "999";
        stack[0] = value;
        layer["effects"] = stack;
        layers[0] = layer;
        bad["layers"] = layers;
        QFile f(root + QString("/bad-%1.motion").arg(code));
        check(f.open(QIODevice::WriteOnly), "invalid fixture");
        f.write(QJsonDocument(bad).toJson());
        f.close();
        bool rejected = false;
        try {
            (void)Store::load(file(f.fileName()));
        } catch (const motion::persistence::MotionDocumentError &) {
            rejected = true;
        }
        check(rejected, "invalid shadow document rejected");
    }
    for (int version = 1; version <= 16; ++version) {
        CompositionDocument old(160, 90, {30, 1});
        LayerId oldid = 0;
        check(old.addContentLayer(LayerKind::Shape, "Old", 0, &oldid), "old fixture");
        const auto name = root + QString("/old-v%1.motion").arg(version);
        Store::save(file(name), project(old));
        QFile f(name);
        check(f.open(QIODevice::ReadOnly), "old JSON read");
        auto oldjson = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        oldjson["version"] = version;
        check(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "old JSON write");
        f.write(QJsonDocument(oldjson).toJson());
        f.close();
        check(Store::load(file(name)).layers[0].effects.empty(), "v1-v16 migration has no shadow");
    }
}
void keyBudget() {
    CompositionLayer layer{1, LayerKind::Shape, "Budget"};
    layer.duration_frames = 30;
    layer.content = defaultShapeLayerContent({160, 90});
    layer.keyframes.opacity.resize(maximum_document_keyframes - 1);
    for (std::size_t i = 0; i < layer.keyframes.opacity.size(); ++i)
        layer.keyframes.opacity[i] = {std::int64_t(i), 1};
    std::vector<CompositionLayer> layers;
    layers.push_back(std::move(layer));
    CompositionDocument doc(160, 90, {30, 1}, std::move(layers));
    check(doc.setLayerEffects(1, {DropShadowEffect{}}), "budget effect fixture");
    check(doc.setEffectKeyframe(1, 0, EffectParameterProperty::ShadowOpacityPercent, 0, 50),
          "exact global key budget");
    check(!doc.setEffectKeyframe(1, 0, EffectParameterProperty::ShadowOpacityPercent, 1, 60),
          "insertion rejects global overflow");
    check(
        !doc.replaceEffectKeyframes(1, 0, EffectParameterProperty::ShadowOpacityPercent, {{0, 50}, {1, 60}}),
        "curve replacement rejects global overflow");
    auto effects = doc.layers()[0].effects;
    effects.push_back(effects.front());
    check(!doc.setLayerEffects(1, effects), "stack replacement rejects global overflow");
}
void render(const QString &root) {
    auto source = std::make_shared<const media::RgbaFrame>(pixels());
    CompositionLayer image{1, LayerKind::Image, "Image"};
    image.source_path = file(root + "/image.png");
    image.duration_frames = 30;
    QImage png(source->rgba_pixels.data(), 2, 2, 8, QImage::Format_RGBA8888);
    check(png.save(QString::fromStdU16String(image.source_path.u16string())), "write export media");
    DropShadowEffect shadow;
    shadow.parameters.softness_pixels = 0;
    shadow.parameters.distance_pixels = .5;
    shadow.parameters.angle_degrees = 0;
    shadow.parameters.color = {30, 50, 70};
    image.effects = {shadow};
    CompositionDocument doc(40, 20, {30, 1}, {image});
    CompositionFrameRenderer renderer;
    auto r = request(doc, 0, source);
    const auto actual = renderer.render(r);
    check(bool(actual), "expanded image render");
    bool budget_refused = false;
    MotionMaskRasterCache limited_cache;
    try {
        (void)renderExpandedLayer(r.sceneLayers()[0], source, r, limited_cache, nullptr, {}, nullptr, nullptr,
                                  1);
    } catch (const std::runtime_error &) {
        budget_refused = true;
    }
    check(budget_refused, "remaining scene preparation budget is checked before allocating");

    const auto p = shadow.parameters;
    const auto bounds = e::dropShadowBounds(2, 2, p);
    const auto prepared = e::applyDropShadow(*source, p);
    auto transformed = c::transformAffineFrame(*prepared, 40, 20,
                                               {10, 0, 0, 10, 10. - bounds.left * 10, -bounds.top * 10.}, 1);
    check(bool(transformed), "independent fit transform");
    auto expected = c::FrameCompositor::compose(40, 20, {{&*transformed, {}}});
    check(expected.has_value(), "reference composition");
    same(*expected, *actual);
    check(doc.setEffectKeyframe(1, 0, EffectParameterProperty::ShadowDistancePixels, 0, .5) &&
              doc.setEffectKeyframe(1, 0, EffectParameterProperty::ShadowDistancePixels, 20, 4.5) &&
              doc.setEffectKeyframe(1, 0, EffectParameterProperty::ShadowOpacityPercent, 0, 50) &&
              doc.setEffectKeyframe(1, 0, EffectParameterProperty::ShadowOpacityPercent, 20, 75),
          "animated render fixture");
    auto animated = renderer.render(request(doc, 10, source));
    auto interpolated = p;
    interpolated.distance_pixels = 2.5;
    interpolated.opacity_percent = 62.5;
    const auto ab = e::dropShadowBounds(2, 2, interpolated);
    const auto alpha = e::applyDropShadow(*source, interpolated);
    auto local =
        c::transformAffineFrame(*alpha, 40, 20, {10, 0, 0, 10, 10. - ab.left * 10, -ab.top * 10.}, 1);
    auto reference_animated = c::FrameCompositor::compose(40, 20, {{&*local, {}}});
    check(animated && reference_animated, "animated reference");
    same(*reference_animated, *animated);
    auto no_shadow = doc;
    auto fx = no_shadow.layers()[0].effects;
    std::get<DropShadowEffect>(fx[0]).enabled = false;
    check(no_shadow.setLayerEffects(1, fx), "disable render shadow");
    auto disabled = renderer.render(request(no_shadow, 0, source));
    check(no_shadow.setLayerEffects(1, {}), "remove render shadow");
    auto legacy = renderer.render(request(no_shadow, 0, source));
    check(disabled && legacy, "legacy render");
    same(*disabled, *legacy);
    auto masked_doc = doc;
    const auto shadow_mask = masked_doc.createMask(1, 0, c::ShapeMaskKind::Rectangle);
    check(shadow_mask, "shadow mask fixture");
    c::ShapeMaskParameters mask_parameters;
    mask_parameters.width = 0;
    check(masked_doc.setMaskParameters(shadow_mask, mask_parameters), "empty shadow effect mask");
    auto suppressed = renderer.render(request(masked_doc, 0, source));
    check(bool(suppressed), "suppressed shadow render");
    same(*legacy, *suppressed);
    mask_parameters.width = .5;
    mask_parameters.height = 1;
    check(masked_doc.setMaskParameters(shadow_mask, mask_parameters), "partial shadow effect mask");
    c::ShapeMaskRasterCache independent_mask_cache;
    auto mask_coverage = independent_mask_cache.coverage(
        c::ShapeMaskKind::Rectangle, mask_parameters, bounds.width, bounds.height, {},
        c::MaskRasterSpace{2, 2, -double(bounds.left), -double(bounds.top)});
    media::RgbaFrame aligned{bounds.width, bounds.height, bounds.width * 4,
                             std::vector<std::uint8_t>(std::size_t(bounds.width) * bounds.height * 4)};
    for (int y = 0; y < 2; ++y)
        std::copy_n(source->rgba_pixels.data() + y * source->stride, 8,
                    aligned.rgba_pixels.data() + std::size_t(y + bounds.top) * aligned.stride +
                        bounds.left * 4);
    auto masked_reference = *prepared;
    check(mask_coverage && c::maskedBlend(aligned, masked_reference, *mask_coverage),
          "independent stable logical mask reference");
    auto mask_projected = c::transformAffineFrame(
        masked_reference, 40, 20, {10, 0, 0, 10, 10. - bounds.left * 10, -bounds.top * 10.}, 1);
    auto mask_expected = c::FrameCompositor::compose(40, 20, {{&*mask_projected, {}}});
    auto mask_actual = renderer.render(request(masked_doc, 0, source));
    check(mask_expected && mask_actual, "partial mask render");
    same(*mask_expected, *mask_actual);
    // Exercise all mask stages, an affine parent, curves and prepared-pixel mattes.
    LayerId parent = 0;
    check(doc.addContentLayer(LayerKind::Null, "Parent", 0, &parent) &&
              doc.setLayerScaleAtFrame(parent, 1.1, .8, 0) && doc.setLayerRotationAtFrame(parent, 23, 0) &&
              doc.setLayerParent(1, parent, 0),
          "affine parent");
    check(doc.setLayerAnchorPoint(1, {3, -2}) && doc.setLayerScaleAtFrame(1, .9, 1.2, 0),
          "external anchor and scale");
    auto mask = doc.createMask(1, -2, c::ShapeMaskKind::Ellipse);
    check(mask && doc.associateMask(1, 0, mask) && doc.createMask(1, -1, c::ShapeMaskKind::Rectangle),
          "source/effect/transform masks");
    auto frame = renderer.render(request(doc, 0, source));
    check(bool(frame), "masked expanded layer");
    LayerId shape = 0;
    check(doc.createPathLayer(
              {{{{-4, -2}, {}, {2, -1}}, {{4, -2}, {-2, -1}, {}}, {{4, 2}, {}, {}}, {{-4, 2}, {}, {}}}, true},
              0, &shape),
          "Bezier source");
    check(doc.setLayerEffects(shape, {shadow, GaussianBlurEffect{true, 1},
                                      ColorAdjustmentEffect{true, 5, 110, 90}, shadow}) &&
              doc.setLayerTrimPathsEnabled(shape, true) &&
              doc.setLayerTrimValueAtFrame(shape, TrimPathProperty::End, 60, 0),
          "ordered shadows and Trim");
    check(doc.setLayerTrackMatte(1, TrackMatteBinding{shape, c::TrackMatteMode::Alpha}) &&
              doc.setLayerBlendMode(1, c::BlendMode::Screen),
          "matte and blend");
    LayerId text_id = 0;
    check(doc.addContentLayer(LayerKind::Text, "Shadow text", 0, &text_id), "text fixture");
    auto text = std::get<TextLayerContent>(doc.layers().back().content);
    text.text = "Shadow";
    text.font_size_pixels = 8;
    text.box_width = 30;
    text.box_height = 12;
    check(doc.setTextLayerContent(text_id, text) && doc.setLayerEffects(text_id, {shadow}), "text shadow");
    const auto video_path = file(root + "/source.mkv");
    media::VideoEncodingSettings video_settings;
    video_settings.output_path = video_path;
    video_settings.container_name = "matroska";
    video_settings.video_encoder_name = "ffv1";
    video_settings.width = 16;
    video_settings.height = 8;
    video_settings.frame_rate_numerator = 30;
    media::RgbaFrame video_pixels{16, 8, 64, std::vector<std::uint8_t>(16 * 8 * 4, 255)};
    for (std::size_t i = 0; i < video_pixels.rgba_pixels.size(); i += 4) {
        video_pixels.rgba_pixels[i] = 50;
        video_pixels.rgba_pixels[i + 1] = 100;
        video_pixels.rgba_pixels[i + 2] = 170;
    }
    {
        media::VideoEncoder encoder(video_settings);
        encoder.writeVideo(video_pixels, 0);
        encoder.writeVideo(video_pixels, 1);
        encoder.finish();
    }
    auto layers = doc.layers();
    CompositionLayer movie{99, LayerKind::Video, "Shadow video"};
    movie.source_path = video_path;
    movie.source_frame_count = 2;
    movie.source_frame_rate = 30;
    movie.duration_frames = 2;
    movie.maximum_timeline_duration_frames = 2;
    movie.source_duration_frames = 2;
    movie.transform = {.75, .7, .35, 13, .7};
    movie.effects = {shadow};
    layers.push_back(movie);
    CompositionLayer offline{100, LayerKind::Image, "Offline ancestor"};
    offline.visible = false;
    offline.timeline_start_frame = 80;
    offline.duration_frames = 1;
    offline.source_path = file(root + "/offline.png");
    offline.scale_y = ScaleChannel{.8, {}};
    offline.transform.position_x = 0;
    offline.transform.position_y = 0;
    offline.transform.rotation_degrees = 13;
    layers.push_back(offline);
    for (auto &layer : layers)
        if (layer.id == parent)
            layer.parent_id = offline.id;
    doc = CompositionDocument(doc.canvasSize().width, doc.canvasSize().height, doc.frameRate(),
                              std::move(layers), doc.masks());
    auto &metrics = motion::diagnostics::PerformanceMetrics::instance();
    metrics.setEnabled(true);
    metrics.reset();
    r = request(doc, 0, source);
    auto mixed = renderer.render(r);
    check(bool(mixed), "complete shadow pipeline");
    (void)metrics.takeSnapshotAndReset();
    auto warmed = renderer.render(r);
    auto cached = metrics.takeSnapshotAndReset();
    check(warmed && cached &&
              cached->timings[std::size_t(motion::diagnostics::PreviewTimingStage::TextShapeRasterization)]
                      .count == 0,
          "shadow reuses source rasters");
    check(cached->timestamp_seek_attempts == 0 && cached->forward_decode_attempts == 0,
          "warm video uses its decoded frame");
    auto movie_effects = doc.layers()[doc.layers().size() - 2].effects;
    check(!movie_effects.empty(), "video effect cache fixture");
    std::get<DropShadowEffect>(movie_effects[0]).parameters.angle_degrees = 90;
    check(doc.setLayerEffects(99, movie_effects), "edit shadow without source changes");
    metrics.reset();
    auto changed = renderer.render(request(doc, 0, source));
    auto reused = metrics.takeSnapshotAndReset();
    check(changed && reused && reused->timestamp_seek_attempts == 0 && reused->forward_decode_attempts == 0 &&
              reused->timings[std::size_t(motion::diagnostics::PreviewTimingStage::TextShapeRasterization)]
                      .count == 0,
          "shadow edit reuses decoded video and content rasters");
    mixed = changed;
    r = request(doc, 0, source);
    auto candidate = r;
    candidate.gesture_session = 7;
    candidate.position_override = LayerPositionOverride{99, .8, .6};
    auto candidate_reference = renderer.render(candidate, {}, true, PreviewRequestMode::Gesture);
    CompositionGpuMetrics fallback_metrics;
    metrics.reset();
    CompositionFrameRenderer recovery(true, true, nullptr, &fallback_metrics);
    auto recovered = recovery.render(candidate, {}, true, PreviewRequestMode::Gesture);
    auto recovery_profile = metrics.takeSnapshotAndReset();
    check(candidate_reference && recovered && fallback_metrics.fallback_frames == 1 &&
              fallback_metrics.composition_frames == 0,
          "GPU recovery uses a bounded CPU fallback");
    same(*candidate_reference, *recovered);
    check(recovery_profile && recovery_profile->gpu_drop_shadow_fallbacks > 0,
          "shadow fallback has a separate diagnostic counter");
    std::cout << "Drop Shadow forced fallback native=0 cpu=" << fallback_metrics.fallback_frames << '\n';

    metrics.reset();
    CompositionFrameRenderer quiet(false);
    check(bool(quiet.render(r)), "quiet render");
    check(!metrics.takeSnapshotAndReset(), "disabled instrumentation stays silent for shadows");
    check(!renderer.render(r, [] { return true; }, true, PreviewRequestMode::Gesture), "cancelled render");
    MotionExportSnapshot snapshot{
        doc.canvasSize(), doc.frameRate(), doc.layers(), {{1, source}}, doc.masks()};
    for (auto &layer : snapshot.layers)
        layer.duration_frames = 1;
    MotionExportSettings settings{file(root + "/shadow.mkv"), "matroska", "ffv1", 40, 20, {30, 1}, 10};
    std::atomic_bool cancelled = false;
    MotionVideoExporter::exportVideo(snapshot, settings, cancelled);
    media::VideoEncodingSettings reference;
    reference.output_path = file(root + "/reference.mkv");
    reference.container_name = "matroska";
    reference.video_encoder_name = "ffv1";
    reference.width = 40;
    reference.height = 20;
    reference.frame_rate_numerator = 30;
    {
        media::VideoEncoder encoder(reference);
        encoder.writeVideo(*mixed, 0);
        encoder.finish();
    }
    auto video = media::VideoPlaybackSession::open(settings.output_path),
         reference_video = media::VideoPlaybackSession::open(reference.output_path);
    auto a = video->decode_next_frame(), b = reference_video->decode_next_frame();
    check(a && b, "decode reference export");
    same(**a, **b);
}
void gpu(bool required) {
    motion::diagnostics::PerformanceMetrics::instance().reset();
    c::OpenGlImageOperation refused;
    refused.kind = c::OpenGlImageOperationKind::DropShadow;
    c::VulkanFrameCompositor vk;
    check(vk.processImage(refused).status == c::OpenGlCompositionStatus::Unsupported,
          "Vulkan explicitly refuses shadow");
    auto surface = c::OpenGlFrameCompositor::createSurface();
    c::OpenGlFrameCompositor compositor(surface.get());
    auto input = pixels();
    e::DropShadowParameters p;
    p.color = {30, 80, 170};
    p.distance_pixels = 2.5;
    p.angle_degrees = 0;
    p.softness_pixels = 1;
    c::OpenGlImageOperation operation;
    operation.kind = c::OpenGlImageOperationKind::DropShadow;
    operation.background = {&input};
    operation.drop_shadow = p;
    auto result = compositor.processImage(operation);
    if (result.status != c::OpenGlCompositionStatus::Complete) {
        std::cout << "Drop Shadow native unavailable: " << result.operation << " / " << result.cause << '\n';
        check(!required, "native GPU must succeed without fallback");
        return;
    }
    auto cpu = e::applyDropShadow(input, p);
    auto read = compositor.readback(result.frame);
    check(cpu && read.frame, "GPU shadow readback");
    same(*cpu, *read.frame, 2);
    result.frame.reset();
    (void)compositor.collectReleasedTextureFrames();
    check(!compositor.processImage(operation, [] { return true; }).frame, "GPU cancellation");
    auto reused = compositor.processImage(operation);
    check(reused.status == c::OpenGlCompositionStatus::Complete, "GPU reuse after cancellation");
    std::cout << "Drop Shadow native=1 fallback=0\n";
    reused.frame.reset();
    (void)compositor.collectReleasedTextureFrames();
    for (double angle : {45., 90., -270., 720.125}) {
        operation.drop_shadow.angle_degrees = angle;
        auto expected = e::applyDropShadow(input, operation.drop_shadow);
        auto texture = compositor.processImage(operation);
        check(texture.status == c::OpenGlCompositionStatus::Complete, "native shadow direction");
        auto bytes = compositor.readback(texture.frame);
        check(expected && bytes.frame, "native direction readback");
        same(*expected, *bytes.frame, 2);
        texture.frame.reset();
        (void)compositor.collectReleasedTextureFrames();
    }

    CompositionDocument scene(160, 90, {30, 1});
    LayerId id = 0, parent = 0;
    check(scene.addContentLayer(LayerKind::Shape, "Native Shadow", 0, &id), "native scene");
    auto content = std::get<ShapeLayerContent>(scene.layers()[0].content);
    content.width = 30;
    content.height = 20;
    content.fill_color = {180, 80, 40, 180};
    check(scene.setShapeLayerContent(id, content), "native shape");
    DropShadowEffect effect;
    effect.parameters = p;
    check(scene.setLayerEffects(id, {effect}), "native shadow effect");
    check(scene.addContentLayer(LayerKind::Null, "Parent", 0, &parent) &&
              scene.setLayerScaleAtFrame(parent, 1.1, .8, 0) &&
              scene.setLayerRotationAtFrame(parent, 17, 0) && scene.setLayerParent(id, parent, 0),
          "native affine parent");
    check(scene.setLayerAnchorPoint(id, {7, -5}) && scene.setLayerScaleAtFrame(id, .9, 1.2, 0),
          "native local geometry");
    const auto mask = scene.createMask(id, -2, c::ShapeMaskKind::Ellipse);
    check(mask && scene.associateMask(id, 0, mask) && scene.createMask(id, -1, c::ShapeMaskKind::Rectangle),
          "native mask stages");
    auto r = request(scene);
    CompositionFrameRenderer cpu_renderer(false);
    const auto reference = cpu_renderer.render(r);
    CompositionGpuMetrics metrics;
    CompositionFrameRenderer native_renderer(true, true, surface.get(), &metrics);
    const auto native = native_renderer.render(r, {}, true);
    check(reference && native, "native prepared layer");
    same(*reference, *native, 2);
    check(metrics.composition_frames == 1 && metrics.fallback_frames == 0,
          "native layer cannot pass through fallback");
    std::optional<c::OpenGlReadbackTicket> ticket;
    bool async_failure = false;
    (void)native_renderer.render(r, {}, true, PreviewRequestMode::Interactive, &ticket, &async_failure);
    check(ticket && !async_failure, "shadow asynchronous readback submitted");
    auto collected = native_renderer.collectAsyncReadback(*ticket);
    check(collected.frame.has_value(), "shadow asynchronous readback collected");
    same(*reference, *collected.frame, 2);
    native_renderer.shutdown();
    auto budget = std::make_shared<c::OpenGlTexturePoolBudget>(64ULL * 1024 * 1024, 1);
    {
        c::OpenGlFrameCompositor bounded(surface.get(), c::OpenGlPrecisionPolicy::Automatic, nullptr, budget);
        auto limited = bounded.processImage(operation);
        check(limited.status == c::OpenGlCompositionStatus::Busy ||
                  limited.status == c::OpenGlCompositionStatus::Unsupported,
              "bounded shadow pool refuses without unbounded retries");
    }
    check(budget->bytes() == 0, "failed shadow releases texture reservations");
}
template <class T> T *widget(QObject &owner, const char *name) {
    auto *p = dynamic_cast<T *>(owner.findChild<QObject *>(name));
    check(p, "UI control exists");
    return p;
}
void ui(const QString &root) {
    CompositionDocument doc(160, 90, {30, 1});
    LayerId id = 0;
    check(doc.addContentLayer(LayerKind::Shape, "Shape", 0, &id), "UI fixture");
    auto filename = root + "/ui.motion";
    Store::save(file(filename), project(doc));
    MainWindow window(nullptr, file(root + "/autosave"), "shadow");
    window.resize(1200, 850);
    window.show();
    QApplication::processEvents();
    QTimer::singleShot(0, [filename] {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        check(dialog, "open dialog");
        dialog->selectFile(filename);
        QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
    });
    widget<QAction>(window, "motion-open-composition-action")->trigger();
    auto *rows = widget<QWidget>(window, "motion-timeline-layer-rows");
    for (auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
        QMouseEvent event(type, QPointF(70, 17), rows->mapToGlobal(QPoint(70, 17)), Qt::LeftButton,
                          type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(rows, &event);
    }
    widget<QAction>(window, "motion-add-drop-shadow")->trigger();
    auto *undo = widget<QAction>(window, "motion-undo-action");
    auto *redo = widget<QAction>(window, "motion-redo-action");
    auto *opacity = widget<QDoubleSpinBox>(window, "motion-shadow-value-0");
    auto *angle = widget<QDoubleSpinBox>(window, "motion-shadow-value-1");
    auto *key = widget<QToolButton>(window, "motion-shadow-key-0");
    auto *timeline = widget<TimelineNavigator>(window, "motion-timeline");
    check(opacity->value() == 50 && angle->value() == 45 && opacity->decimals() == 6, "shadow defaults");
    undo->trigger();
    check(window.compositionDocument()->layers()[0].effects.empty() && redo->isEnabled(),
          "add single history");
    redo->trigger();
    opacity->setValue(25);
    QMetaObject::invokeMethod(opacity, "editingFinished", Qt::DirectConnection);
    undo->trigger();
    check(std::get<DropShadowEffect>(window.compositionDocument()->layers()[0].effects[0])
                  .parameters.opacity_percent == 50,
          "value edit single history");
    redo->trigger();
    key->click();
    timeline->setCurrentFrame(20);
    key->click();
    opacity->setValue(75);
    QMetaObject::invokeMethod(opacity, "editingFinished", Qt::DirectConnection);
    timeline->setCurrentFrame(10);
    check(opacity->isReadOnly() && opacity->value() == 50, "between keys readonly");
    key->click();
    check(!opacity->isReadOnly(), "insert evaluated key");
    timeline->setLayerExpanded(id, true);
    timeline->setTransformGroupExpanded(id, false);
    timeline->setEffectsGroupExpanded(id, true);
    widget<QSlider>(window, "motion-timeline-zoom-slider")->setValue(21);
    QApplication::processEvents();
    const auto send = [&](QEvent::Type type, QPointF point, Qt::MouseButton button,
                          Qt::MouseButtons buttons) {
        QMouseEvent event(type, point, rows->mapToGlobal(point.toPoint()), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(rows, &event);
    };
    const QPointF segment(timeline->frameToViewportX(5), 119);
    send(QEvent::MouseButtonPress, segment, Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, segment, Qt::LeftButton, Qt::NoButton);
    auto *preset = widget<QComboBox>(window, "motion-curve-editor-preset");
    check(preset->isEnabled(), "shadow timeline selects graph segment");
    preset->setCurrentIndex(1);
    check(std::get<DropShadowEffect>(window.compositionDocument()->layers()[0].effects[0])
                  .keyframes[0][0]
                  .interpolation == a::InterpolationMode::CubicBezier,
          "shadow Graph Editor easing");
    undo->trigger();
    const auto keys =
        std::get<DropShadowEffect>(window.compositionDocument()->layers()[0].effects[0]).keyframes;
    const auto drag = [&](int to) {
        send(QEvent::MouseButtonPress, {double(timeline->frameToViewportX(10)), 119}, Qt::LeftButton,
             Qt::LeftButton);
        send(QEvent::MouseMove, {double(timeline->frameToViewportX(to)), 119}, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, {double(timeline->frameToViewportX(to)), 119}, Qt::LeftButton,
             Qt::NoButton);
    };
    drag(15);
    check(std::get<DropShadowEffect>(window.compositionDocument()->layers()[0].effects[0])
                  .keyframes[0][1]
                  .frame == 15,
          "shadow key drag");
    undo->trigger();
    drag(20);
    check(std::get<DropShadowEffect>(window.compositionDocument()->layers()[0].effects[0]).keyframes ==
                  keys &&
              redo->isEnabled(),
          "shadow collision preserves Redo");
    timeline->setCurrentFrame(10);
    const auto before = window.compositionDocument()->layers();
    QTimer::singleShot(0, [] {
        auto *dialog = qobject_cast<QColorDialog *>(QApplication::activeModalWidget());
        check(dialog, "RGB picker");
        check(!dialog->testOption(QColorDialog::ShowAlphaChannel), "shadow color has no alpha");
        dialog->reject();
    });
    widget<QPushButton>(window, "motion-shadow-color")->click();
    check(window.compositionDocument()->layers() == before, "color cancel no history");
    QTimer::singleShot(0, [] {
        auto *dialog = qobject_cast<QColorDialog *>(QApplication::activeModalWidget());
        check(dialog, "confirm RGB picker");
        dialog->setCurrentColor(QColor(12, 34, 56));
        dialog->accept();
    });
    widget<QPushButton>(window, "motion-shadow-color")->click();
    check(std::get<DropShadowEffect>(window.compositionDocument()->layers()[0].effects[0]).parameters.color ==
              std::array<std::uint8_t, 3>{12, 34, 56},
          "static RGB applied");
    undo->trigger();
    check(window.compositionDocument()->layers() == before && redo->isEnabled(), "RGB single Undo");
    auto *list = widget<QListWidget>(window, "motion-layer-effects");
    auto *item = list->item(0);
    item->setCheckState(Qt::Unchecked);
    check(!std::get<DropShadowEffect>(window.compositionDocument()->layers()[0].effects[0]).enabled,
          "disable UI");
    undo->trigger();
    check(window.compositionDocument()->layers() == before, "disable Undo retains keys");
    widget<QAction>(window, "motion-save-composition-action")->trigger();
    check(Store::load(file(filename)).layers == before, "save confirmed values");
    widget<QAction>(window, "motion-new-null-layer-action")->trigger();
    check(!widget<QPushButton>(window, "motion-add-effect")->isEnabled(), "Null controls disabled");
    undo->trigger();
    QTimer::singleShot(0, [] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            box->button(QMessageBox::Discard)->click();
    });
    window.close();
    QApplication::processEvents();
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("CreativeSuiteTests");
    app.setApplicationName("MotionDropShadow");
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir root;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.path());
    try {
        check(root.isValid(), "temporary fixture");
        check(creative_suite::diagnostics::Logger::instance().initialize(file(root.path() + "/logs")), "log");
        numeric();
        model(root.path());
        keyBudget();
        render(root.path());
        const bool native_required = app.arguments().contains("--require-gpu");
        gpu(native_required);
        if (!native_required)
            ui(root.path());
        std::cout << "Drop Shadow tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
