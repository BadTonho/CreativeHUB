#include "media/video_frame.h"
#include "rendering/frame_compositor.h"
#include "rendering/text_renderer.h"
#include "timeline/timeline_model.h"

#include <QGuiApplication>

#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    try {
        timeline::TextStyle style;
        style.content = "Hello";
        style.font_size_pixels = 32.0;
        style.color = {255, 10, 20, 200};
        style.alignment = timeline::TextAlignment::Center;
        const auto text_frame = rendering::renderText(style);
        require(text_frame.has_value(), "Text rasterization returned no frame.");
        require(text_frame->width > 0 && text_frame->height > 0 &&
                    text_frame->stride >= text_frame->width * 4 &&
                    text_frame->rgba_pixels.size() >=
                        static_cast<std::size_t>(text_frame->stride) * text_frame->height,
                "Text rasterization returned invalid RGBA dimensions.");
        bool has_alpha = false;
        for (std::size_t index = 3; index < text_frame->rgba_pixels.size(); index += 4) {
            if (text_frame->rgba_pixels[index] != 0) {
                has_alpha = true;
                break;
            }
        }
        require(has_alpha, "Rasterized text contained no visible pixels.");

        media::VideoFrame video;
        video.width = 4;
        video.height = 4;
        video.stride = 16;
        video.rgba_pixels.assign(64, 0);
        for (std::size_t index = 0; index < video.rgba_pixels.size(); index += 4) {
            video.rgba_pixels[index] = 20;
            video.rgba_pixels[index + 1] = 40;
            video.rgba_pixels[index + 2] = 80;
            video.rgba_pixels[index + 3] = 255;
        }
        const std::vector<rendering::CompositionLayer> layers{
            {&video, {}},
            {&*text_frame, {}}};
        const auto composed = rendering::FrameCompositor::compose(64, 64, layers);
        require(composed.has_value() && composed->width == 64 &&
                    composed->height == 64 && composed->stride == 256,
                "Text and video composition returned invalid output.");

        const std::vector<rendering::CompositionLayer> text_only_layers{
            {&*text_frame, {}}};
        const auto text_only_composed = rendering::FrameCompositor::compose(
            64, 64, text_only_layers);
        require(text_only_composed.has_value(),
                "Text-only composition returned no frame.");
        bool text_only_has_visible_pixels = false;
        for (std::size_t index = 0;
             index + 3 < text_only_composed->rgba_pixels.size();
             index += 4) {
            if (text_only_composed->rgba_pixels[index + 3] != 255 ||
                text_only_composed->rgba_pixels[index] != 0 ||
                text_only_composed->rgba_pixels[index + 1] != 0 ||
                text_only_composed->rgba_pixels[index + 2] != 0) {
                text_only_has_visible_pixels = true;
                break;
            }
        }
        require(text_only_has_visible_pixels,
                "Text-only composition contained no visible pixels.");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
