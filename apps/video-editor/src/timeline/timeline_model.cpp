#include "timeline_model.h"

#include "../media/still_image_decoder.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace timeline {
namespace {

std::optional<std::int64_t> sourceFrameCountFromMetadata(
    const media::VideoMetadata& metadata) {
    if (metadata.frame_count.has_value() && *metadata.frame_count > 0) {
        return *metadata.frame_count;
    }
    if (!metadata.duration_seconds.has_value() ||
        !metadata.frame_rate.has_value() ||
        !std::isfinite(*metadata.duration_seconds) ||
        !std::isfinite(*metadata.frame_rate) ||
        *metadata.duration_seconds <= 0.0 ||
        *metadata.frame_rate <= 0.0) {
        return std::nullopt;
    }
    const double estimated = *metadata.duration_seconds * *metadata.frame_rate;
    if (!std::isfinite(estimated) ||
        estimated > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(estimated)));
}

std::optional<std::int64_t> timelineFramesForAudioDuration(
    double duration_seconds,
    FrameRate timeline_rate) noexcept {
    if (!std::isfinite(duration_seconds) || duration_seconds <= 0.0 ||
        !validFrameRate(timeline_rate)) return std::nullopt;
    const long double frames = static_cast<long double>(duration_seconds) *
        timeline_rate.numerator / timeline_rate.denominator;
    const auto exclusive_max = std::ldexp(1.0L, 63);
    if (!std::isfinite(frames) || frames >= exclusive_max) return std::nullopt;
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(frames)));
}

std::optional<std::int64_t> timelineFramesToMicroseconds(
    std::int64_t frames,
    FrameRate timeline_rate) noexcept {
    if (frames < 0 || !validFrameRate(timeline_rate)) return std::nullopt;
    const long double microseconds = static_cast<long double>(frames) *
        timeline_rate.denominator * 1'000'000.0L / timeline_rate.numerator;
    const auto exclusive_max = std::ldexp(1.0L, 63);
    if (!std::isfinite(microseconds) || microseconds >= exclusive_max) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(std::llround(microseconds));
}

std::optional<std::int64_t> secondsToMicroseconds(double seconds) noexcept {
    if (!std::isfinite(seconds) || seconds <= 0.0) return std::nullopt;
    const long double microseconds = static_cast<long double>(seconds) * 1'000'000.0L;
    const auto exclusive_max = std::ldexp(1.0L, 63);
    if (!std::isfinite(microseconds) || microseconds >= exclusive_max) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(std::llround(microseconds));
}

std::optional<std::int64_t> sourceFramesToMicroseconds(
    std::int64_t source_frames,
    double source_frame_rate) noexcept {
    if (source_frames < 0 || !std::isfinite(source_frame_rate) ||
        source_frame_rate <= 0.0 || source_frame_rate > 1000.0) {
        return std::nullopt;
    }
    const long double microseconds = static_cast<long double>(source_frames) *
        1'000'000.0L / source_frame_rate;
    const auto exclusive_max = std::ldexp(1.0L, 63);
    if (!std::isfinite(microseconds) || microseconds >= exclusive_max) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(std::llround(microseconds));
}

std::optional<std::pair<std::int64_t, std::int64_t>> audioSourceRangeForVideo(
    const TimelineClip& video_clip,
    const media::VideoMetadata& metadata) noexcept {
    if (video_clip.kind != ClipKind::Video || !metadata.audio.has_value()) {
        return std::nullopt;
    }
    const auto source_rate = video_clip.frame_rate.value_or(
        metadata.frame_rate.value_or(0.0));
    if (!std::isfinite(source_rate) || source_rate <= 0.0 ||
        video_clip.source_start_frame < 0 ||
        video_clip.source_duration_frames <= 0 ||
        video_clip.source_start_frame > std::numeric_limits<std::int64_t>::max() -
            video_clip.source_duration_frames) {
        return std::nullopt;
    }
    const auto source_start_us = sourceFramesToMicroseconds(
        video_clip.source_start_frame, source_rate);
    const auto source_end_us = sourceFramesToMicroseconds(
        video_clip.source_start_frame + video_clip.source_duration_frames,
        source_rate);
    if (!source_start_us.has_value() || !source_end_us.has_value() ||
        *source_end_us <= *source_start_us) {
        return std::nullopt;
    }
    auto source_duration_us = *source_end_us - *source_start_us;
    if (metadata.audio->duration_seconds.has_value()) {
        const auto audio_end_us = secondsToMicroseconds(
            *metadata.audio->duration_seconds);
        if (!audio_end_us.has_value() || *audio_end_us <= *source_start_us) {
            return std::nullopt;
        }
        source_duration_us = std::min(
            source_duration_us, *audio_end_us - *source_start_us);
    }
    if (source_duration_us <= 0 || *source_start_us < 0 ||
        source_duration_us > std::numeric_limits<std::int64_t>::max() -
            *source_start_us) {
        return std::nullopt;
    }
    return std::make_pair(*source_start_us, source_duration_us);
}

std::string nextAudioTrackName(const std::vector<TimelineTrack>& tracks) {
    std::size_t number = 1;
    for (;; ++number) {
        const auto candidate = "Audio " + std::to_string(number);
        const auto found = std::find_if(
            tracks.begin(), tracks.end(), [&candidate](const TimelineTrack& track) {
                return track.kind == TrackKind::Audio && track.name == candidate;
            });
        if (found == tracks.end()) return candidate;
    }
}

void appendDefaultTracks(
    std::vector<TimelineTrack>& tracks,
    TrackId& next_track_id) {
    tracks.push_back({next_track_id++, "Video 1", 1.0, false, {}});
    TimelineTrack audio_track{
        next_track_id++, "Audio 1", 1.0, false, {}};
    audio_track.kind = TrackKind::Audio;
    tracks.push_back(std::move(audio_track));
}

std::optional<std::int64_t> timelineFramesForAudioDurationUs(
    std::int64_t duration_us,
    FrameRate timeline_rate) noexcept {
    if (duration_us < 0 || !validFrameRate(timeline_rate)) return std::nullopt;
    const long double frames = static_cast<long double>(duration_us) *
        timeline_rate.numerator /
        (static_cast<long double>(timeline_rate.denominator) * 1'000'000.0L);
    const auto exclusive_max = std::ldexp(1.0L, 63);
    if (!std::isfinite(frames) || frames >= exclusive_max) return std::nullopt;
    return static_cast<std::int64_t>(std::ceil(frames));
}

std::optional<std::int64_t> audioSourceLimitUs(
    const TimelineClip& clip) noexcept {
    if (clip.kind != ClipKind::Audio || clip.source_start_time_us < 0 ||
        clip.source_duration_time_us <= 0 ||
        clip.source_duration_time_us > std::numeric_limits<std::int64_t>::max() -
            clip.source_start_time_us) {
        return std::nullopt;
    }
    const auto current_end = clip.source_start_time_us +
        clip.source_duration_time_us;
    if (clip.duration_seconds.has_value()) {
        const auto media_end = secondsToMicroseconds(*clip.duration_seconds);
        if (media_end.has_value()) return std::max(*media_end, current_end);
    }
    return current_end;
}

std::optional<std::int64_t> timelineFramesForMicroseconds(
    std::int64_t microseconds,
    FrameRate timeline_rate) noexcept {
    if (microseconds < 0 || !validFrameRate(timeline_rate)) return std::nullopt;
    const long double frames = static_cast<long double>(microseconds) *
        timeline_rate.numerator /
        (static_cast<long double>(timeline_rate.denominator) * 1'000'000.0L);
    const auto exclusive_max = std::ldexp(1.0L, 63);
    if (!std::isfinite(frames) || frames >= exclusive_max) return std::nullopt;
    return static_cast<std::int64_t>(std::llround(frames));
}

std::optional<std::int64_t> shiftAudioSourceTime(
    const TimelineClip& clip,
    std::int64_t new_timeline_start_frame,
    FrameRate timeline_rate) noexcept {
    if (clip.kind != ClipKind::Audio || clip.source_start_time_us < 0) {
        return std::nullopt;
    }
    const auto frame_delta = new_timeline_start_frame - clip.timeline_start_frame;
    const auto absolute_delta = frame_delta < 0 ? -frame_delta : frame_delta;
    const auto elapsed = timelineFramesToMicroseconds(absolute_delta, timeline_rate);
    if (!elapsed.has_value()) return std::nullopt;
    if (frame_delta >= 0) {
        if (*elapsed > std::numeric_limits<std::int64_t>::max() -
                clip.source_start_time_us) return std::nullopt;
        return clip.source_start_time_us + *elapsed;
    }
    if (*elapsed > clip.source_start_time_us) return std::nullopt;
    return clip.source_start_time_us - *elapsed;
}

std::optional<std::int64_t> sourceFrameLimit(const TimelineClip& clip) noexcept {
    if (clip.kind != ClipKind::Video) return std::nullopt;
    if (clip.frame_count.has_value() && *clip.frame_count > 0) {
        return clip.frame_count;
    }
    if (!clip.duration_seconds.has_value() || !clip.frame_rate.has_value() ||
        !std::isfinite(*clip.duration_seconds) || !std::isfinite(*clip.frame_rate) ||
        *clip.duration_seconds <= 0.0 || *clip.frame_rate <= 0.0) {
        return std::nullopt;
    }
    const long double estimated =
        static_cast<long double>(*clip.duration_seconds) *
        static_cast<long double>(*clip.frame_rate);
    const auto exclusive_max = std::ldexp(1.0L, 63);
    if (!std::isfinite(estimated) || estimated >= exclusive_max) {
        return std::nullopt;
    }
    const auto rounded = std::ceil(estimated);
    if (rounded >= exclusive_max) return std::nullopt;
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(rounded));
}

std::int64_t sourceDurationForClip(
    const TimelineClip& clip,
    FrameRate timeline_frame_rate) noexcept {
    if (clip.source_duration_frames > 0) return clip.source_duration_frames;
    if (clip.kind != ClipKind::Video) return clip.timeline_duration_frames;
    return sourceFramesForTimelineDuration(
        clip.timeline_duration_frames,
        clip.frame_rate.value_or(timeline_frame_rate.asDouble()),
        timeline_frame_rate).value_or(0);
}

bool preserveTransitionContinuity(TimelineTrack& track) noexcept {
    std::vector<std::pair<std::size_t, std::size_t>> transition_pairs;
    transition_pairs.reserve(track.transitions.size());
    for (const auto& transition : track.transitions) {
        const auto from = std::find_if(track.clips.begin(), track.clips.end(),
            [&transition](const TimelineClip& clip) {
                return clip.clip_id == transition.from_clip_id;
            });
        const auto to = std::find_if(track.clips.begin(), track.clips.end(),
            [&transition](const TimelineClip& clip) {
                return clip.clip_id == transition.to_clip_id;
            });
        if (from == track.clips.end() || to == track.clips.end()) continue;
        const auto from_index = static_cast<std::size_t>(from - track.clips.begin());
        const auto to_index = static_cast<std::size_t>(to - track.clips.begin());
        if (to_index == from_index + 1) {
            transition_pairs.emplace_back(from_index, to_index);
        }
    }
    std::sort(transition_pairs.begin(), transition_pairs.end());

    for (const auto [from_index, to_index] : transition_pairs) {
        auto& from = track.clips[from_index];
        auto& to = track.clips[to_index];
        if (from.timeline_duration_frames <= 0 || to.timeline_start_frame < 0 ||
            from.timeline_start_frame > std::numeric_limits<std::int64_t>::max() -
                from.timeline_duration_frames) {
            return false;
        }
        const auto transition = std::find_if(
            track.transitions.begin(), track.transitions.end(),
            [&from, &to](const TimelineTransition& candidate) {
                return candidate.from_clip_id == from.clip_id &&
                    candidate.to_clip_id == to.clip_id;
            });
        if (transition == track.transitions.end()) return false;

        const auto maximum_transition_duration = std::min(
            from.timeline_duration_frames, to.timeline_duration_frames);
        if (maximum_transition_duration <= 0) return false;
        transition->duration_frames = std::clamp<std::int64_t>(
            transition->duration_frames, 1, maximum_transition_duration);
        const auto from_end = from.timeline_start_frame + from.timeline_duration_frames;
        const auto target_start = isOverlapTransition(transition->kind)
            ? from_end - transition->duration_frames
            : from_end;
        const auto delta = target_start - to.timeline_start_frame;
        if (delta < 0) {
            const auto removed = static_cast<std::uint64_t>(-(delta + 1)) + 1U;
            for (std::size_t index = to_index; index < track.clips.size(); ++index) {
                const auto& clip = track.clips[index];
                if (clip.timeline_start_frame < 0 || clip.timeline_duration_frames <= 0 ||
                    static_cast<std::uint64_t>(clip.timeline_start_frame) < removed ||
                    clip.timeline_duration_frames > std::numeric_limits<std::int64_t>::max() -
                        (clip.timeline_start_frame - static_cast<std::int64_t>(removed))) {
                    return false;
                }
            }
        } else if (delta > 0) {
            for (std::size_t index = to_index; index < track.clips.size(); ++index) {
                const auto& clip = track.clips[index];
                if (clip.timeline_start_frame < 0 || clip.timeline_duration_frames <= 0 ||
                    clip.timeline_start_frame >
                        std::numeric_limits<std::int64_t>::max() - delta ||
                    clip.timeline_start_frame + delta >
                        std::numeric_limits<std::int64_t>::max() -
                            clip.timeline_duration_frames) {
                    return false;
                }
            }
        }
        if (delta < 0) {
            const auto removed = static_cast<std::int64_t>(
                static_cast<std::uint64_t>(-(delta + 1)) + 1U);
            for (std::size_t index = to_index; index < track.clips.size(); ++index) {
                track.clips[index].timeline_start_frame -= removed;
            }
        } else if (delta > 0) {
            for (std::size_t index = to_index; index < track.clips.size(); ++index) {
                track.clips[index].timeline_start_frame += delta;
            }
        }
    }
    return true;
}

bool shiftTimelineSuffix(
    TimelineTrack& track,
    std::size_t first_index,
    std::int64_t delta) noexcept {
    if (first_index >= track.clips.size()) return false;
    if (delta == 0) return true;
    for (std::size_t index = first_index; index < track.clips.size(); ++index) {
        const auto& clip = track.clips[index];
        if (clip.timeline_start_frame < 0 || clip.timeline_duration_frames <= 0) {
            return false;
        }
        std::int64_t shifted_start = 0;
        if (delta < 0) {
            const auto removed = static_cast<std::uint64_t>(-(delta + 1)) + 1U;
            if (static_cast<std::uint64_t>(clip.timeline_start_frame) < removed) {
                return false;
            }
            shifted_start = clip.timeline_start_frame - static_cast<std::int64_t>(removed);
        } else {
            if (clip.timeline_start_frame >
                std::numeric_limits<std::int64_t>::max() - delta) {
                return false;
            }
            shifted_start = clip.timeline_start_frame + delta;
        }
        if (shifted_start > std::numeric_limits<std::int64_t>::max() -
                clip.timeline_duration_frames) {
            return false;
        }
    }
    for (std::size_t index = first_index; index < track.clips.size(); ++index) {
        track.clips[index].timeline_start_frame += delta;
    }
    return true;
}

std::optional<std::int64_t> clipTimelineEnd(const TimelineClip& clip) noexcept {
    if (clip.timeline_start_frame < 0 || clip.timeline_duration_frames <= 0 ||
        clip.timeline_start_frame > std::numeric_limits<std::int64_t>::max() -
            clip.timeline_duration_frames) {
        return std::nullopt;
    }
    return clip.timeline_start_frame + clip.timeline_duration_frames;
}

std::optional<std::int64_t> shiftedSourceStart(
    const TimelineClip& clip,
    std::int64_t new_timeline_start_frame,
    FrameRate timeline_frame_rate) noexcept {
    if (clip.source_start_frame < 0 || new_timeline_start_frame < 0) {
        return std::nullopt;
    }
    if (clip.kind != ClipKind::Video) return clip.source_start_frame;
    const auto delta = new_timeline_start_frame - clip.timeline_start_frame;
    const auto source_delta = sourceFrameOffsetForTimelineFrame(
        std::abs(delta),
        clip.frame_rate.value_or(timeline_frame_rate.asDouble()),
        timeline_frame_rate);
    if (!source_delta.has_value()) return std::nullopt;
    if (delta >= 0) {
        if (*source_delta > std::numeric_limits<std::int64_t>::max() -
                clip.source_start_frame) {
            return std::nullopt;
        }
        return clip.source_start_frame + *source_delta;
    }
    const auto removed = *source_delta;
    if (removed <= clip.source_start_frame) {
        return clip.source_start_frame - removed;
    }
    return std::nullopt;
}

TransformKeyframes reframeKeyframes(
    const TimelineClip& old_clip,
    std::int64_t new_timeline_start_frame,
    std::int64_t new_duration_frames,
    Transform2D& new_transform) noexcept {
    const auto start_delta = new_timeline_start_frame - old_clip.timeline_start_frame;
    const auto old_anchor_frame = std::max<std::int64_t>(0, start_delta);
    new_transform = evaluateTransform(
        old_clip.transform, old_clip.keyframes, old_anchor_frame);

    TransformKeyframes result;
    if (start_delta == 0) {
        for (const auto property : {TransformProperty::PositionX,
                                    TransformProperty::PositionY,
                                    TransformProperty::Scale,
                                    TransformProperty::Rotation,
                                    TransformProperty::Opacity}) {
            for (const auto& keyframe : keyframesFor(old_clip.keyframes, property)) {
                if (keyframe.frame < new_duration_frames) {
                    static_cast<void>(setKeyframe(
                        result, property, keyframe.frame, keyframe.value));
                }
            }
        }
        return result;
    }
    for (const auto property : {TransformProperty::PositionX,
                                TransformProperty::PositionY,
                                TransformProperty::Scale,
                                TransformProperty::Rotation,
                                TransformProperty::Opacity}) {
        const auto& old_keys = keyframesFor(old_clip.keyframes, property);
        if (old_keys.empty()) continue;

        const auto anchor_new_frame = start_delta < 0 ? -start_delta : 0;
        const auto anchor_value = evaluateProperty(
            old_clip.transform,
            old_clip.keyframes,
            property,
            old_anchor_frame);
        static_cast<void>(setKeyframe(
            result, property, anchor_new_frame, anchor_value));

        for (const auto& keyframe : old_keys) {
            if (start_delta > 0 && keyframe.frame < start_delta) continue;
            if (start_delta < 0 &&
                keyframe.frame > std::numeric_limits<std::int64_t>::max() +
                    start_delta) {
                continue;
            }
            const auto shifted_frame = keyframe.frame - start_delta;
            if (shifted_frame >= 0 && shifted_frame < new_duration_frames) {
                static_cast<void>(setKeyframe(
                    result, property, shifted_frame, keyframe.value));
            }
        }
    }
    return result;
}

bool sameOverlapClass(const TimelineClip& left, const TimelineClip& right) noexcept {
    return left.kind == right.kind ||
        (isFrameTimedMediaClipKind(left.kind) &&
         isFrameTimedMediaClipKind(right.kind));
}

bool hasOverlapTransitionForClip(
    const TimelineTrack& track,
    ClipId clip_id) noexcept {
    return std::any_of(track.transitions.begin(), track.transitions.end(),
        [clip_id](const TimelineTransition& transition) {
            return isOverlapTransition(transition.kind) &&
                (transition.from_clip_id == clip_id ||
                 transition.to_clip_id == clip_id);
        });
}

std::vector<AudioGainKeyframe> sliceAudioGainEnvelope(
    const TimelineClip& clip,
    std::int64_t begin_frame,
    std::int64_t end_frame) {
    if (clip.audio_gain_keyframes.empty()) return {};
    begin_frame = std::clamp<std::int64_t>(begin_frame, 0,
        clip.timeline_duration_frames);
    end_frame = std::clamp<std::int64_t>(end_frame, begin_frame,
        clip.timeline_duration_frames);
    std::vector<AudioGainKeyframe> result;
    result.reserve(clip.audio_gain_keyframes.size() + 2);
    result.push_back({0, evaluateAudioGainEnvelope(
        clip.audio_gain_keyframes, static_cast<double>(begin_frame))});
    for (const auto& keyframe : clip.audio_gain_keyframes) {
        if (keyframe.frame <= begin_frame || keyframe.frame >= end_frame) continue;
        result.push_back({keyframe.frame - begin_frame, keyframe.gain});
    }
    if (end_frame > begin_frame) {
        result.push_back({end_frame - begin_frame,
            evaluateAudioGainEnvelope(
                clip.audio_gain_keyframes, static_cast<double>(end_frame))});
    }
    return result;
}

} // namespace

ClipAttributeCompatibility clipAttributeCompatibility(
    const TimelineClipAttributes& source,
    ClipKind target_kind,
    bool target_has_audio_volume_envelope) noexcept {
    const auto is_visual = [](ClipKind kind) {
        return kind == ClipKind::Video || kind == ClipKind::Image;
    };
    const auto is_transformable = [](ClipKind kind) {
        return kind != ClipKind::Audio;
    };
    const auto has_audio_controls = [](ClipKind kind) {
        return kind == ClipKind::Video || kind == ClipKind::Audio;
    };
    return {
        .effects = is_visual(source.source_kind) && is_visual(target_kind),
        .transform = is_transformable(source.source_kind) && is_transformable(target_kind),
        .audio_gain_and_mute = has_audio_controls(source.source_kind) &&
            has_audio_controls(target_kind),
        .audio_volume_envelope = source.audio_volume_envelope.has_value() &&
            target_has_audio_volume_envelope,
        .text = source.source_kind == ClipKind::Text && target_kind == ClipKind::Text,
    };
}

TimelineModel::TimelineModel() {
    appendDefaultTracks(tracks_, next_track_id_);
    assertIdentityInvariants();
}

bool TimelineModel::validName(const std::string& name) noexcept {
    return !name.empty() && name.find_first_not_of(" \t\r\n") != std::string::npos;
}

bool TimelineModel::validAudioGain(double gain) noexcept {
    return std::isfinite(gain) && gain >= 0.0 && gain <= 2.0;
}

bool TimelineModel::validTextStyle(const TextStyle& text) noexcept {
    if (text.font_family.empty() || !std::isfinite(text.font_size_pixels) ||
        text.font_size_pixels <= 0.0 || text.font_size_pixels > 512.0) {
        return false;
    }
    switch (text.alignment) {
    case TextAlignment::Left:
    case TextAlignment::Center:
    case TextAlignment::Right:
        return true;
    }
    return false;
}

bool TimelineModel::validTransitionKind(TransitionKind kind) noexcept {
    switch (kind) {
    case TransitionKind::CrossDissolve:
    case TransitionKind::FadeToBlack:
    case TransitionKind::AudioCrossfade:
        return true;
    }
    return false;
}

std::filesystem::path TimelineModel::canonicalPath(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (!error) return canonical;
    const auto absolute = std::filesystem::absolute(path, error);
    if (!error) return absolute.lexically_normal();
    return path.lexically_normal();
}

std::optional<std::int64_t> TimelineModel::sourceDurationInFrames(
    const media::VideoMetadata& metadata) {
    return sourceFrameCountFromMetadata(metadata);
}

bool TimelineModel::overlaps(
    const TimelineClip& left,
    std::int64_t start_frame,
    std::int64_t duration_frames) noexcept {
    if (duration_frames <= 0 || left.timeline_duration_frames <= 0) return true;
    if (start_frame > std::numeric_limits<std::int64_t>::max() - duration_frames ||
        left.timeline_start_frame > std::numeric_limits<std::int64_t>::max() -
            left.timeline_duration_frames) {
        return true;
    }
    const auto end_frame = start_frame + duration_frames;
    const auto left_end = left.timeline_start_frame + left.timeline_duration_frames;
    return start_frame < left_end && left.timeline_start_frame < end_frame;
}

bool TimelineModel::overlapsSameKind(
    const TimelineClip& left,
    ClipKind kind,
    std::int64_t start_frame,
    std::int64_t duration_frames) noexcept {
    const bool same_visual_media_kind = isFrameTimedMediaClipKind(left.kind) &&
        isFrameTimedMediaClipKind(kind);
    return (left.kind == kind || same_visual_media_kind) &&
        overlaps(left, start_frame, duration_frames);
}

std::optional<ClipEdgeEditPreview> previewClipEdgeEdit(
    const std::vector<TimelineTrack>& tracks,
    ClipLocation location,
    ClipEdge edge,
    std::int64_t requested_boundary_frame,
    ClipEdgeEditMode mode,
    FrameRate timeline_frame_rate) {
    if ((edge != ClipEdge::Left && edge != ClipEdge::Right) ||
        (mode != ClipEdgeEditMode::Rolling &&
         mode != ClipEdgeEditMode::Individual)) {
        return std::nullopt;
    }
    if (location.track_index >= tracks.size()) return std::nullopt;
    const auto& track = tracks[location.track_index];
    if (location.clip_index >= track.clips.size()) return std::nullopt;
    const auto& original = track.clips[location.clip_index];
    const auto original_end = clipTimelineEnd(original);
    const auto original_source_duration = sourceDurationForClip(
        original, timeline_frame_rate);
    if (!original_end.has_value() || original.source_start_frame < 0 ||
        original_source_duration <= 0 ||
        original.source_start_frame > std::numeric_limits<std::int64_t>::max() -
            original_source_duration) {
        return std::nullopt;
    }
    if (original.kind == ClipKind::Audio &&
        (original.source_start_time_us < 0 ||
         original.source_duration_time_us <= 0 ||
         original.source_duration_time_us >
             std::numeric_limits<std::int64_t>::max() -
                 original.source_start_time_us)) {
        return std::nullopt;
    }

    std::optional<std::size_t> shared_neighbor_index;
    if (edge == ClipEdge::Left && location.clip_index > 0) {
        const auto previous_index = location.clip_index - 1;
        const auto previous_end = clipTimelineEnd(track.clips[previous_index]);
        if (previous_end.has_value() && *previous_end == original.timeline_start_frame) {
            shared_neighbor_index = previous_index;
        }
    } else if (edge == ClipEdge::Right &&
               location.clip_index + 1 < track.clips.size()) {
        const auto& next = track.clips[location.clip_index + 1];
        if (next.timeline_start_frame == *original_end) {
            shared_neighbor_index = location.clip_index + 1;
        }
    }
    const auto neighbor_index = mode == ClipEdgeEditMode::Rolling
        ? shared_neighbor_index
        : std::nullopt;

    std::int64_t minimum_boundary = edge == ClipEdge::Left
        ? 0
        : original.timeline_start_frame + 1;
    std::int64_t maximum_boundary = edge == ClipEdge::Left
        ? *original_end - 1
        : std::numeric_limits<std::int64_t>::max();

    if (neighbor_index.has_value()) {
        const auto& neighbor = track.clips[*neighbor_index];
        const auto neighbor_end = clipTimelineEnd(neighbor);
        const auto neighbor_source_duration = sourceDurationForClip(
            neighbor, timeline_frame_rate);
        if (!neighbor_end.has_value() || neighbor.source_start_frame < 0 ||
            neighbor_source_duration <= 0 ||
            neighbor.source_start_frame > std::numeric_limits<std::int64_t>::max() -
                neighbor_source_duration) {
            return std::nullopt;
        }
        if (edge == ClipEdge::Left) {
            minimum_boundary = neighbor.timeline_start_frame + 1;
        } else {
            maximum_boundary = *neighbor_end - 1;
        }
    } else if (edge == ClipEdge::Left) {
        for (std::size_t index = 0; index < track.clips.size(); ++index) {
            if (index == location.clip_index) continue;
            const auto& other = track.clips[index];
            const bool overlaps_adjacent_media =
                mode == ClipEdgeEditMode::Individual &&
                shared_neighbor_index == index &&
                isMediaClipKind(original.kind) && isMediaClipKind(other.kind);
            if (overlaps_adjacent_media) continue;
            const auto other_end = clipTimelineEnd(other);
            if (other_end.has_value() && *other_end <= original.timeline_start_frame &&
                sameOverlapClass(other, original)) {
                minimum_boundary = std::max(minimum_boundary, *other_end);
            }
        }
    } else {
        for (std::size_t index = 0; index < track.clips.size(); ++index) {
            if (index == location.clip_index) continue;
            const auto& other = track.clips[index];
            const bool overlaps_adjacent_media =
                mode == ClipEdgeEditMode::Individual &&
                shared_neighbor_index == index &&
                isMediaClipKind(original.kind) && isMediaClipKind(other.kind);
            if (overlaps_adjacent_media) continue;
            if (other.timeline_start_frame >= *original_end &&
                sameOverlapClass(other, original)) {
                maximum_boundary = std::min(
                    maximum_boundary, other.timeline_start_frame);
            }
        }
    }

    if (original.kind == ClipKind::Video) {
        if (edge == ClipEdge::Left) {
            const auto timeline_before = timelineFrameCapacityForSourceFrames(
                original.source_start_frame,
                original.frame_rate.value_or(timeline_frame_rate.asDouble()),
                timeline_frame_rate);
            if (!timeline_before.has_value()) return std::nullopt;
            minimum_boundary = std::max(
                minimum_boundary,
                std::max<std::int64_t>(
                    0, original.timeline_start_frame - *timeline_before));
        } else {
            const auto current_source_end = original.source_start_frame +
                original_source_duration;
            const auto limit = sourceFrameLimit(original).value_or(current_source_end);
            if (limit < original.source_start_frame) return std::nullopt;
            const auto available = limit - original.source_start_frame;
            const auto timeline_available = timelineFramesForSourceDuration(
                available,
                original.frame_rate.value_or(timeline_frame_rate.asDouble()),
                timeline_frame_rate);
            if (!timeline_available.has_value()) return std::nullopt;
            const auto max_boundary = *timeline_available >
                    std::numeric_limits<std::int64_t>::max() - original.timeline_start_frame
                ? std::numeric_limits<std::int64_t>::max()
                : original.timeline_start_frame + *timeline_available;
            maximum_boundary = std::min(maximum_boundary, max_boundary);
        }
    } else if (original.kind == ClipKind::Audio) {
        const auto source_limit = audioSourceLimitUs(original);
        if (!source_limit.has_value()) return std::nullopt;
        if (edge == ClipEdge::Left) {
            const auto timeline_before = timelineFramesForAudioDurationUs(
                original.source_start_time_us, timeline_frame_rate);
            if (!timeline_before.has_value()) return std::nullopt;
            minimum_boundary = std::max(
                minimum_boundary,
                original.timeline_start_frame - *timeline_before);
        } else {
            const auto available = *source_limit - original.source_start_time_us;
            const auto timeline_available = timelineFramesForAudioDurationUs(
                available, timeline_frame_rate);
            if (!timeline_available.has_value() ||
                *timeline_available > std::numeric_limits<std::int64_t>::max() -
                    original.timeline_start_frame) {
                return std::nullopt;
            }
            maximum_boundary = std::min(
                maximum_boundary,
                original.timeline_start_frame + *timeline_available);
        }
    }

    if (neighbor_index.has_value()) {
        const auto& neighbor = track.clips[*neighbor_index];
        const auto neighbor_source_duration = sourceDurationForClip(
            neighbor, timeline_frame_rate);
        if (edge == ClipEdge::Left && neighbor.kind == ClipKind::Video) {
            const auto current_source_end = neighbor.source_start_frame +
                neighbor_source_duration;
            const auto limit = sourceFrameLimit(neighbor).value_or(current_source_end);
            if (limit < neighbor.source_start_frame) return std::nullopt;
            const auto available = limit - neighbor.source_start_frame;
            const auto timeline_available = timelineFramesForSourceDuration(
                available,
                neighbor.frame_rate.value_or(timeline_frame_rate.asDouble()),
                timeline_frame_rate);
            if (!timeline_available.has_value()) return std::nullopt;
            const auto max_boundary = *timeline_available >
                    std::numeric_limits<std::int64_t>::max() - neighbor.timeline_start_frame
                ? std::numeric_limits<std::int64_t>::max()
                : neighbor.timeline_start_frame + *timeline_available;
            maximum_boundary = std::min(maximum_boundary, max_boundary);
        } else if (edge == ClipEdge::Left && neighbor.kind == ClipKind::Audio) {
            const auto source_limit = audioSourceLimitUs(neighbor);
            if (!source_limit.has_value()) return std::nullopt;
            const auto available = *source_limit - neighbor.source_start_time_us;
            const auto timeline_available = timelineFramesForAudioDurationUs(
                available, timeline_frame_rate);
            if (!timeline_available.has_value() ||
                *timeline_available > std::numeric_limits<std::int64_t>::max() -
                    neighbor.timeline_start_frame) {
                return std::nullopt;
            }
            maximum_boundary = std::min(
                maximum_boundary,
                neighbor.timeline_start_frame + *timeline_available);
        } else if (edge == ClipEdge::Right && neighbor.kind == ClipKind::Video) {
            const auto timeline_before = timelineFrameCapacityForSourceFrames(
                neighbor.source_start_frame,
                neighbor.frame_rate.value_or(timeline_frame_rate.asDouble()),
                timeline_frame_rate);
            if (!timeline_before.has_value()) return std::nullopt;
            minimum_boundary = std::max(
                minimum_boundary,
                std::max<std::int64_t>(
                    0, neighbor.timeline_start_frame - *timeline_before));
        } else if (edge == ClipEdge::Right && neighbor.kind == ClipKind::Audio) {
            const auto timeline_before = timelineFramesForAudioDurationUs(
                neighbor.source_start_time_us, timeline_frame_rate);
            if (!timeline_before.has_value()) return std::nullopt;
            minimum_boundary = std::max(
                minimum_boundary,
                neighbor.timeline_start_frame - *timeline_before);
        }
    }

    if (minimum_boundary > maximum_boundary) return std::nullopt;
    const auto boundary = std::clamp(
        requested_boundary_frame, minimum_boundary, maximum_boundary);

    ClipEdgeEditPreview preview;
    preview.clip_location = location;
    preview.clip = original;
    preview.boundary_frame = boundary;
    if (edge == ClipEdge::Left) {
        preview.clip.timeline_start_frame = boundary;
        preview.clip.timeline_duration_frames = *original_end - boundary;
        const auto source_start = shiftedSourceStart(
            original, boundary, timeline_frame_rate);
        if (!source_start.has_value()) return std::nullopt;
        preview.clip.source_start_frame = *source_start;
        const auto original_source_end = original.source_start_frame +
            original_source_duration;
        if (*source_start > original_source_end) return std::nullopt;
        preview.clip.source_duration_frames =
            boundary == original.timeline_start_frame &&
                    original.source_duration_frames <= 0
                ? original.source_duration_frames
                : original_source_end - *source_start;
        if (original.kind == ClipKind::Audio) {
            const auto source_start_time = shiftAudioSourceTime(
                original, boundary, timeline_frame_rate);
            if (!source_start_time.has_value() ||
                *source_start_time >= original.source_start_time_us +
                    original.source_duration_time_us) {
                return std::nullopt;
            }
            preview.clip.source_start_time_us = *source_start_time;
            preview.clip.source_duration_time_us =
                original.source_start_time_us + original.source_duration_time_us -
                *source_start_time;
        }
    } else {
        preview.clip.timeline_duration_frames = boundary - original.timeline_start_frame;
        if (original.kind == ClipKind::Video &&
            (boundary != *original_end || original.source_duration_frames > 0)) {
            const auto source_duration = sourceFramesForTimelineDuration(
                preview.clip.timeline_duration_frames,
                original.frame_rate.value_or(timeline_frame_rate.asDouble()),
                timeline_frame_rate);
            if (!source_duration.has_value()) return std::nullopt;
            preview.clip.source_duration_frames = *source_duration;
        }
        if (original.kind == ClipKind::Audio) {
            const auto duration_time = timelineFramesToMicroseconds(
                preview.clip.timeline_duration_frames, timeline_frame_rate);
            const auto source_limit = audioSourceLimitUs(original);
            if (!duration_time.has_value() || !source_limit.has_value() ||
                original.source_start_time_us >= *source_limit) {
                return std::nullopt;
            }
            preview.clip.source_duration_time_us = std::min(
                *duration_time,
                *source_limit - original.source_start_time_us);
        }
    }
    if (original.kind == ClipKind::Audio) {
        const auto begin_local = edge == ClipEdge::Left
            ? boundary - original.timeline_start_frame : 0;
        preview.clip.audio_gain_keyframes = sliceAudioGainEnvelope(
            original, begin_local,
            begin_local + preview.clip.timeline_duration_frames);
    }
    preview.clip.keyframes = reframeKeyframes(
        original,
        preview.clip.timeline_start_frame,
        preview.clip.timeline_duration_frames,
        preview.clip.transform);

    if (neighbor_index.has_value()) {
        const auto& original_neighbor = track.clips[*neighbor_index];
        const auto neighbor_end = clipTimelineEnd(original_neighbor);
        if (!neighbor_end.has_value()) return std::nullopt;
        auto neighbor = original_neighbor;
        if (edge == ClipEdge::Left) {
            neighbor.timeline_duration_frames =
                boundary - neighbor.timeline_start_frame;
            if (neighbor.kind == ClipKind::Video) {
                const auto source_duration = sourceFramesForTimelineDuration(
                    neighbor.timeline_duration_frames,
                    neighbor.frame_rate.value_or(timeline_frame_rate.asDouble()),
                    timeline_frame_rate);
                if (!source_duration.has_value()) return std::nullopt;
                neighbor.source_duration_frames = *source_duration;
            } else if (neighbor.kind == ClipKind::Audio) {
                const auto duration_time = timelineFramesToMicroseconds(
                    neighbor.timeline_duration_frames, timeline_frame_rate);
                const auto source_limit = audioSourceLimitUs(original_neighbor);
                if (!duration_time.has_value() || !source_limit.has_value() ||
                    original_neighbor.source_start_time_us >= *source_limit) {
                    return std::nullopt;
                }
                neighbor.source_duration_time_us = std::min(
                    *duration_time,
                    *source_limit - original_neighbor.source_start_time_us);
                if (neighbor.source_duration_time_us <= 0) return std::nullopt;
            }
        } else {
            neighbor.timeline_start_frame = boundary;
            neighbor.timeline_duration_frames = *neighbor_end - boundary;
            if (original_neighbor.kind == ClipKind::Audio) {
                const auto source_start_time = shiftAudioSourceTime(
                    original_neighbor, boundary, timeline_frame_rate);
                const auto source_limit = audioSourceLimitUs(original_neighbor);
                if (!source_start_time.has_value() || !source_limit.has_value()) {
                    return std::nullopt;
                }
                const auto neighbor_end_time =
                    original_neighbor.source_start_time_us +
                    original_neighbor.source_duration_time_us;
                if (*source_start_time >= neighbor_end_time) return std::nullopt;
                neighbor.source_start_time_us = *source_start_time;
                neighbor.source_duration_time_us =
                    neighbor_end_time - *source_start_time;
            } else {
                const auto source_start = shiftedSourceStart(
                    original_neighbor, neighbor.timeline_start_frame,
                    timeline_frame_rate);
                if (!source_start.has_value()) return std::nullopt;
                neighbor.source_start_frame = *source_start;
                const auto original_neighbor_source_duration = sourceDurationForClip(
                    original_neighbor, timeline_frame_rate);
                const auto original_source_end = original_neighbor.source_start_frame +
                    original_neighbor_source_duration;
                if (*source_start > original_source_end) return std::nullopt;
                neighbor.source_duration_frames = original_source_end - *source_start;
            }
        }
        neighbor.keyframes = reframeKeyframes(
            original_neighbor,
            neighbor.timeline_start_frame,
            neighbor.timeline_duration_frames,
            neighbor.transform);
        if (original_neighbor.kind == ClipKind::Audio) {
            const auto begin_local = edge == ClipEdge::Right
                ? boundary - original_neighbor.timeline_start_frame : 0;
            neighbor.audio_gain_keyframes = sliceAudioGainEnvelope(
                original_neighbor, begin_local,
                begin_local + neighbor.timeline_duration_frames);
        }
        preview.neighbor_location = ClipLocation{
            location.track_index, *neighbor_index};
        preview.neighbor_clip = std::move(neighbor);
    }

    return preview;
}

std::int64_t TimelineModel::trackEnd(const TimelineTrack& track) noexcept {
    std::int64_t end = 0;
    for (const auto& clip : track.clips) {
        if (clip.timeline_start_frame <= std::numeric_limits<std::int64_t>::max() -
                clip.timeline_duration_frames) {
            end = std::max(end, clip.timeline_start_frame + clip.timeline_duration_frames);
        }
    }
    return end;
}

TimelineTrack* TimelineModel::trackAt(std::size_t track_index) noexcept {
    return track_index < tracks_.size() ? &tracks_[track_index] : nullptr;
}

const TimelineTrack* TimelineModel::trackAt(std::size_t track_index) const noexcept {
    return track_index < tracks_.size() ? &tracks_[track_index] : nullptr;
}

AddTrackResult TimelineModel::addTrack(std::string name, TrackKind kind) {
    if (!validName(name)) return AddTrackResult::InvalidName;
    TimelineTrack track{next_track_id_++, std::move(name), 1.0, false, {}};
    track.kind = kind;
    if (kind == TrackKind::Audio) tracks_.push_back(std::move(track));
    else tracks_.insert(tracks_.begin(), std::move(track));
    assertIdentityInvariants();
    return AddTrackResult::Added;
}

TrackMutationResult TimelineModel::renameTrack(
    std::size_t track_index,
    std::string name) {
    auto* track = trackAt(track_index);
    if (track == nullptr) return TrackMutationResult::InvalidIndex;
    if (!validName(name)) return TrackMutationResult::InvalidName;
    if (track->name == name) return TrackMutationResult::NoChange;
    track->name = std::move(name);
    return TrackMutationResult::Changed;
}

TrackMutationResult TimelineModel::moveTrack(
    std::size_t from_index,
    std::size_t to_index) {
    if (from_index >= tracks_.size() || to_index >= tracks_.size()) {
        return TrackMutationResult::InvalidIndex;
    }
    if (from_index == to_index) return TrackMutationResult::NoChange;
    auto moved = std::move(tracks_[from_index]);
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(from_index));
    tracks_.insert(
        tracks_.begin() + static_cast<std::ptrdiff_t>(to_index),
        std::move(moved));
    for (auto& track : tracks_) {
        for (auto& clip : track.clips) clip.track_id = track.track_id;
    }
    assertIdentityInvariants();
    return TrackMutationResult::Changed;
}

TrackMutationResult TimelineModel::removeTrack(std::size_t track_index) {
    if (track_index >= tracks_.size()) return TrackMutationResult::InvalidIndex;
    if (!tracks_[track_index].clips.empty()) return TrackMutationResult::NotEmpty;
    if (tracks_.size() == 1) return TrackMutationResult::NoChange;
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(track_index));
    assertIdentityInvariants();
    return TrackMutationResult::Changed;
}

AddClipResult TimelineModel::addClip(
    std::size_t track_index,
    const media::VideoMetadata& metadata,
    std::int64_t timeline_start_frame) {
    auto* track = trackAt(track_index);
    if (track == nullptr) return AddClipResult::InvalidTrack;
    const bool audio_source = metadata.kind == media::MediaKind::Audio;
    if ((track->kind == TrackKind::Audio) != audio_source) {
        return AddClipResult::IncompatibleTrack;
    }
    const auto source_duration_frames = audio_source
        ? std::optional<std::int64_t>{}
        : sourceDurationInFrames(metadata);
    if (!audio_source && !source_duration_frames.has_value()) {
        return AddClipResult::InvalidTimingMetadata;
    }
    const auto source_rate = metadata.frame_rate.value_or(frame_rate_.asDouble());
    const auto duration_frames = audio_source
        ? (metadata.duration_seconds.has_value()
            ? timelineFramesForAudioDuration(*metadata.duration_seconds, frame_rate_)
            : std::nullopt)
        : timelineFramesForSourceDuration(
              *source_duration_frames, source_rate, frame_rate_);
    if (!duration_frames.has_value()) return AddClipResult::InvalidTimingMetadata;
    if (timeline_start_frame < 0 ||
        *duration_frames > std::numeric_limits<std::int64_t>::max() - timeline_start_frame) {
        return AddClipResult::InvalidPosition;
    }
    const auto clip_kind = audio_source ? ClipKind::Audio
        : metadata.kind == media::MediaKind::Image ? ClipKind::Image
                                                   : ClipKind::Video;
    for (const auto& existing : track->clips) {
        if (overlapsSameKind(existing, clip_kind, timeline_start_frame,
                             *duration_frames)) {
            return AddClipResult::Overlap;
        }
    }
    TimelineClip clip;
    clip.timeline_start_frame = timeline_start_frame;
    clip.timeline_duration_frames = *duration_frames;
    clip.source_path = canonicalPath(metadata.source_path);
    clip.display_name = metadata.display_name;
    clip.duration_seconds = metadata.duration_seconds;
    clip.frame_rate = audio_source ? std::nullopt : metadata.frame_rate;
    clip.frame_count = audio_source ? std::nullopt : metadata.frame_count;
    clip.track_id = track->track_id;
    clip.kind = clip_kind;
    clip.source_duration_frames = source_duration_frames.value_or(0);
    clip.source_start_time_us = 0;
    if (audio_source) {
        const auto source_duration_us = secondsToMicroseconds(
            *metadata.duration_seconds);
        if (!source_duration_us.has_value()) {
            return AddClipResult::InvalidTimingMetadata;
        }
        clip.source_duration_time_us = *source_duration_us;
    }
    clip.clip_id = next_clip_id_++;
    track->clips.push_back(std::move(clip));
    std::stable_sort(track->clips.begin(), track->clips.end(),
              [](const auto& left, const auto& right) {
                  return left.timeline_start_frame < right.timeline_start_frame;
              });
    removeInvalidTransitions(*track);
    assertIdentityInvariants();
    return AddClipResult::Added;
}

AddClipResult TimelineModel::addAudioCompanion(
    ClipId video_clip_id,
    const media::VideoMetadata& metadata,
    ClipId* audio_clip_id) {
    const auto video_location = locateClip(video_clip_id);
    if (!video_location.has_value()) return AddClipResult::InvalidTrack;
    auto& video_track = tracks_[video_location->track_index];
    auto& video_clip = video_track.clips[video_location->clip_index];
    if (video_clip.kind != ClipKind::Video || video_track.kind != TrackKind::Video) {
        return AddClipResult::IncompatibleTrack;
    }
    if (video_clip.linked_clip_id.has_value()) {
        if (audio_clip_id != nullptr) *audio_clip_id = *video_clip.linked_clip_id;
        return AddClipResult::Added;
    }
    const auto source_range = audioSourceRangeForVideo(video_clip, metadata);
    if (!source_range.has_value()) return AddClipResult::InvalidTimingMetadata;
    const auto audio_timeline_duration = timelineFramesForAudioDurationUs(
        source_range->second, frame_rate_);
    if (!audio_timeline_duration.has_value() || *audio_timeline_duration <= 0) {
        return AddClipResult::InvalidTimingMetadata;
    }
    const auto companion_duration = std::min(
        video_clip.timeline_duration_frames, *audio_timeline_duration);

    std::optional<std::size_t> destination_track;
    for (std::size_t index = 0; index < tracks_.size(); ++index) {
        const auto& track = tracks_[index];
        if (track.kind != TrackKind::Audio) continue;
        const bool overlaps = std::any_of(
            track.clips.begin(), track.clips.end(),
            [&video_clip, companion_duration](const auto& clip) {
                return overlapsSameKind(
                    clip, ClipKind::Audio, video_clip.timeline_start_frame,
                    companion_duration);
            });
        if (!overlaps) {
            destination_track = index;
            break;
        }
    }
    if (!destination_track.has_value()) {
        const auto name = nextAudioTrackName(tracks_);
        if (addTrack(name, TrackKind::Audio) != AddTrackResult::Added) {
            return AddClipResult::InvalidTrack;
        }
        destination_track = tracks_.size() - 1;
    }

    const auto refreshed_video_location = locateClip(video_clip_id);
    if (!refreshed_video_location.has_value()) return AddClipResult::InvalidTrack;
    auto& refreshed_video = tracks_[refreshed_video_location->track_index]
        .clips[refreshed_video_location->clip_index];
    auto& audio_track = tracks_[*destination_track];
    TimelineClip audio_clip;
    audio_clip.timeline_start_frame = refreshed_video.timeline_start_frame;
    audio_clip.source_start_frame = 0;
    audio_clip.timeline_duration_frames = std::min(
        refreshed_video.timeline_duration_frames, *audio_timeline_duration);
    audio_clip.source_path = canonicalPath(metadata.source_path);
    audio_clip.display_name = metadata.display_name;
    audio_clip.duration_seconds = metadata.audio->duration_seconds.has_value()
        ? metadata.audio->duration_seconds
        : metadata.duration_seconds;
    audio_clip.frame_rate.reset();
    audio_clip.frame_count.reset();
    audio_clip.audio_gain = refreshed_video.audio_gain;
    audio_clip.audio_muted = refreshed_video.audio_muted;
    audio_clip.clip_id = next_clip_id_++;
    audio_clip.track_id = audio_track.track_id;
    audio_clip.kind = ClipKind::Audio;
    audio_clip.source_start_time_us = source_range->first;
    audio_clip.source_duration_time_us = source_range->second;
    audio_clip.linked_clip_id = video_clip_id;
    refreshed_video.linked_clip_id = audio_clip.clip_id;
    refreshed_video.audio_extracted = true;
    refreshed_video.audio_companion_pending = false;
    if (audio_clip_id != nullptr) *audio_clip_id = audio_clip.clip_id;
    audio_track.clips.push_back(std::move(audio_clip));
    std::stable_sort(audio_track.clips.begin(), audio_track.clips.end(),
        [](const auto& left, const auto& right) {
            return left.timeline_start_frame < right.timeline_start_frame;
        });
    assertIdentityInvariants();
    return AddClipResult::Added;
}

bool TimelineModel::linkAudio(ClipId video_clip_id, ClipId audio_clip_id) {
    const auto video_location = locateClip(video_clip_id);
    const auto audio_location = locateClip(audio_clip_id);
    if (!video_location.has_value() || !audio_location.has_value()) return false;
    auto& video_track = tracks_[video_location->track_index];
    auto& audio_track = tracks_[audio_location->track_index];
    if (hasOverlapTransitionForClip(audio_track, audio_clip_id)) return false;
    auto& video_clip = video_track.clips[video_location->clip_index];
    auto& audio_clip = audio_track.clips[audio_location->clip_index];
    if (video_track.kind != TrackKind::Video || video_clip.kind != ClipKind::Video ||
        audio_track.kind != TrackKind::Audio || audio_clip.kind != ClipKind::Audio ||
        video_clip.source_path != audio_clip.source_path ||
        video_clip.timeline_start_frame != audio_clip.timeline_start_frame) {
        return false;
    }
    video_clip.linked_clip_id = audio_clip_id;
    video_clip.audio_extracted = true;
    video_clip.audio_companion_pending = false;
    audio_clip.linked_clip_id = video_clip_id;
    return true;
}

bool TimelineModel::unlinkAudio(ClipId clip_id) {
    const auto location = locateClip(clip_id);
    if (!location.has_value()) return false;
    auto& clip = tracks_[location->track_index].clips[location->clip_index];
    if (!clip.linked_clip_id.has_value()) return false;
    const auto peer_id = *clip.linked_clip_id;
    const auto peer_location = locateClip(peer_id);
    if (peer_location.has_value()) {
        auto& peer = tracks_[peer_location->track_index].clips[peer_location->clip_index];
        if (peer.linked_clip_id == clip_id) {
            peer.linked_clip_id.reset();
            if (peer.kind == ClipKind::Video) peer.audio_extracted = true;
        }
    }
    clip.linked_clip_id.reset();
    if (clip.kind == ClipKind::Video) clip.audio_extracted = true;
    return true;
}

PendingMediaTimingMigrationResult TimelineModel::migratePendingMediaTiming(
    const std::filesystem::path& source_path,
    const media::VideoMetadata& metadata) {
    const auto canonical_source = canonicalPath(source_path);
    const auto source_kind = metadata.kind == media::MediaKind::Image
        ? ClipKind::Image
        : metadata.kind == media::MediaKind::Audio ? ClipKind::Audio
                                                   : ClipKind::Video;
    if (source_kind == ClipKind::Audio) {
        return PendingMediaTimingMigrationResult::NoPendingClips;
    }
    const auto source_rate = source_kind == ClipKind::Image
        ? media::kStillImageFrameRate
        : metadata.frame_rate.value_or(frame_rate_.asDouble());
    if (!validFrameRate(frame_rate_) || !std::isfinite(source_rate) ||
        source_rate <= 0.0 || source_rate > 1000.0) {
        return PendingMediaTimingMigrationResult::InvalidMetadata;
    }

    auto migrated_tracks = tracks_;
    bool found_pending = false;
    bool changed_track = false;
    const auto available_source_frames = sourceFrameCountFromMetadata(metadata);
    for (auto& track : migrated_tracks) {
        bool track_changed = false;
        for (auto& clip : track.clips) {
            if (!clip.source_duration_migration_pending ||
                !isFrameTimedMediaClipKind(clip.kind) ||
                canonicalPath(clip.source_path) != canonical_source) {
                continue;
            }
            found_pending = true;
            if (clip.source_duration_frames <= 0 || clip.source_start_frame < 0) {
                return PendingMediaTimingMigrationResult::InvalidMetadata;
            }
            if (available_source_frames.has_value() &&
                (clip.source_start_frame > *available_source_frames ||
                 clip.source_duration_frames > *available_source_frames -
                    clip.source_start_frame)) {
                return PendingMediaTimingMigrationResult::SourceRangeOutOfBounds;
            }
            const auto timeline_duration = timelineFramesForSourceDuration(
                clip.source_duration_frames, source_rate, frame_rate_);
            if (!timeline_duration.has_value() || clip.timeline_start_frame < 0 ||
                *timeline_duration > std::numeric_limits<std::int64_t>::max() -
                    clip.timeline_start_frame) {
                return PendingMediaTimingMigrationResult::InvalidMetadata;
            }

            clip.kind = source_kind;
            clip.timeline_duration_frames = *timeline_duration;
            clip.source_duration_migration_pending = false;
            clip.duration_seconds = metadata.duration_seconds;
            clip.frame_rate = source_rate;
            clip.frame_count = metadata.frame_count;
            clip.display_name = metadata.display_name;
            track_changed = true;
            changed_track = true;
        }
        if (track_changed && !preserveTransitionContinuity(track)) {
            return PendingMediaTimingMigrationResult::TimelineRangeOverflow;
        }
    }
    TimelineModel staged = *this;
    staged.tracks_ = std::move(migrated_tracks);
    std::vector<ClipId> pending_audio_video_clips;
    for (const auto& track : staged.tracks_) {
        for (const auto& clip : track.clips) {
            if (clip.kind == ClipKind::Video && clip.audio_companion_pending &&
                canonicalPath(clip.source_path) == canonical_source) {
                pending_audio_video_clips.push_back(clip.clip_id);
                found_pending = true;
            }
        }
    }
    for (const auto clip_id : pending_audio_video_clips) {
        if (metadata.audio.has_value()) {
            const auto result = staged.addAudioCompanion(clip_id, metadata);
            if (result != AddClipResult::Added) {
                return result == AddClipResult::InvalidTimingMetadata
                    ? PendingMediaTimingMigrationResult::InvalidMetadata
                    : PendingMediaTimingMigrationResult::TimelineRangeOverflow;
            }
        } else if (const auto location = staged.locateClip(clip_id)) {
            auto& clip = staged.tracks_[location->track_index].clips[location->clip_index];
            clip.audio_companion_pending = false;
            clip.audio_extracted = false;
        }
        changed_track = true;
    }
    if (!found_pending || !changed_track) {
        return PendingMediaTimingMigrationResult::NoPendingClips;
    }
    tracks_ = std::move(staged.tracks_);
    next_track_id_ = staged.next_track_id_;
    next_clip_id_ = staged.next_clip_id_;
    assertIdentityInvariants();
    return PendingMediaTimingMigrationResult::Migrated;
}

AddClipResult TimelineModel::addTextClip(
    std::size_t track_index,
    std::int64_t timeline_start_frame,
    std::int64_t duration_frames,
    double frame_rate) {
    auto* track = trackAt(track_index);
    if (track == nullptr) return AddClipResult::InvalidTrack;
    if (track->kind != TrackKind::Video) return AddClipResult::IncompatibleTrack;
    if (timeline_start_frame < 0 || duration_frames <= 0 ||
        duration_frames > std::numeric_limits<std::int64_t>::max() - timeline_start_frame ||
        !std::isfinite(frame_rate) || frame_rate <= 0.0) {
        return AddClipResult::InvalidPosition;
    }
    for (const auto& existing : track->clips) {
        if (overlapsSameKind(
                existing, ClipKind::Text, timeline_start_frame, duration_frames)) {
            return AddClipResult::Overlap;
        }
    }

    TimelineClip clip;
    clip.timeline_start_frame = timeline_start_frame;
    clip.timeline_duration_frames = duration_frames;
    clip.display_name = "Text";
    clip.duration_seconds = static_cast<double>(duration_frames) / frame_rate;
    clip.frame_rate = frame_rate;
    clip.frame_count = duration_frames;
    clip.clip_id = next_clip_id_++;
    clip.track_id = track->track_id;
    clip.kind = ClipKind::Text;
    track->clips.push_back(std::move(clip));
    std::stable_sort(track->clips.begin(), track->clips.end(),
              [](const auto& left, const auto& right) {
                  return left.timeline_start_frame < right.timeline_start_frame;
              });
    removeInvalidTransitions(*track);
    assertIdentityInvariants();
    return AddClipResult::Added;
}

MoveClipResult TimelineModel::moveClip(
    ClipLocation from,
    ClipLocation to,
    std::int64_t timeline_start_frame) {
    auto* source_track = trackAt(from.track_index);
    auto* target_track = trackAt(to.track_index);
    if (source_track == nullptr || from.clip_index >= source_track->clips.size()) {
        return MoveClipResult::InvalidIndex;
    }
    if (target_track == nullptr) return MoveClipResult::InvalidTrack;
    const auto clip = source_track->clips[from.clip_index];
    if ((target_track->kind == TrackKind::Audio) !=
        (clip.kind == ClipKind::Audio)) {
        return MoveClipResult::InvalidTrack;
    }
    if (timeline_start_frame < 0 || clip.timeline_duration_frames <= 0 ||
        timeline_start_frame > std::numeric_limits<std::int64_t>::max() -
            clip.timeline_duration_frames) {
        return MoveClipResult::InvalidPosition;
    }
    if (from.track_index == to.track_index && from.clip_index == to.clip_index &&
        clip.timeline_start_frame == timeline_start_frame) {
        return MoveClipResult::NoChange;
    }
    if (hasOverlapTransitionForClip(*source_track, clip.clip_id)) {
        auto staged = *this;
        if (!staged.removeOverlappingTransitionsForClip(from.track_index, clip.clip_id)) {
            return MoveClipResult::InvalidPosition;
        }
        const auto result = staged.moveClip(from, to, timeline_start_frame);
        if (result == MoveClipResult::Moved) *this = std::move(staged);
        return result;
    }
    for (std::size_t index = 0; index < target_track->clips.size(); ++index) {
        if (target_track == source_track && index == from.clip_index) continue;
        if (overlapsSameKind(target_track->clips[index], clip.kind,
                             timeline_start_frame, clip.timeline_duration_frames)) {
            return MoveClipResult::Overlap;
        }
    }
    source_track->clips.erase(
        source_track->clips.begin() + static_cast<std::ptrdiff_t>(from.clip_index));
    auto moved = clip;
    moved.timeline_start_frame = timeline_start_frame;
    moved.track_id = target_track->track_id;
    target_track->clips.push_back(std::move(moved));
    std::stable_sort(target_track->clips.begin(), target_track->clips.end(),
              [](const auto& left, const auto& right) {
                  return left.timeline_start_frame < right.timeline_start_frame;
              });
    removeInvalidTransitions(*source_track);
    if (target_track != source_track) removeInvalidTransitions(*target_track);
    assertIdentityInvariants();
    return MoveClipResult::Moved;
}

SplitClipResult TimelineModel::splitClip(
    std::size_t track_index,
    std::size_t clip_index,
    std::int64_t local_frame) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return SplitClipResult::InvalidIndex;
    }
    if (hasOverlapTransitionForClip(*track, track->clips[clip_index].clip_id)) {
        const auto clip_id = track->clips[clip_index].clip_id;
        auto staged = *this;
        if (!staged.removeOverlappingTransitionsForClip(track_index, clip_id)) {
            return SplitClipResult::InvalidBoundary;
        }
        const auto result = staged.splitClip(track_index, clip_index, local_frame);
        if (result == SplitClipResult::Split) *this = std::move(staged);
        return result;
    }
    auto& clip = track->clips[clip_index];
    if (local_frame <= 0 || local_frame >= clip.timeline_duration_frames ||
        clip.source_start_frame < 0) {
        return SplitClipResult::InvalidBoundary;
    }
    const auto source_duration = sourceDurationForClip(clip, frame_rate_);
    std::int64_t source_offset = 0;
    if (clip.kind == ClipKind::Video) {
        const auto mapped = sourceFrameOffsetForTimelineFrame(
            local_frame,
            clip.frame_rate.value_or(frame_rate_.asDouble()),
            frame_rate_,
            source_duration);
        if (!mapped.has_value() || *mapped <= 0 ||
            *mapped >= source_duration ||
            *mapped > std::numeric_limits<std::int64_t>::max() -
                clip.source_start_frame) {
            return SplitClipResult::InvalidBoundary;
        }
        source_offset = *mapped;
    }
    std::int64_t audio_source_offset_us = 0;
    if (clip.kind == ClipKind::Audio) {
        const auto mapped = timelineFramesToMicroseconds(local_frame, frame_rate_);
        if (!mapped.has_value() || *mapped <= 0 ||
            *mapped >= clip.source_duration_time_us ||
            *mapped > std::numeric_limits<std::int64_t>::max() -
                clip.source_start_time_us) {
            return SplitClipResult::InvalidBoundary;
        }
        audio_source_offset_us = *mapped;
    }
    const auto original_transform = clip.transform;
    TimelineClip right = clip;
    right.clip_id = next_clip_id_++;
    if (clip.kind == ClipKind::Audio) {
        clip.audio_gain_keyframes = sliceAudioGainEnvelope(
            clip, 0, local_frame);
        right.audio_gain_keyframes = sliceAudioGainEnvelope(
            right, local_frame, clip.timeline_duration_frames);
    }
    right.source_start_frame += source_offset;
    if (clip.kind == ClipKind::Audio) {
        right.source_start_time_us += audio_source_offset_us;
        right.source_duration_time_us -= audio_source_offset_us;
        clip.source_duration_time_us = audio_source_offset_us;
    }
    right.timeline_start_frame += local_frame;
    right.timeline_duration_frames -= local_frame;
    if (clip.kind == ClipKind::Video) {
        right.source_duration_frames = source_duration - source_offset;
        clip.source_duration_frames = source_offset;
    }
    right.keyframes = splitKeyframes(
        clip.transform,
        clip.keyframes,
        local_frame,
        right.transform);
    clip.keyframes = trimKeyframes(
        clip.transform,
        clip.keyframes,
        0,
        0,
        local_frame,
        clip.transform);
    clip.transform = original_transform;
    clip.timeline_duration_frames = local_frame;
    track->clips.insert(
        track->clips.begin() + static_cast<std::ptrdiff_t>(clip_index + 1),
        std::move(right));
    removeInvalidTransitions(*track);
    assertIdentityInvariants();
    return SplitClipResult::Split;
}

RemoveClipResult TimelineModel::removeClip(
    std::size_t track_index,
    std::size_t clip_index) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return RemoveClipResult::InvalidIndex;
    }
    if (hasOverlapTransitionForClip(*track, track->clips[clip_index].clip_id)) {
        const auto clip_id = track->clips[clip_index].clip_id;
        auto staged = *this;
        if (!staged.removeOverlappingTransitionsForClip(track_index, clip_id)) {
            return RemoveClipResult::InvalidIndex;
        }
        const auto result = staged.removeClip(track_index, clip_index);
        if (result == RemoveClipResult::Removed) *this = std::move(staged);
        return result;
    }
    track->clips.erase(track->clips.begin() + static_cast<std::ptrdiff_t>(clip_index));
    removeInvalidTransitions(*track);
    assertIdentityInvariants();
    return RemoveClipResult::Removed;
}

std::optional<RippleDeleteOutcome> TimelineModel::rippleDeleteClip(ClipId clip_id) {
    const auto source_location = locateClip(clip_id);
    if (!source_location.has_value()) return std::nullopt;
    const auto& source_track = tracks_[source_location->track_index];
    if (source_location->clip_index >= source_track.clips.size()) return std::nullopt;
    const auto source_clip = source_track.clips[source_location->clip_index];
    if (source_clip.timeline_start_frame < 0 ||
        source_clip.timeline_duration_frames <= 0 ||
        source_clip.timeline_start_frame > std::numeric_limits<std::int64_t>::max() -
            source_clip.timeline_duration_frames) {
        return std::nullopt;
    }

    const auto source_track_id = source_track.track_id;
    const auto source_track_index = source_location->track_index;
    const auto source_clip_index = source_location->clip_index;
    const auto ripple_frames = source_clip.timeline_duration_frames;
    std::vector<ClipId> downstream_clip_ids;
    for (std::size_t index = source_clip_index + 1;
         index < source_track.clips.size(); ++index) {
        downstream_clip_ids.push_back(source_track.clips[index].clip_id);
    }

    RippleDeleteOutcome outcome;
    std::unordered_set<ClipId> moving_ids;
    for (const auto downstream_id : downstream_clip_ids) {
        const auto location = locateClip(downstream_id);
        if (!location.has_value()) return std::nullopt;
        const auto& clip = tracks_[location->track_index].clips[location->clip_index];
        moving_ids.insert(clip.clip_id);
        if (!clip.linked_clip_id.has_value()) continue;
        const auto peer = locateClip(*clip.linked_clip_id);
        if (!peer.has_value()) return std::nullopt;
        moving_ids.insert(*clip.linked_clip_id);
    }

    TimelineModel staged = *this;
    const auto staged_source = staged.locateClip(clip_id);
    if (!staged_source.has_value() || staged_source->track_index != source_track_index ||
        staged.removeClip(staged_source->track_index, staged_source->clip_index) !=
            RemoveClipResult::Removed) {
        return std::nullopt;
    }
    outcome.removed_clip_ids.push_back(clip_id);
    const auto add_track = [&outcome](TrackId track_id) {
        if (std::find(outcome.affected_track_ids.begin(),
                      outcome.affected_track_ids.end(), track_id) ==
            outcome.affected_track_ids.end()) {
            outcome.affected_track_ids.push_back(track_id);
        }
    };
    const auto add_clip = [](std::vector<ClipId>& ids, ClipId value) {
        if (std::find(ids.begin(), ids.end(), value) == ids.end()) ids.push_back(value);
    };
    add_track(source_track_id);

    if (source_clip.linked_clip_id.has_value()) {
        const auto peer = staged.locateClip(*source_clip.linked_clip_id);
        if (!peer.has_value()) return std::nullopt;
        const auto peer_track_id = staged.tracks_[peer->track_index].track_id;
        if (staged.removeClip(peer->track_index, peer->clip_index) !=
            RemoveClipResult::Removed) {
            return std::nullopt;
        }
        add_clip(outcome.removed_clip_ids, *source_clip.linked_clip_id);
        add_track(peer_track_id);
    }

    std::unordered_map<ClipId, std::int64_t> displacement_by_clip;
    const auto maximum_shift_before_blocker = [&staged, &moving_ids](
        const TimelineClip& moving_clip,
        std::size_t moving_track_index,
        std::int64_t requested_shift) -> std::optional<std::int64_t> {
        if (moving_clip.timeline_start_frame < 0 ||
            moving_clip.timeline_duration_frames <= 0 || requested_shift < 0) {
            return std::nullopt;
        }
        if (!clipTimelineEnd(moving_clip).has_value()) return std::nullopt;
        auto maximum_shift = std::min(requested_shift,
                                      moving_clip.timeline_start_frame);
        const auto& track = staged.tracks_[moving_track_index];
        for (const auto& obstacle : track.clips) {
            const bool same_overlap_class = obstacle.kind == moving_clip.kind ||
                (isFrameTimedMediaClipKind(obstacle.kind) &&
                 isFrameTimedMediaClipKind(moving_clip.kind));
            if (obstacle.clip_id == moving_clip.clip_id ||
                moving_ids.contains(obstacle.clip_id) ||
                obstacle.timeline_start_frame > moving_clip.timeline_start_frame ||
                !same_overlap_class) {
                continue;
            }
            const auto obstacle_end = clipTimelineEnd(obstacle);
            if (!obstacle_end.has_value()) return std::nullopt;
            const auto available = *obstacle_end >= moving_clip.timeline_start_frame
                ? std::int64_t{0}
                : moving_clip.timeline_start_frame - *obstacle_end;
            maximum_shift = std::min(maximum_shift, available);
        }
        return maximum_shift;
    };

    for (const auto downstream_id : downstream_clip_ids) {
        const auto primary_location = staged.locateClip(downstream_id);
        if (!primary_location.has_value() ||
            primary_location->track_index != source_track_index) {
            return std::nullopt;
        }
        const auto primary_clip = staged.tracks_[primary_location->track_index]
            .clips[primary_location->clip_index];
        std::optional<ClipLocation> peer_location;
        TimelineClip peer_clip;
        if (primary_clip.linked_clip_id.has_value()) {
            peer_location = staged.locateClip(*primary_clip.linked_clip_id);
            if (!peer_location.has_value()) return std::nullopt;
            peer_clip = staged.tracks_[peer_location->track_index]
                .clips[peer_location->clip_index];
        }

        auto allowed_shift = maximum_shift_before_blocker(
            primary_clip, primary_location->track_index, ripple_frames);
        if (!allowed_shift.has_value()) return std::nullopt;
        if (peer_location.has_value()) {
            const auto peer_shift = maximum_shift_before_blocker(
                peer_clip, peer_location->track_index, ripple_frames);
            if (!peer_shift.has_value()) return std::nullopt;
            allowed_shift = std::min(*allowed_shift, *peer_shift);
        }

        if (*allowed_shift > 0) {
            auto refreshed_primary = staged.locateClip(primary_clip.clip_id);
            if (!refreshed_primary.has_value()) return std::nullopt;
            auto& mutable_primary = staged.tracks_[refreshed_primary->track_index]
                .clips[refreshed_primary->clip_index];
            mutable_primary.timeline_start_frame -= *allowed_shift;
            displacement_by_clip[primary_clip.clip_id] = *allowed_shift;
            add_clip(outcome.moved_clip_ids, primary_clip.clip_id);
            add_track(staged.tracks_[refreshed_primary->track_index].track_id);

            if (peer_location.has_value()) {
                auto refreshed_peer = staged.locateClip(peer_clip.clip_id);
                if (!refreshed_peer.has_value()) return std::nullopt;
                auto& mutable_peer = staged.tracks_[refreshed_peer->track_index]
                    .clips[refreshed_peer->clip_index];
                mutable_peer.timeline_start_frame -= *allowed_shift;
                displacement_by_clip[peer_clip.clip_id] = *allowed_shift;
                add_clip(outcome.moved_clip_ids, peer_clip.clip_id);
                add_track(staged.tracks_[refreshed_peer->track_index].track_id);
            }
        }

        if (*allowed_shift < ripple_frames) {
            outcome.stopped_at_collision = true;
            break;
        }
    }

    for (auto& track : staged.tracks_) {
        std::stable_sort(track.clips.begin(), track.clips.end(),
            [](const TimelineClip& left, const TimelineClip& right) {
                return left.timeline_start_frame < right.timeline_start_frame;
            });
        for (auto transition = track.transitions.begin();
             transition != track.transitions.end();) {
            const auto from_shift = displacement_by_clip.find(
                transition->from_clip_id);
            const auto to_shift = displacement_by_clip.find(
                transition->to_clip_id);
            const auto from_displacement = from_shift == displacement_by_clip.end()
                ? std::int64_t{0} : from_shift->second;
            const auto to_displacement = to_shift == displacement_by_clip.end()
                ? std::int64_t{0} : to_shift->second;
            if (from_displacement == to_displacement) {
                ++transition;
                continue;
            }

            const auto indexes = transitionClipIndexes(track, *transition);
            bool keep = indexes.has_value() &&
                indexes->second == indexes->first + 1;
            if (keep && isOverlapTransition(transition->kind)) {
                const auto& from = track.clips[indexes->first];
                const auto& to = track.clips[indexes->second];
                const auto from_end = clipTimelineEnd(from);
                const auto overlap = from_end.has_value()
                    ? *from_end - to.timeline_start_frame : 0;
                const auto maximum = std::min(
                    from.timeline_duration_frames, to.timeline_duration_frames);
                keep = overlap > 0 && maximum > 0;
                if (keep) transition->duration_frames = std::min(overlap, maximum);
            } else if (keep) {
                const auto& from = track.clips[indexes->first];
                const auto& to = track.clips[indexes->second];
                const auto from_end = clipTimelineEnd(from);
                keep = from_end.has_value() && *from_end == to.timeline_start_frame;
            }
            if (keep) {
                ++transition;
            } else {
                transition = track.transitions.erase(transition);
            }
        }
        removeInvalidTransitions(track);
    }

    for (std::size_t track_index = 0;
         track_index < staged.tracks_.size(); ++track_index) {
        const auto& track = staged.tracks_[track_index];
        for (std::size_t left = 0; left < track.clips.size(); ++left) {
            for (std::size_t right = left + 1; right < track.clips.size(); ++right) {
                if (!overlapsSameKind(
                        track.clips[left], track.clips[right].kind,
                        track.clips[right].timeline_start_frame,
                        track.clips[right].timeline_duration_frames)) {
                    continue;
                }
                const auto* transition = staged.transitionBetween(
                    track_index, left, right);
                if (right != left + 1 || transition == nullptr ||
                    !isOverlapTransition(transition->kind)) {
                    return std::nullopt;
                }
            }
        }
    }

    staged.assertIdentityInvariants();
    *this = std::move(staged);
    return outcome;
}

TrimClipResult TimelineModel::trimClip(
    std::size_t track_index,
    std::size_t clip_index,
    std::int64_t new_source_start_frame,
    std::int64_t new_duration_frames,
    std::optional<std::int64_t> new_source_start_time_us) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return TrimClipResult::InvalidIndex;
    }
    if (hasOverlapTransitionForClip(*track, track->clips[clip_index].clip_id)) {
        const auto clip_id = track->clips[clip_index].clip_id;
        auto staged = *this;
        if (!staged.removeOverlappingTransitionsForClip(track_index, clip_id)) {
            return TrimClipResult::InvalidRange;
        }
        const auto result = staged.trimClip(
            track_index, clip_index, new_source_start_frame, new_duration_frames,
            new_source_start_time_us);
        if (result == TrimClipResult::Trimmed) *this = std::move(staged);
        return result;
    }
    auto& clip = track->clips[clip_index];
    if (clip.kind == ClipKind::Audio) {
        if (!new_source_start_time_us.has_value() ||
            *new_source_start_time_us < clip.source_start_time_us ||
            clip.source_duration_time_us <= 0 || new_duration_frames <= 0) {
            return TrimClipResult::InvalidRange;
        }
        const auto new_duration_us = timelineFramesToMicroseconds(
            new_duration_frames, frame_rate_);
        if (clip.source_start_time_us < 0 ||
            clip.source_duration_time_us >
                std::numeric_limits<std::int64_t>::max() -
                    clip.source_start_time_us) {
            return TrimClipResult::InvalidRange;
        }
        const auto old_end_us = clip.source_start_time_us +
            clip.source_duration_time_us;
        const auto source_limit = audioSourceLimitUs(clip);
        if (!new_duration_us.has_value() || !source_limit.has_value() ||
            *new_source_start_time_us >= old_end_us ||
            *new_source_start_time_us >= *source_limit) {
            return TrimClipResult::InvalidRange;
        }
        for (std::size_t index = 0; index < track->clips.size(); ++index) {
            if (index != clip_index && overlapsSameKind(
                    track->clips[index], clip.kind,
                    clip.timeline_start_frame, new_duration_frames)) {
                return TrimClipResult::InvalidRange;
            }
        }
        const auto source_delta = *new_source_start_time_us -
            clip.source_start_time_us;
        const auto local_start = timelineFramesForMicroseconds(
            source_delta, frame_rate_).value_or(0);
        const auto audio_gain_keyframes = sliceAudioGainEnvelope(
            clip, local_start, local_start + new_duration_frames);
        Transform2D trimmed_transform;
        const auto trimmed_keyframes = trimKeyframes(
            clip.transform, clip.keyframes, 0, local_start,
            new_duration_frames, trimmed_transform);
        clip.source_start_time_us = *new_source_start_time_us;
        clip.source_duration_time_us = std::min(
            *new_duration_us,
            *source_limit - *new_source_start_time_us);
        clip.timeline_duration_frames = new_duration_frames;
        clip.audio_gain_keyframes = audio_gain_keyframes;
        clip.transform = trimmed_transform;
        clip.keyframes = trimmed_keyframes;
        removeInvalidTransitions(*track);
        return TrimClipResult::Trimmed;
    }
    const auto old_source_duration = sourceDurationForClip(clip, frame_rate_);
    if (clip.source_start_frame < 0 || clip.timeline_duration_frames <= 0 ||
        old_source_duration < 0 ||
        new_source_start_frame < clip.source_start_frame ||
        new_source_start_frame < 0 || new_duration_frames <= 0 ||
        new_source_start_frame > std::numeric_limits<std::int64_t>::max() -
            old_source_duration) {
        return TrimClipResult::InvalidRange;
    }
    const auto old_end = clip.source_start_frame + old_source_duration;
    const auto converted_duration = clip.kind == ClipKind::Video
        ? sourceFramesForTimelineDuration(
              new_duration_frames,
              clip.frame_rate.value_or(frame_rate_.asDouble()), frame_rate_)
        : std::optional<std::int64_t>(old_source_duration);
    if (!converted_duration.has_value() ||
        new_source_start_frame > std::numeric_limits<std::int64_t>::max() -
            *converted_duration) {
        return TrimClipResult::InvalidRange;
    }
    const auto new_end = new_source_start_frame + *converted_duration;
    if (clip.source_start_frame > old_end || new_source_start_frame >= old_end ||
        new_end > old_end) {
        return TrimClipResult::InvalidRange;
    }
    for (std::size_t index = 0; index < track->clips.size(); ++index) {
        if (index != clip_index && overlapsSameKind(track->clips[index], clip.kind,
                                                    clip.timeline_start_frame,
                                                    new_duration_frames)) {
            return TrimClipResult::InvalidRange;
        }
    }
    const auto source_delta = new_source_start_frame - clip.source_start_frame;
    const auto local_start = clip.kind == ClipKind::Video
        ? timelineFrameOffsetForSourceFrame(
              source_delta,
              clip.frame_rate.value_or(frame_rate_.asDouble()), frame_rate_).value_or(0)
        : source_delta;
    Transform2D trimmed_transform;
    const auto trimmed_keyframes = trimKeyframes(
        clip.transform,
        clip.keyframes,
        0,
        local_start,
        new_duration_frames,
        trimmed_transform);
    clip.source_start_frame = new_source_start_frame;
    clip.source_duration_frames = *converted_duration;
    clip.timeline_duration_frames = new_duration_frames;
    clip.transform = trimmed_transform;
    clip.keyframes = trimmed_keyframes;
    removeInvalidTransitions(*track);
    return TrimClipResult::Trimmed;
}

TrimClipResult TimelineModel::trimClipEdge(
    std::size_t track_index,
    std::size_t clip_index,
    ClipEdge edge,
    std::int64_t boundary_frame,
    ClipEdgeEditMode mode) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return TrimClipResult::InvalidIndex;
    }
    if (hasOverlapTransitionForClip(*track, track->clips[clip_index].clip_id)) {
        const auto clip_id = track->clips[clip_index].clip_id;
        auto staged = *this;
        if (!staged.removeOverlappingTransitionsForClip(track_index, clip_id)) {
            return TrimClipResult::InvalidRange;
        }
        const auto result = staged.trimClipEdge(
            track_index, clip_index, edge, boundary_frame, mode);
        if (result == TrimClipResult::Trimmed) *this = std::move(staged);
        return result;
    }
    const auto preview = previewClipEdgeEdit(
        tracks_, ClipLocation{track_index, clip_index}, edge, boundary_frame, mode,
        frame_rate_);
    if (!preview.has_value()) return TrimClipResult::InvalidRange;

    const bool clip_changed = preview->clip != track->clips[clip_index];
    const bool neighbor_changed = preview->neighbor_location.has_value() &&
        preview->neighbor_clip.has_value() &&
        *preview->neighbor_clip != track->clips[
            preview->neighbor_location->clip_index];
    if (!clip_changed && !neighbor_changed) return TrimClipResult::NoChange;

    track->clips[clip_index] = preview->clip;
    if (preview->neighbor_location.has_value() && preview->neighbor_clip.has_value()) {
        track->clips[preview->neighbor_location->clip_index] = *preview->neighbor_clip;
    }
    std::stable_sort(track->clips.begin(), track->clips.end(),
              [](const auto& left, const auto& right) {
                  return left.timeline_start_frame < right.timeline_start_frame;
              });
    removeInvalidTransitions(*track);
    return TrimClipResult::Trimmed;
}

AddClipResult TimelineModel::addClip(const media::VideoMetadata& metadata) {
    const auto* track = trackAt(0);
    return addClip(0, metadata, track == nullptr ? 0 : trackEnd(*track));
}

MoveClipResult TimelineModel::moveClip(std::size_t from_index, std::size_t to_index) {
    if (from_index >= clipCount() || to_index >= clipCount()) {
        return MoveClipResult::InvalidIndex;
    }
    auto* track = trackAt(0);
    if (from_index == to_index) return MoveClipResult::NoChange;
    if (from_index < to_index) {
        std::rotate(track->clips.begin() + static_cast<std::ptrdiff_t>(from_index),
                    track->clips.begin() + static_cast<std::ptrdiff_t>(from_index + 1),
                    track->clips.begin() + static_cast<std::ptrdiff_t>(to_index + 1));
    } else {
        std::rotate(track->clips.begin() + static_cast<std::ptrdiff_t>(to_index),
                    track->clips.begin() + static_cast<std::ptrdiff_t>(from_index),
                    track->clips.begin() + static_cast<std::ptrdiff_t>(from_index + 1));
    }
    std::int64_t start = 0;
    for (auto& clip : track->clips) {
        clip.timeline_start_frame = start;
        start += clip.timeline_duration_frames;
    }
    return MoveClipResult::Moved;
}

SplitClipResult TimelineModel::splitClip(std::size_t clip_index, std::int64_t local_frame) {
    return splitClip(0, clip_index, local_frame);
}

RemoveClipResult TimelineModel::removeClip(std::size_t clip_index) {
    return removeClip(0, clip_index);
}

TrimClipResult TimelineModel::trimClip(
    std::size_t clip_index,
    std::int64_t new_source_start_frame,
    std::int64_t new_duration_frames) {
    return trimClip(0, clip_index, new_source_start_frame, new_duration_frames);
}

void TimelineModel::clear() noexcept {
    for (auto& track : tracks_) {
        track.clips.clear();
        track.transitions.clear();
    }
}

bool TimelineModel::hasClip() const noexcept {
    return std::any_of(tracks_.begin(), tracks_.end(),
                       [](const auto& track) { return !track.clips.empty(); });
}

std::size_t TimelineModel::clipCount() const noexcept {
    std::size_t count = 0;
    for (const auto& track : tracks_) count += track.clips.size();
    return count;
}

std::size_t TimelineModel::clipCount(std::size_t track_index) const noexcept {
    const auto* track = trackAt(track_index);
    return track == nullptr ? 0 : track->clips.size();
}

std::size_t TimelineModel::trackCount() const noexcept {
    return tracks_.size();
}

std::int64_t TimelineModel::totalDurationFrames() const noexcept {
    std::int64_t end = 0;
    for (const auto& track : tracks_) end = std::max(end, trackEnd(track));
    return end;
}

const std::vector<TimelineTrack>& TimelineModel::tracks() const noexcept {
    return tracks_;
}

FrameRate TimelineModel::frameRate() const noexcept {
    return frame_rate_;
}

bool TimelineModel::setImageEditorVariant(
    ClipId clip_id,
    std::optional<media::LinkedImageReference> link) {
    const auto location = locateClip(clip_id);
    if (!location.has_value()) return false;
    auto& clip = tracks_[location->track_index].clips[location->clip_index];
    if (clip.kind != ClipKind::Image || clip.image_editor_variant == link) return false;
    clip.image_editor_variant = std::move(link);
    return true;
}

bool TimelineModel::setStillImageOverride(
    ClipId clip_id,
    std::shared_ptr<const media::VideoFrame> frame) {
    const auto location = locateClip(clip_id);
    if (!location.has_value()) return false;
    auto& clip = tracks_[location->track_index].clips[location->clip_index];
    if (clip.kind != ClipKind::Image || clip.still_image_override == frame) return false;
    clip.still_image_override = std::move(frame);
    return true;
}

std::optional<std::size_t> TimelineModel::firstClipIndexForSource(
    const std::filesystem::path& source_path) const {
    if (tracks_.empty()) return std::nullopt;
    const auto canonical_source = canonicalPath(source_path);
    for (std::size_t index = 0; index < tracks_.front().clips.size(); ++index) {
        if (canonicalPath(tracks_.front().clips[index].source_path) == canonical_source) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<ClipLocation> TimelineModel::clipAt(
    std::size_t track_index,
    std::int64_t timeline_frame) const {
    const auto* track = trackAt(track_index);
    if (track == nullptr || timeline_frame < 0) return std::nullopt;
    std::optional<ClipLocation> video_match;
    for (std::size_t index = 0; index < track->clips.size(); ++index) {
        const auto& clip = track->clips[index];
        if (timeline_frame >= clip.timeline_start_frame &&
            timeline_frame < clip.timeline_start_frame + clip.timeline_duration_frames) {
            if (clip.kind == ClipKind::Text) return ClipLocation{track_index, index};
            video_match = ClipLocation{track_index, index};
        }
    }
    return video_match;
}

std::optional<ClipLocation> TimelineModel::topClipAt(std::int64_t timeline_frame) const {
    for (std::size_t track = 0; track < tracks_.size(); ++track) {
        const auto location = clipAt(track, timeline_frame);
        if (!location.has_value()) continue;
        const auto& timeline_track = tracks_[track];
        const auto& visible_clip = timeline_track.clips[location->clip_index];
        for (const auto& transition : timeline_track.transitions) {
            if (transition.kind != TransitionKind::CrossDissolve ||
                transition.to_clip_id != visible_clip.clip_id) {
                continue;
            }
            const auto indexes = transitionClipIndexes(timeline_track, transition);
            if (!indexes.has_value() || indexes->second != indexes->first + 1) continue;
            const auto& from = timeline_track.clips[indexes->first];
            if (from.timeline_start_frame < 0 || from.timeline_duration_frames <= 0 ||
                from.timeline_start_frame > std::numeric_limits<std::int64_t>::max() -
                    from.timeline_duration_frames) {
                continue;
            }
            const auto cut_frame = from.timeline_start_frame +
                from.timeline_duration_frames;
            if (visible_clip.timeline_start_frame == cut_frame -
                    transition.duration_frames &&
                timeline_frame < cut_frame) {
                return ClipLocation{track, indexes->first};
            }
        }
        return location;
    }
    return std::nullopt;
}

std::optional<std::size_t> TimelineModel::locateTrack(TrackId track_id) const {
    if (track_id == 0) return std::nullopt;
    for (std::size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
        if (tracks_[track_index].track_id == track_id) return track_index;
    }
    return std::nullopt;
}

std::optional<ClipLocation> TimelineModel::locateClip(ClipId clip_id) const {
    for (std::size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
        for (std::size_t clip_index = 0; clip_index < tracks_[track_index].clips.size(); ++clip_index) {
            if (tracks_[track_index].clips[clip_index].clip_id == clip_id) {
                return ClipLocation{track_index, clip_index};
            }
        }
    }
    return std::nullopt;
}

const TimelineTransition* TimelineModel::transitionBetween(
    std::size_t track_index,
    std::size_t from_clip_index,
    std::size_t to_clip_index) const noexcept {
    const auto* track = trackAt(track_index);
    if (track == nullptr || from_clip_index >= track->clips.size() ||
        to_clip_index >= track->clips.size()) {
        return nullptr;
    }
    const auto from_id = track->clips[from_clip_index].clip_id;
    const auto to_id = track->clips[to_clip_index].clip_id;
    const auto found = std::find_if(
        track->transitions.begin(),
        track->transitions.end(),
        [from_id, to_id](const TimelineTransition& transition) {
            return transition.from_clip_id == from_id &&
                transition.to_clip_id == to_id;
        });
    return found == track->transitions.end() ? nullptr : &*found;
}

TimelineModel::Snapshot TimelineModel::snapshot() const {
    assertIdentityInvariants();
    Snapshot result;
    result.tracks = tracks_;
    result.next_track_id = next_track_id_;
    result.next_clip_id = next_clip_id_;
    result.frame_rate = frame_rate_;
    return result;
}

void TimelineModel::ensureIdentifiers() {
    TrackId max_track = 0;
    ClipId max_clip = 0;
    for (auto& track : tracks_) {
        if (track.track_id == 0) track.track_id = next_track_id_++;
        max_track = std::max(max_track, track.track_id);
        for (auto& clip : track.clips) {
            clip.track_id = track.track_id;
            if (clip.clip_id == 0) clip.clip_id = next_clip_id_++;
            max_clip = std::max(max_clip, clip.clip_id);
        }
    }
    next_track_id_ = std::max(next_track_id_, max_track + 1);
    next_clip_id_ = std::max(next_clip_id_, max_clip + 1);
}

void TimelineModel::restore(Snapshot snapshot) {
    tracks_ = std::move(snapshot.tracks);
    frame_rate_ = validFrameRate(snapshot.frame_rate)
        ? reducedFrameRate(snapshot.frame_rate)
        : FrameRate{};
    next_track_id_ = snapshot.next_track_id;
    next_clip_id_ = snapshot.next_clip_id;
    ensureIdentifiers();
    if (tracks_.empty()) {
        appendDefaultTracks(tracks_, next_track_id_);
    }
    for (auto& track : tracks_) removeInvalidTransitions(track);
    for (auto& track : tracks_) {
        for (auto& clip : track.clips) {
            if (clip.source_duration_frames > 0 ||
                clip.kind == ClipKind::Text || clip.kind == ClipKind::Audio) {
                continue;
            }
            clip.source_duration_frames = sourceDurationForClip(clip, frame_rate_);
        }
    }
    assertIdentityInvariants();
}

void TimelineModel::assertIdentityInvariants() const {
#ifndef NDEBUG
    std::unordered_set<TrackId> track_ids;
    std::unordered_set<ClipId> clip_ids;
    for (const auto& track : tracks_) {
        assert(track.track_id != 0);
        assert(track_ids.insert(track.track_id).second);
        for (const auto& clip : track.clips) {
            assert(clip.clip_id != 0);
            assert(clip.track_id == track.track_id);
            assert(clip_ids.insert(clip.clip_id).second);
        }
    }
#endif
}

std::optional<std::pair<std::size_t, std::size_t>>
TimelineModel::transitionClipIndexes(
    const TimelineTrack& track,
    const TimelineTransition& transition) noexcept {
    std::optional<std::size_t> from_index;
    std::optional<std::size_t> to_index;
    for (std::size_t index = 0; index < track.clips.size(); ++index) {
        if (track.clips[index].clip_id == transition.from_clip_id) {
            from_index = index;
        }
        if (track.clips[index].clip_id == transition.to_clip_id) {
            to_index = index;
        }
    }
    if (!from_index.has_value() || !to_index.has_value()) return std::nullopt;
    return std::make_pair(*from_index, *to_index);
}

bool TimelineModel::removeOverlappingTransitionsForClip(
    std::size_t track_index,
    ClipId clip_id) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_id == 0) return false;

    struct AttachedTransition {
        std::size_t from_index = 0;
        std::size_t to_index = 0;
        ClipId from_id = 0;
        ClipId to_id = 0;
    };
    std::vector<AttachedTransition> attached;
    for (const auto& transition : track->transitions) {
        if (!isOverlapTransition(transition.kind) ||
            (transition.from_clip_id != clip_id &&
             transition.to_clip_id != clip_id)) {
            continue;
        }
        const auto indexes = transitionClipIndexes(*track, transition);
        if (!indexes.has_value()) continue;
        attached.push_back({
            indexes->first, indexes->second,
            transition.from_clip_id, transition.to_clip_id});
    }
    if (attached.empty()) return false;
    std::sort(attached.begin(), attached.end(),
        [](const AttachedTransition& left, const AttachedTransition& right) {
            return left.from_index < right.from_index;
        });

    for (const auto& item : attached) {
        auto transition = std::find_if(
            track->transitions.begin(), track->transitions.end(),
            [&item](const TimelineTransition& candidate) {
                return candidate.from_clip_id == item.from_id &&
                    candidate.to_clip_id == item.to_id &&
                    isOverlapTransition(candidate.kind);
            });
        if (transition == track->transitions.end()) continue;
        const auto indexes = transitionClipIndexes(*track, *transition);
        if (!indexes.has_value() || indexes->second <= indexes->first) {
            track->transitions.erase(transition);
            continue;
        }
        const auto& from = track->clips[indexes->first];
        if (from.timeline_start_frame < 0 || from.timeline_duration_frames <= 0 ||
            from.timeline_start_frame > std::numeric_limits<std::int64_t>::max() -
                from.timeline_duration_frames) {
            return false;
        }
        auto target_start = from.timeline_start_frame + from.timeline_duration_frames;
        for (std::size_t index = indexes->first + 1;
             index < indexes->second; ++index) {
            const auto end = clipTimelineEnd(track->clips[index]);
            if (!end.has_value()) return false;
            target_start = std::max(target_start, *end);
        }
        target_start = std::max(target_start,
            track->clips[indexes->second].timeline_start_frame);
        const auto delta = target_start -
            track->clips[indexes->second].timeline_start_frame;
        if (delta > 0 && !shiftTimelineSuffix(*track, indexes->second, delta)) {
            return false;
        }
        track->transitions.erase(transition);
    }
    return true;
}

void TimelineModel::removeInvalidTransitions(TimelineTrack& track) noexcept {
    std::size_t index = 0;
    while (index < track.transitions.size()) {
        auto& transition = track.transitions[index];
        const auto indexes = transitionClipIndexes(track, transition);
        const auto pair = std::make_pair(
            transition.from_clip_id, transition.to_clip_id);
        bool duplicate_pair = false;
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (track.transitions[prior].from_clip_id == pair.first &&
                track.transitions[prior].to_clip_id == pair.second) {
                duplicate_pair = true;
                break;
            }
        }
        bool keep = validTransitionKind(transition.kind) &&
            indexes.has_value() && indexes->second == indexes->first + 1 &&
            transition.duration_frames > 0;
        if (keep && transition.kind == TransitionKind::AudioCrossfade) {
            const auto& from = track.clips[indexes->first];
            const auto& to = track.clips[indexes->second];
            keep = track.kind == TrackKind::Audio &&
                from.kind == ClipKind::Audio && to.kind == ClipKind::Audio &&
                !from.linked_clip_id.has_value() &&
                !to.linked_clip_id.has_value();
        } else if (keep) {
            keep = track.kind == TrackKind::Video;
        }
        if (keep) {
            keep = track.clips[indexes->first].kind != ClipKind::Text ||
                track.clips[indexes->second].kind != ClipKind::Text;
        }
        if (keep) {
            keep = !duplicate_pair;
        }
        if (keep) {
            const auto& from = track.clips[indexes->first];
            const auto& to = track.clips[indexes->second];
            const auto maximum = std::min(
                from.timeline_duration_frames, to.timeline_duration_frames);
            keep = maximum > 0 && from.timeline_start_frame >= 0 &&
                from.timeline_duration_frames > 0 &&
                from.timeline_start_frame <= std::numeric_limits<std::int64_t>::max() -
                    from.timeline_duration_frames;
            if (keep) {
                transition.duration_frames = std::min(
                    transition.duration_frames, maximum);
                const auto from_end = from.timeline_start_frame +
                    from.timeline_duration_frames;
                const auto target_start = isOverlapTransition(transition.kind)
                    ? from_end - transition.duration_frames
                    : from_end;
                const auto delta = target_start - to.timeline_start_frame;
                keep = shiftTimelineSuffix(track, indexes->second, delta);
            }
        }
        if (keep) {
            ++index;
            continue;
        }

        // If an invalidated Cross Dissolve left its endpoints overlapping,
        // move the later clip and its suffix to the first free frame.
        if (!duplicate_pair && isOverlapTransition(transition.kind) &&
            indexes.has_value() &&
            indexes->second > indexes->first) {
            const auto to_index = indexes->second;
            std::optional<std::int64_t> occupied_end;
            for (std::size_t prior = indexes->first; prior < to_index; ++prior) {
                const auto end = clipTimelineEnd(track.clips[prior]);
                if (!end.has_value()) continue;
                occupied_end = occupied_end.has_value()
                    ? std::max(*occupied_end, *end)
                    : *end;
            }
            if (occupied_end.has_value() &&
                track.clips[to_index].timeline_start_frame < *occupied_end) {
                const auto delta = *occupied_end -
                    track.clips[to_index].timeline_start_frame;
                static_cast<void>(shiftTimelineSuffix(track, to_index, delta));
            }
        }
        track.transitions.erase(track.transitions.begin() +
            static_cast<std::ptrdiff_t>(index));
    }
}

void TimelineModel::updateDisplayNameForSource(
    const std::filesystem::path& source_path,
    const std::string& display_name) {
    const auto canonical_source = canonicalPath(source_path);
    for (auto& track : tracks_) {
        for (auto& clip : track.clips) {
            if (isMediaClipKind(clip.kind) &&
                canonicalPath(clip.source_path) == canonical_source) {
                clip.display_name = display_name;
            }
        }
    }
}

AudioParameterResult TimelineModel::setClipAudio(
    std::size_t track_index,
    std::size_t clip_index,
    double gain,
    bool muted) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return AudioParameterResult::InvalidIndex;
    }
    if (!validAudioGain(gain)) return AudioParameterResult::InvalidValue;
    auto& clip = track->clips[clip_index];
    if (clip.audio_gain == gain && clip.audio_muted == muted) {
        return AudioParameterResult::NoChange;
    }
    clip.audio_gain = gain;
    clip.audio_muted = muted;
    return AudioParameterResult::Changed;
}

AudioParameterResult TimelineModel::setTrackAudio(
    std::size_t track_index,
    double gain,
    bool muted) {
    auto* track = trackAt(track_index);
    if (track == nullptr) return AudioParameterResult::InvalidIndex;
    if (!validAudioGain(gain)) return AudioParameterResult::InvalidValue;
    if (track->audio_gain == gain && track->audio_muted == muted) {
        return AudioParameterResult::NoChange;
    }
    track->audio_gain = gain;
    track->audio_muted = muted;
    return AudioParameterResult::Changed;
}

AudioParameterResult TimelineModel::setClipAudioGainKeyframes(
    std::size_t track_index,
    std::size_t clip_index,
    std::vector<AudioGainKeyframe> keyframes) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return AudioParameterResult::InvalidIndex;
    }
    auto& clip = track->clips[clip_index];
    if (clip.kind != ClipKind::Audio ||
        !validAudioGainKeyframes(keyframes, clip.timeline_duration_frames)) {
        return AudioParameterResult::InvalidValue;
    }
    if (clip.audio_gain_keyframes == keyframes) {
        return AudioParameterResult::NoChange;
    }
    clip.audio_gain_keyframes = std::move(keyframes);
    return AudioParameterResult::Changed;
}

TransformParameterResult TimelineModel::setClipTransform(
    std::size_t track_index,
    std::size_t clip_index,
    const Transform2D& transform) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return TransformParameterResult::InvalidIndex;
    }
    if (!validTransform(transform)) return TransformParameterResult::InvalidValue;
    auto& clip = track->clips[clip_index];
    if (clip.transform == transform) return TransformParameterResult::NoChange;
    clip.transform = transform;
    return TransformParameterResult::Changed;
}

TransformParameterResult TimelineModel::setClipTransformAttributes(
    std::size_t track_index,
    std::size_t clip_index,
    const Transform2D& transform,
    TransformKeyframes keyframes) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return TransformParameterResult::InvalidIndex;
    }
    auto& clip = track->clips[clip_index];
    if (!validTransform(transform) ||
        !creative_suite::animation::validTransformKeyframes(keyframes) ||
        clip.timeline_duration_frames <= 0) {
        return TransformParameterResult::InvalidValue;
    }
    for (const auto property : {TransformProperty::PositionX,
                                TransformProperty::PositionY,
                                TransformProperty::Scale,
                                TransformProperty::Rotation,
                                TransformProperty::Opacity}) {
        for (const auto& keyframe : keyframesFor(keyframes, property)) {
            if (keyframe.frame < 0 || keyframe.frame >= clip.timeline_duration_frames) {
                return TransformParameterResult::InvalidValue;
            }
        }
    }
    if (clip.transform == transform && clip.keyframes == keyframes) {
        return TransformParameterResult::NoChange;
    }
    clip.transform = transform;
    clip.keyframes = std::move(keyframes);
    return TransformParameterResult::Changed;
}

TransformParameterResult TimelineModel::setClipKeyframe(
    std::size_t track_index,
    std::size_t clip_index,
    TransformProperty property,
    std::int64_t local_frame,
    double value) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return TransformParameterResult::InvalidIndex;
    }
    const auto& clip = track->clips[clip_index];
    if (local_frame < 0 || local_frame >= clip.timeline_duration_frames ||
        !validKeyframeValue(property, value)) {
        return TransformParameterResult::InvalidValue;
    }
    auto& mutable_clip = track->clips[clip_index];
    const auto before = mutable_clip.keyframes;
    if (!setKeyframe(mutable_clip.keyframes, property, local_frame, value)) {
        return TransformParameterResult::InvalidValue;
    }
    return before == mutable_clip.keyframes
        ? TransformParameterResult::NoChange
        : TransformParameterResult::Changed;
}

TransformParameterResult TimelineModel::removeClipKeyframe(
    std::size_t track_index,
    std::size_t clip_index,
    TransformProperty property,
    std::int64_t local_frame) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return TransformParameterResult::InvalidIndex;
    }
    if (!removeKeyframe(track->clips[clip_index].keyframes, property, local_frame)) {
        return TransformParameterResult::NoChange;
    }
    return TransformParameterResult::Changed;
}

TextParameterResult TimelineModel::setClipText(
    std::size_t track_index,
    std::size_t clip_index,
    const TextStyle& text) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return TextParameterResult::InvalidIndex;
    }
    if (track->clips[clip_index].kind != ClipKind::Text || !validTextStyle(text)) {
        return TextParameterResult::InvalidValue;
    }
    auto& clip = track->clips[clip_index];
    if (clip.text == text) return TextParameterResult::NoChange;
    clip.text = text;
    clip.display_name = text.content.empty() ? "Text" : text.content;
    return TextParameterResult::Changed;
}

EffectMutationResult TimelineModel::setClipEffects(
    std::size_t track_index,
    std::size_t clip_index,
    std::vector<creative_suite::effects::EffectInstance> effects) {
    auto* track = trackAt(track_index);
    if (track == nullptr || clip_index >= track->clips.size()) {
        return EffectMutationResult::InvalidIndex;
    }
    auto& clip = track->clips[clip_index];
    if (clip.kind != ClipKind::Video && clip.kind != ClipKind::Image) {
        return EffectMutationResult::IncompatibleClip;
    }
    if (!creative_suite::effects::isValidStack(effects)) {
        return EffectMutationResult::InvalidValue;
    }
    if (clip.effects == effects) return EffectMutationResult::NoChange;
    clip.effects = std::move(effects);
    return EffectMutationResult::Changed;
}

TransitionMutationResult TimelineModel::addTransition(
    std::size_t track_index,
    std::size_t from_clip_index,
    std::size_t to_clip_index,
    TransitionKind kind,
    std::int64_t duration_frames) {
    auto* track = trackAt(track_index);
    if (track == nullptr || from_clip_index >= track->clips.size() ||
        to_clip_index >= track->clips.size()) {
        return TransitionMutationResult::InvalidIndex;
    }
    if (!validTransitionKind(kind) || from_clip_index + 1 != to_clip_index) {
        return TransitionMutationResult::InvalidBoundary;
    }
    const auto& from = track->clips[from_clip_index];
    const auto& to = track->clips[to_clip_index];
    const bool audio_crossfade = kind == TransitionKind::AudioCrossfade;
    if ((audio_crossfade &&
         (track->kind != TrackKind::Audio || from.kind != ClipKind::Audio ||
          to.kind != ClipKind::Audio || from.linked_clip_id.has_value() ||
          to.linked_clip_id.has_value())) ||
        (!audio_crossfade && track->kind != TrackKind::Video)) {
        return TransitionMutationResult::InvalidBoundary;
    }
    if (from.kind == ClipKind::Text && to.kind == ClipKind::Text) {
        return TransitionMutationResult::InvalidBoundary;
    }
    if (const auto* existing = transitionBetween(
            track_index, from_clip_index, to_clip_index);
        existing != nullptr) {
        return TransitionMutationResult::NoChange;
    }
    if (from.timeline_start_frame >
            std::numeric_limits<std::int64_t>::max() - from.timeline_duration_frames ||
        from.timeline_start_frame + from.timeline_duration_frames !=
            to.timeline_start_frame) {
        return TransitionMutationResult::InvalidBoundary;
    }
    const auto maximum = std::min(from.timeline_duration_frames,
                                  to.timeline_duration_frames);
    if (duration_frames <= 0 || maximum <= 0 || duration_frames > maximum) {
        return TransitionMutationResult::InvalidRange;
    }
    auto updated = *track;
    if (isOverlapTransition(kind) &&
        !shiftTimelineSuffix(updated, to_clip_index, -duration_frames)) {
        return TransitionMutationResult::InvalidRange;
    }
    updated.transitions.push_back(TimelineTransition{
        from.clip_id, to.clip_id, kind, duration_frames});
    if (audio_crossfade) {
        for (std::size_t left = 0; left < updated.clips.size(); ++left) {
            for (std::size_t right = left + 1; right < updated.clips.size(); ++right) {
                if (!overlaps(updated.clips[left],
                              updated.clips[right].timeline_start_frame,
                              updated.clips[right].timeline_duration_frames)) continue;
                const auto left_id = updated.clips[left].clip_id;
                const auto right_id = updated.clips[right].clip_id;
                const bool permitted_pair = left + 1 == right &&
                    std::any_of(updated.transitions.begin(), updated.transitions.end(),
                        [left_id, right_id](const TimelineTransition& transition) {
                            return transition.kind == TransitionKind::AudioCrossfade &&
                                transition.from_clip_id == left_id &&
                                transition.to_clip_id == right_id;
                        });
                if (!permitted_pair) return TransitionMutationResult::InvalidRange;
            }
        }
    }
    *track = std::move(updated);
    return TransitionMutationResult::Added;
}

TransitionMutationResult TimelineModel::updateTransition(
    std::size_t track_index,
    std::size_t from_clip_index,
    std::size_t to_clip_index,
    TransitionKind kind,
    std::int64_t duration_frames) {
    auto* track = trackAt(track_index);
    if (track == nullptr || from_clip_index >= track->clips.size() ||
        to_clip_index >= track->clips.size()) {
        return TransitionMutationResult::InvalidIndex;
    }
    if (!validTransitionKind(kind) || from_clip_index + 1 != to_clip_index) {
        return TransitionMutationResult::InvalidBoundary;
    }
    const auto& requested_from = track->clips[from_clip_index];
    const auto& requested_to = track->clips[to_clip_index];
    const bool audio_crossfade = kind == TransitionKind::AudioCrossfade;
    if ((audio_crossfade &&
         (track->kind != TrackKind::Audio ||
          requested_from.kind != ClipKind::Audio ||
          requested_to.kind != ClipKind::Audio ||
          requested_from.linked_clip_id.has_value() ||
          requested_to.linked_clip_id.has_value())) ||
        (!audio_crossfade && track->kind != TrackKind::Video)) {
        return TransitionMutationResult::InvalidBoundary;
    }
    const auto from_id = track->clips[from_clip_index].clip_id;
    const auto to_id = track->clips[to_clip_index].clip_id;
    const auto found = std::find_if(
        track->transitions.begin(), track->transitions.end(),
        [from_id, to_id](const TimelineTransition& transition) {
            return transition.from_clip_id == from_id &&
                transition.to_clip_id == to_id;
        });
    if (found == track->transitions.end()) return TransitionMutationResult::NotFound;
    const auto maximum = std::min(
        track->clips[from_clip_index].timeline_duration_frames,
        track->clips[to_clip_index].timeline_duration_frames);
    if (duration_frames <= 0 || maximum <= 0 || duration_frames > maximum) {
        return TransitionMutationResult::InvalidRange;
    }
    if (found->kind == kind && found->duration_frames == duration_frames) {
        return TransitionMutationResult::NoChange;
    }
    auto updated = *track;
    auto updated_transition = std::find_if(
        updated.transitions.begin(), updated.transitions.end(),
        [from_id, to_id](const TimelineTransition& transition) {
            return transition.from_clip_id == from_id &&
                transition.to_clip_id == to_id;
        });
    const auto& from = updated.clips[from_clip_index];
    const auto& to = updated.clips[to_clip_index];
    if (from.timeline_start_frame < 0 || from.timeline_duration_frames <= 0 ||
        from.timeline_start_frame > std::numeric_limits<std::int64_t>::max() -
            from.timeline_duration_frames) {
        return TransitionMutationResult::InvalidRange;
    }
    const auto from_end = from.timeline_start_frame + from.timeline_duration_frames;
    const auto target_start = isOverlapTransition(kind)
        ? from_end - duration_frames
        : from_end;
    if (!shiftTimelineSuffix(
            updated, to_clip_index, target_start - to.timeline_start_frame)) {
        return TransitionMutationResult::InvalidRange;
    }
    if (audio_crossfade) {
        for (std::size_t left = 0; left < updated.clips.size(); ++left) {
            for (std::size_t right = left + 1; right < updated.clips.size(); ++right) {
                if (!overlaps(updated.clips[left],
                             updated.clips[right].timeline_start_frame,
                             updated.clips[right].timeline_duration_frames)) {
                    continue;
                }
                const auto left_id = updated.clips[left].clip_id;
                const auto right_id = updated.clips[right].clip_id;
                const bool permitted_pair = left + 1 == right &&
                    std::any_of(updated.transitions.begin(), updated.transitions.end(),
                        [left_id, right_id](const TimelineTransition& transition) {
                            return transition.kind == TransitionKind::AudioCrossfade &&
                                transition.from_clip_id == left_id &&
                                transition.to_clip_id == right_id;
                        });
                if (!permitted_pair) return TransitionMutationResult::InvalidRange;
            }
        }
    }
    updated_transition->kind = kind;
    updated_transition->duration_frames = duration_frames;
    *track = std::move(updated);
    return TransitionMutationResult::Updated;
}

TransitionMutationResult TimelineModel::removeTransition(
    std::size_t track_index,
    std::size_t from_clip_index,
    std::size_t to_clip_index) {
    auto* track = trackAt(track_index);
    if (track == nullptr || from_clip_index >= track->clips.size() ||
        to_clip_index >= track->clips.size()) {
        return TransitionMutationResult::InvalidIndex;
    }
    const auto from_id = track->clips[from_clip_index].clip_id;
    const auto to_id = track->clips[to_clip_index].clip_id;
    const auto found = std::find_if(
        track->transitions.begin(),
        track->transitions.end(),
        [from_id, to_id](const TimelineTransition& transition) {
            return transition.from_clip_id == from_id &&
                transition.to_clip_id == to_id;
        });
    if (found == track->transitions.end()) return TransitionMutationResult::NotFound;
    auto updated = *track;
    if (isOverlapTransition(found->kind)) {
        const auto& from = updated.clips[from_clip_index];
        const auto& to = updated.clips[to_clip_index];
        if (from.timeline_start_frame < 0 || from.timeline_duration_frames <= 0 ||
            from.timeline_start_frame > std::numeric_limits<std::int64_t>::max() -
                from.timeline_duration_frames) {
            return TransitionMutationResult::InvalidRange;
        }
        const auto from_end = from.timeline_start_frame + from.timeline_duration_frames;
        if (!shiftTimelineSuffix(
                updated, to_clip_index, from_end - to.timeline_start_frame)) {
            return TransitionMutationResult::InvalidRange;
        }
    }
    updated.transitions.erase(updated.transitions.begin() +
        static_cast<std::ptrdiff_t>(found - track->transitions.begin()));
    *track = std::move(updated);
    return TransitionMutationResult::Removed;
}

} // namespace timeline
