#pragma once

#include <creative_suite/animation/animation.h>
#include <creative_suite/composition/shape_mask.h>
#include "media/video_frame.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace color {

struct PowerWindowTrackResult {
    creative_suite::composition::ShapeMaskKeyframes keyframes;
    std::int64_t last_valid_frame = 0;
    double confidence = 0.0;
    std::size_t tracked_frame_count = 0;
    bool reached_end = false;
};

class PowerWindowTracker final {
public:
    PowerWindowTracker();
    ~PowerWindowTracker();
    PowerWindowTracker(PowerWindowTracker&&) noexcept;
    PowerWindowTracker& operator=(PowerWindowTracker&&) noexcept;
    PowerWindowTracker(const PowerWindowTracker&) = delete;
    PowerWindowTracker& operator=(const PowerWindowTracker&) = delete;

    [[nodiscard]] bool begin(const media::VideoFrame& first_frame,
        std::int64_t frame_number, creative_suite::composition::ShapeMaskKind kind,
        const creative_suite::composition::ShapeMaskParameters& initial_geometry);
    // Returns false when confidence falls below the accepted tracking threshold.
    [[nodiscard]] bool advance(const media::VideoFrame& frame,
        std::int64_t frame_number, const std::function<bool()>& should_cancel = {});
    [[nodiscard]] PowerWindowTrackResult result(bool reached_end = false) const;
    [[nodiscard]] bool active() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

// Tracks translation, uniform scale and rotation through a sequence of source
// frames. Frames and frame_numbers must have matching lengths and adjacent
// frame numbers must differ by exactly one in either direction.
[[nodiscard]] std::optional<PowerWindowTrackResult> trackPowerWindow(
    std::span<const media::VideoFrame> frames,
    std::span<const std::int64_t> frame_numbers,
    creative_suite::composition::ShapeMaskKind kind,
    const creative_suite::composition::ShapeMaskParameters& initial_geometry,
    const std::function<bool()>& should_cancel = {});

}  // namespace color
