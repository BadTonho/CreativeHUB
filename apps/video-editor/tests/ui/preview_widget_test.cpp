#include "ui/preview/preview_widget.h"

#include "rendering/preview_performance_metrics.h"

#include <QApplication>

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
