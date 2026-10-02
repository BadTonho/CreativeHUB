#include "rendering/opengl_preview_surface.h"
#include "rendering/preview_performance_metrics.h"
#include "ui/preview/preview_widget.h"

#include <QApplication>
#include <QColor>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QOpenGLContext>
#include <QThread>
#include <QOffscreenSurface>
#include <QSurfaceFormat>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <string>

namespace {

constexpr int kSkipReturnCode = 77;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<typename Predicate>
bool waitFor(Predicate&& predicate, int timeout_ms = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout_ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return predicate();
}

media::VideoFramePtr makeFrame(
    int width,
    int height,
    int row_padding,
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue) {
    auto frame = std::make_shared<media::VideoFrame>();
    frame->width = width;
    frame->height = height;
    frame->stride = width * 4 + row_padding;
    frame->rgba_pixels.assign(
        static_cast<std::size_t>(frame->stride) * static_cast<std::size_t>(height),
        0xCD);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto offset = static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(frame->stride) +
                static_cast<std::size_t>(x) * 4U;
            frame->rgba_pixels[offset] = red;
            frame->rgba_pixels[offset + 1] = green;
            frame->rgba_pixels[offset + 2] = blue;
            frame->rgba_pixels[offset + 3] = 255;
        }
    }
    return frame;
}

bool closeTo(int actual, int expected, int tolerance = 8) {
    return std::abs(actual - expected) <= tolerance;
}

bool colorIs(const QColor& actual, int red, int green, int blue, int tolerance = 8) {
    return closeTo(actual.red(), red, tolerance) &&
        closeTo(actual.green(), green, tolerance) &&
        closeTo(actual.blue(), blue, tolerance);
}

bool waitForSurfaceColor(
    rendering::OpenGLPreviewSurface* surface,
    int red,
    int green,
    int blue,
    QImage& image) {
    return waitFor([&] {
        image = surface->grabFramebuffer();
        return !image.isNull() &&
            colorIs(image.pixelColor(image.width() / 2, image.height() / 2),
                    red, green, blue);
    });
}

// A GUI-created surface with all compositor resources owned by a real worker.
class NativeProducer {
public:
    NativeProducer() : surface_(creative_suite::composition::OpenGlFrameCompositor::createSurface()) {
        executor_ = new QObject;
        executor_->moveToThread(&thread_);
        QObject::connect(&thread_, &QThread::finished, executor_, &QObject::deleteLater);
        thread_.start();
        QMetaObject::invokeMethod(executor_, [this] {
            compositor_ = std::make_unique<creative_suite::composition::OpenGlFrameCompositor>(surface_.get(),
                creative_suite::composition::OpenGlPrecisionPolicy::Automatic, QOpenGLContext::globalShareContext());
        }, Qt::BlockingQueuedConnection);
    }
    ~NativeProducer() {
        QMetaObject::invokeMethod(executor_, [this] { compositor_.reset(); }, Qt::BlockingQueuedConnection);
        thread_.quit(); thread_.wait();
    }
    rendering::PreviewFramePayload compose(media::VideoFramePtr source, int width = 16, int height = 16) {
        return render({{source.get()}}, width, height, 2);
    }
    rendering::PreviewFramePayload render(const std::vector<creative_suite::composition::CompositionLayer>& layers,
        int width, int height, int mode) {
        rendering::PreviewFramePayload payload;
        std::string failure;
        QMetaObject::invokeMethod(executor_, [&] {
            creative_suite::composition::OpenGlCompositionTimings timing;
            if (mode == 2) {
                auto output = compositor_->composeTexture(width, height, layers, {}, &timing);
                failure = output.cause; payload.gpu = std::move(output.frame);
            } else {
                std::optional<media::VideoFrame> rgba;
                if (mode == 1) {
                    auto output = compositor_->compose(width, height, layers, {}, &timing);
                    failure = output.cause; rgba = std::move(output.frame);
                } else rgba = creative_suite::composition::FrameCompositor::compose(width, height, layers);
                if (rgba) payload.rgba = std::make_shared<const media::VideoFrame>(std::move(*rgba));
            }
            rendering::PreviewPerformanceMetrics::instance().recordCompositionBackend(mode != 0, timing);
            rendering::PreviewPerformanceMetrics::instance().setTexturePoolState(
                compositor_->texturePoolBytes(), compositor_->texturePoolOccupancy());
        }, Qt::BlockingQueuedConnection);
        if (!payload.valid()) throw std::runtime_error("Native producer failed: " + failure);
        return payload;
    }
    media::VideoFramePtr readback(const rendering::PreviewFramePayload& payload) {
        media::VideoFramePtr rgba;
        QMetaObject::invokeMethod(executor_, [&] {
            auto result = compositor_->readback(payload.gpu);
            if (result.frame) rgba = std::make_shared<const media::VideoFrame>(std::move(*result.frame));
        }, Qt::BlockingQueuedConnection);
        return rgba;
    }
    void restart() {
        QMetaObject::invokeMethod(executor_, [this] {
            compositor_.reset();
            compositor_ = std::make_unique<creative_suite::composition::OpenGlFrameCompositor>(surface_.get(),
                creative_suite::composition::OpenGlPrecisionPolicy::Automatic, QOpenGLContext::globalShareContext());
        }, Qt::BlockingQueuedConnection);
    }
private:
    std::unique_ptr<QOffscreenSurface> surface_;
    QThread thread_;
    QObject* executor_ = nullptr;
    std::unique_ptr<creative_suite::composition::OpenGlFrameCompositor> compositor_;
};

void benchmarkDelivery() {
    NativeProducer producer;
    PreviewWidget widget;
    widget.resize(960, 540); widget.show();
    auto* surface = widget.findChild<rendering::OpenGLPreviewSurface*>();
    require(waitFor([&] { return widget.textureDeliveryAvailable(); }), "Benchmark viewer sharing unavailable.");
    auto background = makeFrame(1920, 1080, 0, 80, 120, 160);
    auto overlay = std::make_shared<media::VideoFrame>(*makeFrame(1280, 720, 12, 210, 70, 130));
    for (int y = 0; y < overlay->height; ++y) for (int x = 0; x < overlay->width; ++x)
        overlay->rgba_pixels[y * overlay->stride + x * 4 + 3] = static_cast<std::uint8_t>((x * 29 + y * 31) % 256);
    creative_suite::animation::Transform2D scaled; scaled.scale = .72; scaled.opacity = .6;
    auto rotated = scaled; rotated.rotation_degrees = 23;
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    for (int workload = 0; workload < 2; ++workload) for (auto size : {std::pair{1920, 1080}, {960, 540}, {480, 270}}) {
        const std::vector<creative_suite::composition::CompositionLayer> layers = workload == 0
            ? std::vector<creative_suite::composition::CompositionLayer>{{background.get()}, {overlay.get(), scaled}, {overlay.get(), rotated}}
            : std::vector<creative_suite::composition::CompositionLayer>{{background.get()}};
        for (int mode = 0; mode < 3; ++mode) {
            double total_ms = 0;
            constexpr int repeats = 10;
            metrics.reset();
            for (int sample = -2; sample < repeats; ++sample) {
                const auto start = std::chrono::steady_clock::now();
                auto payload = producer.render(layers, size.first, size.second, mode);
                const auto trace = metrics.createFrameDeliveryTrace(3, sample + 2);
                widget.setFrame(std::move(payload), trace);
                require(waitFor([&] { return surface->lastPresentedDeliveryTraceId() == trace; }),
                    "Benchmark frame did not reach Qt presentation.");
                if (sample >= 0) total_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                else metrics.reset();
            }
            const auto s = metrics.takeSnapshotAndReset();
            std::printf("delivery_benchmark workload=%s output=%dx%d backend=%s to_qt_swap_ms=%.3f source_upload_bytes=%llu readback_bytes=%llu viewer_uploads=%llu viewer_upload_bytes=%llu pool_bytes=%llu pool_peak_bytes=%llu wait_submit_ms=%.4f consumer_submit_ms=%.4f producer_submit_ms=%.4f\n",
                workload == 0 ? "three_layers" : "opaque_identity", size.first, size.second,
                mode == 0 ? "cpu_rgba" : mode == 1 ? "gpu_rgba" : "gpu_texture", total_ms / repeats,
                static_cast<unsigned long long>(s.gpu_composition_uploaded_bytes / repeats),
                static_cast<unsigned long long>(s.gpu_composition_readback_bytes / repeats),
                static_cast<unsigned long long>(s.gpu_upload.count), static_cast<unsigned long long>(s.viewer_uploaded_bytes / repeats),
                static_cast<unsigned long long>(s.texture_pool_bytes),
                static_cast<unsigned long long>(s.texture_pool_peak_bytes), s.gpu_fence_wait_submission.averageMilliseconds(),
                s.gpu_consumer_fence_submission.averageMilliseconds(), s.gpu_producer_fence_submission.averageMilliseconds());
            require(mode != 2 || (s.gpu_upload.count == 0 && s.gpu_composition_readback_bytes == 0),
                "Benchmark direct path silently performed final-frame transfers.");
        }
    }
    widget.releaseGpuFrames(); widget.close();
}

void directDelivery() {
    NativeProducer producer;
    PreviewWidget widget;
    widget.resize(400, 320); widget.show();
    auto* surface = widget.findChild<rendering::OpenGLPreviewSurface*>();
    require(waitFor([&] { return widget.textureDeliveryAvailable(); }), "Native viewer did not share the global context.");
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.reset();
    auto source = std::make_shared<media::VideoFrame>(*makeFrame(4, 4, 3, 220, 30, 20));
    for (int y = 2; y < 4; ++y) for (int x = 0; x < 4; ++x) {
        auto* p = source->rgba_pixels.data() + y * source->stride + x * 4;
        p[0] = 20; p[1] = 50; p[2] = 210;
    }
    auto payload = producer.compose(source);
    widget.setFrame(payload, metrics.createFrameDeliveryTrace(2, 20));
    QImage image;
    require(waitFor([&] {
        image = surface->grabFramebuffer();
        return !image.isNull() && colorIs(image.pixelColor(image.width() / 2, image.height() / 4), 220, 30, 20) &&
            colorIs(image.pixelColor(image.width() / 2, image.height() * 3 / 4), 20, 50, 210);
    }), "Shared texture orientation or pixels are incorrect.");
    // Reuse an existing owned RGBA texture after a different GPU aspect ratio.
    widget.setFrame(source);
    require(waitFor([&] {
        image = surface->grabFramebuffer();
        return colorIs(image.pixelColor(image.width() / 2, 40), 220, 30, 20);
    }), "RGBA warmup did not fill its square viewing area.");
    auto wide = producer.compose(source, 32, 16);
    widget.setFrame(wide);
    require(waitFor([&] { image = surface->grabFramebuffer(); return image.pixelColor(image.width() / 2, 40).red() < 80; }),
        "Wide direct output did not preserve its bars.");
    widget.setFrame(source);
    require(waitFor([&] { image = surface->grabFramebuffer(); return colorIs(image.pixelColor(image.width() / 2, 40), 220, 30, 20); }),
        "Returning to an owned texture kept the previous direct frame's geometry.");
    wide = {};
    metrics.reset();
    widget.setFrame(payload, metrics.createFrameDeliveryTrace(2, 21));
    widget.resize(640, 360);
    require(waitFor([&] { image = surface->grabFramebuffer(); return image.size() == surface->size(); }),
        "Direct frame did not redraw after resize.");
    require(colorIs(image.pixelColor(image.width() / 2, image.height() / 4), 220, 30, 20) &&
        image.pixelColor(4, image.height() / 2).red() < 80, "Direct resize changed orientation or letterboxing.");
    widget.setGrayscaleEnabled(true);
    require(waitFor([&] {
        image = surface->grabFramebuffer(); auto p = image.pixelColor(image.width() / 2, image.height() / 4);
        return closeTo(p.red(), p.green(), 2) && closeTo(p.red(), 86, 2);
    }), "Direct grayscale differs from the RGBA viewer.");
    auto snapshot = metrics.takeSnapshotAndReset();
    require(snapshot.texture_delivery_frames == 1 && snapshot.gpu_upload.count == 0 &&
        snapshot.gpu_composition_readback_bytes == 0 && snapshot.gpu_fence_wait_submission.count > 0 &&
        snapshot.gpu_consumer_fence_submission.count > 0 && snapshot.frame_delivery.stage_counts[
            static_cast<std::size_t>(rendering::PreviewFrameDeliveryStage::GpuTextureAccepted)] == 1 &&
        snapshot.frame_delivery.stage_counts[static_cast<std::size_t>(rendering::PreviewFrameDeliveryStage::GpuUploaded)] == 0,
        "Direct delivery reported a final-frame upload/readback or omitted fences.");
    widget.setDeliveryEpoch(3);
    widget.setFrame(payload, metrics.createFrameDeliveryTrace(2, 19));
    require(widget.currentPayload().gpu == payload.gpu, "A stale delivery replaced the current texture.");
    widget.setGrayscaleEnabled(false);
    bool recovery_requested = false;
    QObject::connect(&widget, &PreviewWidget::gpuFallbackRequested, &widget,
        [&](QString, qint64) { recovery_requested = true; });
    surface->gpuFailure("Controlled direct presentation failure", -24);
    require(recovery_requested && widget.currentPayload().gpu, "Presentation failure lost the retained texture.");
    auto rgba = producer.readback(payload);
    require(bool(rgba), "Retained texture could not be recovered.");
    widget.setFrame(rgba);
    require(!widget.currentPayload().gpu && !widget.usesGpuPreview(), "RGBA recovery did not release the texture.");
    image = widget.grab().toImage();
    require(colorIs(image.pixelColor(image.width() / 2, image.height() / 4), 220, 30, 20),
        "Recovered CPU presentation differs from the direct frame.");
    widget.releaseGpuFrames(); widget.close();
}

void directFenceRecovery() {
    NativeProducer producer;
    PreviewWidget widget;
    widget.resize(400, 320); widget.show();
    auto* surface = widget.findChild<rendering::OpenGLPreviewSurface*>();
    require(waitFor([&] { return widget.textureDeliveryAvailable(); }), "Fence recovery viewer sharing unavailable.");
    const auto source = makeFrame(4, 4, 0, 30, 180, 70);
    auto payload = producer.compose(source);
    widget.setFrame(payload);
    QImage image;
    require(waitForSurfaceColor(surface, 30, 180, 70, image), "Fence recovery initial frame unavailable.");
    bool requested = false;
    QObject::connect(&widget, &PreviewWidget::gpuFallbackRequested, &widget, [&](QString, qint64) { requested = true; });
    std::string cause; std::int64_t code = 0;
    require(!payload.gpu->endUse(nullptr, cause, code), "Lost consumer context was not rejected.");
    widget.setFrame(payload);
    require(waitFor([&] { return requested; }) && widget.usesGpuPreview(),
        "Texture synchronization failure unnecessarily disabled the RGBA GPU viewer.");
    auto rgba = producer.readback(payload);
    require(bool(rgba), "A failed consumer prevented owning-worker recovery.");
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    const auto rgba_trace = metrics.createFrameDeliveryTrace(4, 1);
    widget.setFrame(rgba, rgba_trace);
    require(waitFor([&] { return surface->lastPresentedDeliveryTraceId() == rgba_trace; }) &&
        waitForSurfaceColor(surface, 30, 180, 70, image) && !widget.currentPayload().gpu,
        "Texture failure did not recover through RGBA presentation.");
    producer.restart();
    widget.setDeliveryEpoch(2, true);
    payload = producer.compose(source);
    payload.delivery_epoch = 2;
    const auto retry_trace = metrics.createFrameDeliveryTrace(4, 2);
    widget.setFrame(payload, retry_trace);
    require(waitFor([&] { return surface->lastPresentedDeliveryTraceId() == retry_trace; }) &&
        waitForSurfaceColor(surface, 30, 180, 70, image) && widget.currentPayload().gpu && widget.usesGpuPreview(),
        "Preference-cycle retry did not restore direct texture presentation.");
    widget.releaseGpuFrames(); widget.close();
}

}  // namespace

int main(int argc, char* argv[]) {
    qunsetenv("CREATIVE_SUITE_DISABLE_GPU_PREVIEW");
    QSurfaceFormat requested_format; requested_format.setVersion(3, 2);
    requested_format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(requested_format);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication application(argc, argv);

    try {
        PreviewWidget widget;
        widget.resize(480, 360);
        bool fallback_requested = false;
        QString fallback_message;
        qint64 fallback_error_code = 0;
        QObject::connect(
            &widget,
            &PreviewWidget::gpuFallbackRequested,
            &widget,
            [&](const QString& message, qint64 error_code) {
                fallback_requested = true;
                fallback_message = message;
                fallback_error_code = error_code;
            });

        auto* surface = widget.findChild<rendering::OpenGLPreviewSurface*>();
        require(surface != nullptr, "GPU Preview surface was not constructed.");
        widget.show();

        const bool context_reported = waitFor([&] {
            return surface->context() != nullptr || fallback_requested;
        });
        if (!context_reported || surface->context() == nullptr ||
            !surface->context()->isValid()) {
            std::fprintf(stderr,
                "OpenGL preview skipped: this platform did not create a valid OpenGL context.\n");
            return kSkipReturnCode;
        }
        require(surface->isValid(), "Qt created a context but the OpenGL widget is invalid.");
        require(widget.usesGpuPreview() && !fallback_requested,
                "A valid OpenGL context unexpectedly triggered CPU fallback.");
        const auto format = surface->context()->format();
        require(format.majorVersion() > 3 ||
                    (format.majorVersion() == 3 && format.minorVersion() >= 2),
                "The OpenGL context is older than the required 3.2 core profile.");

        auto& metrics = rendering::PreviewPerformanceMetrics::instance();
        metrics.setEnabled(true);
        metrics.reset();

        const auto first_trace = metrics.createFrameDeliveryTrace(1, 10);
        widget.setFrame(makeFrame(4, 2, 4, 220, 120, 40), first_trace);
        QImage image;
        require(waitForSurfaceColor(surface, 220, 120, 40, image),
                "OpenGL did not present the initial padded-stride frame.");
        require(!image.isNull() && image.size() == surface->size(),
                "OpenGL framebuffer capture returned an invalid image.");
        require(image.pixelColor(image.width() / 2, 4).red() < 80,
                "OpenGL did not preserve letterboxing for a mismatched aspect ratio.");

        widget.setGrayscaleEnabled(true);
        require(waitFor([&] {
            image = surface->grabFramebuffer();
            if (image.isNull()) return false;
            const auto gray_pixel = image.pixelColor(image.width() / 2, image.height() / 2);
            return closeTo(gray_pixel.red(), gray_pixel.green(), 2) &&
                closeTo(gray_pixel.green(), gray_pixel.blue(), 2) &&
                closeTo(gray_pixel.red(), 141, 8);
        }), "OpenGL did not render the expected grayscale output.");
        const QColor gray = image.pixelColor(image.width() / 2, image.height() / 2);
        require(closeTo(gray.red(), gray.green(), 2) &&
                    closeTo(gray.green(), gray.blue(), 2) &&
                    closeTo(gray.red(), 141, 8),
                "OpenGL grayscale output differs from the expected luminance.");

        widget.setGrayscaleEnabled(false);
        widget.resize(320, 480);
        widget.setFrame(makeFrame(2, 4, 0, 20, 80, 210),
                        metrics.createFrameDeliveryTrace(1, 11));
        require(waitForSurfaceColor(surface, 20, 80, 210, image),
                "OpenGL did not redraw after frame dimensions changed.");
        require(image.size() == surface->size() &&
                    colorIs(image.pixelColor(image.width() / 2, image.height() / 2),
                            20, 80, 210),
                "OpenGL texture resize did not display the replacement frame.");

        widget.clearFrame(QStringLiteral("Preview cleared by regression test."));
        require(waitFor([&] {
            image = surface->grabFramebuffer();
            return !image.isNull() &&
                image.pixelColor(image.width() / 2, image.height() / 2).red() < 80;
        }), "OpenGL did not clear the previous GPU frame.");
        require(image.pixelColor(image.width() / 2, image.height() / 2).red() < 80,
                "Clearing Preview left the previous GPU frame visible.");

        const auto fallback_trace = metrics.createFrameDeliveryTrace(1, 12);
        widget.setFrame(makeFrame(2, 4, 0, 30, 190, 70), fallback_trace);
        require(waitForSurfaceColor(surface, 30, 190, 70, image),
                "OpenGL did not present the frame used to verify CPU fallback.");
        surface->gpuFailure(QStringLiteral("Controlled test failure."), -23);
        require(fallback_requested && !widget.usesGpuPreview() &&
                    fallback_message == QStringLiteral("Controlled test failure.") &&
                    fallback_error_code == -23,
                "A reported GPU failure did not switch Preview to its CPU fallback.");
        image = widget.grab().toImage();
        require(colorIs(image.pixelColor(image.width() / 2, image.height() / 2),
                        30, 190, 70),
                "CPU fallback did not retain and display the current frame.");

        const auto invalid_trace = metrics.createFrameDeliveryTrace(1, 13);
        widget.setFrame(std::make_shared<const media::VideoFrame>(), invalid_trace);
        const auto snapshot = metrics.takeSnapshotAndReset();
        require(snapshot.gpu_presented_frames >= 3 &&
                    snapshot.frame_delivery.stage_counts[static_cast<std::size_t>(
                        rendering::PreviewFrameDeliveryStage::GpuUploaded)] >= 3 &&
                    snapshot.frame_delivery.stage_counts[static_cast<std::size_t>(
                        rendering::PreviewFrameDeliveryStage::GpuDrawn)] >= 3 &&
                    snapshot.gpu_presented_frames >= 3,
                "Preview metrics did not record GPU upload and draw stages.");
        require(snapshot.frame_delivery.drop_counts[static_cast<std::size_t>(
                    rendering::PreviewFrameDeliveryDropReason::InvalidFrame)] == 1,
                "Preview metrics did not classify the invalid frame.");

        directDelivery();
        directFenceRecovery();
        if (argc > 1 && std::string(argv[1]) == "--benchmark") benchmarkDelivery();
        metrics.setEnabled(false);
        widget.close();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
