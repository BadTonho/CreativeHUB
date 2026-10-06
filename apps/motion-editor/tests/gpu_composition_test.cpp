#include "rendering/composition_frame_renderer.h"
#include "rendering/preview_renderer.h"
#include "diagnostics/performance_metrics.h"
#include "settings/gpu_composition_preferences.h"

#include <creative_suite/composition/opengl_frame_compositor.h>
#include <creative_suite/diagnostics/logger.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool waitFor(const std::function<bool()>& condition)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(2);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return condition();
}

creative_suite::media::RgbaFramePtr imageFrame()
{
    auto frame = std::make_shared<creative_suite::media::RgbaFrame>();
    frame->width = 2;
    frame->height = 2;
    frame->stride = 8;
    frame->rgba_pixels = {
        40, 100, 200, 255, 40, 100, 200, 255,
        40, 100, 200, 255, 40, 100, 200, 255};
    return frame;
}

motion::ui::PreviewRequest request()
{
    motion::ui::PreviewLayerSnapshot image;
    image.id = 1;
    image.kind = motion::model::LayerKind::Image;
    image.still_frame = imageFrame();

    motion::ui::PreviewLayerSnapshot shape;
    shape.id = 2;
    shape.kind = motion::model::LayerKind::Shape;
    motion::model::ShapeLayerContent content;
    content.width = 24;
    content.height = 24;
    content.fill_color = {220, 30, 70, 128};
    shape.content = content;

    motion::ui::PreviewLayerSnapshot text;
    text.id = 3;
    text.kind = motion::model::LayerKind::Text;
    motion::model::TextLayerContent text_content;
    text_content.text = "Motion";
    text_content.font_size_pixels = 9;
    text_content.box_width = 28;
    text_content.box_height = 14;
    text_content.color = {240, 220, 40, 180};
    text.content = text_content;

    return {{32, 32}, {24, 1},
            {std::move(image), std::move(shape), std::move(text)}};
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    qunsetenv("CREATIVE_SUITE_MOTION_GPU_COMPOSITION");
    require(!motion::settings::gpuCompositionEnabled(),
            "GPU composition is disabled by default when the setting is absent");
    require(!motion::settings::resolveGpuCompositionEnabled(nullptr) &&
                !motion::settings::resolveGpuCompositionEnabled("") &&
                !motion::settings::resolveGpuCompositionEnabled("0") &&
                !motion::settings::resolveGpuCompositionEnabled("true") &&
                motion::settings::resolveGpuCompositionEnabled("1"),
            "GPU preview requires the exact environment opt-in");

    QTemporaryDir log_directory;
    require(log_directory.isValid(), "a temporary diagnostic directory is available");
    auto& logger = creative_suite::diagnostics::Logger::instance();
    const auto log_path = std::filesystem::path(log_directory.path().toStdString());
    require(logger.initialize(log_path), "GPU fallback diagnostics can be captured");

    auto& metrics = motion::diagnostics::PerformanceMetrics::instance();
    metrics.setEnabled(true);
    metrics.reset();

    QObject fallback_receiver;
    std::uint64_t fallback_generation = 0;
    creative_suite::media::RgbaFramePtr fallback_frame;
    motion::ui::PreviewRenderer fallback_renderer(
        &fallback_receiver,
        [&](std::uint64_t generation, motion::ui::PreviewRequestMode,
            std::uint64_t, creative_suite::media::RgbaFramePtr frame) {
            fallback_generation = generation;
            fallback_frame = std::move(frame);
        }, {}, &metrics, true, nullptr);
    auto fallback_request = request();
    fallback_request.layers.front().effects = {
        motion::model::ColorAdjustmentEffect{true, 18.0, 132.0, 74.0},
        motion::model::ColorAdjustmentEffect{true, -7.0, 83.0, 145.0}};
    const auto failed_context_generation = fallback_renderer.submit(fallback_request);
    require(waitFor([&] { return fallback_generation == failed_context_generation; }) &&
                fallback_frame != nullptr && fallback_frame->width == 32 &&
                fallback_frame->height == 32,
            "an unavailable OpenGL context still returns the CPU preview frame");
    motion::ui::CompositionFrameRenderer fallback_cpu_renderer(false);
    const auto fallback_cpu_frame = fallback_cpu_renderer.render(fallback_request);
    require(fallback_cpu_frame != nullptr &&
                fallback_frame->rgba_pixels == fallback_cpu_frame->rgba_pixels,
            "failed GPU initialization preserves byte-identical CPU output");
    creative_suite::media::RgbaFramePtr subsequent_fallback_frame;
    const auto subsequent_fallback_generation = fallback_renderer.submit(fallback_request);
    require(waitFor([&] {
                return fallback_generation == subsequent_fallback_generation;
            }) && fallback_frame != nullptr,
            "the worker continues rendering after its OpenGL backend fails");
    motion::ui::CompositionFrameRenderer subsequent_cpu_renderer(false);
    subsequent_fallback_frame = subsequent_cpu_renderer.render(fallback_request);
    require(subsequent_fallback_frame != nullptr &&
                subsequent_fallback_frame->rgba_pixels == fallback_frame->rgba_pixels,
            "subsequent requests remain byte-identical to CPU after disabling the worker GPU path");
    fallback_renderer.stopAndWait();
    auto fallback_snapshot = metrics.takeSnapshotAndReset();
    require(fallback_snapshot.has_value() &&
                fallback_snapshot->gpu_composition_fallbacks == 1 &&
                fallback_snapshot->gpu_composition_failures == 1 &&
                fallback_snapshot->gpu_color_adjustment_fallbacks == 4 &&
                fallback_snapshot->gpu_color_adjustment_failures == 1,
            "context failure falls back for color effects and later worker requests stay CPU");

    std::ifstream log_file(logger.log_path(), std::ios::binary);
    const std::string log_contents(
        (std::istreambuf_iterator<char>(log_file)), std::istreambuf_iterator<char>());
    require(log_contents.find("gpu_composition_fallback") != std::string::npos &&
                log_contents.find("canvas_width=\"32\"") != std::string::npos,
            "GPU fallback logs the operation and canvas context");
    auto surface = creative_suite::composition::OpenGlFrameCompositor::createSurface();
    if (surface) {
        QObject gpu_receiver;
        std::uint64_t gpu_generation = 0;
        creative_suite::media::RgbaFramePtr gpu_frame;
        motion::ui::PreviewRenderer gpu_renderer(
            &gpu_receiver,
            [&](std::uint64_t generation, motion::ui::PreviewRequestMode,
                std::uint64_t, creative_suite::media::RgbaFramePtr frame) {
                gpu_generation = generation;
                gpu_frame = std::move(frame);
            }, {}, &metrics, true, surface.get());
        const auto composition = request();
        const auto generation = gpu_renderer.submit(composition);
        require(waitFor([&] { return gpu_generation == generation; }) &&
                    gpu_frame != nullptr && gpu_frame->width == 32 && gpu_frame->height == 32,
                "GPU preview produces an RGBA frame or falls back to CPU");

        motion::ui::CompositionFrameRenderer cpu_renderer(false);
        const auto cpu_frame = cpu_renderer.render(composition);
        require(cpu_frame != nullptr &&
                    cpu_frame->rgba_pixels.size() == gpu_frame->rgba_pixels.size(),
                "GPU preview retains the current CPU RGBA output contract");
        for (std::size_t index = 0; index < cpu_frame->rgba_pixels.size(); ++index) {
            const auto cpu_value = static_cast<int>(cpu_frame->rgba_pixels[index]);
            const auto gpu_value = static_cast<int>(gpu_frame->rgba_pixels[index]);
            require(std::abs(cpu_value - gpu_value) <= 2,
                    "layer order and alpha match CPU output within two channel values");
        }
        gpu_renderer.stopAndWait();
        const auto gpu_snapshot = metrics.takeSnapshotAndReset();
        require(gpu_snapshot.has_value() &&
                    gpu_snapshot->gpu_composition_frames +
                        gpu_snapshot->gpu_composition_fallbacks == 1,
                "preview metrics record the successful backend or its fallback");
        std::cout << "gpu_composition_frames=" << gpu_snapshot->gpu_composition_frames
                  << " gpu_composition_fallbacks="
                  << gpu_snapshot->gpu_composition_fallbacks << '\n';

        const bool gpu_backend_available = gpu_snapshot->gpu_composition_frames == 1;
        motion::ui::PreviewLayerSnapshot color_layer;
        color_layer.id = 10;
        color_layer.kind = motion::model::LayerKind::Image;
        color_layer.still_frame = imageFrame();
        color_layer.effects = {
            motion::model::ColorAdjustmentEffect{true, 18.0, 132.0, 74.0},
            motion::model::ColorAdjustmentEffect{false, 100.0, 0.0, 0.0},
            motion::model::ColorAdjustmentEffect{true, -7.0, 83.0, 145.0},
            motion::model::ColorAdjustmentEffect{true, 2.0, 105.0, 91.0}};
        if (gpu_backend_available) {
            const motion::ui::PreviewRequest color_request{{2, 2}, {24, 1}, {color_layer}};
            metrics.reset();
            QObject color_receiver;
            std::uint64_t color_generation = 0;
            creative_suite::media::RgbaFramePtr color_gpu_frame;
            motion::ui::PreviewRenderer color_renderer(
                &color_receiver,
                [&](std::uint64_t generation, motion::ui::PreviewRequestMode,
                    std::uint64_t, creative_suite::media::RgbaFramePtr frame) {
                    color_generation = generation;
                    color_gpu_frame = std::move(frame);
                }, {}, &metrics, true, surface.get());
            const auto color_expected_generation = color_renderer.submit(color_request);
            require(waitFor([&] { return color_generation == color_expected_generation; }) &&
                        color_gpu_frame != nullptr,
                    "GPU Color Adjustment preview produces a frame");
            motion::ui::CompositionFrameRenderer color_cpu_renderer(false);
            const auto color_cpu_frame = color_cpu_renderer.render(color_request);
            require(color_cpu_frame != nullptr &&
                        color_cpu_frame->rgba_pixels.size() == color_gpu_frame->rgba_pixels.size(),
                    "CPU Color Adjustment reference produces a frame");
            for (std::size_t index = 0; index < color_cpu_frame->rgba_pixels.size(); ++index) {
                const auto delta = std::abs(static_cast<int>(color_cpu_frame->rgba_pixels[index]) -
                                            static_cast<int>(color_gpu_frame->rgba_pixels[index]));
                require(delta <= (index % 4 == 3 ? 0 : 1),
                        "Motion GPU Color Adjustment matches CPU within one RGB level and exact alpha");
            }
            color_renderer.stopAndWait();
            const auto color_snapshot = metrics.takeSnapshotAndReset();
            require(color_snapshot.has_value() &&
                        color_snapshot->gpu_color_adjustment_effects == 3 &&
                        color_snapshot->gpu_color_adjustment_fallbacks == 0 &&
                        color_snapshot->gpu_color_adjustment_failures == 0,
                    "Motion GPU Color Adjustment records enabled effect counts without fallback");
        }

        color_layer.effects.insert(color_layer.effects.begin(),
            motion::model::GaussianBlurEffect{true, 1.0});
        const motion::ui::PreviewRequest mixed_request{{2, 2}, {24, 1}, {color_layer}};
        metrics.reset();
        QObject mixed_receiver;
        std::uint64_t mixed_generation = 0;
        creative_suite::media::RgbaFramePtr mixed_frame;
        motion::ui::PreviewRenderer mixed_renderer(
            &mixed_receiver,
            [&](std::uint64_t generation, motion::ui::PreviewRequestMode,
                std::uint64_t, creative_suite::media::RgbaFramePtr frame) {
                mixed_generation = generation;
                mixed_frame = std::move(frame);
            }, {}, &metrics, true, surface.get());
        const auto mixed_expected_generation = mixed_renderer.submit(mixed_request);
        require(waitFor([&] { return mixed_generation == mixed_expected_generation; }) &&
                    mixed_frame != nullptr,
                "mixed CPU Gaussian Blur stack still renders through the preview");
        motion::ui::CompositionFrameRenderer mixed_cpu_renderer(false);
        const auto mixed_cpu_frame = mixed_cpu_renderer.render(mixed_request);
        require(mixed_cpu_frame != nullptr && mixed_cpu_frame->rgba_pixels == mixed_frame->rgba_pixels,
                "mixed Gaussian Blur and Color Adjustment stack remains byte-identical to CPU");
        mixed_renderer.stopAndWait();
        const auto mixed_snapshot = metrics.takeSnapshotAndReset();
        require(mixed_snapshot.has_value() &&
                    mixed_snapshot->gpu_color_adjustment_effects == 0 &&
                    mixed_snapshot->gpu_color_adjustment_fallbacks == 3,
                "Gaussian Blur sends the entire ordered effect stack through CPU");
    }

    return EXIT_SUCCESS;
}
