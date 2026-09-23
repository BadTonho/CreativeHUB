#pragma once

#include "media/video_frame.h"
#include "media/video_metadata.h"
#include "project/project_document.h"
#include "timeline/timeline_history.h"
#include "timeline/timeline_model.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace application {

struct ImportedMedia {
    media::VideoMetadata metadata;
    media::VideoFrame first_frame;
    std::string display_name;
    std::string bin_path = "Unsorted";
    bool offline = false;
};

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
    [[nodiscard]] std::vector<ImportedMedia>& mediaItemsForUi() noexcept;
    [[nodiscard]] const std::vector<std::string>& binPaths() const noexcept;
    [[nodiscard]] std::vector<std::string>& binPathsForUi() noexcept;

    [[nodiscard]] const EditorSelection& selection() const noexcept;
    [[nodiscard]] EditorSelection& selectionForUi() noexcept;

    [[nodiscard]] const std::optional<std::filesystem::path>& projectPath() const noexcept;
    [[nodiscard]] std::optional<std::filesystem::path>& projectPathForUi() noexcept;
    [[nodiscard]] const std::optional<project::ProjectDocument>&
    savedProjectDocument() const noexcept;
    [[nodiscard]] std::optional<project::ProjectDocument>&
    savedProjectDocumentForUi() noexcept;

    [[nodiscard]] bool projectDirty() const noexcept;
    void setProjectDirty(bool dirty) noexcept;
    [[nodiscard]] bool& projectDirtyForUi() noexcept;

    [[nodiscard]] std::int64_t playheadFrame() const noexcept;
    void setPlayheadFrame(std::int64_t frame) noexcept;
    [[nodiscard]] std::int64_t& playheadFrameForUi() noexcept;
    [[nodiscard]] std::optional<std::int64_t>& preservedPlayheadFrameForUi() noexcept;

    [[nodiscard]] timeline::EditState captureEditState() const;
    void restoreEditState(timeline::EditState state);

private:
    friend class TimelineCommandService;

    timeline::TimelineModel timeline_;
    timeline::TimelineHistory history_;
    std::vector<ImportedMedia> media_items_;
    std::vector<std::string> bin_paths_{"Unsorted"};
    EditorSelection selection_;
    std::optional<std::filesystem::path> project_path_;
    std::optional<project::ProjectDocument> saved_project_document_;
    bool project_dirty_ = false;
    std::int64_t playhead_frame_ = 0;
    std::optional<std::int64_t> preserved_playhead_frame_;
};

} // namespace application
