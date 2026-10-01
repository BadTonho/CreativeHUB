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

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

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

}  // namespace

int main(int argc, char* argv[]) {
    qunsetenv("CREATIVE_SUITE_DISABLE_GPU_PREVIEW");
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

        metrics.setEnabled(false);
        widget.close();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
