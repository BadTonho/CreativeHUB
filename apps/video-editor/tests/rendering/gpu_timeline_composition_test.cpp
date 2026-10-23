#include "playback/playback_worker.h"
#include <creative_suite/composition/vulkan_frame_compositor.h>
#include "rendering/graph_generators.h"
#include "rendering/preview_performance_metrics.h"
#include "logging/logger.h"
#include "workspaces/fusion/nodes/evaluation/node_graph_evaluator.h"
#ifdef CREATIVE_SUITE_TEST_IMAGE_EDITOR_MASKS
#include "image_document_session.h"
#endif

#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QThread>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QEventLoop>
#include <QTimer>
#include <QOpenGLContext>
#include <QSurfaceFormat>

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>

using namespace creative_suite::composition;
namespace {
void fusionParity(GpuFrameCompositor& gpu, QOffscreenSurface* surface) {
    using namespace fusion::nodes;
    auto source = std::make_shared<media::VideoFrame>(media::VideoFrame{37, 23, 148, std::vector<std::uint8_t>(148 * 23)});
    for (std::size_t i = 0; i < source->rgba_pixels.size(); ++i)
        source->rgba_pixels[i] = static_cast<std::uint8_t>(i * 31 + i / 7);
    auto foreground = std::make_shared<media::VideoFrame>(media::VideoFrame{13, 17, 52, std::vector<std::uint8_t>(52 * 17)});
    for (std::size_t i = 0; i < foreground->rgba_pixels.size(); ++i)
        foreground->rgba_pixels[i] = static_cast<std::uint8_t>(i * 11 + i / 3);
    NodeGraph graph;
    graph.nodes = {{1, NodeType::Input}, {2, NodeType::Input}, {3, NodeType::Transform},
        {4, NodeType::Color}, {5, NodeType::Effect}, {6, NodeType::Merge}, {7, NodeType::Output}};
    graph.next_id = 8;
    graph.nodes[2].transform.position_x = 0.47;
    graph.nodes[2].transform.position_y = 0.63;
    graph.nodes[2].transform.scale = 0.79;
    graph.nodes[2].transform.opacity = 0.37;
    graph.nodes[3].color = {12, 131, 83};
    graph.nodes[4].effect = creative_suite::effects::makeDefaultInstance("video.grayscale");
    graph.connections = {{1, 3, 0}, {3, 4, 0}, {4, 5, 0}, {5, 6, 0}, {2, 6, 1}, {6, 7, 0}};
    const InputFrames inputs{{1, source}, {2, foreground}};
    for (const double rotation : {0.0, 17.0, -43.0}) {
        graph.nodes[2].transform.rotation_degrees = rotation;
        bool used = false;
        const auto cpu = evaluate(graph, inputs, EvaluationContext{9});
        const auto output = evaluate(graph, inputs, EvaluationContext{9, &gpu, &used});
        if (!cpu || !output || !used) throw std::runtime_error("Fusion parity did not execute the native GPU graph.");
        if (cpu->width != output->width || cpu->height != output->height)
            throw std::runtime_error("Fusion GPU geometry changed.");
        for (std::size_t i = 0; i < cpu->rgba_pixels.size(); ++i)
            if (std::abs(int(cpu->rgba_pixels[i]) - int(output->rgba_pixels[i])) > (i % 4 == 3 ? 0 : 2))
                throw std::runtime_error("Fusion GPU intermediate alpha or RGB parity failed at byte " + std::to_string(i));
    }
    auto generated = makeCompositionGraph(); generated.nodes[0].background_color = {70, 80, 90, 128};
    Node text; text.id = 3; text.type = NodeType::Text; text.text.content = "Fusion";
    Node merge; merge.id = 4; merge.type = NodeType::Merge;
    generated.nodes.push_back(text); generated.nodes.push_back(merge); generated.next_id = 5;
    generated.connections = {{1, 4, 0}, {3, 4, 1}, {4, 2, 0}};
    InputFrames generators; rendering::prepareGraphFrames(generated, generators, 320, 180, true);
    EvaluationContext cpu_context; cpu_context.canvas_width = 320; cpu_context.canvas_height = 180;
    auto gpu_context = cpu_context; bool generated_gpu_used = false;
    gpu_context.gpu = &gpu; gpu_context.gpu_used = &generated_gpu_used;
    const auto generated_cpu = evaluate(generated, generators, cpu_context);
    const auto generated_gpu = evaluate(generated, generators, gpu_context);
    if (!generated_cpu || !generated_gpu || !generated_gpu_used)
        throw std::runtime_error("Fusion generators did not execute the native GPU graph.");
    for (std::size_t byte = 0; byte < generated_cpu->rgba_pixels.size(); ++byte)
        if (std::abs(int(generated_cpu->rgba_pixels[byte]) - int(generated_gpu->rgba_pixels[byte])) > 2)
            throw std::runtime_error("Fusion generator CPU/GPU parity failed.");
    for (const auto mask_type : {NodeType::RectangleMask,NodeType::EllipseMask})
    for (const auto receiver : {NodeType::Input,NodeType::Output,NodeType::Transform,NodeType::Color,NodeType::Effect,NodeType::Merge}) {
        auto masked=makePassthroughGraph(); Node mask;mask.id=3;mask.type=mask_type;
        mask.mask.rotation=31;mask.mask.soft_edge=0.2;mask.mask.invert=receiver==NodeType::Transform;
        masked.nodes.push_back(mask);masked.next_id=4;
        if (receiver==NodeType::Input || receiver==NodeType::Output) masked.connections.push_back({3,receiver==NodeType::Input ? 1ULL : 2ULL,2});
        else {
            Node n;n.id=4;n.type=receiver;n.color.brightness=27;n.transform.position_x=0.75;
            n.effect=creative_suite::effects::makeDefaultInstance("video.grayscale");
            masked.nodes.push_back(n);masked.next_id=5;masked.connections={{1,4,0},{4,2,0},{3,4,2}};
            if (receiver==NodeType::Merge) {Node fg;fg.id=5;masked.nodes.push_back(fg);masked.next_id=6;masked.connections.push_back({5,4,1});}
        }
        InputFrames frames{{1,source},{5,foreground}};
        bool used=false; const auto cpu=evaluate(masked,frames,EvaluationContext{9});
        const auto output=evaluate(masked,frames,EvaluationContext{9,&gpu,&used});
        if (!cpu || !output || !used) throw std::runtime_error("Masked graph did not execute the native GPU receiver " + std::to_string(int(receiver)));
        for (std::size_t i=0;i<cpu->rgba_pixels.size();++i)
            if (std::abs(int(cpu->rgba_pixels[i])-int(output->rgba_pixels[i]))>2)
                throw std::runtime_error("Mask CPU/GPU parity failed at byte "+std::to_string(i));
        auto viewed=evaluate(masked,frames,3,EvaluationContext{9,&gpu,&used});
        if (!viewed || !used) throw std::runtime_error("Mask VIEW did not execute on GPU.");
    }
    auto limited_graph=makePassthroughGraph();Node limited_mask;limited_mask.id=3;limited_mask.type=NodeType::RectangleMask;
    limited_graph.nodes.push_back(limited_mask);limited_graph.next_id=4;limited_graph.connections.push_back({3,2,2});
    auto budget=std::make_shared<OpenGlTexturePoolBudget>(64ULL*1024*1024,1);
    auto limited_gpu=createGpuFrameCompositor(surface,OpenGlPrecisionPolicy::Automatic,nullptr,budget);
    bool limited_used=true;auto fallback=evaluate(limited_graph,inputs,EvaluationContext{0,limited_gpu.get(),&limited_used});
    auto reference=evaluate(limited_graph,inputs);
    if (!fallback || !reference || limited_used || fallback->rgba_pixels!=reference->rgba_pixels)
        throw std::runtime_error("Bounded masked graph did not recover the complete CPU result.");
    bool used = false;
    const auto cancelled = evaluate(graph, inputs, EvaluationContext{0, &gpu, &used, [] { return true; }});
    if (cancelled || used) throw std::runtime_error("A cancelled Fusion GPU graph published a frame.");
}
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
media::VideoFramePtr raster(const QImage& image) {
    const auto rgba = image.convertToFormat(QImage::Format_RGBA8888);
    auto frame = std::make_shared<media::VideoFrame>();
    frame->width = rgba.width(); frame->height = rgba.height(); frame->stride = rgba.bytesPerLine();
    frame->rgba_pixels.assign(rgba.constBits(), rgba.constBits() + rgba.sizeInBytes());
    return frame;
}
void compare(const playback::VideoFramePtr& cpu, const playback::VideoFramePtr& gpu) {
    require(cpu && gpu && cpu->width == gpu->width && cpu->height == gpu->height,
        "Timeline GPU output geometry differs from CPU.");
    for (std::size_t i = 0; i < cpu->rgba_pixels.size(); ++i)
        if (std::abs(int(cpu->rgba_pixels[i]) - int(gpu->rgba_pixels[i])) > (i % 4 == 3 ? 0 : 2)) require(false,
            "Timeline CPU/GPU pixel mismatch at " + std::to_string(i));
}

void directWorker(QOffscreenSurface* surface, playback::CompositionLayerSpec image,
    playback::CompositionLayerSpec incoming, playback::CompositionLayerSpec text,
    media::VideoFramePtr published, media::VideoFramePtr refreshed) {
    playback::PlaybackWorker cpu, direct;
    media::VideoFramePtr expected;
    rendering::PreviewFramePayload delivered;
    int errors = 0;
    QObject::connect(&cpu, &playback::PlaybackWorker::frameReady,
        [&](media::VideoFramePtr frame, qint64, quint64, quint64) { expected = std::move(frame); });
    QObject::connect(&direct, &playback::PlaybackWorker::previewFrameReady,
        [&](rendering::PreviewFramePayload frame, qint64, quint64, quint64) { delivered = std::move(frame); });
    QObject::connect(&direct, &playback::PlaybackWorker::playbackError, [&](QString, qint64, quint64) { ++errors; });
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    quint64 epoch = 1;
    auto enable = [&] {
        direct.setGpuCompositionEnabled(true, surface);
        direct.setGpuTextureDelivery(true, epoch, QOpenGLContext::globalShareContext());
    };
    enable();
    for (const auto quality : {playback::PreviewQuality::Full, playback::PreviewQuality::Half, playback::PreviewQuality::Quarter}) {
        cpu.setPreviewQuality(quality); direct.setPreviewQuality(quality);
        for (const auto kind : {timeline::TransitionKind::CrossDissolve, timeline::TransitionKind::FadeToBlack}) {
            QVector<playback::CompositionTransitionSpec> transitions{{0, 0, 1, 60, 10, kind}};
            cpu.setComposition({image, incoming, text}, transitions, 30);
            direct.setComposition({image, incoming, text}, transitions, 30);
            for (qint64 frame : {0, 49, 50, 55, 59, 60, 65, 75}) {
                metrics.reset();
                cpu.renderCompositionFrame(frame, frame, 30);
                direct.renderCompositionFrame(frame, frame, 30);
                require(delivered.gpu && !delivered.rgba && delivered.delivery_epoch == epoch,
                    "Native timeline did not use direct delivery.");
                auto cached = delivered.gpu;
                direct.renderCompositionFrame(frame, frame, 30);
                require(delivered.gpu == cached, "Direct final frame cache lost its lease.");
                auto normal = metrics.takeSnapshotAndReset();
                require(normal.gpu_composition_readback_bytes == 0 && normal.gpu_composition_frames == 1 &&
                    normal.composition_cache_hits == 1, "Normal direct composition read pixels or missed the cache.");
                direct.recoverPreviewFrame(delivered, frame, 30, epoch);
                compare(expected, delivered.rgba);
                cached.reset(); delivered = {};
                direct.setGpuCompositionEnabled(false, surface);
                // Allow nonblocking retirement and producer fence collection.
                QThread::msleep(6); QCoreApplication::processEvents();
                ++epoch; enable();
            }
        }
    }
    image.still_frame = published;
    direct.setComposition({image}, {}, 31); direct.renderCompositionFrame(0, 0, 31);
    require(bool(delivered.gpu), "Published PNG did not use direct delivery.");
    direct.recoverPreviewFrame(delivered, 0, 31, epoch);
    const auto old = delivered.rgba;
    delivered = {}; direct.setGpuCompositionEnabled(false, surface);
    QThread::msleep(6); QCoreApplication::processEvents(); ++epoch; enable();
    image.still_frame = refreshed;
    direct.setComposition({image}, {}, 32); direct.renderCompositionFrame(0, 0, 32);
    require(bool(delivered.gpu), "Refreshed PNG did not use direct delivery.");
    direct.recoverPreviewFrame(delivered, 0, 32, epoch);
    require(old && delivered.rgba && old->rgba_pixels != delivered.rgba->rgba_pixels,
        "Direct delivery cached the previous published PNG.");
    delivered = {}; direct.setGpuCompositionEnabled(false, surface);
    QThread::msleep(6); QCoreApplication::processEvents(); ++epoch; enable();
    direct.setPreviewQuality(playback::PreviewQuality::Quarter);
    direct.setComposition({image}, {}, 33);
    std::vector<rendering::PreviewFramePayload> held;
    for (qint64 frame : {0, 1, 2}) {
        direct.renderCompositionFrame(frame, frame, 33);
        require(bool(delivered.gpu), "Direct pool warmup failed."); held.push_back(delivered);
    }
    auto before = delivered.gpu;
    direct.renderCompositionFrame(3, 3, 33);
    direct.renderCompositionFrame(4, 4, 33);
    require(delivered.gpu == before, "Busy request overwrote a leased frame.");
    held[0] = {};
    QEventLoop retry;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &retry, [&] { if (delivered.timeline_frame == 4) retry.quit(); });
    poll.start(5); QTimer::singleShot(2000, &retry, &QEventLoop::quit); retry.exec();
    require(delivered.timeline_frame == 4 && delivered.gpu && errors == 0,
        "Paused Busy retry did not coalesce to the latest request.");
    direct.setGpuTextureDelivery(false, ++epoch, QOpenGLContext::globalShareContext());
    direct.renderCompositionFrame(4, 4, 33);
    require(delivered.rgba && !delivered.gpu, "Disabling direct presentation did not retain GPU RGBA fallback.");
    held.clear(); delivered = {}; before.reset();
    direct.setGpuCompositionEnabled(false, surface);
    QThread::msleep(6); QCoreApplication::processEvents(); ++epoch; enable();
    direct.renderCompositionFrame(5, 5, 33);
    require(bool(delivered.gpu), "Direct delivery did not retry after off/on.");
    std::string cause; std::int64_t code = 0;
    if (sessionGraphicsBackend() == GraphicsBackend::Vulkan)
        direct.recoverPreviewFrame(delivered, 5, 33, epoch);
    else
        require(!delivered.gpu->endUse(nullptr, cause, code), "Lost consumer context was not rejected.");
    QThread::msleep(6); QCoreApplication::processEvents();
    direct.renderCompositionFrame(6, 6, 33);
    require(delivered.rgba && !delivered.gpu && errors == 0,
        "A consumer/fence failure did not disable direct delivery and preserve RGBA fallback.");
    direct.renderCompositionFrame(7, 7, 33);
    require(delivered.rgba && !delivered.gpu, "Direct failure unexpectedly retried within the same activation.");
    delivered = {};
}
} // namespace

int main(int argc, char** argv) {
    QSurfaceFormat format; format.setVersion(3, 2); format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QGuiApplication app(argc, argv);
    const bool vulkan = app.arguments().contains("--vulkan");
    std::shared_ptr<VulkanDevice> vulkan_device;
    if (vulkan) {
        std::string cause; vulkan_device = VulkanDevice::create(cause);
        if (!vulkan_device) { std::cerr << cause << '\n'; return app.arguments().contains("--require-gpu") ? 1 : 77; }
        setSessionVulkanDevice(vulkan_device); setSessionGraphicsBackend(GraphicsBackend::Vulkan);
    }
    try {
        QTemporaryDir temp;
        require(temp.isValid(), "Temporary fixture unavailable.");
        require(logging::Logger::instance().initialize(QFileInfo(temp.path()).filesystemFilePath()),
            "GPU test log unavailable.");
        auto surface = OpenGlFrameCompositor::createSurface();
        if (!surface) { std::cout << "SKIP: native OpenGL unavailable.\n"; return 77; }
        playback::CompositionLayerSpec image;
        image.kind = timeline::ClipKind::Image; image.segment_frame_count = 60;
        image.track_index = 0; image.clip_index = 0;
        QImage source(16, 16, QImage::Format_RGBA8888); source.fill(QColor(190, 70, 30, 180));
        image.still_frame = raster(source);
        image.keyframes.position_x = {{0, .3}, {59, .7}};
        auto incoming = image; incoming.clip_index = 1; incoming.timeline_start_frame = 50;
        source.fill(QColor(20, 150, 80, 210)); incoming.still_frame = raster(source);
        playback::CompositionLayerSpec text;
        text.kind = timeline::ClipKind::Text; text.segment_frame_count = 110;
        text.track_index = 1; text.clip_index = 0;
        text.text.content = "Native timeline GPU"; text.text.font_size_pixels = 32;
        text.transform.scale = .6; text.transform.opacity = .7;
        const auto published_path = temp.filePath("published.png");
#ifdef CREATIVE_SUITE_TEST_IMAGE_EDITOR_MASKS
        const auto imported_path = temp.filePath("imported.png");
        source.fill(QColor(150, 90, 230, 255)); require(source.save(imported_path), "Fixture save failed.");
        image_editor::ImageDocumentSession producer;
        QString error;
        require(producer.createCanvas(QSize(16, 16), Qt::transparent, &error) &&
            producer.importRasterImages(image_editor::prepareRasterImport({imported_path}).images, {}, &error),
            "Image producer setup failed: " + error.toStdString());
        const auto rasterLayerId = producer.selectedLayerId();
        require(producer.addLayerMask(rasterLayerId), "Image producer mask setup failed.");
        auto curvesId = producer.addCurvesAdjustment();
        auto curves = creative_suite::effects::CurvesParameters{};
        curves.channels[0] = {{0, 0}, {96, 220}, {255, 255}};
        curves.channels[1] = {{0, 0}, {160, 60}, {255, 255}};
        require(!curvesId.isEmpty() && producer.setCurves(curvesId, curves) &&
            producer.selectLayer(rasterLayerId) &&
            producer.applyLayerMaskEraseStroke({QPointF(8, 8)}, 4, &error) &&
            producer.exportImage(published_path, &error), "Masked PNG producer failed: " + error.toStdString());
        const auto published = raster(QImage(published_path));
        require(producer.applyLayerMaskEraseStroke({QPointF(3, 3)}, 4, &error) &&
            producer.exportImage(published_path, &error), "PNG producer refresh failed.");
        const auto refreshed = raster(QImage(published_path));
#else
        const auto published = image.still_frame;
        const auto refreshed = incoming.still_frame;
#endif
        std::exception_ptr failure;
        bool unsupported_context = false;
        std::unique_ptr<QThread> thread(QThread::create([&] {
            try {
                auto probe = createGpuFrameCompositor(surface.get());
                const auto initial = probe->compose(16, 16, {});
                if (initial.status == OpenGlCompositionStatus::Failed &&
                    (initial.operation == "create-context" || initial.operation == "check-context")) {
                    unsupported_context = true; return;
                }
                require(initial.frame.has_value(), "GPU initialization failed: " + initial.cause);
                auto& metrics = rendering::PreviewPerformanceMetrics::instance();
                metrics.setEnabled(true); metrics.reset();
                playback::PlaybackWorker cpu, gpu;
                playback::VideoFramePtr cpu_frame, gpu_frame;
                int failures = 0, warnings = 0;
                QObject::connect(&cpu, &playback::PlaybackWorker::frameReady,
                    [&](playback::VideoFramePtr f, qint64, quint64, quint64) { cpu_frame = std::move(f); });
                QObject::connect(&gpu, &playback::PlaybackWorker::frameReady,
                    [&](playback::VideoFramePtr f, qint64, quint64, quint64) { gpu_frame = std::move(f); });
                QObject::connect(&gpu, &playback::PlaybackWorker::playbackError,
                    [&](QString, qint64, quint64) { ++failures; });
                QObject::connect(&gpu, &playback::PlaybackWorker::compositionWarning,
                    [&](QString, qint64, quint64) { ++warnings; });
                gpu.setGpuCompositionEnabled(true, surface.get());
                cpu.setActiveCompositionClip(0, 0); gpu.setActiveCompositionClip(0, 0);
                for (const auto quality : {playback::PreviewQuality::Full, playback::PreviewQuality::Half,
                     playback::PreviewQuality::Quarter}) {
                    cpu.setPreviewQuality(quality); gpu.setPreviewQuality(quality);
                    for (const auto kind : {timeline::TransitionKind::CrossDissolve, timeline::TransitionKind::FadeToBlack}) {
                        const QVector<playback::CompositionTransitionSpec> transitions{{0, 0, 1, 60, 10, kind}};
                        cpu.setComposition({image, incoming, text}, transitions, 10);
                        gpu.setComposition({image, incoming, text}, transitions, 10);
                        for (qint64 frame : {0, 49, 50, 55, 59, 60, 65, 75}) {
                            cpu.renderCompositionFrame(frame, frame, 10);
                            gpu.renderCompositionFrame(frame, frame, 10);
                            compare(cpu_frame, gpu_frame);
                        }
                    }
                }
                image.still_frame = published;
                gpu.setComposition({image}, {}, 11); gpu.renderCompositionFrame(0, 0, 11);
                const auto old = gpu_frame;
                image.still_frame = refreshed;
                gpu.setComposition({image}, {}, 12); gpu.renderCompositionFrame(0, 0, 12);
                require(old->rgba_pixels != gpu_frame->rgba_pixels, "GPU kept stale published PNG pixels.");
                cpu.setComposition({image}, {}, 12); cpu.renderCompositionFrame(0, 0, 12);
                compare(cpu_frame, gpu_frame);
                const auto snapshot = metrics.takeSnapshotAndReset();
                require(failures == 0 && warnings == 0 && snapshot.gpu_composition_frames == 50 &&
                    snapshot.gpu_composition_fallbacks == 0 && snapshot.gpu_composition_readback_bytes > 0,
                    "Native timeline coverage silently used CPU fallback.");
                // Change the backend from a queued worker operation during real playback.
                QEventLoop loop;
                bool toggled_off = false, toggled_on = false;
                QObject::connect(&gpu, &playback::PlaybackWorker::frameReady,
                    [&](playback::VideoFramePtr, qint64 frame, quint64, quint64) {
                        if (!toggled_off && frame >= 1) {
                            toggled_off = true;
                            QMetaObject::invokeMethod(&gpu, [&] { gpu.setGpuCompositionEnabled(false, surface.get()); }, Qt::QueuedConnection);
                        } else if (toggled_off && !toggled_on && frame >= 3) {
                            toggled_on = true;
                            QMetaObject::invokeMethod(&gpu, [&] { gpu.setGpuCompositionEnabled(true, surface.get()); }, Qt::QueuedConnection);
                        } else if (toggled_on && frame >= 5) loop.quit();
                    });
                QTimer::singleShot(2000, &loop, &QEventLoop::quit);
                gpu.play(); loop.exec(); gpu.pause();
                require(toggled_off && toggled_on && failures == 0 && warnings == 0,
                    "Native toggle interrupted playback.");
                directWorker(surface.get(), image, incoming, text, published, refreshed);
                auto graph_gpu = createGpuFrameCompositor(surface.get(), OpenGlPrecisionPolicy::Automatic, nullptr, std::make_shared<OpenGlTexturePoolBudget>(256ULL*1024*1024,8));
                fusionParity(*graph_gpu,surface.get());
                metrics.setEnabled(false);
            } catch (...) { failure = std::current_exception(); }
        }));
        thread->start(); thread->wait();
        if (failure) std::rethrow_exception(failure);
        if (unsupported_context) return 77;
        surface.reset();
        std::cout << "Native GPU timeline, transitions, quality, playback and PNG refresh passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
