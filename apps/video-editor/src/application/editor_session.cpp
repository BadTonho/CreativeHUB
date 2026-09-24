#include "editor_session.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace application {

const timeline::TimelineModel& EditorSession::timeline() const noexcept {
    return timeline_;
}

timeline::TimelineModel& EditorSession::legacyTimelineForUi() noexcept {
    return timeline_;
}

const std::vector<ImportedMedia>& EditorSession::mediaItems() const noexcept {
    return media_library_.items();
}

const std::vector<std::string>& EditorSession::binPaths() const noexcept {
    return media_library_.bins();
}

const media::MediaLibrary& EditorSession::mediaLibrary() const noexcept {
    return media_library_;
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

const std::optional<project::ProjectDocument>&
EditorSession::savedProjectDocument() const noexcept {
    return saved_project_document_;
}

bool EditorSession::projectDirty() const noexcept {
    return project_dirty_;
}

const bool& EditorSession::projectDirtyState() const noexcept {
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
    assertInvariants();
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
    assertInvariants();
}

void EditorSession::assertInvariants() const {
#ifndef NDEBUG
    timeline_.assertIdentityInvariants();
    assert(playhead_frame_ >= 0);

    if (selection_.active_track_id.has_value()) {
        assert(timeline_.locateTrack(*selection_.active_track_id).has_value());
    }

    if (selection_.active_clip_id.has_value()) {
        const auto location = timeline_.locateClip(*selection_.active_clip_id);
        assert(location.has_value());
        if (!location.has_value()) return;
        assert(selection_.active_track_id.has_value());
        if (!selection_.active_track_id.has_value()) return;
        const auto& track = timeline_.tracks()[location->track_index];
        assert(track.track_id == *selection_.active_track_id);
        assert(track.clips[location->clip_index].track_id == track.track_id);
    }

    if (selection_.active_transition.has_value()) {
        const auto& selected = *selection_.active_transition;
        const auto track_index = timeline_.locateTrack(selected.track_id);
        const auto from = timeline_.locateClip(selected.from_clip_id);
        const auto to = timeline_.locateClip(selected.to_clip_id);
        assert(track_index.has_value());
        assert(from.has_value());
        assert(to.has_value());
        if (!track_index.has_value() || !from.has_value() || !to.has_value()) return;
        assert(from->track_index == *track_index);
        assert(to->track_index == *track_index);
        assert(from->clip_index + 1 == to->clip_index);
        assert(timeline_.transitionBetween(
                   *track_index, from->clip_index, to->clip_index) != nullptr);
    }
#endif
}

} // namespace application
