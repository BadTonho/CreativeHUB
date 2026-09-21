#include "preview_widget.h"

#include "rendering/preview_performance_metrics.h"

#include <QApplication>

#include <cstdint>
#include <cstdio>
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
        auto& metrics = rendering::PreviewPerformanceMetrics::instance();
        metrics.setEnabled(true);
        metrics.reset();
        widget.setFrame(frame);
        const auto preview_snapshot = metrics.takeSnapshotAndReset();
        require(preview_snapshot.submitted_frames == 1,
                "Preview metrics did not record the submitted frame.");
        require(preview_snapshot.preview_submit.count == 1,
                "Preview metrics did not time frame submission.");
        metrics.setEnabled(false);
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
