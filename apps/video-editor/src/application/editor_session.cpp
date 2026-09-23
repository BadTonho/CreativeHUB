#include "editor_session.h"

#include <algorithm>
#include <utility>

namespace application {

const timeline::TimelineModel& EditorSession::timeline() const noexcept {
    return timeline_;
}

timeline::TimelineModel& EditorSession::legacyTimelineForUi() noexcept {
    return timeline_;
}

const std::vector<ImportedMedia>& EditorSession::mediaItems() const noexcept {
    return media_items_;
}

std::vector<ImportedMedia>& EditorSession::mediaItemsForUi() noexcept {
    return media_items_;
}

const std::vector<std::string>& EditorSession::binPaths() const noexcept {
    return bin_paths_;
}

std::vector<std::string>& EditorSession::binPathsForUi() noexcept {
    return bin_paths_;
}

const EditorSelection& EditorSession::selection() const noexcept {
    return selection_;
}

EditorSelection& EditorSession::selectionForUi() noexcept {
    return selection_;
}

const std::optional<std::filesystem::path>& EditorSession::projectPath() const noexcept {
    return project_path_;
}

std::optional<std::filesystem::path>& EditorSession::projectPathForUi() noexcept {
    return project_path_;
}

const std::optional<project::ProjectDocument>&
EditorSession::savedProjectDocument() const noexcept {
    return saved_project_document_;
}

std::optional<project::ProjectDocument>&
EditorSession::savedProjectDocumentForUi() noexcept {
    return saved_project_document_;
}

bool EditorSession::projectDirty() const noexcept {
    return project_dirty_;
}

void EditorSession::setProjectDirty(bool dirty) noexcept {
    project_dirty_ = dirty;
}

bool& EditorSession::projectDirtyForUi() noexcept {
    return project_dirty_;
}

std::int64_t EditorSession::playheadFrame() const noexcept {
    return playhead_frame_;
}

void EditorSession::setPlayheadFrame(std::int64_t frame) noexcept {
    playhead_frame_ = std::max<std::int64_t>(0, frame);
}

std::int64_t& EditorSession::playheadFrameForUi() noexcept {
    return playhead_frame_;
}

std::optional<std::int64_t>& EditorSession::preservedPlayheadFrameForUi() noexcept {
    return preserved_playhead_frame_;
}

timeline::EditState EditorSession::captureEditState() const {
    timeline::EditState state;
    state.timeline = timeline_.snapshot();
    state.active_track_id = selection_.active_track_id;
    state.active_clip_id = selection_.active_clip_id;
    state.selected_source_path = selection_.selected_source_path;
    state.active_transition = selection_.active_transition;
    state.playhead_frame = playhead_frame_;
    state.preserved_playhead_frame = preserved_playhead_frame_;
    return state;
}

void EditorSession::restoreEditState(timeline::EditState state) {
    timeline_.restore(std::move(state.timeline));
    selection_.active_track_id = state.active_track_id;
    selection_.active_clip_id = state.active_clip_id;
    selection_.selected_source_path = std::move(state.selected_source_path);
    selection_.active_transition = state.active_transition;
    playhead_frame_ = std::max<std::int64_t>(0, state.playhead_frame);
    preserved_playhead_frame_ = state.preserved_playhead_frame;
}

} // namespace application
