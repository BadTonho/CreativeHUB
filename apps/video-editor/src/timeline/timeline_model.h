#pragma once

#include "../media/video_metadata.h"
#include "timeline_transform.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace timeline {

using TrackId = std::uint64_t;
using ClipId = std::uint64_t;

enum class ClipKind {
    Video,
    Image,
    Text,
};

[[nodiscard]] constexpr bool isMediaClipKind(ClipKind kind) noexcept {
    return kind == ClipKind::Video || kind == ClipKind::Image;
}

enum class TransitionKind {
    CrossDissolve,
    FadeToBlack,
};

enum class TextAlignment {
    Left,
    Center,
    Right,
};

struct TextStyle {
    std::string content = "Text";
    std::string font_family = "Sans Serif";
    double font_size_pixels = 48.0;
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};
    TextAlignment alignment = TextAlignment::Center;

    friend bool operator==(const TextStyle&, const TextStyle&) = default;
};

struct TimelineClip {
    std::int64_t timeline_start_frame = 0;
    std::int64_t source_start_frame = 0;
    std::int64_t timeline_duration_frames = 0;
    std::filesystem::path source_path;
    std::string display_name;
    std::optional<double> duration_seconds;
    std::optional<double> frame_rate;
    std::optional<std::int64_t> frame_count;
    double audio_gain = 1.0;
    bool audio_muted = false;
    ClipId clip_id = 0;
    TrackId track_id = 0;
    Transform2D transform;
    TransformKeyframes keyframes;
    ClipKind kind = ClipKind::Video;
    TextStyle text;

    friend bool operator==(const TimelineClip&, const TimelineClip&) = default;
};

struct TimelineTransition {
    ClipId from_clip_id = 0;
    ClipId to_clip_id = 0;
    TransitionKind kind = TransitionKind::CrossDissolve;
    std::int64_t duration_frames = 15;

    friend bool operator==(const TimelineTransition&, const TimelineTransition&) = default;
};

struct TimelineTrack {
    TrackId track_id = 0;
    std::string name;
    double audio_gain = 1.0;
    bool audio_muted = false;
    std::vector<TimelineClip> clips;
    std::vector<TimelineTransition> transitions;

    friend bool operator==(const TimelineTrack&, const TimelineTrack&) = default;
};

struct ClipLocation {
    std::size_t track_index = 0;
    std::size_t clip_index = 0;

    friend bool operator==(const ClipLocation&, const ClipLocation&) = default;
};

enum class ClipEdge { Left, Right };
// Rolling edits resize both clips at a shared cut; individual edits preserve
// the neighbor and may overlap an adjacent media clip.
enum class ClipEdgeEditMode { Rolling, Individual };

struct ClipEdgeEditPreview {
    ClipLocation clip_location;
    TimelineClip clip;
    std::optional<ClipLocation> neighbor_location;
    std::optional<TimelineClip> neighbor_clip;
    std::int64_t boundary_frame = 0;
};

[[nodiscard]] std::optional<ClipEdgeEditPreview> previewClipEdgeEdit(
    const std::vector<TimelineTrack>& tracks,
    ClipLocation location,
    ClipEdge edge,
    std::int64_t boundary_frame,
    ClipEdgeEditMode mode = ClipEdgeEditMode::Rolling);

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
enum class TrimClipResult { Trimmed, NoChange, InvalidIndex, InvalidRange };
enum class AudioParameterResult { Changed, InvalidIndex, InvalidValue, NoChange };
enum class TransformParameterResult { Changed, InvalidIndex, InvalidValue, NoChange };
enum class TextParameterResult { Changed, InvalidIndex, InvalidValue, NoChange };
enum class TransitionMutationResult {
    Added,
    Updated,
    Removed,
    InvalidIndex,
    InvalidBoundary,
    InvalidRange,
    NotFound,
    NoChange,
};

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
    TrimClipResult trimClipEdge(
        std::size_t track_index,
        std::size_t clip_index,
        ClipEdge edge,
        std::int64_t boundary_frame,
        ClipEdgeEditMode mode = ClipEdgeEditMode::Rolling);

    // Compatibility helpers for the original single-track API.
    AddClipResult addClip(const media::VideoMetadata& metadata);
    AddClipResult addTextClip(
        std::size_t track_index,
        std::int64_t timeline_start_frame,
        std::int64_t duration_frames,
        double frame_rate = 30.0);
    MoveClipResult moveClip(std::size_t from_index, std::size_t to_index);
    SplitClipResult splitClip(std::size_t clip_index, std::int64_t local_frame);
    RemoveClipResult removeClip(std::size_t clip_index);
    TrimClipResult trimClip(
        std::size_t clip_index,
        std::int64_t new_source_start_frame,
        std::int64_t new_duration_frames);
    AudioParameterResult setClipAudio(
        std::size_t track_index,
        std::size_t clip_index,
        double gain,
        bool muted);
    AudioParameterResult setTrackAudio(
        std::size_t track_index,
        double gain,
        bool muted);
    TransformParameterResult setClipTransform(
        std::size_t track_index,
        std::size_t clip_index,
        const Transform2D& transform);
    TransformParameterResult setClipKeyframe(
        std::size_t track_index,
        std::size_t clip_index,
        TransformProperty property,
        std::int64_t local_frame,
        double value);
    TransformParameterResult removeClipKeyframe(
        std::size_t track_index,
        std::size_t clip_index,
        TransformProperty property,
        std::int64_t local_frame);
    TextParameterResult setClipText(
        std::size_t track_index,
        std::size_t clip_index,
        const TextStyle& text);
    TransitionMutationResult addTransition(
        std::size_t track_index,
        std::size_t from_clip_index,
        std::size_t to_clip_index,
        TransitionKind kind,
        std::int64_t duration_frames = 15);
    TransitionMutationResult updateTransition(
        std::size_t track_index,
        std::size_t from_clip_index,
        std::size_t to_clip_index,
        TransitionKind kind,
        std::int64_t duration_frames);
    TransitionMutationResult removeTransition(
        std::size_t track_index,
        std::size_t from_clip_index,
        std::size_t to_clip_index);

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
    [[nodiscard]] const TimelineTransition* transitionBetween(
        std::size_t track_index,
        std::size_t from_clip_index,
        std::size_t to_clip_index) const noexcept;
    [[nodiscard]] Snapshot snapshot() const;
    void restore(Snapshot snapshot);
    void updateDisplayNameForSource(
        const std::filesystem::path& source_path,
        const std::string& display_name);

    [[nodiscard]] static bool validAudioGain(double gain) noexcept;
    [[nodiscard]] static bool validTextStyle(const TextStyle& text) noexcept;

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
    [[nodiscard]] static bool overlapsSameKind(
        const TimelineClip& left,
        ClipKind kind,
        std::int64_t start_frame,
        std::int64_t duration_frames) noexcept;
    [[nodiscard]] static std::int64_t trackEnd(
        const TimelineTrack& track) noexcept;
    [[nodiscard]] static bool validTransitionKind(TransitionKind kind) noexcept;
    [[nodiscard]] static std::optional<std::pair<std::size_t, std::size_t>>
    transitionClipIndexes(
        const TimelineTrack& track,
        const TimelineTransition& transition) noexcept;
    static void removeInvalidTransitions(TimelineTrack& track) noexcept;
    [[nodiscard]] TimelineTrack* trackAt(std::size_t track_index) noexcept;
    [[nodiscard]] const TimelineTrack* trackAt(std::size_t track_index) const noexcept;
    void ensureIdentifiers();

    std::vector<TimelineTrack> tracks_;
    TrackId next_track_id_ = 1;
    ClipId next_clip_id_ = 1;
};

} // namespace timeline
