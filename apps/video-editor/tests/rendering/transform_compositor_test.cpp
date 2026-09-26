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
        std::vector<std::uint64_t> layer_elapsed_nanoseconds;
        const auto composed = rendering::FrameCompositor::compose(
            4,
            4,
            layers,
            &layer_elapsed_nanoseconds);
        require(composed.has_value() && composed->width == 4 && composed->height == 4,
                "The compositor did not create the expected canvas.");
        require(layer_elapsed_nanoseconds.size() == layers.size(),
                "The compositor did not report one timing for each Preview layer.");
        require(composed->rgba_pixels[0] > 100 && composed->rgba_pixels[1] > 100,
                "The compositor did not blend alpha layers.");

        const auto opaque_full = fullFrame(31, 63, 127);
        const auto direct = rendering::FrameCompositor::compose(
            4,
            4,
            std::vector<rendering::CompositionLayer>{{&opaque_full, identity}});
        require(direct.has_value() && direct->rgba_pixels == opaque_full.rgba_pixels,
                "The opaque identity composition changed the source pixels.");

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
        const auto optimized = rendering::FrameCompositor::compose(
            32,
            24,
            std::vector<rendering::CompositionLayer>{{
                &sparse,
                text_transform,
                coverage}});
        const auto general = rendering::FrameCompositor::compose(
            32,
            24,
            std::vector<rendering::CompositionLayer>{{&sparse, text_transform}});
        require(optimized.has_value() && general.has_value() &&
                    optimized->rgba_pixels == general->rgba_pixels,
                "The alpha coverage fast path changed composed pixels.");

        auto rotated = text_transform;
        rotated.rotation_degrees = 15.0;
        require(!rendering::FrameCompositor::canUseAlphaCoverageFastPath(
                    rendering::CompositionLayer{&sparse, rotated, coverage}),
                "A rotated alpha coverage layer incorrectly used the fast path.");
        const auto rotated_with_coverage = rendering::FrameCompositor::compose(
            32,
            24,
            std::vector<rendering::CompositionLayer>{{
                &sparse,
                rotated,
                coverage}});
        const auto rotated_general = rendering::FrameCompositor::compose(
            32,
            24,
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
            32,
            24,
            std::vector<rendering::CompositionLayer>{{
                &empty,
                text_transform,
                empty_coverage}});
        const auto empty_general = rendering::FrameCompositor::compose(32, 24, {});
        require(empty_composed.has_value() && empty_general.has_value() &&
                    empty_composed->rgba_pixels == empty_general->rgba_pixels,
                "Empty alpha coverage changed the destination.");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
