#include "project_file_detail.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

#include "../media/media_library.h"
#include "../timeline/timeline_zoom.h"

namespace project::detail {
namespace {

[[noreturn]] void throwJson(ProjectErrorCode code,
                            const std::filesystem::path& project_path,
                            const char* message) {
    throw ProjectError(code, message, std::nullopt, project_path);
}

bool validAudioGain(double gain) {
    return std::isfinite(gain) && gain >= 0.0 && gain <= 2.0;
}

bool validTextStyle(const timeline::TextStyle& text) {
    if (text.font_family.empty() || !std::isfinite(text.font_size_pixels) ||
        text.font_size_pixels <= 0.0 || text.font_size_pixels > 512.0) {
        return false;
    }
    switch (text.alignment) {
    case timeline::TextAlignment::Left:
    case timeline::TextAlignment::Center:
    case timeline::TextAlignment::Right:
        return true;
    }
    return false;
}

bool validTransitionKind(timeline::TransitionKind kind) {
    switch (kind) {
    case timeline::TransitionKind::CrossDissolve:
    case timeline::TransitionKind::FadeToBlack:
        return true;
    }
    return false;
}

bool validKeyframeList(
    const std::vector<timeline::Keyframe>& keyframes,
    timeline::TransformProperty property,
    std::int64_t duration_frames) {
    std::int64_t previous = -1;
    for (const auto& keyframe : keyframes) {
        if (keyframe.frame < 0 || keyframe.frame >= duration_frames ||
            keyframe.frame <= previous ||
            !timeline::validKeyframeValue(property, keyframe.value)) {
            return false;
        }
        previous = keyframe.frame;
    }
    return true;
}

} // namespace

void validateDocument(const ProjectDocument& document,
                      const std::filesystem::path& project_path) {
    if (document.canvas_width != 1920 || document.canvas_height != 1080) {
        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an unsupported canvas size; only 1920x1080 is supported.");
    }
    if (!std::isfinite(document.timeline_zoom) ||
        document.timeline_zoom < timeline::kMinTimelineZoomFactor ||
        document.timeline_zoom > timeline::kMaxTimelineZoomFactor) {
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid timeline zoom; expected a value from 0.25 to 512.0.");
    }
    if (!std::isfinite(document.timeline_row_height) ||
        document.timeline_row_height < timeline::kMinimumTrackRowHeight ||
        document.timeline_row_height > timeline::kMaximumTrackRowHeight) {
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid timeline row height; expected a value from 30.0 to 180.0.");
    }
    std::vector<std::filesystem::path> media_paths;
    for (const auto& media : document.media) {
        if (media.source_path.empty()) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an empty media path.");
        }
        if (!media::MediaLibrary::validBinPath(media.bin_path)) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid media bin path.");
        }
        const auto canonical = media::MediaLibrary::canonicalPath(media.source_path);
        if (std::find(media_paths.begin(), media_paths.end(), canonical) != media_paths.end()) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains duplicate media paths.");
        }
        media_paths.push_back(canonical);
    }

    for (const auto& bin : document.bins) {
        if (!media::MediaLibrary::validBinPath(bin)) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid bin path.");
        }
    }

    auto validate_clip = [&project_path](const ProjectClip& clip) {
        if (timeline::isMediaClipKind(clip.kind) && clip.source_path.empty()) {
            throwJson(ProjectErrorCode::InvalidTimeline, project_path, "Project JSON contains a media clip without a source.");
        }
        if (clip.timeline_start_frame < 0 ||
            clip.source_start_frame < 0 || clip.duration_frames <= 0) {
            throwJson(ProjectErrorCode::InvalidTimeline, project_path, "Project JSON contains an invalid timeline segment.");
        }
        if (clip.duration_frames >
            std::numeric_limits<std::int64_t>::max() -
                clip.timeline_start_frame) {
            throwJson(ProjectErrorCode::InvalidTimeline, project_path, "Project JSON contains an overflowing timeline range.");
        }
        if (clip.duration_frames >
            std::numeric_limits<std::int64_t>::max() -
                clip.source_start_frame) {
            throwJson(ProjectErrorCode::InvalidTimeline, project_path, "Project JSON contains an overflowing media source range.");
        }
        if (!validAudioGain(clip.audio_gain)) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid clip audio gain.");
        }
        if (clip.kind == timeline::ClipKind::Text && !validTextStyle(clip.text)) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains invalid text clip styling.");
        }
        if (!timeline::validTransform(clip.transform) ||
            !validKeyframeList(clip.keyframes.position_x,
                               timeline::TransformProperty::PositionX,
                               clip.duration_frames) ||
            !validKeyframeList(clip.keyframes.position_y,
                               timeline::TransformProperty::PositionY,
                               clip.duration_frames) ||
            !validKeyframeList(clip.keyframes.scale,
                               timeline::TransformProperty::Scale,
                               clip.duration_frames) ||
            !validKeyframeList(clip.keyframes.rotation,
                               timeline::TransformProperty::Rotation,
                               clip.duration_frames) ||
            !validKeyframeList(clip.keyframes.opacity,
                               timeline::TransformProperty::Opacity,
                               clip.duration_frames)) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains invalid clip transform or keyframes.");
        }
    };
    std::unordered_set<timeline::TrackId> track_ids;
    std::unordered_set<timeline::ClipId> clip_ids;
    for (const auto& track : document.timeline_tracks) {
        if (track.track_id == 0 ||
            track.track_id > static_cast<timeline::TrackId>(
                std::numeric_limits<std::int64_t>::max()) ||
            !track_ids.insert(track.track_id).second) {
            throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                      "Project JSON contains a missing or duplicate track identifier.");
        }
        if (track.name.empty()) {
            throwJson(ProjectErrorCode::InvalidTimeline, project_path, "Project JSON contains a track without a name.");
        }
        if (!validAudioGain(track.audio_gain)) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid track audio gain.");
        }
        for (const auto& clip : track.clips) {
            if (clip.clip_id == 0 ||
                clip.clip_id > static_cast<timeline::ClipId>(
                    std::numeric_limits<std::int64_t>::max()) ||
                !clip_ids.insert(clip.clip_id).second) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains a missing or duplicate clip identifier.");
            }
            validate_clip(clip);
        }
        for (std::size_t left = 0; left < track.clips.size(); ++left) {
            const auto& first = track.clips[left];
            const auto first_end =
                first.timeline_start_frame + first.duration_frames;
            for (std::size_t right = left + 1; right < track.clips.size(); ++right) {
                const auto& second = track.clips[right];
                const auto second_end =
                    second.timeline_start_frame + second.duration_frames;
                if (first.kind == timeline::ClipKind::Text &&
                    second.kind == timeline::ClipKind::Text &&
                    second.timeline_start_frame < first_end &&
                    first.timeline_start_frame <
                        second_end) {
                    throwJson(ProjectErrorCode::InvalidTimeline, project_path, "Project JSON contains overlapping clips on one track.");
                }
            }
        }
        std::vector<std::pair<std::size_t, std::size_t>> transition_pairs;
        for (const auto& transition : track.transitions) {
            if (!validTransitionKind(transition.kind) ||
                transition.from_clip_index >= track.clips.size() ||
                transition.to_clip_index >= track.clips.size()) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains a transition with invalid clip indexes.");
            }
            if (transition.from_clip_index + 1 != transition.to_clip_index) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains a transition between non-consecutive clips.");
            }
            const auto& from = track.clips[transition.from_clip_index];
            const auto& to = track.clips[transition.to_clip_index];
            if (from.timeline_start_frame >
                    std::numeric_limits<std::int64_t>::max() - from.duration_frames ||
                from.timeline_start_frame + from.duration_frames !=
                    to.timeline_start_frame) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains a transition across a gap.");
            }
            const auto maximum = std::min(from.duration_frames, to.duration_frames);
            if (transition.duration_frames <= 0 ||
                transition.duration_frames > maximum) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains a transition with an invalid duration.");
            }
            const auto pair = std::make_pair(
                transition.from_clip_index, transition.to_clip_index);
            if (std::find(transition_pairs.begin(), transition_pairs.end(), pair) !=
                transition_pairs.end()) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains duplicate transitions.");
            }
            transition_pairs.push_back(pair);
        }
    }
}

} // namespace project::detail
