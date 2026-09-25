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
            if (!exists || (project_media.offline && !linked_output_exists)) {
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
                    static_cast<void>(loaded_library.setImageEditorLink(
                        metadata.source_path,
                        project_media.image_editor_link));
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
                    static_cast<void>(loaded_library.setImageEditorLink(
                        project_media.source_path,
                        project_media.image_editor_link));
                }
            }
            ++completed_steps;
        }
        normalized_document.bins = loaded_library.bins();

        timeline::TimelineModel::Snapshot snapshot;
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
            snapshot.tracks.push_back(timeline::TimelineTrack{
                track_id,
                project_track.name.empty()
                    ? "Video " + std::to_string(track_index + 1)
                    : project_track.name,
                project_track.audio_gain,
                project_track.audio_muted,
                {}});

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
                    text_clip.duration_seconds =
                        static_cast<double>(project_clip.duration_frames) / 30.0;
                    text_clip.frame_rate = 30.0;
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
                const auto frame_count = loaded_item.offline
                    ? std::optional<std::int64_t>{}
                    : availableFrameCount(metadata);
                if ((!loaded_item.offline && !frame_count) ||
                    project_clip.source_start_frame < 0 ||
                    project_clip.duration_frames <= 0 ||
                    (!loaded_item.offline &&
                     (project_clip.source_start_frame > *frame_count ||
                      project_clip.duration_frames > *frame_count -
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
                clip.source_path = media::MediaLibrary::canonicalPath(metadata.source_path);
                clip.display_name = loaded_item.display_name;
                clip.duration_seconds = metadata.duration_seconds;
                clip.frame_rate = metadata.frame_rate;
                clip.frame_count = metadata.frame_count;
                clip.audio_gain = project_clip.audio_gain;
                clip.audio_muted = project_clip.audio_muted;
                clip.clip_id = project_clip.clip_id;
                clip.track_id = track_id;
                clip.transform = project_clip.transform;
                clip.keyframes = project_clip.keyframes;
                clip.kind = metadata.kind == media::MediaKind::Image
                    ? timeline::ClipKind::Image
                    : timeline::ClipKind::Video;
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
        prepared.timeline = std::move(snapshot);
        prepared.active_project_path = active_project_path.has_value()
            ? std::optional<std::filesystem::path>(
                  media::MediaLibrary::canonicalPath(*active_project_path))
            : std::nullopt;
        prepared.document = std::move(normalized_document);
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
