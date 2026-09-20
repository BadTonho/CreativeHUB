#pragma once

#include "../media/video_metadata.h"

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace timeline {

struct TimelineClip {
    std::int64_t timeline_start_frame = 0;
    std::int64_t timeline_duration_frames = 0;
    std::filesystem::path source_path;
    std::string display_name;
    std::optional<double> duration_seconds;
    std::optional<double> frame_rate;
    std::optional<std::int64_t> frame_count;
};

enum class AddClipResult {
    Added,
    InvalidTimingMetadata,
};

class TimelineModel final {
public:
    AddClipResult addClip(const media::VideoMetadata& metadata);
    void clear() noexcept;

    [[nodiscard]] bool hasClip() const noexcept;
    [[nodiscard]] std::size_t clipCount() const noexcept;
    [[nodiscard]] std::int64_t totalDurationFrames() const noexcept;
    [[nodiscard]] const std::vector<TimelineClip>& clips() const noexcept;
    [[nodiscard]] std::optional<std::size_t> firstClipIndexForSource(
        const std::filesystem::path& source_path) const;

private:
    std::vector<TimelineClip> clips_;
};

} // namespace timeline
