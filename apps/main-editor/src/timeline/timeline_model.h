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
    std::int64_t source_start_frame = 0;
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

enum class MoveClipResult {
    Moved,
    NoChange,
    InvalidIndex,
};

enum class SplitClipResult {
    Split,
    InvalidIndex,
    InvalidBoundary,
};

enum class RemoveClipResult {
    Removed,
    InvalidIndex,
};

enum class TrimClipResult {
    Trimmed,
    InvalidIndex,
    InvalidRange,
};

class TimelineModel final {
public:
    struct Snapshot {
        std::vector<TimelineClip> clips;
    };

    AddClipResult addClip(const media::VideoMetadata& metadata);
    MoveClipResult moveClip(std::size_t from_index, std::size_t to_index);
    SplitClipResult splitClip(
        std::size_t clip_index,
        std::int64_t local_frame);
    RemoveClipResult removeClip(std::size_t clip_index);
    TrimClipResult trimClip(
        std::size_t clip_index,
        std::int64_t new_source_start_frame,
        std::int64_t new_duration_frames);
    void clear() noexcept;

    [[nodiscard]] bool hasClip() const noexcept;
    [[nodiscard]] std::size_t clipCount() const noexcept;
    [[nodiscard]] std::int64_t totalDurationFrames() const noexcept;
    [[nodiscard]] const std::vector<TimelineClip>& clips() const noexcept;
    [[nodiscard]] std::optional<std::size_t> firstClipIndexForSource(
        const std::filesystem::path& source_path) const;
    [[nodiscard]] Snapshot snapshot() const;
    void restore(Snapshot snapshot);
    void updateDisplayNameForSource(
        const std::filesystem::path& source_path,
        const std::string& display_name);

private:
    std::vector<TimelineClip> clips_;
};

} // namespace timeline
