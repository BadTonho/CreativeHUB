#include <creative_suite/composition/frame_compositor.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

creative_suite::media::RgbaFrame frame(
    int width,
    int height,
    std::vector<std::uint8_t> pixels) {
    creative_suite::media::RgbaFrame result;
    result.width = width;
    result.height = height;
    result.stride = width * 4;
    result.rgba_pixels = std::move(pixels);
    return result;
}

bool pixelEquals(
    const creative_suite::media::RgbaFrame& image,
    int x,
    int y,
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha) {
    const auto offset = static_cast<std::size_t>(y) * image.stride +
        static_cast<std::size_t>(x) * 4;
    return image.rgba_pixels[offset] == red &&
        image.rgba_pixels[offset + 1] == green &&
        image.rgba_pixels[offset + 2] == blue &&
        image.rgba_pixels[offset + 3] == alpha;
}

} // namespace

int main() {
    using namespace creative_suite;

    try {
        const auto empty = composition::FrameCompositor::compose(3, 2, {});
        require(empty.has_value() && empty->width == 3 && empty->height == 2 &&
                    empty->stride == 12,
                "An empty layer list did not create the requested canvas.");
        for (int y = 0; y < empty->height; ++y) {
            for (int x = 0; x < empty->width; ++x) {
                require(pixelEquals(*empty, x, y, 0, 0, 0, 255),
                        "An empty composition did not return opaque black.");
            }
        }

        const auto source = frame(2, 2, {
            255, 0, 0, 255, 0, 255, 0, 255,
            0, 0, 255, 255, 255, 255, 255, 255});
        composition::CompositionLayer identity_layer;
        identity_layer.frame = &source;
        const auto identity = composition::FrameCompositor::compose(
            2, 2, {identity_layer});
        require(identity.has_value() &&
                    identity->rgba_pixels == source.rgba_pixels,
                "Identity composition changed the source RGBA pixels.");

        auto shifted_transform = animation::Transform2D{};
        shifted_transform.position_x = 0.25;
        const auto shifted = composition::FrameCompositor::compose(
            4,
            2,
            {composition::CompositionLayer{&source, shifted_transform}});
        require(shifted.has_value() &&
                    pixelEquals(*shifted, 0, 0, 255, 0, 0, 255) &&
                    pixelEquals(*shifted, 1, 0, 0, 255, 0, 255) &&
                    pixelEquals(*shifted, 0, 1, 0, 0, 255, 255) &&
                    pixelEquals(*shifted, 1, 1, 255, 255, 255, 255) &&
                    pixelEquals(*shifted, 2, 0, 0, 0, 0, 255) &&
                    pixelEquals(*shifted, 3, 1, 0, 0, 0, 255),
                "Normalized position or aspect-fit placement changed.");

        auto scaled_transform = animation::Transform2D{};
        scaled_transform.scale = 2.0;
        const auto scaled = composition::FrameCompositor::compose(
            4,
            2,
            {composition::CompositionLayer{&source, scaled_transform}});
        require(scaled.has_value() &&
                    pixelEquals(*scaled, 0, 0, 255, 0, 0, 255) &&
                    pixelEquals(*scaled, 1, 0, 255, 0, 0, 255) &&
                    pixelEquals(*scaled, 2, 0, 0, 255, 0, 255) &&
                    pixelEquals(*scaled, 3, 0, 0, 255, 0, 255) &&
                    pixelEquals(*scaled, 0, 1, 0, 0, 255, 255) &&
                    pixelEquals(*scaled, 2, 1, 255, 255, 255, 255),
                "Uniform layer scaling changed its source sampling.");

        auto rotated_transform = animation::Transform2D{};
        rotated_transform.rotation_degrees = 90.0;
        const auto rotated = composition::FrameCompositor::compose(
            2,
            2,
            {composition::CompositionLayer{&source, rotated_transform}});
        require(rotated.has_value() &&
                    pixelEquals(*rotated, 0, 0, 0, 0, 255, 255) &&
                    pixelEquals(*rotated, 1, 0, 255, 0, 0, 255) &&
                    pixelEquals(*rotated, 0, 1, 255, 255, 255, 255) &&
                    pixelEquals(*rotated, 1, 1, 0, 255, 0, 255),
                "Rotation was not applied around the layer center.");

        const auto opaque_red = frame(1, 1, {255, 0, 0, 255});
        const auto half_alpha_blue = frame(1, 1, {0, 0, 255, 128});
        const auto red_then_blue = composition::FrameCompositor::compose(
            1,
            1,
            {composition::CompositionLayer{&opaque_red, {}},
             composition::CompositionLayer{&half_alpha_blue, {}}});
        require(red_then_blue.has_value() &&
                    pixelEquals(*red_then_blue, 0, 0, 127, 0, 128, 255),
                "Layer order or source-over alpha blending changed.");

        const auto blue_then_red = composition::FrameCompositor::compose(
            1,
            1,
            {composition::CompositionLayer{&half_alpha_blue, {}},
             composition::CompositionLayer{&opaque_red, {}}});
        require(blue_then_red.has_value() &&
                    pixelEquals(*blue_then_red, 0, 0, 255, 0, 0, 255),
                "Later layers were not composed above earlier layers.");

        auto invalid_transform = animation::Transform2D{};
        invalid_transform.opacity = 1.5;
        media::RgbaFrame invalid_dimensions;
        invalid_dimensions.width = 0;
        invalid_dimensions.height = 1;
        const auto missing_pixels = frame(1, 1, {});
        const std::vector<composition::CompositionLayer> with_invalid_layers{
            {&opaque_red, {}},
            {nullptr, {}},
            {&half_alpha_blue, invalid_transform},
            {&invalid_dimensions, {}},
            {&missing_pixels, {}}};
        const auto invalid_layers_skipped = composition::FrameCompositor::compose(
            1, 1, with_invalid_layers);
        require(invalid_layers_skipped.has_value() &&
                    invalid_layers_skipped->rgba_pixels == opaque_red.rgba_pixels,
                "Invalid layers were not skipped while valid layers were composed.");

        require(!composition::FrameCompositor::compose(0, 1, {}).has_value() &&
                    !composition::FrameCompositor::compose(1, 0, {}).has_value(),
                "A non-positive canvas dimension was accepted.");
        const int first_unrepresentable_stride_width =
            std::numeric_limits<int>::max() / 4 + 1;
        require(!composition::FrameCompositor::compose(
                     first_unrepresentable_stride_width, 1, {}).has_value(),
                "A canvas whose RGBA stride overflows int was accepted.");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
