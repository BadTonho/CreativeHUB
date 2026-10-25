#include "workspaces/color/evaluation/power_window_tracker.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

media::VideoFrame texturedFrame() {
    media::VideoFrame frame{160, 120, 640, std::vector<std::uint8_t>(160 * 120 * 4)};
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const double wave = 125.0 + 48.0 * std::sin(x * .19 + std::sin(y * .06)) *
                std::cos(y * .16) + 31.0 * std::sin((x + y * 1.7) * .075);
            const double tile = ((x / 13 + y / 11) % 2) ? 26.0 : -26.0;
            const auto value = static_cast<std::uint8_t>(std::clamp(
                std::lround(wave + tile), 0L, 255L));
            const auto offset = static_cast<std::size_t>(y) * frame.stride + x * 4;
            frame.rgba_pixels[offset] = value;
            frame.rgba_pixels[offset + 1] = static_cast<std::uint8_t>((value + x * 3) & 0xff);
            frame.rgba_pixels[offset + 2] = static_cast<std::uint8_t>((value + y * 5) & 0xff);
            frame.rgba_pixels[offset + 3] = 255;
        }
    }
    return frame;
}

media::VideoFrame transformed(const media::VideoFrame& source, double step) {
    media::VideoFrame result{source.width, source.height, source.stride,
        std::vector<std::uint8_t>(source.rgba_pixels.size(), 0)};
    const double scale = std::pow(1.025, step);
    const double angle = step * 1.4 * 3.14159265358979323846 / 180.0;
    const double c = std::cos(angle), s = std::sin(angle);
    for (int y = 0; y < result.height; ++y) for (int x = 0; x < result.width; ++x) {
        const double dx = x - source.width * .5 - step * 1.8;
        const double dy = y - source.height * .5 + step * .9;
        const double sx = (c * dx + s * dy) / scale + source.width * .5;
        const double sy = (-s * dx + c * dy) / scale + source.height * .5;
        const int ix = static_cast<int>(std::lround(sx));
        const int iy = static_cast<int>(std::lround(sy));
        if (ix < 0 || iy < 0 || ix >= source.width || iy >= source.height) continue;
        const auto from = static_cast<std::size_t>(iy) * source.stride + ix * 4;
        const auto to = static_cast<std::size_t>(y) * result.stride + x * 4;
        for (int channel = 0; channel < 4; ++channel)
            result.rgba_pixels[to + channel] = source.rgba_pixels[from + channel];
    }
    return result;
}

double average(const creative_suite::animation::ScalarKeyframes& keys) {
    return keys.size() > 1 ? (keys.back().value - keys.front().value) /
        static_cast<double>(keys.size() - 1) : 0.0;
}

}  // namespace

int main() {
    try {
        const auto base = texturedFrame();
        std::vector<media::VideoFrame> forward;
        std::vector<std::int64_t> forward_frames;
        for (int frame = 0; frame <= 4; ++frame) {
            forward.push_back(transformed(base, frame));
            forward_frames.push_back(frame);
        }
        creative_suite::composition::ShapeMaskParameters initial;
        initial.center_x = .5;
        initial.center_y = .5;
        initial.width = .55;
        initial.height = .60;
        const auto tracked = color::trackPowerWindow(forward, forward_frames,
            creative_suite::composition::ShapeMaskKind::Rectangle, initial);
        require(tracked && tracked->reached_end && tracked->tracked_frame_count == 5,
            "The tracker must follow the synthetic clip forward without losing confidence.");
        require(tracked->keyframes[0].size() == 5 && tracked->keyframes[1].size() == 5 &&
                tracked->keyframes[2].size() == 5,
            "A tracked transform must store position, scale and rotation per frame.");
        require(tracked->keyframes[0].back().value - tracked->keyframes[0].front().value > .025 &&
                tracked->keyframes[0].back().value > tracked->keyframes[0].front().value,
            "Forward tracking must follow horizontal movement.");
        require(tracked->keyframes[2].back().value > initial.width &&
                tracked->keyframes[4].back().value > initial.rotation,
            "Forward tracking must recover uniform scale and rotation.");

        std::vector<media::VideoFrame> backward(forward.rbegin(), forward.rend());
        std::vector<std::int64_t> backward_frames{4, 3, 2, 1, 0};
        const auto at_end = creative_suite::composition::evaluateShapeMask(
            initial, tracked->keyframes, 4);
        const auto reverse = color::trackPowerWindow(backward, backward_frames,
            creative_suite::composition::ShapeMaskKind::Rectangle, at_end);
        require(reverse && reverse->reached_end && reverse->last_valid_frame == 0,
            "The tracker must process the same synthetic motion backward.");
        require(std::abs(reverse->keyframes[0].back().value - initial.center_x) < .06 &&
                std::abs(reverse->keyframes[2].back().value - initial.width) < .08,
            "Backward tracking must return the geometry to its initial position and scale.");

        auto lost = forward;
        lost.back() = media::VideoFrame{160, 120, 640,
            std::vector<std::uint8_t>(160 * 120 * 4, 0)};
        const auto partial = color::trackPowerWindow(lost, forward_frames,
            creative_suite::composition::ShapeMaskKind::Rectangle, initial);
        require(partial && !partial->reached_end && partial->last_valid_frame < 4 &&
                partial->tracked_frame_count >= 1,
            "Low-confidence tracking must retain valid earlier frames and report where it stopped.");

        const auto canceled = color::trackPowerWindow(forward, forward_frames,
            creative_suite::composition::ShapeMaskKind::Rectangle, initial,
            [] { return true; });
        require(!canceled, "Cancellation must discard an incomplete tracker result.");
        int cancellation_polls = 0;
        const auto canceled_during_frame = color::trackPowerWindow(forward,
            forward_frames, creative_suite::composition::ShapeMaskKind::Rectangle,
            initial, [&cancellation_polls] { return ++cancellation_polls > 3; });
        require(!canceled_during_frame,
            "Cancellation during optical-flow processing must discard the partial result.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
