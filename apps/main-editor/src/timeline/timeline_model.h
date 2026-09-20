#pragma once

#include "../media/video_metadata.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace timeline {

struct TimelineClip {
    std::filesystem::path source_path;
    std::string display_name;
    std::optional<double> duration_seconds;
    std::optional<double> frame_rate;
    std::optional<std::int64_t> frame_count;
};

enum class AddClipResult {
    Added,
    AlreadyPresent,
    Occupied,
};

class TimelineModel final {
public:
    AddClipResult addClip(const media::VideoMetadata& metadata);
    void clear() noexcept;

    [[nodiscard]] bool hasClip() const noexcept;
    [[nodiscard]] const TimelineClip* clip() const noexcept;

private:
    std::optional<TimelineClip> clip_;
};

} // namespace timeline
