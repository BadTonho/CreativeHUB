#include "project_document_mapper.h"

#include "media/media_library.h"

#include <algorithm>
#include <utility>

namespace application {

project::ProjectDocument ProjectDocumentMapper::toDocument(
    const EditorSession& session,
    TimelinePresentationState presentation) {
    project::ProjectDocument document;
    document.timeline_zoom = presentation.zoom;
    document.timeline_row_height = presentation.row_height;
    const auto& library = session.mediaLibrary();
    document.media.reserve(library.size());
    document.bins = library.bins();
    for (const auto& item : library.items()) {
        project::ProjectMedia project_media{
            media::MediaLibrary::canonicalPath(item.metadata.source_path),
            item.display_name,
            item.bin_path,
            item.offline,
            item.metadata.kind};
        project_media.image_editor_link = item.image_editor_link;
        document.media.push_back(std::move(project_media));
    }

    for (const auto& track : session.timeline().tracks()) {
        project::ProjectTrack project_track;
        project_track.track_id = track.track_id;
        project_track.name = track.name;
        project_track.audio_gain = track.audio_gain;
        project_track.audio_muted = track.audio_muted;
        project_track.clips.reserve(track.clips.size());
        for (const auto& clip : track.clips) {
            project::ProjectClip project_clip;
            project_clip.clip_id = clip.clip_id;
            if (timeline::isMediaClipKind(clip.kind)) {
                project_clip.source_path = media::MediaLibrary::canonicalPath(clip.source_path);
            }
            project_clip.timeline_start_frame = clip.timeline_start_frame;
            project_clip.source_start_frame = clip.source_start_frame;
            project_clip.duration_frames = clip.timeline_duration_frames;
            project_clip.audio_gain = clip.audio_gain;
            project_clip.audio_muted = clip.audio_muted;
            project_clip.transform = clip.transform;
            project_clip.keyframes = clip.keyframes;
            project_clip.kind = clip.kind;
            project_clip.text = clip.text;
            project_clip.image_editor_variant = clip.image_editor_variant;
            project_track.clips.push_back(std::move(project_clip));
        }
        for (const auto& transition : track.transitions) {
            const auto from = std::find_if(
                track.clips.begin(), track.clips.end(),
                [&transition](const timeline::TimelineClip& clip) {
                    return clip.clip_id == transition.from_clip_id;
                });
            const auto to = std::find_if(
                track.clips.begin(), track.clips.end(),
                [&transition](const timeline::TimelineClip& clip) {
                    return clip.clip_id == transition.to_clip_id;
                });
            if (from != track.clips.end() && to != track.clips.end()) {
                project_track.transitions.push_back(project::ProjectTransition{
                    static_cast<std::size_t>(std::distance(track.clips.begin(), from)),
                    static_cast<std::size_t>(std::distance(track.clips.begin(), to)),
                    transition.kind,
                    transition.duration_frames});
            }
        }
        document.timeline_tracks.push_back(std::move(project_track));
    }
    return document;
}

} // namespace application
