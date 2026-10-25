#pragma once

#include "../media/video_metadata.h"
#include "timeline_frame_rate.h"
#include "timeline_transform.h"
#include "text_style.h"
#include "../workspaces/color/model/color_graph.h"
#include "workspaces/fusion/nodes/model/node_graph.h"
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
#include <variant>
#include <vector>

namespace application {
class EditorSession;
}

namespace timeline {

using TrackId = std::uint64_t;
using ClipId = std::uint64_t;
using AudioBusId = std::uint64_t;

enum class AudioSendTap { PreFader, PostFader };

struct AudioSend {
    AudioBusId destination_bus_id = 0;
    double gain = 1.0;
    AudioSendTap tap = AudioSendTap::PostFader;

    friend bool operator==(const AudioSend&, const AudioSend&) = default;
};

struct AudioEqualizerParameters {
    double low_shelf_frequency_hz = 120.0;
    double low_shelf_gain_db = 0.0;
    double mid_frequency_hz = 1000.0;
    double mid_gain_db = 0.0;
    double mid_q = 1.0;
    double high_shelf_frequency_hz = 8000.0;
    double high_shelf_gain_db = 0.0;

    friend bool operator==(const AudioEqualizerParameters&,
                           const AudioEqualizerParameters&) = default;
};

struct AudioCompressorParameters {
    double threshold_db = -18.0;
    double ratio = 4.0;
    double attack_ms = 10.0;
    double release_ms = 100.0;
    double makeup_gain_db = 0.0;

    friend bool operator==(const AudioCompressorParameters&,
                           const AudioCompressorParameters&) = default;
};

using AudioEffectParameters = std::variant<
    AudioEqualizerParameters, AudioCompressorParameters>;

struct AudioEffectInstance {
    AudioEffectParameters parameters = AudioEqualizerParameters{};
    bool enabled = false;

    friend bool operator==(const AudioEffectInstance&,
                           const AudioEffectInstance&) = default;
};

[[nodiscard]] inline bool validAudioEffectChain(
    const std::vector<AudioEffectInstance>& effects) noexcept {
    if (effects.size() > 2) return false;
    bool has_equalizer = false;
    bool has_compressor = false;
    for (const auto& effect : effects) {
        if (const auto* eq = std::get_if<AudioEqualizerParameters>(
                &effect.parameters)) {
            if (has_equalizer ||
                !std::isfinite(eq->low_shelf_frequency_hz) ||
                eq->low_shelf_frequency_hz < 20.0 ||
                eq->low_shelf_frequency_hz > 500.0 ||
                !std::isfinite(eq->low_shelf_gain_db) ||
                eq->low_shelf_gain_db < -18.0 ||
                eq->low_shelf_gain_db > 18.0 ||
                !std::isfinite(eq->mid_frequency_hz) ||
                eq->mid_frequency_hz < 20.0 ||
                eq->mid_frequency_hz > 20000.0 ||
                !std::isfinite(eq->mid_gain_db) ||
                eq->mid_gain_db < -18.0 || eq->mid_gain_db > 18.0 ||
                !std::isfinite(eq->mid_q) || eq->mid_q < 0.1 ||
                eq->mid_q > 10.0 ||
                !std::isfinite(eq->high_shelf_frequency_hz) ||
                eq->high_shelf_frequency_hz < 500.0 ||
                eq->high_shelf_frequency_hz > 20000.0 ||
                !std::isfinite(eq->high_shelf_gain_db) ||
                eq->high_shelf_gain_db < -18.0 ||
                eq->high_shelf_gain_db > 18.0) {
                return false;
            }
            has_equalizer = true;
        } else if (const auto* compressor = std::get_if<
                       AudioCompressorParameters>(&effect.parameters)) {
            if (has_compressor ||
                !std::isfinite(compressor->threshold_db) ||
                compressor->threshold_db < -60.0 ||
                compressor->threshold_db > 0.0 ||
                !std::isfinite(compressor->ratio) ||
                compressor->ratio < 1.0 || compressor->ratio > 20.0 ||
                !std::isfinite(compressor->attack_ms) ||
                compressor->attack_ms < 0.1 ||
                compressor->attack_ms > 200.0 ||
                !std::isfinite(compressor->release_ms) ||
                compressor->release_ms < 10.0 ||
                compressor->release_ms > 1000.0 ||
                !std::isfinite(compressor->makeup_gain_db) ||
                compressor->makeup_gain_db < 0.0 ||
                compressor->makeup_gain_db > 24.0) {
                return false;
            }
            has_compressor = true;
        } else {
            return false;
        }
    }
    return true;
}

struct AudioBus {
    AudioBusId bus_id = 0;
    std::string name;
    double audio_gain = 1.0;
    bool audio_muted = false;
    double audio_pan = 0.0;

    friend bool operator==(const AudioBus&, const AudioBus&) = default;
};

struct MasterAudioMix {
    double audio_gain = 1.0;
    bool audio_muted = false;

    friend bool operator==(const MasterAudioMix&, const MasterAudioMix&) = default;
};

enum class ClipKind {
    Video,
    Image,
    Text,
    Audio,
    Adjustment,
    FusionComposition,
};

[[nodiscard]] constexpr bool isGeneratedClipKind(ClipKind kind) noexcept {
    return kind == ClipKind::Text || kind == ClipKind::Adjustment || kind == ClipKind::FusionComposition;
}

[[nodiscard]] constexpr bool supportsClipEffects(ClipKind kind) noexcept {
    return kind == ClipKind::Video || kind == ClipKind::Image || kind == ClipKind::Adjustment || kind == ClipKind::FusionComposition;
}

[[nodiscard]] constexpr bool supportsNodeGraph(ClipKind kind) noexcept {
    return kind == ClipKind::Video || kind == ClipKind::Image || kind == ClipKind::FusionComposition;
}

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

struct AudioGainKeyframe {
    std::int64_t frame = 0;
    double gain = 1.0;

    friend bool operator==(const AudioGainKeyframe&, const AudioGainKeyframe&) = default;
};

struct ColorGradeVersion {
    std::string id;
    std::string name;
    color::Graph graph;

    friend bool operator==(const ColorGradeVersion&, const ColorGradeVersion&) = default;
};

[[nodiscard]] inline bool validColorGradeVersions(
    const std::vector<ColorGradeVersion>& versions) {
    for (std::size_t index = 0; index < versions.size(); ++index) {
        const auto& version = versions[index];
        if (version.id.empty() ||
            version.name.find_first_not_of(" \t\r\n") == std::string::npos ||
            !color::validate(version.graph)) {
            return false;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (versions[previous].id == version.id ||
                versions[previous].name == version.name) {
                return false;
            }
        }
    }
    return true;
}

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
    // Canonical Color correction graph. `effects` is a compatibility projection
    // used by existing Edit controls and linear GPU effect paths.
    std::optional<color::Graph> color_graph;
    std::vector<ColorGradeVersion> color_grade_versions;
    std::optional<fusion::nodes::NodeGraph> node_graph;
    ClipId clip_id = 0;
    TrackId track_id = 0;
    Transform2D transform;
    TransformKeyframes keyframes;
    ClipKind kind = ClipKind::Video;
    TextStyle text;
    std::optional<media::LinkedImageReference> image_editor_variant;
    std::optional<media::MotionLinkReference> motion_link;
    std::shared_ptr<const media::VideoFrame> still_image_override;
    std::int64_t source_duration_frames = 0;
    bool source_duration_migration_pending = false;
    std::int64_t source_start_time_us = 0;
    std::int64_t source_duration_time_us = 0;
    std::optional<ClipId> linked_clip_id;
    bool audio_extracted = false;
    bool audio_companion_pending = false;
    std::int64_t composition_input_offset_frames = 0;

    friend bool operator==(const TimelineClip&, const TimelineClip&) = default;
};

[[nodiscard]] inline color::Graph colorGraphForClip(const TimelineClip& clip) {
    return clip.color_graph.value_or(color::fromEffectStack(clip.effects));
}

// A clipboard snapshot of editable clip attributes. Timeline identity, timing,
// media source, track membership, transitions, and linkage are deliberately
// excluded so attribute paste cannot duplicate or reposition a clip.
struct TimelineClipAttributes {
    ClipKind source_kind = ClipKind::Video;
    Transform2D transform;
    TransformKeyframes transform_keyframes;
    std::vector<creative_suite::effects::EffectInstance> effects;
    std::optional<color::Graph> color_graph;
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
    double audio_pan = 0.0;
    std::optional<AudioBusId> output_bus_id;
    std::vector<AudioSend> audio_sends;
    std::vector<AudioEffectInstance> audio_effects;

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
enum class AudioBusMutationResult {
    Changed,
    InvalidTarget,
    InvalidName,
    InvalidValue,
    NoChange,
};
enum class TransformParameterResult { Changed, InvalidIndex, InvalidValue, NoChange };
enum class TextParameterResult { Changed, InvalidIndex, InvalidValue, NoChange };
enum class EffectMutationResult { Changed, InvalidIndex, InvalidValue, IncompatibleClip, NoChange };
enum class NodeGraphMutationResult { Changed, InvalidIndex, InvalidValue, IncompatibleClip, NoChange };
enum class ColorGraphMutationResult { Changed, InvalidIndex, InvalidValue, IncompatibleClip, NoChange };
enum class ColorGradeVersionsMutationResult { Changed, InvalidIndex, InvalidValue, IncompatibleClip, NoChange };
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
        std::vector<AudioBus> audio_buses;
        MasterAudioMix master_audio_mix;
        AudioBusId next_audio_bus_id = 1;

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
    AddClipResult addAdjustmentClip(
        std::size_t track_index, std::int64_t timeline_start_frame,
        std::int64_t duration_frames);
    AddClipResult addFusionComposition(std::size_t track_index,
        std::int64_t timeline_start_frame, std::int64_t duration_frames);
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
    AudioParameterResult setTrackAudioMix(
        TrackId track_id,
        double gain,
        bool muted,
        double pan,
        std::optional<AudioBusId> output_bus_id,
        std::vector<AudioSend> sends);
    AudioParameterResult setTrackAudioEffects(
        TrackId track_id,
        std::vector<AudioEffectInstance> effects);
    AudioParameterResult setAudioBusMix(
        AudioBusId bus_id,
        double gain,
        bool muted,
        double pan);
    AudioParameterResult setMasterAudioMix(double gain, bool muted);
    AudioBusMutationResult addAudioBus(
        std::string name,
        AudioBusId* created_bus_id = nullptr);
    AudioBusMutationResult renameAudioBus(AudioBusId bus_id, std::string name);
    AudioBusMutationResult removeAudioBus(AudioBusId bus_id);
    [[nodiscard]] const std::vector<AudioBus>& audioBuses() const noexcept;
    [[nodiscard]] const MasterAudioMix& masterAudioMix() const noexcept;
    [[nodiscard]] std::optional<std::size_t> locateAudioBus(
        AudioBusId bus_id) const noexcept;
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
    ColorGraphMutationResult setClipColorGraph(
        std::size_t track_index,
        std::size_t clip_index,
        color::Graph graph);
    ColorGradeVersionsMutationResult setClipColorGradeVersions(
        std::size_t track_index,
        std::size_t clip_index,
        std::vector<ColorGradeVersion> versions);
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
    [[nodiscard]] bool setMotionLink(
        ClipId clip_id,
        std::optional<media::MotionLinkReference> link);
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
    [[nodiscard]] static bool validAudioPan(double pan) noexcept;
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
    std::vector<AudioBus> audio_buses_;
    MasterAudioMix master_audio_mix_;
    FrameRate frame_rate_;
    TrackId next_track_id_ = 1;
    ClipId next_clip_id_ = 1;
    AudioBusId next_audio_bus_id_ = 1;
};

} // namespace timeline
