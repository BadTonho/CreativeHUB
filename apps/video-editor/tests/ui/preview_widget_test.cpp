#include "ui/preview/preview_widget.h"

#include "rendering/preview_performance_metrics.h"

#include <QApplication>
#include <QColor>
#include <QLabel>
#include <QMouseEvent>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <cstdlib>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

media::VideoFrame makeFrame() {
    media::VideoFrame frame;
    frame.width = 4;
    frame.height = 2;
    frame.stride = frame.width * 4;
    frame.rgba_pixels.resize(static_cast<std::size_t>(frame.stride * frame.height));
    for (std::size_t index = 0; index < frame.rgba_pixels.size(); index += 4) {
        frame.rgba_pixels[index + 0] = 220;
        frame.rgba_pixels[index + 1] = 120;
        frame.rgba_pixels[index + 2] = 40;
        frame.rgba_pixels[index + 3] = 255;
    }
    return frame;
}

} // namespace

int main(int argc, char* argv[]) {
    qputenv("CREATIVE_SUITE_DISABLE_GPU_PREVIEW", "1");
    QApplication application(argc, argv);

    try {
        PreviewWidget widget;
        widget.resize(640, 360);
        widget.show();

        const auto frame = makeFrame();
        auto shared_frame = std::make_shared<const media::VideoFrame>(frame);
        std::weak_ptr<const media::VideoFrame> weak_frame = shared_frame;
        auto& metrics = rendering::PreviewPerformanceMetrics::instance();
        metrics.setEnabled(true);
        metrics.reset();
        const auto delivery_trace_id = metrics.createFrameDeliveryTrace(7, 23);
        widget.setFrame(shared_frame, delivery_trace_id);
        shared_frame.reset();
        require(!weak_frame.expired(),
                "Preview did not retain the submitted frame while displayed.");
        application.processEvents();
        const auto preview_snapshot = metrics.takeSnapshotAndReset();
        require(preview_snapshot.submitted_frames == 1,
                "Preview metrics did not record the submitted frame.");
        require(preview_snapshot.cpu_presented_frames == 1,
                "Preview metrics did not record the CPU-presented frame.");
        require(preview_snapshot.preview_submit.count == 1,
                "Preview metrics did not time frame submission.");
        require(delivery_trace_id != 0 &&
                    preview_snapshot.frame_delivery.stage_counts[static_cast<std::size_t>(
                        rendering::PreviewFrameDeliveryStage::PreviewSubmitted)] == 1 &&
                    preview_snapshot.frame_delivery.stage_counts[static_cast<std::size_t>(
                        rendering::PreviewFrameDeliveryStage::CpuPainted)] == 1 &&
                    preview_snapshot.frame_delivery.sample_count == 1 &&
                    preview_snapshot.frame_delivery.samples[0].trace_id == delivery_trace_id &&
                    preview_snapshot.frame_delivery.samples[0].last_stage ==
                        rendering::PreviewFrameDeliveryStage::CpuPainted,
                "The CPU Preview did not complete the worker frame delivery trace.");
        const auto cleared_trace_id = metrics.createFrameDeliveryTrace(7, 24);
        widget.setFrame(
            std::make_shared<const media::VideoFrame>(makeFrame()),
            cleared_trace_id);
        widget.clearFrame("Discard pending paint.");
        const auto cleared_snapshot = metrics.takeSnapshotAndReset();
        require(cleared_snapshot.frame_delivery.drop_counts[static_cast<std::size_t>(
                    rendering::PreviewFrameDeliveryDropReason::PreviewOverwritten)] == 1 &&
                    cleared_snapshot.frame_delivery.samples[0].trace_id == cleared_trace_id &&
                    cleared_snapshot.frame_delivery.samples[0].drop_reason ==
                        rendering::PreviewFrameDeliveryDropReason::PreviewOverwritten,
                "Clearing the CPU Preview did not mark its pending frame as overwritten.");
        metrics.setEnabled(false);
        widget.clearFrame("Frame released.");
        require(weak_frame.expired(),
                "Preview retained a frame after it was cleared.");
        widget.setFrame(frame);
        widget.setGrayscaleEnabled(true);
        require(widget.isGrayscaleEnabled(), "Grayscale state was not enabled.");
        widget.resize(320, 240);
        application.processEvents();

        widget.setGrayscaleEnabled(false);
        widget.resize(400, 200);
        widget.setFrame(makeFrame());
        application.processEvents();
        const auto baseline = widget.grab().toImage();
        QImage reference(8, 4, QImage::Format_RGBA8888);
        for (int y = 0; y < reference.height(); ++y) {
            auto* pixels = reference.scanLine(y);
            const QColor color = y < reference.height() / 2
                ? QColor(15, 35, 220) : QColor(230, 190, 20);
            for (int x = 0; x < reference.width(); ++x) {
                pixels[x * 4] = static_cast<uchar>(color.red());
                pixels[x * 4 + 1] = static_cast<uchar>(color.green());
                pixels[x * 4 + 2] = static_cast<uchar>(color.blue());
                pixels[x * 4 + 3] = 255;
            }
        }
        widget.setComparisonReferenceImage(reference);
        widget.setComparison(creative_suite::composition::FrameComparisonMode::Off, 0.5F);
        application.processEvents();
        require(widget.grab().toImage() == baseline,
                "Off comparison changed the existing CPU Viewer output.");

        widget.setComparison(creative_suite::composition::FrameComparisonMode::SplitScreen,
            0.5F);
        application.processEvents();
        auto compared = widget.grab().toImage();
        require(compared.pixelColor(100, 75).blue() > 200 &&
                    compared.pixelColor(100, 125).red() > 200 &&
                    compared.pixelColor(300, 75).red() > 200 &&
                    compared.pixelColor(100, 20) != QColor(15, 35, 220),
                "CPU split-screen comparison lost left/right order or source aspect ratio.");

        widget.setComparison(creative_suite::composition::FrameComparisonMode::Wipe, 0.0F);
        application.processEvents();
        compared = widget.grab().toImage();
        require(compared.pixelColor(80, 50).red() > 200 &&
                    compared.pixelColor(80, 150).red() > 200 &&
                    compared.pixelColor(320, 50).red() > 200,
                "CPU wipe at zero percent must show only the current Viewer.");
        widget.setComparison(creative_suite::composition::FrameComparisonMode::Wipe, 0.5F);
        application.processEvents();
        compared = widget.grab().toImage();
        require(compared.pixelColor(80, 50).blue() > 200 &&
                    compared.pixelColor(80, 150).red() > 200 &&
                    compared.pixelColor(320, 100).red() > 200,
                "CPU wipe at fifty percent did not split reference and current frame.");
        widget.setComparison(creative_suite::composition::FrameComparisonMode::Wipe, 1.0F);
        application.processEvents();
        compared = widget.grab().toImage();
        require(compared.pixelColor(80, 50).blue() > 200 &&
                    compared.pixelColor(320, 150).red() > 200,
                "CPU wipe at one hundred percent must show only the reference.");

        widget.setComparison(creative_suite::composition::FrameComparisonMode::Wipe, 0.5F);
        float dragged_fraction = -1.0F;
        QObject::connect(&widget, &PreviewWidget::wipePositionDragged, &widget,
            [&](float fraction) { dragged_fraction = fraction; });
        auto* cpu_surface = widget.findChild<QLabel*>("previewCpuSurface");
        require(cpu_surface != nullptr, "CPU comparison surface is missing.");
        const auto global_press = QPointF(cpu_surface->mapToGlobal(QPoint(200, 100)));
        const auto global_move = QPointF(cpu_surface->mapToGlobal(QPoint(300, 100)));
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(200, 100), global_press,
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
            QPointingDevice::primaryPointingDevice());
        QMouseEvent move(QEvent::MouseMove, QPointF(300, 100), global_move,
            Qt::NoButton, Qt::LeftButton, Qt::NoModifier,
            QPointingDevice::primaryPointingDevice());
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(300, 100), global_move,
            Qt::LeftButton, Qt::NoButton, Qt::NoModifier,
            QPointingDevice::primaryPointingDevice());
        QApplication::sendEvent(cpu_surface, &press);
        QApplication::sendEvent(cpu_surface, &move);
        QApplication::sendEvent(cpu_surface, &release);
        require(dragged_fraction > 0.74F && dragged_fraction < 0.76F,
                "Dragging the CPU wipe divider did not report its normalized position.");
        widget.setComparison(creative_suite::composition::FrameComparisonMode::Off, 0.5F);
        widget.setComparisonReferenceImage({});

        media::VideoFrame invalid_frame;
        widget.setFrame(invalid_frame);
        widget.clearFrame("Preview cleared.");
        widget.setGrayscaleEnabled(false);
        require(!widget.isGrayscaleEnabled(), "Grayscale state was not disabled.");
        widget.close();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
