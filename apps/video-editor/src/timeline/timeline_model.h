#pragma once

#include "../media/video_metadata.h"
#include "timeline_frame_rate.h"
#include "timeline_transform.h"
#include "fusion/nodes/model/node_graph.h"
#include <creative_suite/effects/effects.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace application {
class EditorSession;
}

namespace timeline {

using TrackId = std::uint64_t;
using ClipId = std::uint64_t;

enum class ClipKind {
    Video,
    Image,
    Text,
    Audio,
};

enum class TrackKind {
    Video,
    Audio,
};

[[nodiscard]] constexpr bool isMediaClipKind(ClipKind kind) noexcept {
    return kind == ClipKind::Video || kind == ClipKind::Image ||
        kind == ClipKind::Audio;
}

[[nodiscard]] constexpr bool isFrameTimedMediaClipKind(ClipKind kind) noexcept {
    return kind == ClipKind::Video || kind == ClipKind::Image;
}

enum class TransitionKind {
    CrossDissolve,
    FadeToBlack,
    AudioCrossfade,
};

[[nodiscard]] constexpr bool isOverlapTransition(TransitionKind kind) noexcept {
    return kind == TransitionKind::CrossDissolve ||
        kind == TransitionKind::AudioCrossfade;
}

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

struct AudioGainKeyframe {
    std::int64_t frame = 0;
    double gain = 1.0;

    friend bool operator==(const AudioGainKeyframe&, const AudioGainKeyframe&) = default;
};

[[nodiscard]] inline double evaluateAudioGainEnvelope(
    const std::vector<AudioGainKeyframe>& keyframes,
    double local_frame) noexcept {
    if (keyframes.empty() || !std::isfinite(local_frame)) return 1.0;
    if (local_frame <= static_cast<double>(keyframes.front().frame)) {
        return keyframes.front().gain;
    }
    for (std::size_t index = 1; index < keyframes.size(); ++index) {
        const auto& left = keyframes[index - 1];
        const auto& right = keyframes[index];
        if (local_frame <= static_cast<double>(right.frame)) {
            const auto duration = static_cast<double>(right.frame - left.frame);
            if (duration <= 0.0) return right.gain;
            const auto fraction = std::clamp(
                (local_frame - static_cast<double>(left.frame)) / duration,
                0.0, 1.0);
            return left.gain + (right.gain - left.gain) * fraction;
        }
    }
    return keyframes.back().gain;
}

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
    std::vector<AudioGainKeyframe> audio_gain_keyframes;
    std::vector<creative_suite::effects::EffectInstance> effects;
    std::optional<fusion::nodes::NodeGraph> node_graph;
    ClipId clip_id = 0;
    TrackId track_id = 0;
    Transform2D transform;
    TransformKeyframes keyframes;
    ClipKind kind = ClipKind::Video;
    TextStyle text;
    std::optional<media::LinkedImageReference> image_editor_variant;
    std::shared_ptr<const media::VideoFrame> still_image_override;
    std::int64_t source_duration_frames = 0;
    bool source_duration_migration_pending = false;
    std::int64_t source_start_time_us = 0;
    std::int64_t source_duration_time_us = 0;
    std::optional<ClipId> linked_clip_id;
    bool audio_extracted = false;
    bool audio_companion_pending = false;

    friend bool operator==(const TimelineClip&, const TimelineClip&) = default;
};

// A clipboard snapshot of editable clip attributes. Timeline identity, timing,
// media source, track membership, transitions, and linkage are deliberately
// excluded so attribute paste cannot duplicate or reposition a clip.
struct TimelineClipAttributes {
    ClipKind source_kind = ClipKind::Video;
    Transform2D transform;
    TransformKeyframes transform_keyframes;
    std::vector<creative_suite::effects::EffectInstance> effects;
    double audio_gain = 1.0;
    bool audio_muted = false;
    std::optional<std::vector<AudioGainKeyframe>> audio_volume_envelope;
    TextStyle text;
};

struct ClipAttributeOptions {
    bool effects = false;
    bool transform = false;
    bool audio_gain_and_mute = false;
    bool audio_volume_envelope = false;
    bool text = false;

    friend bool operator==(const ClipAttributeOptions&, const ClipAttributeOptions&) = default;
};

struct ClipAttributeCompatibility {
    bool effects = false;
    bool transform = false;
    bool audio_gain_and_mute = false;
    bool audio_volume_envelope = false;
    bool text = false;
};

[[nodiscard]] ClipAttributeCompatibility clipAttributeCompatibility(
    const TimelineClipAttributes& source,
    ClipKind target_kind,
    bool target_has_audio_volume_envelope) noexcept;

struct TimelineTransition {
    ClipId from_clip_id = 0;
    ClipId to_clip_id = 0;
    TransitionKind kind = TransitionKind::CrossDissolve;
    std::int64_t duration_frames = 15;

    friend bool operator==(const TimelineTransition&, const TimelineTransition&) = default;
};

struct TransitionSelection {
    TrackId track_id = 0;
    ClipId from_clip_id = 0;
    ClipId to_clip_id = 0;

    friend bool operator==(const TransitionSelection&, const TransitionSelection&) = default;
};

struct TimelineTrack {
    TrackId track_id = 0;
    std::string name;
    double audio_gain = 1.0;
    bool audio_muted = false;
    std::vector<TimelineClip> clips;
    std::vector<TimelineTransition> transitions;
    TrackKind kind = TrackKind::Video;

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
    ClipEdgeEditMode mode = ClipEdgeEditMode::Rolling,
    FrameRate timeline_frame_rate = {});

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
    IncompatibleTrack,
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
struct RippleDeleteOutcome {
    std::vector<ClipId> removed_clip_ids;
    std::vector<ClipId> moved_clip_ids;
    std::vector<TrackId> affected_track_ids;
    bool stopped_at_collision = false;
};
enum class TrimClipResult { Trimmed, NoChange, InvalidIndex, InvalidRange };
enum class AudioParameterResult { Changed, InvalidIndex, InvalidValue, NoChange };
enum class TransformParameterResult { Changed, InvalidIndex, InvalidValue, NoChange };
enum class TextParameterResult { Changed, InvalidIndex, InvalidValue, NoChange };
enum class EffectMutationResult { Changed, InvalidIndex, InvalidValue, IncompatibleClip, NoChange };
enum class NodeGraphMutationResult { Changed, InvalidIndex, InvalidValue, IncompatibleClip, NoChange };
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

enum class PendingMediaTimingMigrationResult {
    NoPendingClips,
    Migrated,
    InvalidMetadata,
    SourceRangeOutOfBounds,
    TimelineRangeOverflow,
};

class TimelineModel final {
public:
    struct Snapshot {
        std::vector<TimelineTrack> tracks;
        TrackId next_track_id = 1;
        ClipId next_clip_id = 1;
        FrameRate frame_rate;

        friend bool operator==(const Snapshot&, const Snapshot&) = default;
    };

    TimelineModel();

    AddTrackResult addTrack(std::string name, TrackKind kind = TrackKind::Video);
    TrackMutationResult renameTrack(std::size_t track_index, std::string name);
    TrackMutationResult moveTrack(std::size_t from_index, std::size_t to_index);
    TrackMutationResult removeTrack(std::size_t track_index);

    AddClipResult addClip(
        std::size_t track_index,
        const media::VideoMetadata& metadata,
        std::int64_t timeline_start_frame);
    AddClipResult addAudioCompanion(
        ClipId video_clip_id,
        const media::VideoMetadata& metadata,
        ClipId* audio_clip_id = nullptr);
    [[nodiscard]] bool linkAudio(ClipId video_clip_id, ClipId audio_clip_id);
    [[nodiscard]] bool unlinkAudio(ClipId clip_id);
    [[nodiscard]] PendingMediaTimingMigrationResult migratePendingMediaTiming(
        const std::filesystem::path& source_path,
        const media::VideoMetadata& metadata);
    MoveClipResult moveClip(
        ClipLocation from,
        ClipLocation to,
        std::int64_t timeline_start_frame);
    SplitClipResult splitClip(
        std::size_t track_index,
        std::size_t clip_index,
        std::int64_t local_frame);
    RemoveClipResult removeClip(std::size_t track_index, std::size_t clip_index);
    [[nodiscard]] std::optional<RippleDeleteOutcome> rippleDeleteClip(ClipId clip_id);
    TrimClipResult trimClip(
        std::size_t track_index,
        std::size_t clip_index,
        std::int64_t new_source_start_frame,
        std::int64_t new_duration_frames,
        std::optional<std::int64_t> new_source_start_time_us = std::nullopt);
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
    AudioParameterResult setClipAudioGainKeyframes(
        std::size_t track_index,
        std::size_t clip_index,
        std::vector<AudioGainKeyframe> keyframes);
    [[nodiscard]] static bool validAudioGainKeyframes(
        const std::vector<AudioGainKeyframe>& keyframes,
        std::int64_t duration_frames) noexcept {
        if (duration_frames <= 0) return keyframes.empty();
        std::int64_t previous = -1;
        for (const auto& keyframe : keyframes) {
            if (keyframe.frame < 0 || keyframe.frame > duration_frames ||
                keyframe.frame <= previous || !std::isfinite(keyframe.gain) ||
                keyframe.gain < 0.0 || keyframe.gain > 2.0) {
                return false;
            }
            previous = keyframe.frame;
        }
        return true;
    }
    TransformParameterResult setClipTransform(
        std::size_t track_index,
        std::size_t clip_index,
        const Transform2D& transform);
    TransformParameterResult setClipTransformAttributes(
        std::size_t track_index,
        std::size_t clip_index,
        const Transform2D& transform,
        TransformKeyframes keyframes);
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
    EffectMutationResult setClipEffects(
        std::size_t track_index,
        std::size_t clip_index,
        std::vector<creative_suite::effects::EffectInstance> effects);
    NodeGraphMutationResult setClipNodeGraph(
        std::size_t track_index,
        std::size_t clip_index,
        std::optional<fusion::nodes::NodeGraph> graph);
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
    [[nodiscard]] FrameRate frameRate() const noexcept;
    [[nodiscard]] const std::vector<TimelineTrack>& tracks() const noexcept;
    [[nodiscard]] bool setImageEditorVariant(
        ClipId clip_id,
        std::optional<media::LinkedImageReference> link);
    [[nodiscard]] bool setStillImageOverride(
        ClipId clip_id,
        std::shared_ptr<const media::VideoFrame> frame);
    [[nodiscard]] std::optional<std::size_t> firstClipIndexForSource(
        const std::filesystem::path& source_path) const;
    [[nodiscard]] std::optional<ClipLocation> clipAt(
        std::size_t track_index,
        std::int64_t timeline_frame) const;
    [[nodiscard]] std::optional<ClipLocation> topClipAt(
        std::int64_t timeline_frame) const;
    [[nodiscard]] std::optional<std::size_t> locateTrack(TrackId track_id) const;
    [[nodiscard]] std::optional<ClipLocation> locateClip(ClipId clip_id) const;
    [[nodiscard]] const TimelineTransition* transitionBetween(
        std::size_t track_index,
        std::size_t from_clip_index,
        std::size_t to_clip_index) const noexcept;
    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] static std::optional<Snapshot> rescaleSnapshotFrameRate(
        const Snapshot& snapshot,
        FrameRate target_frame_rate);
    void restore(Snapshot snapshot);
    void updateDisplayNameForSource(
        const std::filesystem::path& source_path,
        const std::string& display_name);

    [[nodiscard]] static bool validAudioGain(double gain) noexcept;
    [[nodiscard]] static bool validTextStyle(const TextStyle& text) noexcept;

private:
    friend class application::EditorSession;

    [[nodiscard]] static std::optional<std::int64_t> sourceDurationInFrames(
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
    bool removeOverlappingTransitionsForClip(
        std::size_t track_index,
        ClipId clip_id);
    static void removeInvalidTransitions(TimelineTrack& track) noexcept;
    [[nodiscard]] TimelineTrack* trackAt(std::size_t track_index) noexcept;
    [[nodiscard]] const TimelineTrack* trackAt(std::size_t track_index) const noexcept;
    void ensureIdentifiers();
    void assertIdentityInvariants() const;

    std::vector<TimelineTrack> tracks_;
    FrameRate frame_rate_;
    TrackId next_track_id_ = 1;
    ClipId next_clip_id_ = 1;
};

} // namespace timeline
