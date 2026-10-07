#include "project_open_service.h"

#include "media/media_library.h"
#include "media/still_image_decoder.h"
#include "media/video_metadata.h"
#include "media/video_decoder.h"
#include "media/video_probe.h"
#include "media_import_service.h"
#include "project/project_file.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <system_error>
#include <utility>

namespace application {
namespace {

media::MediaItem importProjectMedia(
    const std::filesystem::path& input_path,
    media::MediaKind kind) {
    const auto path = media::MediaLibrary::canonicalPath(input_path);
    media::VideoMetadata metadata;
    media::VideoFrame first_frame;
    if (kind == media::MediaKind::Image) {
        const media::StillImageDecoder decoder;
        metadata = decoder.probe(path);
        first_frame = decoder.decode_first_frame(path);
    } else if (kind == media::MediaKind::Audio) {
        metadata = media::VideoProbe{}.probe(path);
        if (metadata.kind != media::MediaKind::Audio || !metadata.audio.has_value()) {
            throw media::MediaError("The source does not contain an audio-only stream.");
        }
    } else {
        const media::VideoProbe probe;
        const media::VideoDecoder decoder;
        metadata = probe.probe(path);
        first_frame = decoder.decode_first_frame(path);
    }
    metadata.kind = kind;
    metadata.source_path = path;
    const auto name = metadata.display_name.empty()
        ? media::MediaLibrary::defaultDisplayName(path)
        : metadata.display_name;
    metadata.display_name = name;
    return {std::move(metadata), std::move(first_frame), name,
            std::string(media::default_bin), false};
}

std::optional<std::int64_t> availableFrameCount(const media::VideoMetadata& metadata) {
    if (metadata.frame_count && *metadata.frame_count > 0) return metadata.frame_count;
    if (!metadata.duration_seconds || !metadata.frame_rate ||
        !std::isfinite(*metadata.duration_seconds) ||
        !std::isfinite(*metadata.frame_rate) ||
        *metadata.duration_seconds <= 0.0 || *metadata.frame_rate <= 0.0) {
        return std::nullopt;
    }
    const double estimate = *metadata.duration_seconds * *metadata.frame_rate;
    if (!std::isfinite(estimate) ||
        estimate > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(estimate)));
}

std::optional<std::int64_t> availableAudioDurationTimeUs(
    const media::VideoMetadata& metadata) {
    if (!metadata.audio.has_value() ||
        !metadata.audio->duration_seconds.has_value() ||
        !std::isfinite(*metadata.audio->duration_seconds) ||
        *metadata.audio->duration_seconds <= 0.0) {
        return std::nullopt;
    }
    const auto value = static_cast<long double>(
        *metadata.audio->duration_seconds) * 1000000.0L;
    if (!std::isfinite(value) || value > static_cast<long double>(
            std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(std::ceil(value));
}

timeline::FrameRate inferLegacyTimelineRate(
    const project::ProjectDocument& document,
    const media::MediaLibrary& library) {
    for (const auto& track : document.timeline_tracks) {
        for (const auto& clip : track.clips) {
            if (clip.kind != timeline::ClipKind::Video) continue;
            const auto media_index = library.indexForPath(clip.source_path);
            if (media_index >= library.size()) continue;
            const auto& item = library.items()[media_index];
            if (item.offline || item.metadata.kind != media::MediaKind::Video ||
                !item.metadata.frame_rate.has_value()) continue;
            const auto rate = timeline::frameRateFromDouble(*item.metadata.frame_rate);
            if (rate.has_value()) return *rate;
        }
    }
    return {};
}

void preserveTransitionContinuity(
    project::ProjectTrack& track,
    const std::filesystem::path& project_path) {
    auto transitions = track.transitions;
    std::stable_sort(transitions.begin(), transitions.end(),
        [](const auto& left, const auto& right) {
            return left.from_clip_index < right.from_clip_index;
        });
    for (const auto& transition : transitions) {
        if (transition.from_clip_index >= track.clips.size() ||
            transition.to_clip_index >= track.clips.size() ||
            transition.to_clip_index != transition.from_clip_index + 1) continue;
        auto& from = track.clips[transition.from_clip_index];
        const auto maximum = std::min(
            from.duration_frames,
            track.clips[transition.to_clip_index].duration_frames);
        auto found = std::find_if(track.transitions.begin(), track.transitions.end(),
            [&transition](const auto& candidate) {
                return candidate.from_clip_index == transition.from_clip_index &&
                    candidate.to_clip_index == transition.to_clip_index;
            });
        if (found != track.transitions.end()) {
            found->duration_frames = std::clamp<std::int64_t>(
                found->duration_frames, 1, maximum);
        }
        const auto cut = from.timeline_start_frame + from.duration_frames;
        const auto expected_incoming_start =
            transition.kind == timeline::TransitionKind::CrossDissolve
            ? cut - (found != track.transitions.end()
                         ? found->duration_frames
                         : transition.duration_frames)
            : cut;
        const auto delta = expected_incoming_start -
            track.clips[transition.to_clip_index].timeline_start_frame;
        if (delta != 0) {
            for (std::size_t index = transition.to_clip_index;
                 index < track.clips.size(); ++index) {
                auto& clip = track.clips[index];
                if ((delta < 0 && clip.timeline_start_frame < -delta) ||
                    (delta > 0 && clip.timeline_start_frame >
                        std::numeric_limits<std::int64_t>::max() - delta)) {
                    throw project::ProjectError(
                        project::ProjectErrorCode::InvalidTimeline,
                        "A timing migration would move a clip outside the supported timeline range.",
                        std::nullopt,
                        project_path);
                }
                clip.timeline_start_frame += delta;
            }
        }
    }
}

std::optional<std::int64_t> secondsToMicroseconds(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0) return std::nullopt;
    const auto value = static_cast<long double>(seconds) * 1000000.0L;
    if (!std::isfinite(value) || value >= std::ldexp(1.0L, 63)) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(std::llround(value));
}

std::optional<std::int64_t> sourceFramesToMicroseconds(
    std::int64_t frames,
    double frame_rate) {
    if (frames < 0 || !std::isfinite(frame_rate) || frame_rate <= 0.0 ||
        frame_rate > 1000.0) return std::nullopt;
    const auto value = static_cast<long double>(frames) * 1000000.0L / frame_rate;
    if (!std::isfinite(value) || value >= std::ldexp(1.0L, 63)) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(std::llround(value));
}

std::optional<std::int64_t> timelineFramesForDurationUs(
    std::int64_t duration_us,
    timeline::FrameRate frame_rate) {
    if (duration_us <= 0 || !timeline::validFrameRate(frame_rate)) {
        return std::nullopt;
    }
    const auto value = static_cast<long double>(duration_us) *
        frame_rate.numerator /
        (static_cast<long double>(frame_rate.denominator) * 1000000.0L);
    if (!std::isfinite(value) || value >= std::ldexp(1.0L, 63)) {
        return std::nullopt;
    }
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(value)));
}

std::string nextAudioTrackName(const std::vector<project::ProjectTrack>& tracks) {
    for (std::size_t number = 1;; ++number) {
        const auto name = "Audio " + std::to_string(number);
        if (std::none_of(tracks.begin(), tracks.end(), [&name](const auto& track) {
                return track.kind == timeline::TrackKind::Audio && track.name == name;
            })) return name;
    }
}

void migrateAudioCompanions(
    project::ProjectDocument& document,
    const media::MediaLibrary& library) {
    timeline::TrackId next_track_id = 1;
    timeline::ClipId next_clip_id = 1;
    for (const auto& track : document.timeline_tracks) {
        next_track_id = std::max(next_track_id, track.track_id + 1);
        for (const auto& clip : track.clips) {
            next_clip_id = std::max(next_clip_id, clip.clip_id + 1);
        }
    }

    std::vector<timeline::ClipId> videos_to_migrate;
    for (auto& track : document.timeline_tracks) {
        for (auto& clip : track.clips) {
            if (clip.kind != timeline::ClipKind::Video) continue;
            if (document.audio_companion_migration_required ||
                clip.audio_companion_pending) {
                videos_to_migrate.push_back(clip.clip_id);
            }
        }
    }

    for (const auto video_id : videos_to_migrate) {
        project::ProjectTrack* video_track = nullptr;
        project::ProjectClip* video_clip = nullptr;
        for (auto& track : document.timeline_tracks) {
            const auto found = std::find_if(track.clips.begin(), track.clips.end(),
                [video_id](const auto& clip) { return clip.clip_id == video_id; });
            if (found != track.clips.end()) {
                video_track = &track;
                video_clip = &*found;
                break;
            }
        }
        if (video_clip == nullptr || video_track == nullptr ||
            video_clip->linked_clip_id.has_value()) continue;

        const auto media_index = library.indexForPath(video_clip->source_path);
        if (media_index >= library.size()) continue;
        const auto& item = library.items()[media_index];
        if (item.offline) {
            video_clip->audio_companion_pending = true;
            video_clip->audio_extracted = true;
            continue;
        }
        if (!item.metadata.audio.has_value()) {
            video_clip->audio_companion_pending = false;
            video_clip->audio_extracted = false;
            continue;
        }

        const auto video_snapshot = *video_clip;
        const auto source_rate = item.metadata.frame_rate.value_or(
            document.timeline_frame_rate.asDouble());
        if (video_snapshot.source_start_frame < 0 ||
            video_snapshot.source_duration_frames <= 0 ||
            video_snapshot.source_start_frame > std::numeric_limits<std::int64_t>::max() -
                video_snapshot.source_duration_frames) {
            continue;
        }
        const auto source_start_us = sourceFramesToMicroseconds(
            video_snapshot.source_start_frame, source_rate);
        const auto source_end_us = sourceFramesToMicroseconds(
            video_snapshot.source_start_frame + video_snapshot.source_duration_frames,
            source_rate);
        if (!source_start_us.has_value() || !source_end_us.has_value() ||
            *source_end_us <= *source_start_us) continue;
        auto source_duration_us = *source_end_us - *source_start_us;
        if (item.metadata.audio->duration_seconds.has_value()) {
            const auto audio_end_us = secondsToMicroseconds(
                *item.metadata.audio->duration_seconds);
            if (!audio_end_us.has_value() || *audio_end_us <= *source_start_us) {
                video_clip->audio_companion_pending = false;
                video_clip->audio_extracted = false;
                continue;
            }
            source_duration_us = std::min(
                source_duration_us, *audio_end_us - *source_start_us);
        }
        const auto audio_timeline_duration = timelineFramesForDurationUs(
            source_duration_us, document.timeline_frame_rate);
        if (source_duration_us <= 0 || !audio_timeline_duration.has_value()) continue;

        std::optional<std::size_t> audio_track_index;
        for (std::size_t index = 0; index < document.timeline_tracks.size(); ++index) {
            const auto& track = document.timeline_tracks[index];
            if (track.kind != timeline::TrackKind::Audio) continue;
            const auto start = video_snapshot.timeline_start_frame;
            const auto duration = std::min(
                video_snapshot.duration_frames, *audio_timeline_duration);
            const auto end = start + duration;
            const bool overlaps = std::any_of(
                track.clips.begin(), track.clips.end(), [start, end](const auto& clip) {
                    if (clip.kind != timeline::ClipKind::Audio ||
                        clip.duration_frames <= 0 || clip.timeline_start_frame < 0 ||
                        clip.timeline_start_frame > std::numeric_limits<std::int64_t>::max() -
                            clip.duration_frames) return true;
                    const auto other_end = clip.timeline_start_frame + clip.duration_frames;
                    return start < other_end && clip.timeline_start_frame < end;
                });
            if (!overlaps) {
                audio_track_index = index;
                break;
            }
        }
        if (!audio_track_index.has_value()) {
            project::ProjectTrack audio_track;
            audio_track.track_id = next_track_id++;
            audio_track.name = nextAudioTrackName(document.timeline_tracks);
            audio_track.kind = timeline::TrackKind::Audio;
            document.timeline_tracks.push_back(std::move(audio_track));
            audio_track_index = document.timeline_tracks.size() - 1;
        }

        project::ProjectClip audio_clip;
        audio_clip.clip_id = next_clip_id++;
        audio_clip.source_path = media::MediaLibrary::canonicalPath(
            video_snapshot.source_path);
        audio_clip.kind = timeline::ClipKind::Audio;
        audio_clip.timeline_start_frame = video_snapshot.timeline_start_frame;
        audio_clip.duration_frames = std::min(
            video_snapshot.duration_frames, *audio_timeline_duration);
        audio_clip.audio_gain = video_snapshot.audio_gain;
        audio_clip.audio_muted = video_snapshot.audio_muted;
        audio_clip.source_start_time_us = *source_start_us;
        audio_clip.source_duration_time_us = source_duration_us;
        audio_clip.linked_clip_id = video_snapshot.clip_id;
        video_clip = nullptr;
        for (auto& track : document.timeline_tracks) {
            const auto found = std::find_if(track.clips.begin(), track.clips.end(),
                [video_id](const auto& clip) { return clip.clip_id == video_id; });
            if (found != track.clips.end()) {
                video_clip = &*found;
                break;
            }
        }
        if (video_clip == nullptr) continue;
        video_clip->linked_clip_id = audio_clip.clip_id;
        video_clip->audio_extracted = true;
        video_clip->audio_companion_pending = false;
        document.timeline_tracks[*audio_track_index].clips.push_back(std::move(audio_clip));
        auto& clips = document.timeline_tracks[*audio_track_index].clips;
        std::stable_sort(clips.begin(), clips.end(), [](const auto& left, const auto& right) {
            return left.timeline_start_frame < right.timeline_start_frame;
        });
    }
    document.audio_companion_migration_required = false;
}

ProjectOpenResult cancelledResult(std::vector<ProjectOpenIssue> warnings) {
    ProjectOpenResult result;
    result.status = ProjectOpenStatus::Cancelled;
    result.warnings = std::move(warnings);
    return result;
}

} // namespace

ProjectOpenResult ProjectOpenService::prepare(
    const std::filesystem::path& source_path,
    std::optional<std::filesystem::path> active_project_path,
    std::optional<project::ProjectDocument> saved_baseline,
    const std::atomic_bool& cancel_requested,
    Progress progress) const {
    const auto project_path = media::MediaLibrary::canonicalPath(source_path);
    std::filesystem::path current_media_path;
    std::optional<std::size_t> current_clip_index;
    std::vector<ProjectOpenIssue> warnings;

    try {
        auto document = project::load(project_path);
        const auto loaded_source_document = document;
        if (cancel_requested.load(std::memory_order_relaxed)) {
            return cancelledResult(std::move(warnings));
        }

        if (!saved_baseline && active_project_path &&
            media::MediaLibrary::canonicalPath(*active_project_path) != project_path) {
            try {
                saved_baseline = project::load(*active_project_path);
            } catch (const project::ProjectError& error) {
                warnings.push_back({
                    ProjectOpenIssueKind::Project,
                    media::MediaLibrary::canonicalPath(*active_project_path),
                    "The original project could not be loaded; recovery will continue as a new dirty document.",
                    error.system_error(),
                    error.code()});
                project::ProjectDocument blank_document;
                blank_document.bins = {std::string(media::default_bin)};
                saved_baseline = std::move(blank_document);
            }
        }

        auto normalized_document = document;
        media::MediaLibrary loaded_library;
        std::map<std::filesystem::path, media::LinkedImageReference> loaded_image_editor_links;
        for (const auto& bin : document.bins) {
            if (bin == media::default_bin) continue;
            const auto created = loaded_library.createBin(bin);
            if (created == media::MediaMutationResult::InvalidBin) {
                throw project::ProjectError(
                    project::ProjectErrorCode::InvalidValue,
                    "The project contains an invalid media bin.",
                    std::nullopt,
                    project_path);
            }
        }

        const auto total_steps = document.media.size() + document.timeline_tracks.size();
        std::size_t completed_steps = 0;
        for (std::size_t media_index = 0; media_index < document.media.size(); ++media_index) {
            if (cancel_requested.load(std::memory_order_relaxed)) {
                return cancelledResult(std::move(warnings));
            }
            const auto& project_media = document.media[media_index];
            current_media_path = project_media.source_path;
            if (progress) progress(completed_steps, total_steps, project_media.source_path);
            auto& normalized_media = normalized_document.media[media_index];
            if (normalized_media.bin_path.empty()) {
                normalized_media.bin_path = std::string(media::default_bin);
            }

            auto decode_path = project_media.source_path;
            std::error_code file_error;
            bool source_exists = std::filesystem::is_regular_file(
                project_media.source_path, file_error) && !file_error;
            bool linked_output_exists = false;
            if (project_media.kind == media::MediaKind::Image &&
                project_media.image_editor_link.has_value()) {
                std::error_code output_error;
                linked_output_exists = std::filesystem::is_regular_file(
                    project_media.image_editor_link->published_output_path,
                    output_error) && !output_error;
                if (linked_output_exists) {
                    decode_path = project_media.image_editor_link->published_output_path;
                }
            }
            const bool exists = source_exists || linked_output_exists;
            if (!exists) {
                if (!exists && !project_media.offline) {
                    normalized_media.offline = true;
                    warnings.push_back({
                        ProjectOpenIssueKind::Media,
                        media::MediaLibrary::canonicalPath(project_media.source_path),
                        "Project media is unavailable and was loaded offline.",
                        std::nullopt,
                        std::nullopt});
                }
                media::VideoMetadata metadata;
                metadata.kind = project_media.kind;
                metadata.source_path = media::MediaLibrary::canonicalPath(
                    project_media.source_path);
                metadata.display_name = project_media.display_name.empty()
                    ? media::MediaLibrary::defaultDisplayName(metadata.source_path)
                    : project_media.display_name;
                normalized_media.display_name = metadata.display_name;
                const auto added = loaded_library.addOffline(
                    metadata.source_path,
                    metadata.display_name,
                    normalized_media.bin_path,
                    project_media.kind);
                if (added != media::MediaMutationResult::Changed) {
                    throw project::ProjectError(
                        project::ProjectErrorCode::InvalidValue,
                        "The project contains duplicate or invalid media entries.",
                        std::nullopt,
                        metadata.source_path);
                }
                if (project_media.image_editor_link.has_value()) {
                    loaded_image_editor_links.insert_or_assign(
                        metadata.source_path,
                        *project_media.image_editor_link);
                }
            } else {
                MediaImportService media_importer(
                    [kind = project_media.kind](const auto& path) {
                        return importProjectMedia(path, kind);
                    });
                auto batch = media_importer.process(
                    1, 0, 0, {decode_path}, cancel_requested);
                if (!batch.cancelled && linked_output_exists &&
                    (batch.files.empty() ||
                     batch.files.front().status != MediaImportFileStatus::Imported) &&
                    source_exists) {
                    batch = media_importer.process(
                        1, 0, 0, {project_media.source_path}, cancel_requested);
                    warnings.push_back({
                        ProjectOpenIssueKind::Media,
                        project_media.image_editor_link->published_output_path,
                        "The linked image output could not be decoded; the original image will be used.",
                        std::nullopt,
                        std::nullopt});
                }
                if (batch.cancelled) return cancelledResult(std::move(warnings));
                if (batch.files.empty() ||
                    batch.files.front().status != MediaImportFileStatus::Imported ||
                    !batch.files.front().item) {
                    const auto failure = batch.files.empty()
                        ? std::string("Media preparation did not return a result.")
                        : batch.files.front().cause;
                    const auto error_code = batch.files.empty()
                        ? std::optional<int>{}
                        : batch.files.front().error_code;
                    throw media::MediaError(
                        failure.empty() ? "Media preparation failed." : failure,
                        error_code);
                }
                auto item = std::move(*batch.files.front().item);
                const auto display_name = project_media.display_name.empty()
                    ? item.display_name
                    : project_media.display_name;
                item.display_name = display_name;
                item.metadata.display_name = display_name;
                item.bin_path = normalized_media.bin_path;
                item.metadata.source_path = media::MediaLibrary::canonicalPath(
                    project_media.source_path);
                normalized_media.display_name = display_name;
                normalized_media.offline = false;
                const auto added = loaded_library.addOnline(
                    std::move(item.metadata),
                    std::move(item.first_frame),
                    std::move(item.display_name),
                    std::move(item.bin_path));
                if (added != media::MediaMutationResult::Changed) {
                    throw project::ProjectError(
                        project::ProjectErrorCode::InvalidValue,
                        "The project contains duplicate or invalid media entries.",
                        std::nullopt,
                        project_media.source_path);
                }
                if (project_media.image_editor_link.has_value()) {
                    loaded_image_editor_links.insert_or_assign(
                        media::MediaLibrary::canonicalPath(project_media.source_path),
                        *project_media.image_editor_link);
                }
            }
            ++completed_steps;
        }
        normalized_document.bins = loaded_library.bins();

        if (document.timing_migration_required) {
            document.timeline_frame_rate = inferLegacyTimelineRate(
                document, loaded_library);
        }
        if (!timeline::validFrameRate(document.timeline_frame_rate)) {
            throw project::ProjectError(
                project::ProjectErrorCode::InvalidValue,
                "The project timeline frame rate is invalid.",
                std::nullopt,
                project_path);
        }
        document.timeline_frame_rate = timeline::reducedFrameRate(
            document.timeline_frame_rate);

        for (auto& track : document.timeline_tracks) {
            bool converted_timing = false;
            for (auto& clip : track.clips) {
                if (!timeline::isFrameTimedMediaClipKind(clip.kind)) continue;
                const bool legacy_clip = document.timing_migration_required;
                if (legacy_clip) clip.source_duration_frames = clip.duration_frames;
                if (clip.source_duration_frames <= 0) {
                    throw project::ProjectError(
                        project::ProjectErrorCode::InvalidTimeline,
                        "A media clip has no valid source duration.",
                        std::nullopt,
                        clip.source_path);
                }

                const auto media_index = loaded_library.indexForPath(clip.source_path);
                if (media_index >= loaded_library.size()) {
                    throw project::ProjectError(
                        project::ProjectErrorCode::MediaUnavailable,
                        "A timeline clip refers to media that is not imported in the project.",
                        std::nullopt,
                        clip.source_path);
                }
                const auto& item = loaded_library.items()[media_index];
                if (item.metadata.kind == media::MediaKind::Image) {
                    clip.kind = timeline::ClipKind::Image;
                } else {
                    clip.kind = timeline::ClipKind::Video;
                }

                const bool migrate_now = !item.offline &&
                    (legacy_clip || clip.source_duration_migration_pending);
                if (!migrate_now) {
                    if (legacy_clip) clip.source_duration_migration_pending = true;
                    continue;
                }

                const double source_rate = clip.kind == timeline::ClipKind::Image
                    ? media::kStillImageFrameRate
                    : item.metadata.frame_rate.value_or(
                          document.timeline_frame_rate.asDouble());
                const auto duration = timeline::timelineFramesForSourceDuration(
                    clip.source_duration_frames, source_rate,
                    document.timeline_frame_rate);
                if (!duration.has_value()) {
                    throw project::ProjectError(
                        project::ProjectErrorCode::InvalidTimeline,
                        "A media clip duration could not be converted to the project timeline rate.",
                        std::nullopt,
                        clip.source_path);
                }
                clip.duration_frames = *duration;
                clip.source_duration_migration_pending = false;
                converted_timing = true;

                const auto source_count = availableFrameCount(item.metadata);
                if (source_count.has_value() &&
                    (clip.source_start_frame > *source_count ||
                     clip.source_duration_frames > *source_count -
                         clip.source_start_frame)) {
                    throw project::ProjectError(
                        project::ProjectErrorCode::InvalidTimeline,
                        "A timeline clip is outside the current media source bounds.",
                        std::nullopt,
                        clip.source_path);
                }
            }
            if (converted_timing) preserveTransitionContinuity(track, project_path);
        }
        migrateAudioCompanions(document, loaded_library);
        normalized_document.timeline_frame_rate = document.timeline_frame_rate;
        normalized_document.timeline_tracks = document.timeline_tracks;
        normalized_document.timing_migration_required = false;
        normalized_document.audio_companion_migration_required = false;

        timeline::TimelineModel::Snapshot snapshot;
        snapshot.frame_rate = document.timeline_frame_rate;
        timeline::TrackId next_track_id = 1;
        timeline::ClipId next_clip_id = 1;
        for (std::size_t track_index = 0;
             track_index < document.timeline_tracks.size(); ++track_index) {
            if (cancel_requested.load(std::memory_order_relaxed)) {
                return cancelledResult(std::move(warnings));
            }
            if (progress) progress(completed_steps, total_steps, project_path);
            const auto& project_track = document.timeline_tracks[track_index];
            const auto track_id = project_track.track_id;
            next_track_id = std::max(next_track_id, track_id + 1);
            timeline::TimelineTrack prepared_track{
                track_id,
                project_track.name.empty()
                    ? (project_track.kind == timeline::TrackKind::Audio
                           ? "Audio " + std::to_string(track_index + 1)
                           : "Video " + std::to_string(track_index + 1))
                    : project_track.name,
                project_track.audio_gain,
                project_track.audio_muted,
                {}};
            prepared_track.kind = project_track.kind;
            snapshot.tracks.push_back(std::move(prepared_track));

            for (std::size_t clip_index = 0;
                 clip_index < project_track.clips.size(); ++clip_index) {
                if (cancel_requested.load(std::memory_order_relaxed)) {
                    return cancelledResult(std::move(warnings));
                }
                current_clip_index = clip_index;
                const auto& project_clip = project_track.clips[clip_index];
                if (project_clip.kind == timeline::ClipKind::Text) {
                    if (project_clip.timeline_start_frame < 0 ||
                        project_clip.source_start_frame < 0 ||
                        project_clip.duration_frames <= 0 ||
                        !timeline::TimelineModel::validTextStyle(project_clip.text)) {
                        throw project::ProjectError(
                            project::ProjectErrorCode::InvalidTimeline,
                            "A text timeline clip has invalid timing or style.",
                            std::nullopt,
                            project_path);
                    }
                    timeline::TimelineClip text_clip;
                    text_clip.timeline_start_frame = project_clip.timeline_start_frame;
                    text_clip.source_start_frame = project_clip.source_start_frame;
                    text_clip.timeline_duration_frames = project_clip.duration_frames;
                    text_clip.display_name = project_clip.text.content.empty()
                        ? "Text"
                        : project_clip.text.content;
                    text_clip.duration_seconds = static_cast<double>(
                        project_clip.duration_frames) /
                        document.timeline_frame_rate.asDouble();
                    text_clip.frame_rate = document.timeline_frame_rate.asDouble();
                    text_clip.frame_count = project_clip.duration_frames;
                    text_clip.audio_gain = project_clip.audio_gain;
                    text_clip.audio_muted = project_clip.audio_muted;
                    text_clip.clip_id = project_clip.clip_id;
                    next_clip_id = std::max(next_clip_id, project_clip.clip_id + 1);
                    text_clip.track_id = track_id;
                    text_clip.transform = project_clip.transform;
                    text_clip.keyframes = project_clip.keyframes;
                    text_clip.kind = timeline::ClipKind::Text;
                    text_clip.text = project_clip.text;
                    snapshot.tracks.back().clips.push_back(std::move(text_clip));
                    continue;
                }

                const auto media_index = loaded_library.indexForPath(project_clip.source_path);
                if (media_index == loaded_library.size()) {
                    throw project::ProjectError(
                        project::ProjectErrorCode::MediaUnavailable,
                        "A timeline clip refers to media that is not imported in the project.",
                        std::nullopt,
                        project_clip.source_path);
                }

                const auto& loaded_item = loaded_library.items()[media_index];
                const auto& metadata = loaded_item.metadata;
                if (project_clip.kind == timeline::ClipKind::Audio) {
                    const auto audio_duration = loaded_item.offline
                        ? std::optional<std::int64_t>{}
                        : availableAudioDurationTimeUs(metadata);
                    const bool linked_video_source =
                        metadata.kind == media::MediaKind::Video &&
                        project_clip.linked_clip_id.has_value();
                    const bool audio_source = metadata.kind == media::MediaKind::Audio ||
                        (linked_video_source &&
                         (loaded_item.offline || metadata.audio.has_value()));
                    if ((!loaded_item.offline && !audio_source) ||
                        (!loaded_item.offline && !linked_video_source &&
                         !audio_duration.has_value()) ||
                        project_clip.timeline_start_frame < 0 ||
                        project_clip.duration_frames <= 0 ||
                        project_clip.source_start_time_us < 0 ||
                        project_clip.source_duration_time_us <= 0 ||
                        project_clip.source_start_time_us >
                            std::numeric_limits<std::int64_t>::max() -
                                project_clip.source_duration_time_us ||
                        (audio_duration.has_value() &&
                         project_clip.source_start_time_us +
                                 project_clip.source_duration_time_us >
                             *audio_duration)) {
                        throw project::ProjectError(
                            project::ProjectErrorCode::InvalidTimeline,
                            "An audio timeline clip is outside the current media bounds.",
                            std::nullopt,
                            project_clip.source_path);
                    }
                    timeline::TimelineClip audio_clip;
                    audio_clip.timeline_start_frame = project_clip.timeline_start_frame;
                    audio_clip.source_start_frame = 0;
                    audio_clip.timeline_duration_frames = project_clip.duration_frames;
                    audio_clip.source_path = media::MediaLibrary::canonicalPath(
                        project_clip.source_path);
                    audio_clip.display_name = loaded_item.display_name;
                    audio_clip.duration_seconds = metadata.audio.has_value() &&
                            metadata.audio->duration_seconds.has_value()
                        ? metadata.audio->duration_seconds
                        : std::optional<double>(
                              static_cast<double>(project_clip.source_start_time_us +
                                  project_clip.source_duration_time_us) / 1000000.0);
                    audio_clip.frame_rate.reset();
                    audio_clip.frame_count.reset();
                    audio_clip.audio_gain = project_clip.audio_gain;
                    audio_clip.audio_muted = project_clip.audio_muted;
                    audio_clip.audio_gain_keyframes =
                        project_clip.audio_gain_keyframes;
                    audio_clip.clip_id = project_clip.clip_id;
                    audio_clip.track_id = track_id;
                    audio_clip.transform = project_clip.transform;
                    audio_clip.keyframes = project_clip.keyframes;
                    audio_clip.kind = timeline::ClipKind::Audio;
                    audio_clip.source_start_time_us = project_clip.source_start_time_us;
                    audio_clip.source_duration_time_us =
                        project_clip.source_duration_time_us;
                    audio_clip.linked_clip_id = project_clip.linked_clip_id;
                    snapshot.tracks.back().clips.push_back(std::move(audio_clip));
                    next_clip_id = std::max(next_clip_id, project_clip.clip_id + 1);
                    continue;
                }
                const auto frame_count = loaded_item.offline
                    ? std::optional<std::int64_t>{}
                    : availableFrameCount(metadata);
                const auto source_duration = project_clip.source_duration_frames;
                if ((!loaded_item.offline && !frame_count) ||
                    project_clip.source_start_frame < 0 ||
                    project_clip.duration_frames <= 0 ||
                    (!loaded_item.offline &&
                     (project_clip.source_start_frame > *frame_count ||
                      source_duration > *frame_count -
                          project_clip.source_start_frame)) ||
                    project_clip.timeline_start_frame < 0) {
                    throw project::ProjectError(
                        project::ProjectErrorCode::InvalidTimeline,
                        "A timeline clip is outside the current media bounds.",
                        std::nullopt,
                        project_clip.source_path);
                }

                timeline::TimelineClip clip;
                clip.timeline_start_frame = project_clip.timeline_start_frame;
                clip.source_start_frame = project_clip.source_start_frame;
                clip.timeline_duration_frames = project_clip.duration_frames;
                clip.source_duration_frames = source_duration;
                clip.source_duration_migration_pending =
                    project_clip.source_duration_migration_pending;
                clip.source_path = media::MediaLibrary::canonicalPath(metadata.source_path);
                clip.display_name = loaded_item.display_name;
                clip.duration_seconds = metadata.duration_seconds;
                clip.frame_rate = metadata.frame_rate;
                clip.frame_count = metadata.frame_count;
                clip.audio_gain = project_clip.audio_gain;
                clip.audio_muted = project_clip.audio_muted;
                clip.audio_gain_keyframes = project_clip.audio_gain_keyframes;
                clip.effects = project_clip.effects;
                clip.node_graph = project_clip.node_graph;
                clip.clip_id = project_clip.clip_id;
                clip.track_id = track_id;
                clip.transform = project_clip.transform;
                clip.keyframes = project_clip.keyframes;
                clip.kind = metadata.kind == media::MediaKind::Image
                    ? timeline::ClipKind::Image
                    : timeline::ClipKind::Video;
                clip.linked_clip_id = project_clip.linked_clip_id;
                clip.audio_extracted = project_clip.audio_extracted;
                clip.audio_companion_pending = project_clip.audio_companion_pending;
                clip.image_editor_variant = project_clip.image_editor_variant;
                if (clip.image_editor_variant.has_value() &&
                    clip.kind == timeline::ClipKind::Image) {
                    const auto& variant_path =
                        clip.image_editor_variant->published_output_path;
                    std::error_code variant_error;
                    if (std::filesystem::is_regular_file(variant_path, variant_error) &&
                        !variant_error) {
                        try {
                            clip.still_image_override =
                                std::make_shared<const media::VideoFrame>(
                                    media::StillImageDecoder{}.decode_first_frame(variant_path));
                        } catch (const std::exception& error) {
                            warnings.push_back({
                                ProjectOpenIssueKind::Media,
                                variant_path,
                                std::string("The linked clip image could not be decoded; the Media Pool image will be used. ") + error.what(),
                                std::nullopt,
                                std::nullopt});
                        }
                    } else {
                        warnings.push_back({
                            ProjectOpenIssueKind::Media,
                            variant_path,
                            "The linked clip image output is missing; the Media Pool image will be used.",
                            std::nullopt,
                            std::nullopt});
                    }
                }
                snapshot.tracks.back().clips.push_back(std::move(clip));
                next_clip_id = std::max(next_clip_id, project_clip.clip_id + 1);
            }

            for (const auto& transition : project_track.transitions) {
                if (transition.from_clip_index >= snapshot.tracks.back().clips.size() ||
                    transition.to_clip_index >= snapshot.tracks.back().clips.size()) {
                    throw project::ProjectError(
                        project::ProjectErrorCode::InvalidTimeline,
                        "A timeline transition refers to an invalid clip.",
                        std::nullopt,
                        project_path);
                }
                const auto& from = snapshot.tracks.back().clips[transition.from_clip_index];
                const auto& to = snapshot.tracks.back().clips[transition.to_clip_index];
                snapshot.tracks.back().transitions.push_back({
                    from.clip_id,
                    to.clip_id,
                    transition.kind,
                    transition.duration_frames});
            }
            ++completed_steps;
        }
        snapshot.next_track_id = next_track_id;
        snapshot.next_clip_id = next_clip_id;

        if (cancel_requested.load(std::memory_order_relaxed)) {
            return cancelledResult(std::move(warnings));
        }
        PreparedProject prepared;
        prepared.media_library = std::move(loaded_library);
        prepared.image_editor_links = std::move(loaded_image_editor_links);
        prepared.timeline = std::move(snapshot);
        prepared.active_project_path = active_project_path.has_value()
            ? std::optional<std::filesystem::path>(
                  media::MediaLibrary::canonicalPath(*active_project_path))
            : std::nullopt;
        prepared.document = std::move(normalized_document);
        if (saved_baseline.has_value() &&
            *saved_baseline == loaded_source_document) {
            saved_baseline = prepared.document;
        }
        prepared.saved_baseline = std::move(saved_baseline);

        ProjectOpenResult result;
        result.status = ProjectOpenStatus::Prepared;
        result.prepared = std::move(prepared);
        result.warnings = std::move(warnings);
        return result;
    } catch (const project::ProjectError& error) {
        ProjectOpenResult result;
        result.status = ProjectOpenStatus::Failed;
        result.failure = ProjectOpenIssue{
            ProjectOpenIssueKind::Project,
            error.related_path().empty() ? project_path : error.related_path(),
            error.what(),
            error.system_error(),
            error.code()};
        result.warnings = std::move(warnings);
        return result;
    } catch (const media::MediaError& error) {
        ProjectOpenResult result;
        result.status = ProjectOpenStatus::Failed;
        result.failure = ProjectOpenIssue{
            ProjectOpenIssueKind::Media,
            current_media_path.empty() ? project_path : current_media_path,
            error.what(),
            error.error_code(),
            std::nullopt};
        result.warnings = std::move(warnings);
        return result;
    } catch (const std::exception& error) {
        ProjectOpenResult result;
        result.status = ProjectOpenStatus::Failed;
        result.failure = ProjectOpenIssue{
            ProjectOpenIssueKind::Unexpected,
            current_clip_index.has_value() && !current_media_path.empty()
                ? current_media_path
                : project_path,
            error.what(),
            std::nullopt,
            std::nullopt};
        result.warnings = std::move(warnings);
        return result;
    } catch (...) {
        ProjectOpenResult result;
        result.status = ProjectOpenStatus::Failed;
        result.failure = ProjectOpenIssue{
            ProjectOpenIssueKind::Unexpected,
            current_clip_index.has_value() && !current_media_path.empty()
                ? current_media_path
                : project_path,
            "Unknown project preparation failure.",
            std::nullopt,
            std::nullopt};
        result.warnings = std::move(warnings);
        return result;
    }
}

} // namespace application
