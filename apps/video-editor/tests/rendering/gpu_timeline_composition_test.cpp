#include "playback/playback_worker.h"
#include "rendering/preview_performance_metrics.h"
#include "logging/logger.h"
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

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>

using namespace creative_suite::composition;
namespace {
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
} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
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
            producer.importRasterImages(image_editor::prepareRasterImport({imported_path}).images, {}, &error) &&
            producer.addLayerMask(producer.selectedLayerId()) &&
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
                OpenGlFrameCompositor probe(surface.get());
                const auto initial = probe.compose(16, 16, {});
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
