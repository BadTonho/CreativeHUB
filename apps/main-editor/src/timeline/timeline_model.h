#pragma once

#include "../media/video_metadata.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace timeline {

using TrackId = std::uint64_t;
using ClipId = std::uint64_t;

struct TimelineClip {
    std::int64_t timeline_start_frame = 0;
    std::int64_t source_start_frame = 0;
    std::int64_t timeline_duration_frames = 0;
    std::filesystem::path source_path;
    std::string display_name;
    std::optional<double> duration_seconds;
    std::optional<double> frame_rate;
    std::optional<std::int64_t> frame_count;
    ClipId clip_id = 0;
    TrackId track_id = 0;

    friend bool operator==(const TimelineClip&, const TimelineClip&) = default;
};

struct TimelineTrack {
    TrackId track_id = 0;
    std::string name;
    std::vector<TimelineClip> clips;

    friend bool operator==(const TimelineTrack&, const TimelineTrack&) = default;
};

struct ClipLocation {
    std::size_t track_index = 0;
    std::size_t clip_index = 0;

    friend bool operator==(const ClipLocation&, const ClipLocation&) = default;
};

enum class AddTrackResult { Added, InvalidName };

enum class TrackMutationResult {
    Changed,
    InvalidIndex,
    InvalidName,
    NotEmpty,
    NoChange,
};

enum class AddClipResult {
    Added,
    InvalidTrack,
    InvalidTimingMetadata,
    InvalidPosition,
    Overlap,
};

enum class MoveClipResult {
    Moved,
    NoChange,
    InvalidIndex,
    InvalidTrack,
    InvalidPosition,
    Overlap,
};

enum class SplitClipResult { Split, InvalidIndex, InvalidBoundary };
enum class RemoveClipResult { Removed, InvalidIndex };
enum class TrimClipResult { Trimmed, InvalidIndex, InvalidRange };

class TimelineModel final {
public:
    struct Snapshot {
        std::vector<TimelineTrack> tracks;
        // Kept temporarily for source compatibility with the project loader
        // while callers migrate to the multi-track representation.
        std::vector<TimelineClip> clips;
        TrackId next_track_id = 1;
        ClipId next_clip_id = 1;

        friend bool operator==(const Snapshot&, const Snapshot&) = default;
    };

    TimelineModel();

    AddTrackResult addTrack(std::string name);
    TrackMutationResult renameTrack(std::size_t track_index, std::string name);
    TrackMutationResult moveTrack(std::size_t from_index, std::size_t to_index);
    TrackMutationResult removeTrack(std::size_t track_index);

    AddClipResult addClip(
        std::size_t track_index,
        const media::VideoMetadata& metadata,
        std::int64_t timeline_start_frame);
    MoveClipResult moveClip(
        ClipLocation from,
        ClipLocation to,
        std::int64_t timeline_start_frame);
    SplitClipResult splitClip(
        std::size_t track_index,
        std::size_t clip_index,
        std::int64_t local_frame);
    RemoveClipResult removeClip(std::size_t track_index, std::size_t clip_index);
    TrimClipResult trimClip(
        std::size_t track_index,
        std::size_t clip_index,
        std::int64_t new_source_start_frame,
        std::int64_t new_duration_frames);

    // Compatibility helpers for the original single-track API.
    AddClipResult addClip(const media::VideoMetadata& metadata);
    MoveClipResult moveClip(std::size_t from_index, std::size_t to_index);
    SplitClipResult splitClip(std::size_t clip_index, std::int64_t local_frame);
    RemoveClipResult removeClip(std::size_t clip_index);
    TrimClipResult trimClip(
        std::size_t clip_index,
        std::int64_t new_source_start_frame,
        std::int64_t new_duration_frames);

    void clear() noexcept;

    [[nodiscard]] bool hasClip() const noexcept;
    [[nodiscard]] std::size_t clipCount() const noexcept;
    [[nodiscard]] std::size_t clipCount(std::size_t track_index) const noexcept;
    [[nodiscard]] std::size_t trackCount() const noexcept;
    [[nodiscard]] std::int64_t totalDurationFrames() const noexcept;
    [[nodiscard]] const std::vector<TimelineTrack>& tracks() const noexcept;
    [[nodiscard]] const std::vector<TimelineClip>& clips() const noexcept;
    [[nodiscard]] std::optional<std::size_t> firstClipIndexForSource(
        const std::filesystem::path& source_path) const;
    [[nodiscard]] std::optional<ClipLocation> clipAt(
        std::size_t track_index,
        std::int64_t timeline_frame) const;
    [[nodiscard]] std::optional<ClipLocation> topClipAt(
        std::int64_t timeline_frame) const;
    [[nodiscard]] std::optional<ClipLocation> locateClip(ClipId clip_id) const;
    [[nodiscard]] Snapshot snapshot() const;
    void restore(Snapshot snapshot);
    void updateDisplayNameForSource(
        const std::filesystem::path& source_path,
        const std::string& display_name);

private:
    [[nodiscard]] static std::optional<std::int64_t> durationInFrames(
        const media::VideoMetadata& metadata);
    [[nodiscard]] static std::filesystem::path canonicalPath(
        const std::filesystem::path& path);
    [[nodiscard]] static bool validName(const std::string& name) noexcept;
    [[nodiscard]] static bool overlaps(
        const TimelineClip& left,
        std::int64_t start_frame,
        std::int64_t duration_frames) noexcept;
    [[nodiscard]] static std::int64_t trackEnd(
        const TimelineTrack& track) noexcept;
    [[nodiscard]] TimelineTrack* trackAt(std::size_t track_index) noexcept;
    [[nodiscard]] const TimelineTrack* trackAt(std::size_t track_index) const noexcept;
    void ensureIdentifiers();

    std::vector<TimelineTrack> tracks_;
    TrackId next_track_id_ = 1;
    ClipId next_clip_id_ = 1;
};

} // namespace timeline
