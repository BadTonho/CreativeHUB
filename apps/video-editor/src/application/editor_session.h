#pragma once

#include "media/video_frame.h"
#include "media/media_library.h"
#include "media/video_metadata.h"
#include "project/project_document.h"
#include "timeline/timeline_history.h"
#include "timeline/timeline_model.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace playback {
class PlaybackController;
}

namespace application {

using ImportedMedia = media::MediaItem;

struct EditorSelection {
    std::optional<timeline::TrackId> active_track_id;
    std::optional<timeline::ClipId> active_clip_id;
    std::optional<std::filesystem::path> selected_source_path;
    std::optional<timeline::TransitionSelection> active_transition;
};

// Owns the mutable document state shared by the editor's application layer.
// The explicit UI accessors are a compatibility bridge while non-command
// handlers are migrated into application services.
class EditorSession final {
public:
    [[nodiscard]] const timeline::TimelineModel& timeline() const noexcept;
    [[nodiscard]] timeline::TimelineModel& legacyTimelineForUi() noexcept;

    [[nodiscard]] const std::vector<ImportedMedia>& mediaItems() const noexcept;
    [[nodiscard]] const std::vector<std::string>& binPaths() const noexcept;
    [[nodiscard]] const media::MediaLibrary& mediaLibrary() const noexcept;
    [[nodiscard]] std::optional<media::LinkedImageReference> imageEditorLinkForPath(
        const std::filesystem::path& path) const;
    [[nodiscard]] std::optional<media::MotionLinkReference> motionLinkForPath(
        const std::filesystem::path& path) const;

    [[nodiscard]] const EditorSelection& selection() const noexcept;
    [[nodiscard]] EditorSelection& selectionForUi() noexcept;

    [[nodiscard]] const std::optional<std::filesystem::path>& projectPath() const noexcept;
    [[nodiscard]] const std::optional<project::ProjectDocument>&
    savedProjectDocument() const noexcept;
    [[nodiscard]] int canvasWidth() const noexcept { return canvas_width_; }
    [[nodiscard]] int canvasHeight() const noexcept { return canvas_height_; }

    [[nodiscard]] bool projectDirty() const noexcept;
    [[nodiscard]] const bool& projectDirtyState() const noexcept;

    [[nodiscard]] std::int64_t playheadFrame() const noexcept;
    void setPlayheadFrame(std::int64_t frame) noexcept;
    [[nodiscard]] std::int64_t& playheadFrameForUi() noexcept;
    [[nodiscard]] std::optional<std::int64_t>& preservedPlayheadFrameForUi() noexcept;

    [[nodiscard]] timeline::EditState captureEditState() const;
    void restoreEditState(timeline::EditState state);

private:
    friend class TimelineCommandService;
    friend class MediaController;
    friend class ProjectController;
    friend class playback::PlaybackController;

    void assertInvariants() const;

    timeline::TimelineModel timeline_;
    timeline::TimelineHistory history_;
    media::MediaLibrary media_library_;
    std::map<std::filesystem::path, media::LinkedImageReference> image_editor_links_;
    std::map<std::filesystem::path, media::MotionLinkReference> motion_links_;
    EditorSelection selection_;
    std::optional<std::filesystem::path> project_path_;
    std::optional<project::ProjectDocument> saved_project_document_;
    int canvas_width_ = 1920;
    int canvas_height_ = 1080;
    bool project_dirty_ = false;
    std::int64_t playhead_frame_ = 0;
    std::optional<std::int64_t> preserved_playhead_frame_;
};

} // namespace application
