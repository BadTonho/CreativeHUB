#include "ui/preview_renderer.h"

#include <creative_suite/diagnostics/logger.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QThread>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool waitFor(const std::function<bool()>& condition, int timeout_ms = 10000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeout_ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(2);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return condition();
}

creative_suite::media::RgbaFramePtr solidFrame(
    std::uint8_t red, std::uint8_t green, std::uint8_t blue)
{
    auto frame = std::make_shared<creative_suite::media::RgbaFrame>();
    frame->width = 2;
    frame->height = 2;
    frame->stride = 8;
    frame->rgba_pixels.resize(16);
    for (std::size_t pixel = 0; pixel < 4; ++pixel) {
        frame->rgba_pixels[pixel * 4] = red;
        frame->rgba_pixels[pixel * 4 + 1] = green;
        frame->rgba_pixels[pixel * 4 + 2] = blue;
        frame->rgba_pixels[pixel * 4 + 3] = 255;
    }
    return frame;
}

std::filesystem::path filePath(const QString& value)
{
    const auto utf8 = value.toUtf8();
    const auto* first = reinterpret_cast<const char8_t*>(utf8.constData());
    return std::filesystem::path(std::u8string(first, first + utf8.size()));
}

motion::ui::PreviewRequest imageRequest(
    creative_suite::media::RgbaFramePtr frame)
{
    motion::ui::PreviewLayerSnapshot layer;
    layer.kind = motion::model::LayerKind::Image;
    layer.still_frame = std::move(frame);
    return {{4, 4}, {24, 1}, {std::move(layer)}};
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary preview log directory is available");
    auto& logger = creative_suite::diagnostics::Logger::instance();
    require(logger.initialize(filePath(temporary.path())),
            "preview errors can be logged to a temporary application directory");

    QObject result_receiver;
    motion::ui::PreviewRenderer* renderer_ptr = nullptr;
    std::uint64_t applied_generation = 0;
    creative_suite::media::RgbaFramePtr applied_frame;
    std::vector<std::pair<std::uint64_t, creative_suite::media::RgbaFramePtr>> results;
    motion::ui::PreviewRenderer renderer(&result_receiver,
        [&](std::uint64_t generation, creative_suite::media::RgbaFramePtr frame) {
            results.emplace_back(generation, frame);
            if (renderer_ptr != nullptr && generation == renderer_ptr->generation()) {
                applied_generation = generation;
                applied_frame = std::move(frame);
            }
        });
    renderer_ptr = &renderer;

    const auto stale_generation = renderer.submit(imageRequest(solidFrame(240, 20, 20)));
    const auto final_generation = renderer.submit(imageRequest(solidFrame(10, 220, 30)));
    require(final_generation > stale_generation,
            "new preview requests receive a newer generation");
    require(waitFor([&] { return applied_generation == final_generation; }),
            "the newest queued seek produces a preview frame");
    require(applied_frame != nullptr && applied_frame->width == 4 && applied_frame->height == 4,
            "the shared compositor returns the requested output dimensions");
    const auto center_offset = static_cast<std::size_t>(2 * applied_frame->stride + 2 * 4);
    require(applied_frame->rgba_pixels[center_offset] == 10 &&
                applied_frame->rgba_pixels[center_offset + 1] == 220 &&
                applied_frame->rgba_pixels[center_offset + 2] == 30,
            "an older queued seek cannot replace the most recent preview result");

    motion::ui::PreviewLayerSnapshot unavailable_video;
    unavailable_video.kind = motion::model::LayerKind::Video;
    unavailable_video.source_path = filePath(temporary.path()) /
                                    "missing-source.mkv";
    unavailable_video.source_frame_rate = 24.0;
    unavailable_video.source_frame_count = 120;
    unavailable_video.local_frame = 3;
    motion::ui::PreviewRequest unavailable_request{{4, 4}, {24, 1}, {unavailable_video}};
    const auto unavailable_generation = renderer.submit(std::move(unavailable_request));
    require(waitFor([&] { return applied_generation == unavailable_generation; }),
            "unavailable source media completes as a failed preview request");
    require(applied_frame == nullptr && renderer.isRunning(),
            "a decode failure leaves the preview worker running");

    const auto log_path = logger.log_path();
    std::ifstream log(log_path);
    const std::string log_contents((std::istreambuf_iterator<char>(log)),
                                   std::istreambuf_iterator<char>());
    require(log_contents.find("decode_video_frame") != std::string::npos &&
                log_contents.find("missing-source.mkv") != std::string::npos,
            "a video decode failure is written with operation and source path context");

    const auto recovery_generation = renderer.submit(imageRequest(solidFrame(10, 20, 230)));
    require(waitFor([&] { return applied_generation == recovery_generation; }) &&
                applied_frame != nullptr,
            "preview rendering continues after an unavailable source file");
    renderer.stopAndWait();

    std::cout << "Motion Studio preview renderer tests passed.\n";
    return EXIT_SUCCESS;
}
