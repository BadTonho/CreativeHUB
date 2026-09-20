#include "media/video_frame.h"
#include "rendering/frame_compositor.h"
#include "timeline/timeline_model.h"
#include "timeline/timeline_transform.h"

#include <cmath>
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
        require(model.clips()[0].transform.position_x == 0.25 &&
                    model.clips()[1].transform.position_x == 0.75,
                "Splitting changed the transform bases incorrectly.");
        require(model.clips()[1].source_start_frame == 10 &&
                    model.clips()[1].timeline_duration_frames == 10,
                "The split source offsets were incorrect.");

        require(model.trimClip(0, 1, 12, 6) == timeline::TrimClipResult::Trimmed,
                "The transform clip was not trimmed.");
        require(model.clips()[1].source_start_frame == 12 &&
                    model.clips()[1].timeline_duration_frames == 6,
                "The trimmed source range was incorrect.");

        const auto bottom = solid(255, 0, 0);
        const auto top = solid(0, 255, 0, 128);
        const std::vector<rendering::CompositionLayer> layers{
            {&bottom, identity},
            {&top, identity}};
        const auto composed = rendering::FrameCompositor::compose(4, 4, layers);
        require(composed.has_value() && composed->width == 4 && composed->height == 4,
                "The compositor did not create the expected canvas.");
        require(composed->rgba_pixels[0] > 100 && composed->rgba_pixels[1] > 100,
                "The compositor did not blend alpha layers.");

        const std::vector<rendering::CompositionLayer> reversed_layers{
            {&top, identity},
            {&bottom, identity}};
        const auto reversed = rendering::FrameCompositor::compose(4, 4, reversed_layers);
        require(reversed.has_value() && reversed->rgba_pixels[0] > 200 &&
                    reversed->rgba_pixels[1] < 20,
                "The compositor did not preserve layer order.");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
