#include "timeline_command_service.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace application {
namespace {

EditReason reasonForMove(timeline::MoveClipResult result) noexcept {
    switch (result) {
    case timeline::MoveClipResult::InvalidIndex:
    case timeline::MoveClipResult::InvalidTrack:
        return EditReason::InvalidTarget;
    case timeline::MoveClipResult::InvalidPosition:
        return EditReason::InvalidPosition;
    case timeline::MoveClipResult::Overlap:
        return EditReason::Overlap;
    case timeline::MoveClipResult::Moved:
    case timeline::MoveClipResult::NoChange:
        return EditReason::None;
    }
    return EditReason::InvalidTarget;
}

EditReason reasonForAdd(timeline::AddClipResult result) noexcept {
    switch (result) {
    case timeline::AddClipResult::InvalidTrack:
        return EditReason::InvalidTarget;
    case timeline::AddClipResult::InvalidTimingMetadata:
        return EditReason::InvalidTimingMetadata;
    case timeline::AddClipResult::InvalidPosition:
        return EditReason::InvalidPosition;
    case timeline::AddClipResult::Overlap:
        return EditReason::Overlap;
    case timeline::AddClipResult::Added:
        return EditReason::None;
    }
    return EditReason::InvalidTarget;
}

EditReason reasonForTransition(timeline::TransitionMutationResult result) noexcept {
    switch (result) {
    case timeline::TransitionMutationResult::InvalidIndex:
    case timeline::TransitionMutationResult::NotFound:
        return EditReason::InvalidTarget;
    case timeline::TransitionMutationResult::InvalidBoundary:
        return EditReason::InvalidBoundary;
    case timeline::TransitionMutationResult::InvalidRange:
        return EditReason::InvalidRange;
    case timeline::TransitionMutationResult::Added:
    case timeline::TransitionMutationResult::Updated:
    case timeline::TransitionMutationResult::Removed:
    case timeline::TransitionMutationResult::NoChange:
        return EditReason::None;
    }
    return EditReason::InvalidTarget;
}

} // namespace

TimelineCommandService::TimelineCommandService(EditorSession& session) noexcept
    : session_(session) {}

TimelineEditResult TimelineCommandService::result(
    EditStatus status,
    EditReason reason) const {
    TimelineEditResult output;
    output.status = status;
    output.reason = reason;
    output.selection = session_.selection_;
    output.playhead_frame = session_.playhead_frame_;
    output.preserved_playhead_frame = session_.preserved_playhead_frame_;
    return output;
}

void TimelineCommandService::recordSuccessfulEdit(timeline::EditState before) {
    session_.history_.recordBeforeEdit(std::move(before));
}

void TimelineCommandService::selectClip(timeline::ClipLocation location) {
    const auto& track = session_.timeline_.tracks()[location.track_index];
    const auto& clip = track.clips[location.clip_index];
    session_.selection_.active_track_id = track.track_id;
    session_.selection_.active_clip_id = clip.clip_id;
    session_.selection_.active_transition.reset();
    if (timeline::isMediaClipKind(clip.kind)) {
        session_.selection_.selected_source_path = clip.source_path;
    }
}

void TimelineCommandService::selectClip(timeline::ClipId clip_id) {
    if (const auto location = session_.timeline_.locateClip(clip_id)) {
        selectClip(*location);
    }
}

void TimelineCommandService::selectTransition(
    timeline::TrackId track_id,
    timeline::ClipId from_clip_id,
    timeline::ClipId to_clip_id) {
    session_.selection_.active_transition = timeline::TransitionSelection{
        track_id, from_clip_id, to_clip_id};
}

TimelineEditResult TimelineCommandService::execute(const MoveClipCommand& command) {
    const auto source = session_.timeline_.locateClip(command.clip_id);
    const auto target_track = session_.timeline_.locateTrack(command.target_track_id);
    if (!source || !target_track) return result(EditStatus::Rejected, EditReason::InvalidTarget);

    auto before = session_.captureEditState();
    const auto previous_track_id =
        before.timeline.tracks[source->track_index].track_id;
    const auto moved = session_.timeline_.moveClip(
        *source,
        timeline::ClipLocation{*target_track, 0},
        command.timeline_start_frame);
    if (moved == timeline::MoveClipResult::NoChange) {
        return result(EditStatus::NoChange);
    }
    if (moved != timeline::MoveClipResult::Moved) {
        return result(EditStatus::Rejected, reasonForMove(moved));
    }

    recordSuccessfulEdit(std::move(before));
    selectClip(command.clip_id);
    auto output = result(EditStatus::Applied);
    output.affected_track_ids = {command.target_track_id};
    if (previous_track_id != command.target_track_id) {
        output.affected_track_ids.push_back(previous_track_id);
    }
    output.affected_clip_ids = {command.clip_id};
    output.invalidate_playback = true;
    return output;
}

TimelineEditResult TimelineCommandService::execute(const ReorderClipCommand& command) {
    const auto location = session_.timeline_.locateClip(command.clip_id);
    if (!location || location->track_index != 0 ||
        command.target_index >= session_.timeline_.clipCount(0)) {
        return result(EditStatus::Rejected, EditReason::InvalidTarget);
    }
    auto before = session_.captureEditState();
    const auto moved = session_.timeline_.moveClip(location->clip_index, command.target_index);
    if (moved == timeline::MoveClipResult::NoChange) return result(EditStatus::NoChange);
    if (moved != timeline::MoveClipResult::Moved) {
        return result(EditStatus::Rejected, reasonForMove(moved));
    }
    recordSuccessfulEdit(std::move(before));
    selectClip(command.clip_id);
    auto output = result(EditStatus::Applied);
    output.affected_track_ids = {session_.timeline_.tracks()[0].track_id};
    output.affected_clip_ids = {command.clip_id};
    output.invalidate_playback = true;
    return output;
}

TimelineEditResult TimelineCommandService::execute(const SplitClipCommand& command) {
    const auto location = session_.timeline_.locateClip(command.clip_id);
    if (!location) return result(EditStatus::Rejected, EditReason::InvalidTarget);
    auto before = session_.captureEditState();
    const auto split = session_.timeline_.splitClip(
        location->track_index, location->clip_index, command.local_frame);
    if (split != timeline::SplitClipResult::Split) {
        return result(EditStatus::Rejected, EditReason::InvalidBoundary);
    }
    const auto left = session_.timeline_.locateClip(command.clip_id);
    if (!left) {
        session_.restoreEditState(std::move(before));
        return result(EditStatus::Rejected, EditReason::InvalidTarget);
    }
    const auto& clips = session_.timeline_.tracks()[left->track_index].clips;
    if (left->clip_index + 1 >= clips.size()) {
        session_.restoreEditState(std::move(before));
        return result(EditStatus::Rejected, EditReason::InvalidTarget);
    }
    const auto new_clip_id = clips[left->clip_index + 1].clip_id;
    recordSuccessfulEdit(std::move(before));
    selectClip(new_clip_id);
    session_.playhead_frame_ = 0;
    session_.preserved_playhead_frame_.reset();
    auto output = result(EditStatus::Applied);
    output.affected_track_ids = {clips[left->clip_index].track_id};
    output.affected_clip_ids = {command.clip_id, new_clip_id};
    output.invalidate_playback = true;
    return output;
}

TimelineEditResult TimelineCommandService::execute(const TrimClipEdgeCommand& command) {
    const auto location = session_.timeline_.locateClip(command.clip_id);
    if (!location) return result(EditStatus::Rejected, EditReason::InvalidTarget);
    auto before = session_.captureEditState();
    const auto outcome = timeline::applyClipEdgeTrim(
        session_.timeline_, *location, command.edge, command.boundary_frame,
        command.mode, command.timeline_playhead_frame, command.playback_frame);
    if (outcome.result == timeline::TrimClipResult::NoChange) {
        return result(EditStatus::NoChange);
    }
    if (outcome.result != timeline::TrimClipResult::Trimmed || !outcome.selection) {
        const auto reason = outcome.result == timeline::TrimClipResult::InvalidRange
            ? EditReason::InvalidRange
            : outcome.result == timeline::TrimClipResult::InvalidIndex
                ? EditReason::InvalidTarget
                : EditReason::InvalidBoundary;
        return result(EditStatus::Rejected, reason);
    }
    recordSuccessfulEdit(std::move(before));
    selectClip(outcome.selection->location);
    session_.playhead_frame_ = outcome.selection->playback_frame;
    session_.preserved_playhead_frame_ = outcome.selection->preserved_playhead_frame;
    auto output = result(EditStatus::Applied);
    output.affected_track_ids = {session_.timeline_.tracks()[outcome.selection->location.track_index].track_id};
    output.affected_clip_ids = {command.clip_id};
    output.invalidate_playback = true;
    return output;
}

TimelineEditResult TimelineCommandService::execute(const TrimClipRangeCommand& command) {
    const auto location = session_.timeline_.locateClip(command.clip_id);
    if (!location) return result(EditStatus::Rejected, EditReason::InvalidTarget);
    const auto old_clip = session_.timeline_.tracks()[location->track_index].clips[location->clip_index];
    const bool was_active = session_.selection_.active_clip_id == old_clip.clip_id;
    const auto old_playhead_frame = session_.playhead_frame_;
    if (command.source_start_frame < 0 || command.duration_frames <= 0 ||
        command.source_start_frame > std::numeric_limits<std::int64_t>::max() - command.duration_frames) {
        return result(EditStatus::Rejected, EditReason::InvalidRange);
    }
    if (command.source_start_frame == old_clip.source_start_frame &&
        command.duration_frames == old_clip.timeline_duration_frames) {
        return result(EditStatus::NoChange);
    }
    auto before = session_.captureEditState();
    const auto trimmed = session_.timeline_.trimClip(
        location->track_index, location->clip_index,
        command.source_start_frame, command.duration_frames);
    if (trimmed == timeline::TrimClipResult::NoChange) return result(EditStatus::NoChange);
    if (trimmed != timeline::TrimClipResult::Trimmed) {
        return result(EditStatus::Rejected, EditReason::InvalidRange);
    }
    recordSuccessfulEdit(std::move(before));
    selectClip(command.clip_id);
    const auto source_delta = command.source_start_frame - old_clip.source_start_frame;
    session_.playhead_frame_ = was_active
        ? std::clamp<std::int64_t>(old_playhead_frame - source_delta, 0, command.duration_frames - 1)
        : 0;
    session_.preserved_playhead_frame_.reset();
    auto output = result(EditStatus::Applied);
    output.affected_track_ids = {session_.timeline_.tracks()[location->track_index].track_id};
    output.affected_clip_ids = {command.clip_id};
    output.invalidate_playback = true;
    return output;
}

TimelineEditResult TimelineCommandService::execute(const DeleteClipCommand& command) {
    const auto location = session_.timeline_.locateClip(command.clip_id);
    if (!location) return result(EditStatus::Rejected, EditReason::InvalidTarget);
    const auto track_id = session_.timeline_.tracks()[location->track_index].track_id;
    auto before = session_.captureEditState();
    if (session_.timeline_.removeClip(location->track_index, location->clip_index) !=
        timeline::RemoveClipResult::Removed) {
        return result(EditStatus::Rejected, EditReason::InvalidTarget);
    }
    recordSuccessfulEdit(std::move(before));
    session_.selection_.active_transition.reset();
    session_.playhead_frame_ = 0;
    session_.preserved_playhead_frame_.reset();
    if (!session_.timeline_.hasClip()) {
        session_.selection_.active_track_id.reset();
        session_.selection_.active_clip_id.reset();
    } else {
        const auto remaining_track = session_.timeline_.locateTrack(track_id);
        if (remaining_track && session_.timeline_.clipCount(*remaining_track) > 0) {
            const auto next_index = std::min(
                location->clip_index,
                session_.timeline_.clipCount(*remaining_track) - 1);
            selectClip(timeline::ClipLocation{*remaining_track, next_index});
        } else {
            for (std::size_t track = 0; track < session_.timeline_.trackCount(); ++track) {
                if (session_.timeline_.clipCount(track) > 0) {
                    selectClip(timeline::ClipLocation{track, 0});
                    break;
                }
            }
        }
    }
    auto output = result(EditStatus::Applied);
    output.affected_track_ids = {track_id};
    output.affected_clip_ids = {command.clip_id};
    output.invalidate_playback = true;
    return output;
}

TimelineEditResult TimelineCommandService::execute(const AddMediaClipCommand& command) {
    const auto track = session_.timeline_.locateTrack(command.track_id);
    if (!track) return result(EditStatus::Rejected, EditReason::InvalidTarget);
    const auto media_index = session_.media_library_.indexForPath(command.source_path);
    if (media_index == session_.media_library_.size()) {
        return result(EditStatus::Rejected, EditReason::MediaNotFound);
    }
    const auto& media_item = session_.media_library_.items()[media_index];
    if (media_item.offline) return result(EditStatus::Rejected, EditReason::OfflineMedia);
    std::int64_t start = command.timeline_start_frame.value_or(0);
    if (!command.timeline_start_frame) {
        for (const auto& clip : session_.timeline_.tracks()[*track].clips) {
            if (clip.timeline_start_frame <= std::numeric_limits<std::int64_t>::max() -
                    clip.timeline_duration_frames) {
                start = std::max(start, clip.timeline_start_frame + clip.timeline_duration_frames);
            }
        }
    }
    const auto next_clip_id = session_.timeline_.snapshot().next_clip_id;
    auto before = session_.captureEditState();
    const auto added = session_.timeline_.addClip(*track, media_item.metadata, start);
    if (added != timeline::AddClipResult::Added) {
        return result(EditStatus::Rejected, reasonForAdd(added));
    }
    recordSuccessfulEdit(std::move(before));
    const auto location = session_.timeline_.locateClip(next_clip_id);
    if (!location) return result(EditStatus::Rejected, EditReason::InvalidTarget);
    selectClip(*location);
    session_.selection_.selected_source_path = media_item.metadata.source_path;
    session_.playhead_frame_ = 0;
    session_.preserved_playhead_frame_.reset();
    auto output = result(EditStatus::Applied);
    output.affected_track_ids = {command.track_id};
    output.affected_clip_ids = {next_clip_id};
    output.invalidate_playback = true;
    return output;
}

TimelineEditResult TimelineCommandService::execute(const AddTextClipCommand& command) {
    const auto track = session_.timeline_.locateTrack(command.track_id);
    if (!track) return result(EditStatus::Rejected, EditReason::InvalidTarget);
    const auto next_clip_id = session_.timeline_.snapshot().next_clip_id;
    auto before = session_.captureEditState();
    const auto added = session_.timeline_.addTextClip(
        *track, command.timeline_start_frame, command.duration_frames, command.frame_rate);
    if (added != timeline::AddClipResult::Added) {
        return result(EditStatus::Rejected, reasonForAdd(added));
    }
    recordSuccessfulEdit(std::move(before));
    const auto location = session_.timeline_.locateClip(next_clip_id);
    if (!location) return result(EditStatus::Rejected, EditReason::InvalidTarget);
    selectClip(*location);
    session_.playhead_frame_ = 0;
    session_.preserved_playhead_frame_.reset();
    auto output = result(EditStatus::Applied);
    output.affected_track_ids = {command.track_id};
    output.affected_clip_ids = {next_clip_id};
    output.invalidate_playback = true;
    return output;
}

TimelineEditResult TimelineCommandService::execute(const AddTransitionCommand& command) {
    const auto track = session_.timeline_.locateTrack(command.track_id);
    const auto from = session_.timeline_.locateClip(command.from_clip_id);
    const auto to = session_.timeline_.locateClip(command.to_clip_id);
    if (!track || !from || !to || from->track_index != *track || to->track_index != *track) {
        return result(EditStatus::Rejected, EditReason::InvalidTarget);
    }
    auto before = session_.captureEditState();
    const auto added = session_.timeline_.addTransition(
        *track, from->clip_index, to->clip_index, command.kind, command.duration_frames);
    if (added == timeline::TransitionMutationResult::NoChange) return result(EditStatus::NoChange);
    if (added != timeline::TransitionMutationResult::Added) {
        return result(EditStatus::Rejected, reasonForTransition(added));
    }
    recordSuccessfulEdit(std::move(before));
    selectTransition(command.track_id, command.from_clip_id, command.to_clip_id);
    auto output = result(EditStatus::Applied);
    output.affected_track_ids = {command.track_id};
    output.affected_clip_ids = {command.from_clip_id, command.to_clip_id};
    output.invalidate_playback = true;
    return output;
}

TimelineEditResult TimelineCommandService::execute(const UpdateTransitionCommand& command) {
    const auto track = session_.timeline_.locateTrack(command.track_id);
    const auto from = session_.timeline_.locateClip(command.from_clip_id);
    const auto to = session_.timeline_.locateClip(command.to_clip_id);
    if (!track || !from || !to || from->track_index != *track || to->track_index != *track) {
        return result(EditStatus::Rejected, EditReason::InvalidTarget);
    }
    auto before = session_.captureEditState();
    const auto updated = session_.timeline_.updateTransition(
        *track, from->clip_index, to->clip_index, command.kind, command.duration_frames);
    if (updated == timeline::TransitionMutationResult::NoChange) return result(EditStatus::NoChange);
    if (updated != timeline::TransitionMutationResult::Updated) {
        return result(EditStatus::Rejected, reasonForTransition(updated));
    }
    recordSuccessfulEdit(std::move(before));
    selectTransition(command.track_id, command.from_clip_id, command.to_clip_id);
    auto output = result(EditStatus::Applied);
    output.affected_track_ids = {command.track_id};
    output.affected_clip_ids = {command.from_clip_id, command.to_clip_id};
    output.invalidate_playback = true;
    return output;
}

TimelineEditResult TimelineCommandService::execute(const RemoveTransitionCommand& command) {
    const auto track = session_.timeline_.locateTrack(command.track_id);
    const auto from = session_.timeline_.locateClip(command.from_clip_id);
    const auto to = session_.timeline_.locateClip(command.to_clip_id);
    if (!track || !from || !to || from->track_index != *track || to->track_index != *track) {
        return result(EditStatus::Rejected, EditReason::InvalidTarget);
    }
    auto before = session_.captureEditState();
    const auto removed = session_.timeline_.removeTransition(
        *track, from->clip_index, to->clip_index);
    if (removed == timeline::TransitionMutationResult::NotFound) {
        return result(EditStatus::NoChange, EditReason::TransitionNotFound);
    }
    if (removed != timeline::TransitionMutationResult::Removed) {
        return result(EditStatus::Rejected, reasonForTransition(removed));
    }
    recordSuccessfulEdit(std::move(before));
    if (session_.selection_.active_transition == timeline::TransitionSelection{
            command.track_id, command.from_clip_id, command.to_clip_id}) {
        session_.selection_.active_transition.reset();
    }
    auto output = result(EditStatus::Applied);
    output.affected_track_ids = {command.track_id};
    output.affected_clip_ids = {command.from_clip_id, command.to_clip_id};
    output.invalidate_playback = true;
    return output;
}

TimelineEditResult TimelineCommandService::undo() {
    auto state = session_.history_.undo(session_.captureEditState());
    if (!state) return result(EditStatus::UndoUnavailable);
    session_.restoreEditState(std::move(*state));
    auto output = result(EditStatus::Applied);
    for (const auto& track : session_.timeline_.tracks()) {
        output.affected_track_ids.push_back(track.track_id);
        for (const auto& clip : track.clips) output.affected_clip_ids.push_back(clip.clip_id);
    }
    output.invalidate_playback = true;
    return output;
}

TimelineEditResult TimelineCommandService::redo() {
    auto state = session_.history_.redo(session_.captureEditState());
    if (!state) return result(EditStatus::RedoUnavailable);
    session_.restoreEditState(std::move(*state));
    auto output = result(EditStatus::Applied);
    for (const auto& track : session_.timeline_.tracks()) {
        output.affected_track_ids.push_back(track.track_id);
        for (const auto& clip : track.clips) output.affected_clip_ids.push_back(clip.clip_id);
    }
    output.invalidate_playback = true;
    return output;
}

void TimelineCommandService::recordLegacyEdit(timeline::EditState state) {
    session_.history_.recordBeforeEdit(std::move(state));
}

void TimelineCommandService::clearHistory() noexcept {
    session_.history_.clear();
}

bool TimelineCommandService::canUndo() const noexcept {
    return session_.history_.canUndo();
}

bool TimelineCommandService::canRedo() const noexcept {
    return session_.history_.canRedo();
}

std::size_t TimelineCommandService::undoCount() const noexcept {
    return session_.history_.undoCount();
}

} // namespace application
