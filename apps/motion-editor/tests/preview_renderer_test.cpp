#include "ui/preview_renderer.h"
#include "ui/layer_content_renderer.h"
#include "ui/layer_effect_processor.h"

#include <creative_suite/diagnostics/logger.h>

#include <QGuiApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QThread>

#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
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
    QGuiApplication application(argc, argv);
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary preview log directory is available");
    auto& logger = creative_suite::diagnostics::Logger::instance();
    require(logger.initialize(filePath(temporary.path())),
            "preview errors can be logged to a temporary application directory");

    using motion::model::ShapeKind;
    using motion::model::ShapeLayerContent;
    using motion::model::TextAlignment;
    using motion::model::TextLayerContent;

    creative_suite::media::RgbaFrame transparent_edge;
    transparent_edge.width = 3;
    transparent_edge.height = 1;
    transparent_edge.stride = 12;
    transparent_edge.rgba_pixels = {
        0, 0, 0, 0, 255, 0, 0, 255, 0, 0, 0, 0};
    require(motion::ui::applyLayerEffects(
                transparent_edge,
                {motion::model::GaussianBlurEffect{true, 1.0}}),
            "Gaussian blur completes for a small RGBA frame");
    require(transparent_edge.rgba_pixels[3] > 0 &&
                transparent_edge.rgba_pixels[0] >= 250 &&
                transparent_edge.rgba_pixels[1] == 0 &&
                transparent_edge.rgba_pixels[11] > 0,
            "Gaussian blur preserves red through transparent edges without dark fringes");

    creative_suite::media::RgbaFrame neutral_color;
    neutral_color.width = 1;
    neutral_color.height = 1;
    neutral_color.stride = 4;
    neutral_color.rgba_pixels = {40, 80, 120, 128};
    const auto neutral_original = neutral_color.rgba_pixels;
    require(motion::ui::applyLayerEffects(
                neutral_color,
                {motion::model::ColorAdjustmentEffect{true, 0.0, 100.0, 100.0}}) &&
                neutral_color.rgba_pixels == neutral_original,
            "neutral color adjustment leaves RGBA8 values unchanged");
    require(motion::ui::applyLayerEffects(
                neutral_color,
                {motion::model::ColorAdjustmentEffect{true, 0.0, 0.0, 0.0}}) &&
                neutral_color.rgba_pixels[0] == neutral_color.rgba_pixels[1] &&
                neutral_color.rgba_pixels[1] == neutral_color.rgba_pixels[2] &&
                neutral_color.rgba_pixels[3] == 128,
            "zero contrast and saturation produce gray while preserving alpha");

    creative_suite::media::RgbaFrame ordered_color;
    ordered_color.width = 1;
    ordered_color.height = 1;
    ordered_color.stride = 4;
    ordered_color.rgba_pixels = {102, 102, 102, 255};
    require(motion::ui::applyLayerEffects(ordered_color, {
                motion::model::ColorAdjustmentEffect{true, 20.0, 200.0, 100.0}}),
            "ordered color stack applies its first item");
    const auto brightness_then_contrast = ordered_color.rgba_pixels[0];
    ordered_color.rgba_pixels = {102, 102, 102, 255};
    require(motion::ui::applyLayerEffects(ordered_color, {
                motion::model::ColorAdjustmentEffect{true, 0.0, 200.0, 100.0},
                motion::model::ColorAdjustmentEffect{true, 20.0, 100.0, 100.0}}) &&
                ordered_color.rgba_pixels[0] != brightness_then_contrast,
            "separate effects are applied in displayed stack order");

    auto disabled_frame = *solidFrame(35, 60, 90);
    const auto disabled_original = disabled_frame.rgba_pixels;
    require(motion::ui::applyLayerEffects(disabled_frame, {
                motion::model::ColorAdjustmentEffect{false, 100.0, 0.0, 0.0}}) &&
                disabled_frame.rgba_pixels == disabled_original,
            "disabled effects leave pixels unchanged");

    ShapeLayerContent rectangle;
    rectangle.width = 24;
    rectangle.height = 16;
    rectangle.fill_color = {20, 40, 60, 128};
    rectangle.stroke_color = {240, 30, 10, 255};
    rectangle.stroke_width_pixels = 2;
    const auto rectangle_frame = motion::ui::rasterizeLayerContent(rectangle);
    require(rectangle_frame.has_value() && rectangle_frame->width == 24 &&
                rectangle_frame->height == 16,
            "rectangle content rasterizes at its native pixel dimensions");
    const auto rectangle_center = static_cast<std::size_t>(
        8 * rectangle_frame->stride + 12 * 4);
    require(rectangle_frame->rgba_pixels[rectangle_center] == 20 &&
                rectangle_frame->rgba_pixels[rectangle_center + 1] == 40 &&
                rectangle_frame->rgba_pixels[rectangle_center + 2] == 60 &&
                rectangle_frame->rgba_pixels[rectangle_center + 3] == 128,
            "rectangle fill preserves straight RGBA alpha");
    const auto rectangle_stroke = static_cast<std::size_t>(
        1 * rectangle_frame->stride + 12 * 4);
    require(rectangle_frame->rgba_pixels[rectangle_stroke] >
                rectangle_frame->rgba_pixels[rectangle_stroke + 1] &&
                rectangle_frame->rgba_pixels[rectangle_stroke + 3] > 0,
            "rectangle outline is rasterized over the fill");

    ShapeLayerContent ellipse = rectangle;
    ellipse.shape = ShapeKind::Ellipse;
    ellipse.fill_color = {30, 170, 230, 255};
    ellipse.stroke_width_pixels = 0;
    const auto ellipse_frame = motion::ui::rasterizeLayerContent(ellipse);
    require(ellipse_frame.has_value() && ellipse_frame->rgba_pixels[3] == 0,
            "ellipse corners retain transparent RGBA pixels");
    const auto ellipse_center = static_cast<std::size_t>(
        8 * ellipse_frame->stride + 12 * 4);
    require(ellipse_frame->rgba_pixels[ellipse_center] == 30 &&
                ellipse_frame->rgba_pixels[ellipse_center + 1] == 170 &&
                ellipse_frame->rgba_pixels[ellipse_center + 2] == 230 &&
                ellipse_frame->rgba_pixels[ellipse_center + 3] == 255,
            "ellipse fill is rasterized into the transparent frame");

    TextLayerContent text;
    text.text = "Motion \xE2\x9C\xA8\nStudio";
    text.font_size_pixels = 25;
    text.box_width = 180;
    text.box_height = 90;
    text.color = {230, 210, 20, 128};
    text.alignment = TextAlignment::Left;
    const auto text_frame = motion::ui::rasterizeLayerContent(text);
    require(text_frame.has_value() && text_frame->width == text.box_width &&
                text_frame->height == text.box_height,
            "Unicode multiline text rasterizes in the editable box");
    const auto alpha_bounds = [](const creative_suite::media::RgbaFrame& frame) {
        int left = frame.width;
        int top = frame.height;
        int right = -1;
        int bottom = -1;
        for (int y = 0; y < frame.height; ++y) {
            for (int x = 0; x < frame.width; ++x) {
                const auto offset = static_cast<std::size_t>(y) * frame.stride +
                    static_cast<std::size_t>(x) * 4 + 3;
                if (frame.rgba_pixels[offset] > 8) {
                    left = std::min(left, x);
                    top = std::min(top, y);
                    right = std::max(right, x);
                    bottom = std::max(bottom, y);
                }
            }
        }
        return std::array<int, 4>{left, top, right, bottom};
    };
    const auto left_text_bounds = alpha_bounds(*text_frame);
    require(left_text_bounds[2] >= left_text_bounds[0] &&
                left_text_bounds[3] >= left_text_bounds[1],
            "text rasterization produces visible antialiased glyph pixels");
    for (int y = 0; y < text_frame->height; ++y) {
        for (int x = 0; x < text_frame->width; ++x) {
            const auto offset = static_cast<std::size_t>(y) * text_frame->stride +
                static_cast<std::size_t>(x) * 4;
            require(text_frame->rgba_pixels[offset + 3] <= 128,
                    "text rasterization preserves the selected alpha ceiling");
        }
    }
    text.alignment = TextAlignment::Right;
    const auto right_text_frame = motion::ui::rasterizeLayerContent(text);
    require(right_text_frame.has_value() &&
                alpha_bounds(*right_text_frame)[0] > left_text_bounds[0],
            "text alignment moves glyphs inside the content box");

    QObject result_receiver;
    motion::ui::PreviewRenderer* renderer_ptr = nullptr;
    std::uint64_t applied_generation = 0;
    creative_suite::media::RgbaFramePtr applied_frame;
    std::vector<std::pair<std::uint64_t, creative_suite::media::RgbaFramePtr>> results;
    motion::ui::PreviewRenderer renderer(&result_receiver,
        [&](std::uint64_t generation,
            motion::ui::PreviewRequestMode mode,
            std::uint64_t,
            creative_suite::media::RgbaFramePtr frame) {
            results.emplace_back(generation, frame);
            if (renderer_ptr != nullptr &&
                mode == motion::ui::PreviewRequestMode::Interactive &&
                generation == renderer_ptr->generation()) {
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

    motion::ui::PreviewLayerSnapshot effected_image;
    effected_image.kind = motion::model::LayerKind::Image;
    effected_image.still_frame = solidFrame(40, 80, 120);
    effected_image.effects.emplace_back(
        motion::model::ColorAdjustmentEffect{true, 20.0, 100.0, 100.0});
    const auto effected_generation = renderer.submit(
        motion::ui::PreviewRequest{{4, 4}, {24, 1}, {effected_image}});
    require(waitFor([&] { return applied_generation == effected_generation; }) &&
                applied_frame != nullptr && applied_frame->rgba_pixels[0] == 91 &&
                applied_frame->rgba_pixels[1] == 131 &&
                applied_frame->rgba_pixels[2] == 171,
            "the preview worker applies layer effects before the shared compositor");

    motion::ui::PreviewLayerSnapshot rectangle_layer;
    rectangle_layer.id = 600;
    rectangle_layer.kind = motion::model::LayerKind::Shape;
    motion::model::ShapeLayerContent preview_shape;
    preview_shape.width = 25;
    preview_shape.height = 25;
    preview_shape.fill_color = {245, 35, 15, 255};
    rectangle_layer.content = preview_shape;
    const auto design_generation = renderer.submit(
        motion::ui::PreviewRequest{{100, 100}, {24, 1}, {rectangle_layer}});
    require(waitFor([&] { return applied_generation == design_generation; }) &&
                applied_frame != nullptr,
            "native rectangle layers render through the preview worker");
    int shape_left = 100;
    int shape_top = 100;
    int shape_right = -1;
    int shape_bottom = -1;
    for (int y = 0; y < applied_frame->height; ++y) {
        for (int x = 0; x < applied_frame->width; ++x) {
            const auto offset = static_cast<std::size_t>(y) * applied_frame->stride +
                static_cast<std::size_t>(x) * 4;
            if (applied_frame->rgba_pixels[offset] > 100) {
                shape_left = std::min(shape_left, x);
                shape_top = std::min(shape_top, y);
                shape_right = std::max(shape_right, x);
                shape_bottom = std::max(shape_bottom, y);
            }
        }
    }
    require(shape_left >= 35 && shape_left <= 40 && shape_right >= 59 && shape_right <= 64 &&
                shape_top >= 35 && shape_bottom <= 64,
            "pixel-sized shape geometry is preserved through compositor aspect fitting");
    preview_shape.fill_color = {15, 45, 240, 255};
    rectangle_layer.content = preview_shape;
    const auto changed_shape_generation = renderer.submit(
        motion::ui::PreviewRequest{{100, 100}, {24, 1}, {rectangle_layer}});
    const auto changed_shape_center = static_cast<std::size_t>(
        50 * applied_frame->stride + 50 * 4);
    require(waitFor([&] { return applied_generation == changed_shape_generation; }) &&
                applied_frame != nullptr &&
                applied_frame->rgba_pixels[changed_shape_center] == 15 &&
                applied_frame->rgba_pixels[changed_shape_center + 2] == 240,
            "shape content edits replace cached raster frames");

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

    motion::ui::PreviewLayerSnapshot animated_layer;
    animated_layer.kind = motion::model::LayerKind::Image;
    animated_layer.local_frame = 5;
    animated_layer.still_frame = solidFrame(255, 255, 255);
    require(creative_suite::animation::setKeyframe(
                animated_layer.keyframes,
                creative_suite::animation::TransformProperty::Opacity, 0, 0.0) &&
                creative_suite::animation::setKeyframe(
                    animated_layer.keyframes,
                    creative_suite::animation::TransformProperty::Opacity, 10, 1.0),
            "preview animation fixture accepts opacity keys");
    require(creative_suite::animation::setKeyframeInterpolation(
                animated_layer.keyframes,
                creative_suite::animation::TransformProperty::Opacity, 0,
                creative_suite::animation::InterpolationMode::CubicBezier,
                {0.42, 0.0, 1.0, 1.0}),
            "preview animation fixture accepts an easing curve");
    motion::ui::PreviewRequest animated_request{
        {2, 2}, {24, 1}, {animated_layer}};
    const auto animated_generation = renderer.submit(std::move(animated_request));
    require(waitFor([&] { return applied_generation == animated_generation; }) &&
                applied_frame != nullptr &&
                applied_frame->rgba_pixels[0] >= 75 && applied_frame->rgba_pixels[0] <= 90,
            "preview evaluates the Bézier-eased opacity at an intermediate local frame");
    animated_layer.local_frame = 10;
    const auto endpoint_generation = renderer.submit(motion::ui::PreviewRequest{
        {2, 2}, {24, 1}, {animated_layer}});
    require(waitFor([&] { return applied_generation == endpoint_generation; }) &&
                applied_frame != nullptr && applied_frame->rgba_pixels[0] == 255,
            "preview evaluates the exact keyframe value at its local frame");
    renderer.stopAndWait();

    QObject playback_receiver;
    motion::ui::PreviewRenderer* playback_renderer_ptr = nullptr;
    std::atomic<int> playback_slow_render_count{0};
    std::atomic<bool> playback_was_cancelled{false};
    std::uint64_t playback_applied_generation = 0;
    creative_suite::media::RgbaFramePtr playback_applied_frame;
    std::vector<std::uint8_t> playback_presented_colors;
    motion::ui::PreviewRenderer playback_renderer(
        &playback_receiver,
        [&](std::uint64_t generation,
            motion::ui::PreviewRequestMode mode,
            std::uint64_t,
            creative_suite::media::RgbaFramePtr frame) {
            if (playback_renderer_ptr != nullptr &&
                (mode == motion::ui::PreviewRequestMode::Playback ||
                 generation == playback_renderer_ptr->generation())) {
                playback_applied_generation = generation;
                playback_applied_frame = std::move(frame);
                if (playback_applied_frame != nullptr) {
                    playback_presented_colors.push_back(
                        playback_applied_frame->rgba_pixels[0]);
                }
            }
        },
        [&](const motion::ui::PreviewRequest& request,
            const motion::ui::PreviewRenderer::CancellationPredicate& is_cancelled) {
            const auto color = request.layers.front().still_frame->rgba_pixels[0];
            if (color == 240 || color == 200) {
                ++playback_slow_render_count;
                for (int step = 0; step < 80; ++step) {
                    if (is_cancelled()) {
                        playback_was_cancelled = true;
                        return creative_suite::media::RgbaFramePtr{};
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            if (is_cancelled()) {
                playback_was_cancelled = true;
                return creative_suite::media::RgbaFramePtr{};
            }
            return solidFrame(color, 0, 0);
        });
    playback_renderer_ptr = &playback_renderer;

    const auto first_playback_generation = playback_renderer.submit(
        imageRequest(solidFrame(240, 0, 0)), motion::ui::PreviewRequestMode::Playback);
    require(waitFor([&] { return playback_slow_render_count.load() == 1; }),
            "a deliberately slow playback frame begins rendering");
    (void)playback_renderer.submit(
        imageRequest(solidFrame(120, 0, 0)), motion::ui::PreviewRequestMode::Playback);
    const auto latest_playback_generation = playback_renderer.submit(
        imageRequest(solidFrame(10, 0, 0)), motion::ui::PreviewRequestMode::Playback);
    require(latest_playback_generation > first_playback_generation &&
                waitFor([&] {
                    return playback_applied_generation == latest_playback_generation;
                }),
            "playback coalesces queued frames and eventually renders the newest frame");
    require(!playback_was_cancelled.load() && playback_applied_frame != nullptr &&
                playback_applied_frame->rgba_pixels[0] == 10 &&
                std::find(playback_presented_colors.begin(), playback_presented_colors.end(), 240) !=
                    playback_presented_colors.end(),
            "a completed playback frame can display while newer ticks coalesce to the latest frame");

    const auto slow_playback_generation = playback_renderer.submit(
        imageRequest(solidFrame(200, 0, 0)), motion::ui::PreviewRequestMode::Playback);
    require(waitFor([&] { return playback_slow_render_count.load() == 2; }),
            "a second slow playback frame begins rendering");
    const auto interactive_generation = playback_renderer.submit(
        imageRequest(solidFrame(30, 0, 0)), motion::ui::PreviewRequestMode::Interactive);
    require(interactive_generation > slow_playback_generation &&
                waitFor([&] { return playback_applied_generation == interactive_generation; }),
            "an interactive seek supersedes active playback work");
    require(playback_was_cancelled.load() && playback_applied_frame != nullptr &&
                playback_applied_frame->rgba_pixels[0] == 30,
            "manual preview requests can cancel playback work and present the seek result");
    playback_renderer.stopAndWait();

    std::cout << "Motion Studio preview renderer tests passed.\n";
    return EXIT_SUCCESS;
}
