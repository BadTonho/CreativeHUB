#include "frame_compositor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace rendering {
namespace {

struct Color {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    double alpha = 0.0;
};

Color sampleNearest(const media::VideoFrame& frame, double x, double y) noexcept {
    if (frame.width <= 0 || frame.height <= 0 || frame.stride < frame.width * 4 ||
        frame.rgba_pixels.size() < static_cast<std::size_t>(frame.stride) * frame.height) {
        return {};
    }
    const auto source_x = std::clamp(
        static_cast<int>(std::floor(x)), 0, frame.width - 1);
    const auto source_y = std::clamp(
        static_cast<int>(std::floor(y)), 0, frame.height - 1);
    const auto* pixel = frame.rgba_pixels.data() +
        static_cast<std::size_t>(source_y) * frame.stride +
        static_cast<std::size_t>(source_x) * 4;
    return Color{
        pixel[0] / 255.0,
        pixel[1] / 255.0,
        pixel[2] / 255.0,
        pixel[3] / 255.0};
}

void blend(std::uint8_t* destination, const Color& source) noexcept {
    const double source_alpha = std::clamp(source.alpha, 0.0, 1.0);
    if (source_alpha <= 0.0) return;
    if (source_alpha >= 1.0) {
        destination[0] = static_cast<std::uint8_t>(std::lround(
            std::clamp(source.red, 0.0, 1.0) * 255.0));
        destination[1] = static_cast<std::uint8_t>(std::lround(
            std::clamp(source.green, 0.0, 1.0) * 255.0));
        destination[2] = static_cast<std::uint8_t>(std::lround(
            std::clamp(source.blue, 0.0, 1.0) * 255.0));
        destination[3] = 255;
        return;
    }
    const double destination_alpha = destination[3] / 255.0;
    const double output_alpha = source_alpha + destination_alpha * (1.0 - source_alpha);
    if (output_alpha <= 0.0) return;
    const auto output = [source_alpha, destination_alpha, output_alpha](double source_value,
                                                                         double destination_value) {
        return (source_value * source_alpha +
                destination_value * destination_alpha * (1.0 - source_alpha)) /
            output_alpha;
    };
    destination[0] = static_cast<std::uint8_t>(std::lround(std::clamp(output(
        source.red, destination[0] / 255.0), 0.0, 1.0) * 255.0));
    destination[1] = static_cast<std::uint8_t>(std::lround(std::clamp(output(
        source.green, destination[1] / 255.0), 0.0, 1.0) * 255.0));
    destination[2] = static_cast<std::uint8_t>(std::lround(std::clamp(output(
        source.blue, destination[2] / 255.0), 0.0, 1.0) * 255.0));
    destination[3] = static_cast<std::uint8_t>(std::lround(output_alpha * 255.0));
}

bool isOpaqueFrame(const media::VideoFrame& frame) noexcept {
    if (frame.width <= 0 || frame.height <= 0 || frame.stride < frame.width * 4 ||
        frame.rgba_pixels.size() < static_cast<std::size_t>(frame.stride) * frame.height) {
        return false;
    }
    for (int y = 0; y < frame.height; ++y) {
        const auto* row = frame.rgba_pixels.data() +
            static_cast<std::size_t>(y) * frame.stride;
        for (int x = 0; x < frame.width; ++x) {
            if (row[static_cast<std::size_t>(x) * 4 + 3] != 255) return false;
        }
    }
    return true;
}

bool isFullFrameIdentity(
    const media::VideoFrame& frame,
    int width,
    int height,
    const timeline::Transform2D& transform) noexcept {
    return frame.width == width && frame.height == height &&
        frame.stride == width * 4 &&
        transform.position_x == 0.5 && transform.position_y == 0.5 &&
        transform.scale == 1.0 && transform.rotation_degrees == 0.0 &&
        transform.opacity == 1.0;
}

} // namespace

std::optional<media::VideoFrame> FrameCompositor::compose(
    int width,
    int height,
    const std::vector<CompositionLayer>& layers) {
    if (width <= 0 || height <= 0) return std::nullopt;
    if (static_cast<std::size_t>(width) >
            std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(height) / 4) {
        return std::nullopt;
    }

    media::VideoFrame output;
    output.width = width;
    output.height = height;
    output.stride = width * 4;
    output.rgba_pixels.assign(
        static_cast<std::size_t>(output.stride) * output.height, 0);
    for (auto& pixel : output.rgba_pixels) pixel = 0;
    for (std::size_t index = 3; index < output.rgba_pixels.size(); index += 4) {
        output.rgba_pixels[index] = 255;
    }

    for (const auto& layer : layers) {
        if (layer.frame == nullptr || !timeline::validTransform(layer.transform) ||
            layer.frame->width <= 0 || layer.frame->height <= 0) {
            continue;
        }
        if (isFullFrameIdentity(*layer.frame, width, height, layer.transform) &&
            isOpaqueFrame(*layer.frame)) {
            std::memcpy(
                output.rgba_pixels.data(),
                layer.frame->rgba_pixels.data(),
                output.rgba_pixels.size());
            continue;
        }
        const double fit = std::min(
            static_cast<double>(width) / layer.frame->width,
            static_cast<double>(height) / layer.frame->height);
        const double displayed_width = layer.frame->width * fit * layer.transform.scale;
        const double displayed_height = layer.frame->height * fit * layer.transform.scale;
        if (displayed_width <= 0.0 || displayed_height <= 0.0) continue;
        const double center_x = layer.transform.position_x * width;
        const double center_y = layer.transform.position_y * height;
        const double angle = layer.transform.rotation_degrees * 3.14159265358979323846 / 180.0;
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        const double radius = std::hypot(displayed_width, displayed_height) * 0.5;
        const auto left = std::max(0, static_cast<int>(std::floor(center_x - radius - 1.0)));
        const auto right = std::min(width - 1, static_cast<int>(std::ceil(center_x + radius + 1.0)));
        const auto top = std::max(0, static_cast<int>(std::floor(center_y - radius - 1.0)));
        const auto bottom = std::min(height - 1, static_cast<int>(std::ceil(center_y + radius + 1.0)));
        for (int y = top; y <= bottom; ++y) {
            for (int x = left; x <= right; ++x) {
                const double dx = x + 0.5 - center_x;
                const double dy = y + 0.5 - center_y;
                const double unrotated_x = cosine * dx + sine * dy;
                const double unrotated_y = -sine * dx + cosine * dy;
                if (std::abs(unrotated_x) > displayed_width * 0.5 ||
                    std::abs(unrotated_y) > displayed_height * 0.5) {
                    continue;
                }
                const double source_x =
                    (unrotated_x / displayed_width + 0.5) * layer.frame->width;
                const double source_y =
                    (unrotated_y / displayed_height + 0.5) * layer.frame->height;
                auto color = sampleNearest(*layer.frame, source_x, source_y);
                color.alpha *= layer.transform.opacity;
                auto* destination = output.rgba_pixels.data() +
                    static_cast<std::size_t>(y) * output.stride +
                    static_cast<std::size_t>(x) * 4;
                blend(destination, color);
            }
        }
    }
    return output;
}

} // namespace rendering
