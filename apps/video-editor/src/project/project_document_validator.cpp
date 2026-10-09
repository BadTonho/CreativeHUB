#include "project_file_detail.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
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
    case timeline::TransitionKind::AudioCrossfade:
        return true;
    }
    return false;
}

bool validLinkedImageReference(
    const media::LinkedImageReference& link,
    const std::filesystem::path& source_path) {
    if (link.id.empty() || link.document_path.empty() ||
        link.published_output_path.empty()) return false;
    const auto document_path = media::MediaLibrary::canonicalPath(link.document_path);
    const auto output_path = media::MediaLibrary::canonicalPath(
        link.published_output_path);
    const auto source = media::MediaLibrary::canonicalPath(source_path);
    return document_path != output_path && document_path != source &&
        output_path != source;
}

bool validMotionLinkReference(const media::MotionLinkReference& link,
                              const std::filesystem::path& source_path) {
    if (link.id.empty() || link.document_path.empty() ||
        link.published_output_path.empty() || link.source_path.empty() ||
        link.container.empty() || link.codec.empty() || link.quality.empty() ||
        !std::isfinite(link.bitrate_mbps) || link.bitrate_mbps <= 0.0) return false;
    if (link.source_kind != "video" && link.source_kind != "image") return false;
    const auto document = media::MediaLibrary::canonicalPath(link.document_path);
    const auto output = media::MediaLibrary::canonicalPath(link.published_output_path);
    const auto link_source = media::MediaLibrary::canonicalPath(link.source_path);
    const auto associated_source = media::MediaLibrary::canonicalPath(source_path);
    return document != output && output != link_source && document != link_source &&
        (associated_source == output || associated_source == link_source);
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
    if (!isSupportedCanvasSize(document.canvas_width, document.canvas_height)) {
        throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an unsupported canvas size; expected 1920x1080 or 1080x1920.");
    }
    if (!document.timing_migration_required &&
        !timeline::validFrameRate(document.timeline_frame_rate)) {
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid rational timeline frame rate.");
    }
    if (!std::isfinite(document.timeline_zoom) ||
        document.timeline_zoom < timeline::kMinTimelineZoomFactor ||
        document.timeline_zoom > timeline::kMaxTimelineZoomFactor) {
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid timeline zoom; expected a value from 0.25 to 512.0.");
    }
    const auto valid_row_height = [](double height) {
        return std::isfinite(height) &&
            height >= timeline::kMinimumTrackRowHeight &&
            height <= timeline::kMaximumTrackRowHeight;
    };
    if (!valid_row_height(document.timeline_video_row_height) ||
        !valid_row_height(document.timeline_audio_row_height)) {
        throwJson(ProjectErrorCode::InvalidValue, project_path,
                  "Project JSON contains an invalid Timeline track-group row height; expected a value from 30.0 to 180.0.");
    }
    std::vector<std::filesystem::path> media_paths;
    for (const auto& media : document.media) {
        if (media.source_path.empty()) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an empty media path.");
        }
        if (!media::MediaLibrary::validBinPath(media.bin_path)) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid media bin path.");
        }
        if (media.image_editor_link.has_value() &&
            (media.kind != media::MediaKind::Image ||
             !validLinkedImageReference(*media.image_editor_link, media.source_path))) {
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid Image Editor media link.");
        }
        if (media.motion_link.has_value() &&
            (!validMotionLinkReference(*media.motion_link, media.source_path) ||
             media::MediaLibrary::canonicalPath(media.source_path) !=
                 media::MediaLibrary::canonicalPath(
                     media.motion_link->published_output_path))) {
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid Media Pool Motion Studio link.");
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
        if (clip.kind == timeline::ClipKind::Audio) {
            if (clip.source_start_time_us < 0 || clip.source_duration_time_us <= 0 ||
                clip.source_duration_time_us >
                    std::numeric_limits<std::int64_t>::max() -
                        clip.source_start_time_us) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains an invalid or overflowing audio source range.");
            }
        } else {
            const auto source_duration = timeline::isFrameTimedMediaClipKind(clip.kind)
                ? clip.source_duration_frames
                : clip.duration_frames;
            if (source_duration <= 0 || source_duration >
                std::numeric_limits<std::int64_t>::max() - clip.source_start_frame) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains an invalid or overflowing media source range.");
            }
        }
        if (!validAudioGain(clip.audio_gain)) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains an invalid clip audio gain.");
        }
        if ((clip.kind != timeline::ClipKind::Audio &&
             !clip.audio_gain_keyframes.empty()) ||
            (clip.kind == timeline::ClipKind::Audio &&
             !timeline::TimelineModel::validAudioGainKeyframes(
                 clip.audio_gain_keyframes, clip.duration_frames))) {
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid audio gain envelope.");
        }
        if (clip.kind == timeline::ClipKind::Text && !validTextStyle(clip.text)) {
            throwJson(ProjectErrorCode::InvalidValue, project_path, "Project JSON contains invalid text clip styling.");
        }
        if (!creative_suite::effects::isValidStack(clip.effects) ||
            (!clip.effects.empty() && clip.kind != timeline::ClipKind::Video &&
             clip.kind != timeline::ClipKind::Image)) {
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid effect stack for this clip.");
        }
        if (clip.node_graph.has_value() &&
            ((clip.kind != timeline::ClipKind::Video &&
              clip.kind != timeline::ClipKind::Image) ||
             !fusion::nodes::validate(*clip.node_graph) ||
             !fusion::nodes::validKeyframeRange(
                 *clip.node_graph, clip.duration_frames))) {
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid Fusion node graph for this clip.");
        }
        if (clip.image_editor_variant.has_value() &&
            (clip.kind != timeline::ClipKind::Image ||
             !validLinkedImageReference(*clip.image_editor_variant, clip.source_path))) {
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid linked clip image.");
        }
        if (clip.motion_link.has_value() &&
            (clip.kind != timeline::ClipKind::Video ||
             !validMotionLinkReference(*clip.motion_link, clip.source_path) ||
             media::MediaLibrary::canonicalPath(clip.motion_link->source_path) !=
                 media::MediaLibrary::canonicalPath(clip.source_path))) {
            throwJson(ProjectErrorCode::InvalidValue, project_path,
                      "Project JSON contains an invalid timeline Motion Studio link.");
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
    const auto media_kind_for_path = [&document](const std::filesystem::path& path)
        -> std::optional<media::MediaKind> {
        const auto canonical = media::MediaLibrary::canonicalPath(path);
        const auto found = std::find_if(
            document.media.begin(), document.media.end(),
            [&canonical](const ProjectMedia& item) {
                return media::MediaLibrary::canonicalPath(item.source_path) == canonical;
            });
        if (found == document.media.end()) return std::nullopt;
        return found->kind;
    };
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
            const bool compatible = track.kind == timeline::TrackKind::Audio
                ? clip.kind == timeline::ClipKind::Audio
                : clip.kind != timeline::ClipKind::Audio;
            if (!compatible) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains a clip on an incompatible track type.");
            }
            if (timeline::isMediaClipKind(clip.kind)) {
                const auto media_kind = media_kind_for_path(clip.source_path);
                const auto expected = clip.kind == timeline::ClipKind::Audio
                    ? media::MediaKind::Audio
                    : clip.kind == timeline::ClipKind::Image
                        ? media::MediaKind::Image
                        : media::MediaKind::Video;
                const bool video_audio_companion =
                    clip.kind == timeline::ClipKind::Audio &&
                    clip.linked_clip_id.has_value() &&
                    media_kind == media::MediaKind::Video;
                if (media_kind.has_value() && *media_kind != expected &&
                    !video_audio_companion) {
                    throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                              "Project JSON contains a clip whose source media type does not match its clip type.");
                }
            }
            if (clip.node_graph.has_value()) {
                for (const auto& node : clip.node_graph->nodes) {
                    if (node.type != fusion::nodes::NodeType::Input ||
                        node.source_path.empty()) continue;
                    const auto media = media_kind_for_path(node.source_path);
                    if (!media.has_value() ||
                        (*media != media::MediaKind::Video &&
                         *media != media::MediaKind::Image) ||
                        node.source_is_still != (*media == media::MediaKind::Image)) {
                        throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                                  "A Fusion input must reference video or image media in the project Media Pool.");
                    }
                }
            }
            if ((clip.kind != timeline::ClipKind::Video &&
                 (clip.audio_extracted || clip.audio_companion_pending)) ||
                (clip.audio_companion_pending && clip.linked_clip_id.has_value()) ||
                (clip.audio_companion_pending && !clip.audio_extracted)) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains invalid extracted audio state.");
            }
            validate_clip(clip);
        }
        std::vector<std::pair<std::size_t, std::size_t>> transition_pairs;
        for (const auto& transition : track.transitions) {
            const bool audio_crossfade =
                transition.kind == timeline::TransitionKind::AudioCrossfade;
            if (!validTransitionKind(transition.kind) ||
                (audio_crossfade !=
                 (track.kind == timeline::TrackKind::Audio)) ||
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
            if ((audio_crossfade &&
                 (from.kind != timeline::ClipKind::Audio ||
                  to.kind != timeline::ClipKind::Audio ||
                  from.linked_clip_id.has_value() || to.linked_clip_id.has_value())) ||
                (!audio_crossfade &&
                 (from.kind == timeline::ClipKind::Audio ||
                  to.kind == timeline::ClipKind::Audio)) ||
                (from.kind == timeline::ClipKind::Text &&
                 to.kind == timeline::ClipKind::Text)) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains a transition with incompatible clip types or linked audio.");
            }
            const auto maximum = std::min(from.duration_frames, to.duration_frames);
            if (transition.duration_frames <= 0 ||
                transition.duration_frames > maximum) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains a transition with an invalid duration.");
            }
            const auto from_end = from.timeline_start_frame + from.duration_frames;
            const auto expected_to_start =
                timeline::isOverlapTransition(transition.kind)
                ? from_end - transition.duration_frames
                : from_end;
            if (to.timeline_start_frame != expected_to_start) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains a transition with invalid clip timing.");
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

        for (std::size_t left = 0; left < track.clips.size(); ++left) {
            const auto& first = track.clips[left];
            const auto first_end = first.timeline_start_frame + first.duration_frames;
            for (std::size_t right = left + 1; right < track.clips.size(); ++right) {
                const auto& second = track.clips[right];
                const auto second_end = second.timeline_start_frame + second.duration_frames;
                const bool overlap = second.timeline_start_frame < first_end &&
                    first.timeline_start_frame < second_end;
                const bool audio_crossfade_pair = first.kind == timeline::ClipKind::Audio &&
                    second.kind == timeline::ClipKind::Audio &&
                    std::any_of(track.transitions.begin(), track.transitions.end(),
                        [left, right](const ProjectTransition& transition) {
                            return transition.kind ==
                                    timeline::TransitionKind::AudioCrossfade &&
                                transition.from_clip_index == left &&
                                transition.to_clip_index == right;
                        });
                if (overlap &&
                    ((first.kind == timeline::ClipKind::Text &&
                      second.kind == timeline::ClipKind::Text) ||
                     (first.kind == timeline::ClipKind::Audio &&
                      second.kind == timeline::ClipKind::Audio &&
                      !audio_crossfade_pair))) {
                    throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                              "Project JSON contains overlapping clips that are not allowed on one track.");
                }
            }
        }
    }

    std::unordered_map<timeline::ClipId,
        std::pair<const ProjectClip*, timeline::TrackKind>> clips_by_id;
    for (const auto& track : document.timeline_tracks) {
        for (const auto& clip : track.clips) {
            clips_by_id.emplace(clip.clip_id, std::make_pair(&clip, track.kind));
        }
    }
    for (const auto& track : document.timeline_tracks) {
        for (const auto& clip : track.clips) {
            if (!clip.linked_clip_id.has_value()) continue;
            const auto peer = clips_by_id.find(*clip.linked_clip_id);
            if (peer == clips_by_id.end() || peer->second.first == &clip) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains a missing or self-referencing linked audio clip.");
            }
            const auto& other = *peer->second.first;
            const auto video = clip.kind == timeline::ClipKind::Video
                ? &clip : other.kind == timeline::ClipKind::Video ? &other : nullptr;
            const auto audio = clip.kind == timeline::ClipKind::Audio
                ? &clip : other.kind == timeline::ClipKind::Audio ? &other : nullptr;
            if (video == nullptr || audio == nullptr ||
                clip.kind == other.kind || other.linked_clip_id != clip.clip_id ||
                track.kind == peer->second.second ||
                video->audio_extracted == false ||
                video->timeline_start_frame != audio->timeline_start_frame ||
                media::MediaLibrary::canonicalPath(video->source_path) !=
                    media::MediaLibrary::canonicalPath(audio->source_path)) {
                throwJson(ProjectErrorCode::InvalidTimeline, project_path,
                          "Project JSON contains an inconsistent video and audio clip pair.");
            }
        }
    }
}

} // namespace project::detail
