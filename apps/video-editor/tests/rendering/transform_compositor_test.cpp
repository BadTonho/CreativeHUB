#include "media/video_frame.h"
#include "rendering/frame_compositor.h"
#include "timeline/timeline_model.h"
#include "timeline/timeline_transform.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

media::VideoMetadata metadata(const std::filesystem::path& path) {
    media::VideoMetadata result;
    result.source_path = path;
    result.display_name = "test.mkv";
    result.frame_count = 20;
    result.frame_rate = 20.0;
    result.duration_seconds = 1.0;
    return result;
}

media::VideoFrame solid(int red, int green, int blue, int alpha = 255) {
    media::VideoFrame frame;
    frame.width = 2;
    frame.height = 2;
    frame.stride = 8;
    frame.rgba_pixels.resize(16);
    for (std::size_t index = 0; index < frame.rgba_pixels.size(); index += 4) {
        frame.rgba_pixels[index] = static_cast<std::uint8_t>(red);
        frame.rgba_pixels[index + 1] = static_cast<std::uint8_t>(green);
        frame.rgba_pixels[index + 2] = static_cast<std::uint8_t>(blue);
        frame.rgba_pixels[index + 3] = static_cast<std::uint8_t>(alpha);
    }
    return frame;
}

media::VideoFrame fullFrame(int red, int green, int blue, int alpha = 255) {
    media::VideoFrame frame;
    frame.width = 4;
    frame.height = 4;
    frame.stride = 16;
    frame.rgba_pixels.resize(64);
    for (std::size_t index = 0; index < frame.rgba_pixels.size(); index += 4) {
        frame.rgba_pixels[index] = static_cast<std::uint8_t>(red);
        frame.rgba_pixels[index + 1] = static_cast<std::uint8_t>(green);
        frame.rgba_pixels[index + 2] = static_cast<std::uint8_t>(blue);
        frame.rgba_pixels[index + 3] = static_cast<std::uint8_t>(alpha);
    }
    return frame;
}

media::VideoFrame sparseAlphaFrame() {
    media::VideoFrame frame;
    frame.width = 5;
    frame.height = 4;
    frame.stride = 20;
    frame.rgba_pixels.assign(80, 0);
    const auto setPixel = [&frame](int x, int y, int red, int green, int blue, int alpha) {
        auto* pixel = frame.rgba_pixels.data() +
            static_cast<std::size_t>(y) * frame.stride +
            static_cast<std::size_t>(x) * 4;
        pixel[0] = static_cast<std::uint8_t>(red);
        pixel[1] = static_cast<std::uint8_t>(green);
        pixel[2] = static_cast<std::uint8_t>(blue);
        pixel[3] = static_cast<std::uint8_t>(alpha);
    };
    setPixel(1, 0, 255, 0, 0, 255);
    setPixel(3, 0, 0, 255, 0, 128);
    setPixel(0, 1, 0, 0, 255, 200);
    setPixel(1, 1, 0, 0, 255, 200);
    setPixel(2, 1, 0, 0, 255, 200);
    setPixel(4, 2, 255, 255, 0, 96);
    setPixel(2, 3, 255, 0, 255, 255);
    return frame;
}

media::VideoFrame patternedFrame(int width, int height) {
    media::VideoFrame frame;
    frame.width = width;
    frame.height = height;
    frame.stride = width * 4;
    frame.rgba_pixels.resize(static_cast<std::size_t>(frame.stride) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            auto* pixel = frame.rgba_pixels.data() +
                static_cast<std::size_t>(y) * frame.stride +
                static_cast<std::size_t>(x) * 4;
            pixel[0] = static_cast<std::uint8_t>((x * 47 + y * 13) % 256);
            pixel[1] = static_cast<std::uint8_t>((x * 19 + y * 61) % 256);
            pixel[2] = static_cast<std::uint8_t>((x * 83 + y * 29) % 256);
            pixel[3] = static_cast<std::uint8_t>((x * 37 + y * 53) % 256);
        }
    }
    return frame;
}

media::VideoFrame referenceGeneralComposition(
    int width,
    int height,
    const std::vector<rendering::CompositionLayer>& layers) {
    media::VideoFrame output;
    output.width = width;
    output.height = height;
    output.stride = width * 4;
    output.rgba_pixels.assign(
        static_cast<std::size_t>(output.stride) * output.height, 0);
    for (std::size_t index = 3; index < output.rgba_pixels.size(); index += 4) {
        output.rgba_pixels[index] = 255;
    }

    for (const auto& layer : layers) {
        if (layer.frame == nullptr || !timeline::validTransform(layer.transform) ||
            layer.frame->width <= 0 || layer.frame->height <= 0) {
            continue;
        }
        const auto& frame = *layer.frame;
        const double fit = std::min(
            static_cast<double>(width) / frame.width,
            static_cast<double>(height) / frame.height);
        const double displayed_width = frame.width * fit * layer.transform.scale;
        const double displayed_height = frame.height * fit * layer.transform.scale;
        if (displayed_width <= 0.0 || displayed_height <= 0.0) continue;

        const double center_x = layer.transform.position_x * width;
        const double center_y = layer.transform.position_y * height;
        const double angle = layer.transform.rotation_degrees *
            3.14159265358979323846 / 180.0;
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        const double radius = std::hypot(displayed_width, displayed_height) * 0.5;
        const int left = std::max(
            0, static_cast<int>(std::floor(center_x - radius - 1.0)));
        const int right = std::min(
            width - 1, static_cast<int>(std::ceil(center_x + radius + 1.0)));
        const int top = std::max(
            0, static_cast<int>(std::floor(center_y - radius - 1.0)));
        const int bottom = std::min(
            height - 1, static_cast<int>(std::ceil(center_y + radius + 1.0)));

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
                    (unrotated_x / displayed_width + 0.5) * frame.width;
                const double source_y =
                    (unrotated_y / displayed_height + 0.5) * frame.height;
                const int pixel_x = std::clamp(
                    static_cast<int>(std::floor(source_x)), 0, frame.width - 1);
                const int pixel_y = std::clamp(
                    static_cast<int>(std::floor(source_y)), 0, frame.height - 1);
                const auto* source = frame.rgba_pixels.data() +
                    static_cast<std::size_t>(pixel_y) * frame.stride +
                    static_cast<std::size_t>(pixel_x) * 4;
                const double source_alpha = std::clamp(
                    source[3] / 255.0 * layer.transform.opacity, 0.0, 1.0);
                if (source_alpha <= 0.0) continue;
                auto* destination = output.rgba_pixels.data() +
                    static_cast<std::size_t>(y) * output.stride +
                    static_cast<std::size_t>(x) * 4;
                if (source_alpha >= 1.0) {
                    destination[0] = source[0];
                    destination[1] = source[1];
                    destination[2] = source[2];
                    destination[3] = 255;
                    continue;
                }
                const double destination_alpha = destination[3] / 255.0;
                const double output_alpha = source_alpha +
                    destination_alpha * (1.0 - source_alpha);
                if (output_alpha <= 0.0) continue;
                const auto blend_channel = [source_alpha, destination_alpha,
                                            output_alpha](std::uint8_t source_value,
                                                          std::uint8_t destination_value) {
                    const double value =
                        (source_value / 255.0 * source_alpha +
                         destination_value / 255.0 * destination_alpha *
                             (1.0 - source_alpha)) /
                        output_alpha;
                    return static_cast<std::uint8_t>(std::lround(
                        std::clamp(value, 0.0, 1.0) * 255.0));
                };
                for (int channel = 0; channel < 3; ++channel) {
                    destination[channel] = blend_channel(
                        source[channel], destination[channel]);
                }
                destination[3] = static_cast<std::uint8_t>(std::lround(
                    output_alpha * 255.0));
            }
        }
    }
    return output;
}

} // namespace

int main() {
    try {
        timeline::Transform2D identity;
        require(timeline::validTransform(identity), "The default transform is invalid.");

        timeline::TransformKeyframes keyframes;
        require(timeline::setKeyframe(
                    keyframes,
                    timeline::TransformProperty::PositionX,
                    0,
                    0.0),
                "The first keyframe was not created.");
        require(timeline::setKeyframe(
                    keyframes,
                    timeline::TransformProperty::PositionX,
                    10,
                    1.0),
                "The second keyframe was not created.");
        const auto evaluated = timeline::evaluateTransform(identity, keyframes, 5);
        require(std::abs(evaluated.position_x - 0.5) < 0.0001,
                "Keyframes were not interpolated linearly.");
        require(timeline::removeKeyframe(
                    keyframes,
                    timeline::TransformProperty::PositionX,
                    10),
                "A keyframe was not removed.");
        require(!timeline::validKeyframeValue(
                    timeline::TransformProperty::Opacity, 1.1) &&
                    !timeline::validKeyframeValue(
                        timeline::TransformProperty::Scale, 0.0),
                "Invalid transform keyframe values were accepted.");

        timeline::TimelineModel model;
        require(model.addClip(metadata("transform-source.mkv")) ==
                    timeline::AddClipResult::Added,
                "The transform test clip was not added.");
        auto transform = identity;
        transform.position_x = 0.25;
        require(model.setClipTransform(0, 0, transform) ==
                    timeline::TransformParameterResult::Changed,
                "The clip transform was not updated.");
        require(model.setClipKeyframe(
                    0, 0, timeline::TransformProperty::PositionX, 5, 0.75) ==
                    timeline::TransformParameterResult::Changed,
                "The clip keyframe was not stored.");
        require(model.splitClip(0, 0, 10) == timeline::SplitClipResult::Split,
                "The transform clip was not split.");
        require(model.tracks().front().clips[0].transform.position_x == 0.25 &&
                    model.tracks().front().clips[1].transform.position_x == 0.75,
                "Splitting changed the transform bases incorrectly.");
        require(model.tracks().front().clips[1].source_start_frame == 10 &&
                    model.tracks().front().clips[1].timeline_duration_frames == 10,
                "The split source offsets were incorrect.");

        require(model.trimClip(0, 1, 12, 6) == timeline::TrimClipResult::Trimmed,
                "The transform clip was not trimmed.");
        require(model.tracks().front().clips[1].source_start_frame == 12 &&
                    model.tracks().front().clips[1].timeline_duration_frames == 6,
                "The trimmed source range was incorrect.");

        const auto bottom = solid(255, 0, 0);
        const auto top = solid(0, 255, 0, 128);
        const std::vector<rendering::CompositionLayer> layers{
            {&bottom, identity},
            {&top, identity}};
        rendering::FrameCompositionTimings composition_timings;
        const auto composed = rendering::FrameCompositor::compose(
            512,
            512,
            layers,
            &composition_timings);
        require(composed.has_value() && composed->width == 512 && composed->height == 512,
                "The compositor did not create the expected canvas.");
        require(composition_timings.layers.size() == layers.size() &&
                    composition_timings.output_buffer_create_nanoseconds > 0 &&
                    composition_timings.output_background_fill_nanoseconds > 0 &&
                    composition_timings.layers[0].setup_nanoseconds +
                        composition_timings.layers[0].raster_blend_nanoseconds > 0 &&
                    composition_timings.layers[1].setup_nanoseconds +
                        composition_timings.layers[1].raster_blend_nanoseconds > 0,
                "The compositor did not report output and general raster timings.");
        require(composed->rgba_pixels[0] > 100 && composed->rgba_pixels[1] > 100,
                "The compositor did not blend alpha layers.");

        const auto patterned = patternedFrame(5, 3);
        auto centered = identity;
        auto clipped_overlay = identity;
        clipped_overlay.position_x = 0.25;
        clipped_overlay.position_y = 0.68;
        clipped_overlay.scale = 0.8;
        clipped_overlay.opacity = 0.63;
        auto edge_overlay = identity;
        edge_overlay.position_x = 1.05;
        edge_overlay.position_y = 0.1;
        edge_overlay.scale = 1.2;
        const auto sparse_overlay = sparseAlphaFrame();
        const auto transparent_axis_overlay = fullFrame(0, 0, 0, 0);
        const std::vector<rendering::CompositionLayer> exact_layers{
            {&patterned, centered},
            {&sparse_overlay, clipped_overlay},
            {&transparent_axis_overlay, edge_overlay}};
        rendering::FrameCompositionTimings axis_aligned_timings;
        const auto axis_aligned = rendering::FrameCompositor::compose(
            11, 7, exact_layers, &axis_aligned_timings);
        const auto axis_aligned_reference = referenceGeneralComposition(
            11, 7, exact_layers);
        require(axis_aligned.has_value() &&
                    axis_aligned->rgba_pixels == axis_aligned_reference.rgba_pixels,
                "The unrotated fast path changed pixels from the scalar reference.");
        require(axis_aligned_timings.layers.size() == exact_layers.size() &&
                    axis_aligned_timings.layers[0].raster_blend_nanoseconds > 0 &&
                    axis_aligned_timings.layers[1].raster_blend_nanoseconds > 0 &&
                    axis_aligned_timings.layers[2].fast_path_copy_nanoseconds == 0,
                "The unrotated fast path did not preserve compositor timing categories.");
        const auto empty_output = rendering::FrameCompositor::compose(3, 2, {});
        require(empty_output.has_value(), "The empty output buffer was not created.");
        for (std::size_t index = 0; index < empty_output->rgba_pixels.size(); index += 4) {
            require(empty_output->rgba_pixels[index] == 0 &&
                        empty_output->rgba_pixels[index + 1] == 0 &&
                        empty_output->rgba_pixels[index + 2] == 0 &&
                        empty_output->rgba_pixels[index + 3] == 255,
                    "The empty output buffer did not retain its black opaque background.");
        }

        const auto opaque_full = fullFrame(31, 63, 127);
        const auto direct = rendering::FrameCompositor::compose(
            4,
            4,
            std::vector<rendering::CompositionLayer>{{&opaque_full, identity}});
        require(direct.has_value() && direct->rgba_pixels == opaque_full.rgba_pixels,
                "The opaque identity composition changed the source pixels.");

        media::VideoFrame large_opaque;
        large_opaque.width = 512;
        large_opaque.height = 512;
        large_opaque.stride = large_opaque.width * 4;
        large_opaque.rgba_pixels.resize(
            static_cast<std::size_t>(large_opaque.stride) * large_opaque.height);
        for (std::size_t index = 0; index < large_opaque.rgba_pixels.size(); index += 4) {
            large_opaque.rgba_pixels[index] = 31;
            large_opaque.rgba_pixels[index + 1] = 63;
            large_opaque.rgba_pixels[index + 2] = 127;
            large_opaque.rgba_pixels[index + 3] = 255;
        }
        rendering::FrameCompositionTimings copy_timings;
        const auto copied = rendering::FrameCompositor::compose(
            large_opaque.width,
            large_opaque.height,
            std::vector<rendering::CompositionLayer>{{&large_opaque, identity}},
            &copy_timings);
        require(copied.has_value() && copied->rgba_pixels == large_opaque.rgba_pixels,
                "The compositor changed pixels in the opaque copy path.");
        require(copy_timings.layers.size() == 1 &&
                    copy_timings.layers[0].fast_path_copy_nanoseconds > 0 &&
                    copy_timings.layers[0].raster_blend_nanoseconds == 0,
                "The compositor did not isolate the opaque fast-path copy timing.");

        auto near_identity = identity;
        near_identity.position_x = 0.500001;
        const auto general_equivalent = rendering::FrameCompositor::compose(
            4,
            4,
            std::vector<rendering::CompositionLayer>{{&opaque_full, near_identity}});
        require(general_equivalent.has_value() &&
                    general_equivalent->rgba_pixels == direct->rgba_pixels,
                "The compositor fast path differs from the general transform path.");

        const auto transparent = fullFrame(0, 0, 0, 0);
        const auto transparent_overlay = rendering::FrameCompositor::compose(
            4,
            4,
            std::vector<rendering::CompositionLayer>{{&opaque_full, identity},
                                                     {&transparent, identity}});
        require(transparent_overlay.has_value() &&
                    transparent_overlay->rgba_pixels == opaque_full.rgba_pixels,
                "A transparent layer changed the composed pixels.");

        const std::vector<rendering::CompositionLayer> reversed_layers{
            {&top, identity},
            {&bottom, identity}};
        const auto reversed = rendering::FrameCompositor::compose(4, 4, reversed_layers);
        require(reversed.has_value() && reversed->rgba_pixels[0] > 200 &&
                    reversed->rgba_pixels[1] < 20,
                "The compositor did not preserve layer order.");

        const auto sparse = sparseAlphaFrame();
        const auto coverage = rendering::FrameCompositor::buildAlphaCoverage(sparse);
        require(coverage != nullptr && coverage->rows.size() == 4 &&
                    coverage->rows[0].size() == 2 &&
                    coverage->rows[1].size() == 1,
                "Alpha coverage did not preserve transparent spans.");
        auto text_transform = identity;
        text_transform.position_x = 0.4;
        text_transform.position_y = 0.6;
        text_transform.scale = 0.75;
        text_transform.opacity = 0.73;
        require(rendering::FrameCompositor::canUseAlphaCoverageFastPath(
                    rendering::CompositionLayer{&sparse, text_transform, coverage}),
                "The unrotated alpha coverage layer was not eligible for the fast path.");
        rendering::FrameCompositionTimings alpha_timings;
        const auto optimized = rendering::FrameCompositor::compose(
            512,
            512,
            std::vector<rendering::CompositionLayer>{{
                &sparse,
                text_transform,
                coverage}},
            &alpha_timings);
        const auto general = rendering::FrameCompositor::compose(
            512,
            512,
            std::vector<rendering::CompositionLayer>{{&sparse, text_transform}});
        require(optimized.has_value() && general.has_value() &&
                    optimized->rgba_pixels == general->rgba_pixels,
                "The alpha coverage fast path changed composed pixels.");
        require(alpha_timings.layers.size() == 1 &&
                    alpha_timings.layers[0].setup_nanoseconds > 0 &&
                    alpha_timings.layers[0].raster_blend_nanoseconds > 0 &&
                    alpha_timings.layers[0].fast_path_copy_nanoseconds == 0,
                "The compositor did not isolate alpha-coverage setup and raster timings.");

        auto rotated = text_transform;
        rotated.rotation_degrees = 15.0;
        require(!rendering::FrameCompositor::canUseAlphaCoverageFastPath(
                    rendering::CompositionLayer{&sparse, rotated, coverage}),
                "A rotated alpha coverage layer incorrectly used the fast path.");
        const auto rotated_with_coverage = rendering::FrameCompositor::compose(
            512,
            512,
            std::vector<rendering::CompositionLayer>{{
                &sparse,
                rotated,
                coverage}});
        const auto rotated_general = rendering::FrameCompositor::compose(
            512,
            512,
            std::vector<rendering::CompositionLayer>{{&sparse, rotated}});
        require(rotated_with_coverage.has_value() && rotated_general.has_value() &&
                    rotated_with_coverage->rgba_pixels == rotated_general->rgba_pixels,
                "Rotated alpha coverage did not use the general compositor path.");

        media::VideoFrame empty = sparse;
        std::fill(
            empty.rgba_pixels.begin(),
            empty.rgba_pixels.end(),
            static_cast<std::uint8_t>(0));
        const auto empty_coverage = rendering::FrameCompositor::buildAlphaCoverage(empty);
        const auto empty_composed = rendering::FrameCompositor::compose(
            512,
            512,
            std::vector<rendering::CompositionLayer>{{
                &empty,
                text_transform,
                empty_coverage}});
        const auto empty_general = rendering::FrameCompositor::compose(512, 512, {});
        require(empty_composed.has_value() && empty_general.has_value() &&
                    empty_composed->rgba_pixels == empty_general->rgba_pixels,
                "Empty alpha coverage changed the destination.");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
