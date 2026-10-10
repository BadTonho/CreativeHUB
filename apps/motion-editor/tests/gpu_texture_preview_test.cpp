#include "rendering/preview_renderer.h"
#include "rendering/composition_frame_renderer.h"
#include "ui/viewer/composition_viewer.h"
#include "ui/main_window/main_window.h"
#include "ui/timeline/timeline_navigator.h"
#include <QAction>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/media/video_encoder.h>
#include <QApplication>
#include <QElapsedTimer>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QThread>
#include <QImage>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <fstream>
#include <iterator>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool waitFor(const std::function<bool()>& test, bool events = true) {
    QElapsedTimer timer; timer.start();
    do {
        if (test()) return true;
        if (events) QApplication::processEvents();
        QThread::msleep(1);
    } while (timer.elapsed() < 10000);
    return false;
}
motion::ui::PreviewRequest fixture() {
    using namespace motion::model;
    auto pixels = std::make_shared<creative_suite::media::RgbaFrame>();
    pixels->width = 96; pixels->height = 64; pixels->stride = 96 * 4 + 8;
    pixels->rgba_pixels.resize(pixels->stride * pixels->height);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 96; ++x) {
        auto* p = pixels->rgba_pixels.data() + y * pixels->stride + x * 4;
        p[0] = y < 32 ? 230 : 25; p[1] = x < 48 ? 180 : 40;
        p[2] = y; p[3] = x < 48 ? 255 : 120;
    }
    motion::ui::PreviewLayerSnapshot image;
    image.id = 1; image.kind = LayerKind::Image; image.still_frame = pixels;
    image.effects = {ColorAdjustmentEffect{true, 5, 100, 80},
        GaussianBlurEffect{true, 3}, ColorAdjustmentEffect{true, -2, 105, 100},
        GaussianBlurEffect{false, 10}};
    motion::ui::PreviewLayerSnapshot shape;
    shape.id = 2; shape.kind = LayerKind::Shape;
    auto content = defaultShapeLayerContent({96, 64}, ShapeKind::Ellipse);
    content.width = 20; content.height = 16; content.fill_color = {20, 210, 80, 120};
    shape.content = content;
    shape.transform.position_x = .7; shape.transform.scale = .8;
    motion::ui::PreviewLayerSnapshot text;
    text.id = 3; text.kind = LayerKind::Text;
    auto letters = defaultTextLayerContent({96, 64});
    letters.text = "GPU"; letters.font_size_pixels = 12;
    letters.box_width = 32; letters.box_height = 20;
    text.content = letters;
    text.transform.position_x = .3; text.transform.position_y = .3;
    return {{96, 64}, {30, 1}, {image, shape, text}};
}
void portableChecks() {
    QObject receiver;
    std::atomic<unsigned> rendered{0};
    unsigned delivered = 0;
    std::uint64_t last = 0;
    motion::ui::PreviewRenderer worker(&receiver,
        [&](auto generation, auto, auto, auto frame) {
            require(bool(frame), "portable delivery keeps the RGBA entry point");
            ++delivered; last = generation;
        }, [&](const auto& request, const auto&) {
            ++rendered; return request.layers.front().still_frame;
        });
    for (unsigned n = 1; n <= 32; ++n) {
        last = worker.submit(fixture(), motion::ui::PreviewRequestMode::Playback);
        require(waitFor([&] { return rendered.load() >= n; }, false), "worker completes without draining GUI events");
        require(waitFor([&] { return worker.pendingDeliveryCount() == 1; }, false), "mailbox retains a result");
        require(worker.pendingDeliveryCount() <= 1, "delivery mailbox is bounded");
    }
    const auto expected = last;
    QThread::msleep(10);
    require(waitFor([&] { return delivered == 1; }), "only one GUI delivery is queued");
    require(last == expected, "mailbox presents the most recent completed playback frame");
    (void)worker.submit(fixture());
    require(waitFor([&] { return worker.pendingDeliveryCount() == 1; }, false), "interactive result is queued");
    worker.resetSessions();
    QApplication::processEvents();
    require(delivered == 1, "composition reset removes outstanding delivery");
    (void)worker.submit(fixture());
    require(waitFor([&] { return worker.pendingDeliveryCount() == 1; }, false), "shutdown has a queued result");
    worker.stopAndWait(); QApplication::processEvents();
    require(delivered == 1 && worker.pendingDeliveryCount() == 0, "shutdown releases mailbox before GUI drain");

    auto request = fixture();
    motion::ui::CompositionFrameRenderer cpu(false), unavailable(false, true, nullptr);
    creative_suite::composition::OpenGlTextureFramePtr texture;
    bool busy = false;
    const auto expected_rgba = cpu.render(request);
    const auto recovered = unavailable.render(request, {}, false,
        motion::ui::PreviewRequestMode::Interactive, nullptr, nullptr, &texture, &busy);
    require(recovered && !texture && !busy && recovered->rgba_pixels == expected_rgba->rgba_pixels,
            "missing GPU surface preserves CPU effects and pixels");
    motion::ui::PreviewFrame invalid; invalid.rgba = recovered;
    require(invalid.valid(), "RGBA is a valid single-representation payload");
    invalid.rgba.reset(); require(!invalid.valid(), "empty payload cannot be presented");
}
void applicationChecks() {
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QCoreApplication::setOrganizationName("Motion GPU Regression");
    QCoreApplication::setApplicationName("Texture UI Test");
    auto window = std::make_unique<motion::ui::MainWindow>(nullptr,
        std::filesystem::path(settings.path().toStdString()) / "recovery", "native-gpu-test");
    window->show();
    auto command = [&](const char* name) {
        auto* action = window->findChild<QAction*>(QString::fromLatin1(name));
        require(action != nullptr, "native application action exists");
        return action;
    };
    QTimer::singleShot(0, [] {
        auto* dialog = QApplication::activeModalWidget();
        require(dialog != nullptr, "native composition dialog opens");
        dialog->findChild<QLineEdit*>("motion-canvas-width")->setText("320");
        dialog->findChild<QLineEdit*>("motion-canvas-height")->setText("180");
        auto* rate = dialog->findChild<QComboBox*>("motion-frame-rate");
        for (int n = 1; n < rate->count(); ++n)
            if (rate->itemData(n).toLongLong() == 30 && rate->itemData(n, Qt::UserRole+1).toLongLong() == 1)
                rate->setCurrentIndex(n);
        dialog->findChild<QDialogButtonBox*>("motion-new-composition-buttons")->button(QDialogButtonBox::Ok)->click();
    });
    command("motion-new-composition-action")->trigger();
    command("motion-new-text-layer-action")->trigger();
    auto* viewer = dynamic_cast<motion::ui::CompositionViewer*>(
        window->findChild<QWidget*>("motion-composition-viewer"));
    auto* timeline = window->findChild<motion::ui::TimelineNavigator*>("motion-timeline");
    require(viewer && timeline && waitFor([&] {
        return viewer->texturePresentationAvailable() && viewer->lastPresentedGeneration() > 0 && !viewer->renderedFrame();
    }), "actual Motion workspace presents direct textures after a native layer edit");
    auto* play = command("motion-play-pause-action");
    play->trigger();
    require(waitFor([&] { return timeline->isPlaying() && timeline->currentFrame() > 0; }),
        "actual GPU workspace plays through the Motion clock");
    play->trigger();
    require(!timeline->isPlaying(), "GPU playback pauses");
    auto previous = viewer->lastPresentedGeneration();
    timeline->setCurrentFrame(10);
    require(waitFor([&] { return viewer->lastPresentedGeneration() > previous; }) && !timeline->isPlaying(),
        "a paused seek refreshes direct preview");
    command("motion-loop-action")->setChecked(true);
    const auto end = window->compositionDocument()->layers().front().duration_frames;
    timeline->setCurrentFrame(end-2);
    play->trigger();
    require(waitFor([&] { return timeline->isPlaying() && timeline->currentFrame() < end-2; }),
        "GPU playback loops to the start without freezing");
    previous = viewer->lastPresentedGeneration();
    require(waitFor([&] { return viewer->lastPresentedGeneration() > previous; }),
        "looped GPU playback continues presenting fresh textures");
    // Destruction while playback is active exercises the production shutdown,
    // including GUI-held leases and queued MainWindow callbacks.
    window.reset();
    QApplication::processEvents();
}
void nativeChecks() {
    using namespace motion::ui;
    auto surface = creative_suite::composition::OpenGlFrameCompositor::createSurface();
    require(bool(surface) && QOpenGLContext::globalShareContext(), "native GPU/share context is required");
    auto& metrics = motion::diagnostics::PerformanceMetrics::instance();
    metrics.setEnabled(true); metrics.reset();
    QObject receiver;
    CompositionViewer viewer;
    viewer.setMinimumSize(0, 0); viewer.resize(144, 130);
    viewer.setComposition({96, 64}, std::nullopt);
    PreviewRenderer* worker_ptr = nullptr;
    PreviewRequest request = fixture();
    QTemporaryDir media;
    const auto video_path = std::filesystem::path(media.path().toStdString()) / "synthetic.mkv";
    creative_suite::media::VideoEncodingSettings encoding;
    encoding.output_path = video_path; encoding.container_name = "matroska";
    encoding.video_encoder_name = "ffv1"; encoding.width = 96; encoding.height = 64;
    encoding.frame_rate_numerator = 30;
    {
        creative_suite::media::VideoEncoder encoder(encoding);
        for (int n = 0; n < 6; ++n) encoder.writeVideo(*request.layers.front().still_frame, n);
        encoder.finish();
    }
    PreviewLayerSnapshot video;
    video.id = 4; video.kind = motion::model::LayerKind::Video;
    video.source_path = video_path; video.source_frame_rate = 30; video.source_frame_count = 6;
    video.local_frame = 2; video.transform.scale = .2; video.transform.position_y = .8;
    video.transform.rotation_degrees = 25;
    video.effects = {motion::model::ColorAdjustmentEffect{true, 7, 110, 90}};
    request.layers.push_back(video);
    auto second_image = request.layers.front();
    second_image.id = 5; second_image.effects.clear(); second_image.transform.scale = .2;
    second_image.transform.position_x = .8; second_image.transform.position_y = .2;
    request.layers.push_back(second_image);
    bool recovered = false;
    viewer.enableTexturePresentation([&](bool cpu) {
        recovered = true;
        worker_ptr->recoverPresentation(cpu);
        (void)worker_ptr->submit(request);
    });
    viewer.show();
    require(waitFor([&] { return viewer.texturePresentationAvailable(); }), "native viewer shares textures");
    bool direct = false;
    PreviewRenderer worker(&receiver, {}, {}, &metrics, true, surface.get(),
        [&](PreviewFrame frame) {
            direct = bool(frame.texture);
            viewer.setPreviewFrame(std::move(frame));
        }, QOpenGLContext::globalShareContext());
    worker_ptr = &worker;
    viewer.setFrameValidator([&](const PreviewFrame& frame) {
        return worker.canPresentResult(frame.generation,
            frame.playback ? PreviewRequestMode::Playback : PreviewRequestMode::Interactive,
            frame.cancellation_generation);
    });
    auto generation = worker.submit(request);
    require(waitFor([&] { return viewer.lastPresentedGeneration() == generation; }) && direct,
        "native preview must present a GPU lease rather than CPU fallback");
    auto* gl = viewer.findChild<QOpenGLWidget*>();
    require(gl != nullptr, "GPU surface remains inside the existing viewer");
    const auto gpu_image = gl->grabFramebuffer(); // Readback is test inspection only.
    CompositionFrameRenderer cpu(false);
    CompositionViewer reference;
    reference.setMinimumSize(0, 0); reference.resize(144, 130);
    reference.setComposition({96, 64}, std::nullopt);
    reference.setRenderedFrame(cpu.render(request));
    reference.show(); QApplication::processEvents();
    const auto cpu_image = reference.grab().toImage();
    const double dpr = gl->devicePixelRatioF();
    for (int y = 44; y < 104; ++y) for (int x = 26; x < 118; ++x) {
        const auto a = gpu_image.pixelColor(qRound(x * dpr), qRound(y * dpr));
        const auto b = cpu_image.pixelColor(qRound(x * dpr), qRound(y * dpr));
        require(std::abs(a.red()-b.red()) <= 2 && std::abs(a.green()-b.green()) <= 2 &&
                std::abs(a.blue()-b.blue()) <= 2 && a.alpha() == b.alpha(),
                "GPU presentation preserves mixed effects, alpha and vertical orientation");
    }
    const auto snapshot = metrics.takeSnapshotAndReset();
    require(snapshot && snapshot->delivery.texture_presented == 1 &&
        snapshot->gpu_composition_readback_bytes == 0 && snapshot->delivery.pool_peak_bytes <= 64ULL*1024*1024 &&
        snapshot->delivery.pool_peak_occupancy <= 3, "direct frames have zero readback and bounded output textures");
    viewer.setSelectedLayerAnchor(QPointF(.5, .5));
    QApplication::processEvents();
    const auto overlay = gl->grabFramebuffer();
    const auto guide = overlay.pixelColor(qRound(72*dpr), qRound(73*dpr));
    require(guide.red() > 220 && guide.green() > 140 && guide.blue() < 80,
        "the selected-layer guide remains visible over direct textures");
    bool context_menu = false;
    viewer.setLayerContextMenuHandler([&](const QPoint&) { context_menu = true; });
    QContextMenuEvent menu(QContextMenuEvent::Mouse, QPoint(72, 73), viewer.mapToGlobal(QPoint(72, 73)));
    QApplication::sendEvent(&viewer, &menu);
    require(context_menu && gl->testAttribute(Qt::WA_TransparentForMouseEvents),
        "the GPU child preserves the container's canvas context menu");
    viewer.resize(280, 220); QApplication::processEvents();
    require(viewer.lastPresentedGeneration() == generation, "resize redraws held texture without recomposition");
    const auto old_cancellation = worker.generation();
    for (int i = 0; i < 20; ++i) generation = worker.submit(request);
    require(generation > old_cancellation && waitFor([&] { return viewer.lastPresentedGeneration() == generation; }),
        "rapid seeks present the latest generation");
    worker.resetSessions(); viewer.clearPreviewFrame();
    generation = worker.submit(request);
    require(waitFor([&] { return viewer.lastPresentedGeneration() == generation; }), "replacement composition presents fresh textures");
    require(QMetaObject::invokeMethod(gl->context(), "aboutToBeDestroyed", Qt::DirectConnection),
        "context destruction can be injected through its actual cleanup signal");
    require(waitFor([&] { return recovered; }), "context destruction requests worker recovery");
    generation = worker.generation();
    require(recovered && waitFor([&] { return viewer.lastPresentedGeneration() == generation; }) && !direct && viewer.renderedFrame(),
        "presentation failure recomposes RGBA on CPU without stopping the worker");
    std::ifstream log(creative_suite::diagnostics::Logger::instance().log_path());
    const std::string log_text((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
    require(log_text.find("destroy_viewer_context") != std::string::npos &&
            log_text.find("error_code") != std::string::npos,
        "context failure is logged with operation and error code before recovery");
    viewer.clearPreviewFrame(); worker.stopAndWait();
    // Retain every pool slot: playback drops without logging an error, while
    // paused rendering retries only its latest request until one lease returns.
    std::vector<creative_suite::composition::OpenGlTextureFramePtr> held;
    std::uint64_t pool_generation = 0;
    PreviewRenderer pool_worker(&receiver, {}, {}, &metrics, true, surface.get(),
        [&](PreviewFrame frame) {
            require(bool(frame.texture), "pool recovery remains on the GPU");
            pool_generation = frame.generation; held.push_back(std::move(frame.texture));
        }, QOpenGLContext::globalShareContext());
    for (int i = 0; i < 3; ++i) {
        const auto next = pool_worker.submit(request);
        require(waitFor([&] { return pool_generation == next; }), "three distinct output targets can be retained");
    }
    metrics.reset();
    (void)pool_worker.submit(request, PreviewRequestMode::Playback);
    require(waitFor([&] {
        const auto sample = metrics.takeSnapshotAndReset();
        return sample && sample->delivery.busy_drops > 0;
    }), "a saturated pool drops playback instead of blocking the GUI");
    const auto blocked = pool_worker.submit(request);
    require(waitFor([&] {
        const auto sample = metrics.takeSnapshotAndReset();
        return sample && sample->delivery.busy_retries > 0;
    }), "a saturated pool retries a paused request");
    const auto newest = pool_worker.submit(request);
    held.erase(held.begin());
    require(waitFor([&] { return pool_generation == newest; }) && pool_generation != blocked,
        "a newer seek supersedes a paused retry and reuses a returned slot");
    held.clear(); pool_worker.stopAndWait();
    metrics.reset();
    bool rgba_fallback = false;
    PreviewRenderer no_share_worker(&receiver, {}, {}, &metrics, true, surface.get(),
        [&](PreviewFrame frame) { rgba_fallback = bool(frame.rgba) && !frame.texture; });
    (void)no_share_worker.submit(request);
    require(waitFor([&] { return rgba_fallback; }),
        "unavailable texture sharing retains GPU composition with RGBA delivery");
    const auto rgba_metrics = metrics.takeSnapshotAndReset();
    require(rgba_metrics && rgba_metrics->gpu_composition_frames == 1 &&
        rgba_metrics->gpu_composition_readback_bytes > 0 && !rgba_metrics->gpu_composition_fallbacks,
        "missing sharing uses actual GPU readback rather than silently accepting CPU");
    no_share_worker.stopAndWait();
    QApplication::processEvents(); metrics.setEnabled(false);
}
}
int main(int argc, char** argv) {
    const bool native = argc > 1 && std::string(argv[1]) == "--require-gpu";
    if (native) {
        qputenv("CREATIVE_SUITE_MOTION_GPU_COMPOSITION", "1");
        QSurfaceFormat format; format.setVersion(3, 2); format.setProfile(QSurfaceFormat::CoreProfile);
        QSurfaceFormat::setDefaultFormat(format);
        QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    }
    QApplication app(argc, argv);
    QTemporaryDir logs;
    creative_suite::diagnostics::Logger::instance().initialize(std::filesystem::path(logs.path().toStdString()));
    try {
        portableChecks();
        if (native) { nativeChecks(); applicationChecks(); }
        std::cout << "Motion GPU texture preview " << (native ? "native" : "portable") << " tests passed.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
