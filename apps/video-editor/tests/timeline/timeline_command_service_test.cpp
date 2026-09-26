#include "application/editor_session.h"
#include "application/media_controller.h"
#include "application/timeline_command_service.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool hasUniqueStableIds(const timeline::TimelineModel& model) {
    std::unordered_set<timeline::TrackId> track_ids;
    std::unordered_set<timeline::ClipId> clip_ids;
    for (const auto& track : model.tracks()) {
        if (track.track_id == 0 || !track_ids.insert(track.track_id).second) return false;
        for (const auto& clip : track.clips) {
            if (clip.clip_id == 0 || clip.track_id != track.track_id ||
                !clip_ids.insert(clip.clip_id).second) {
                return false;
            }
        }
    }
    return true;
}

bool hasValidSelection(const application::EditorSession& session) {
    const auto& model = session.timeline();
    const auto& selection = session.selection();
    if (selection.active_track_id.has_value() &&
        !model.locateTrack(*selection.active_track_id).has_value()) {
        return false;
    }
    if (selection.active_clip_id.has_value()) {
        const auto location = model.locateClip(*selection.active_clip_id);
        if (!location.has_value() || !selection.active_track_id.has_value() ||
            model.tracks()[location->track_index].track_id != *selection.active_track_id) {
            return false;
        }
    }
    if (selection.active_transition.has_value()) {
        const auto& active = *selection.active_transition;
        const auto track = model.locateTrack(active.track_id);
        const auto from = model.locateClip(active.from_clip_id);
        const auto to = model.locateClip(active.to_clip_id);
        if (!track.has_value() || !from.has_value() || !to.has_value() ||
            from->track_index != *track || to->track_index != *track ||
            from->clip_index + 1 != to->clip_index ||
            model.transitionBetween(*track, from->clip_index, to->clip_index) == nullptr) {
            return false;
        }
    }
    return true;
}

void requireSessionInvariants(
    const application::EditorSession& session,
    const std::string& message) {
    require(hasUniqueStableIds(session.timeline()) && hasValidSelection(session), message);
}

media::VideoMetadata makeMetadata(const std::filesystem::path& path) {
    media::VideoMetadata metadata;
    metadata.kind = media::MediaKind::Video;
    metadata.source_path = path;
    metadata.display_name = path.filename().string();
    metadata.width = 1920;
    metadata.height = 1080;
    metadata.frame_rate = 30.0;
    metadata.duration_seconds = 4.0;
    metadata.frame_count = 120;
    return metadata;
}

void addMedia(application::EditorSession& session, const std::filesystem::path& path) {
    application::MediaController controller(session);
    auto metadata = makeMetadata(path);
    const auto result = controller.commitImported({
        metadata, {}, metadata.display_name, "Unsorted", false});
    require(result.changed(), "Could not add test media to the editor session.");
}

void run() {
    application::EditorSession session;
    application::TimelineCommandService service(session);
    auto& model = session.legacyTimelineForUi();
    const auto first_track_id = model.tracks().front().track_id;
    require(model.addTrack("Video 2") == timeline::AddTrackResult::Added,
            "Could not create the second test track.");
    const auto second_track_id = model.tracks().front().track_id;

    const auto first_path = std::filesystem::temp_directory_path() / "service-first.mkv";
    const auto second_path = std::filesystem::temp_directory_path() / "service-second.mkv";
    addMedia(session, first_path);
    addMedia(session, second_path);

    const auto added = service.execute(application::AddMediaClipCommand{
        first_path, first_track_id, 0});
    require(added.changed() && added.invalidate_playback,
            "Adding media did not produce a playback-invalidating edit.");
    require(added.affected_clip_ids.size() == 1 && added.affected_clip_ids[0] == 1,
            "Adding media did not return its stable clip ID.");
    require(session.selection().active_clip_id == 1 && service.undoCount() == 1,
            "Adding media did not select the new clip and create one history entry.");
    requireSessionInvariants(session, "Adding media broke stable IDs or selection validity.");

    const auto no_op_move = service.execute(application::MoveClipCommand{
        1, first_track_id, 0});
    require(no_op_move.status == application::EditStatus::NoChange &&
                service.undoCount() == 1,
            "A no-op move created an undo entry.");

    const auto stale_move = service.execute(application::MoveClipCommand{
        999, first_track_id, 10});
    require(stale_move.status == application::EditStatus::Rejected &&
                stale_move.reason == application::EditReason::InvalidTarget &&
                service.undoCount() == 1,
            "A command with a missing clip changed state or history.");

    const auto invalid_position = service.execute(application::AddMediaClipCommand{
        second_path, first_track_id, -1});
    require(invalid_position.status == application::EditStatus::Rejected &&
                invalid_position.reason == application::EditReason::InvalidPosition &&
                service.undoCount() == 1,
            "An invalid media position changed state or history.");

    const auto invalid_move_position = service.execute(application::MoveClipCommand{
        1, first_track_id, -1});
    require(invalid_move_position.status == application::EditStatus::Rejected &&
                invalid_move_position.reason == application::EditReason::InvalidPosition &&
                service.undoCount() == 1,
            "A move to an invalid position changed state or history.");

    const auto missing_reorder = service.execute(application::ReorderClipCommand{999, 0});
    const auto missing_split = service.execute(application::SplitClipCommand{999, 20});
    const auto missing_edge_trim = service.execute(application::TrimClipEdgeCommand{
        999, timeline::ClipEdge::Right, 20,
        timeline::ClipEdgeEditMode::Individual, 0, 0});
    const auto missing_range_trim = service.execute(
        application::TrimClipRangeCommand{999, 0, 20});
    const auto missing_delete = service.execute(application::DeleteClipCommand{999});
    require(missing_reorder.status == application::EditStatus::Rejected &&
                missing_split.status == application::EditStatus::Rejected &&
                missing_edge_trim.status == application::EditStatus::Rejected &&
                missing_range_trim.status == application::EditStatus::Rejected &&
                missing_delete.status == application::EditStatus::Rejected &&
                service.undoCount() == 1,
            "Commands with missing clip IDs changed state or history.");

    const auto missing_media = service.execute(application::AddMediaClipCommand{
        std::filesystem::temp_directory_path() / "not-imported.mkv", first_track_id, 120});
    const auto missing_track_media = service.execute(application::AddMediaClipCommand{
        second_path, 999, 120});
    const auto invalid_text = service.execute(application::AddTextClipCommand{
        first_track_id, -1, 30, 30.0});
    require(missing_media.reason == application::EditReason::MediaNotFound &&
                missing_track_media.reason == application::EditReason::InvalidTarget &&
                invalid_text.reason == application::EditReason::InvalidPosition &&
                service.undoCount() == 1,
            "Rejected media or text additions changed state or history.");

    auto offline_metadata = makeMetadata(
        std::filesystem::temp_directory_path() / "offline.mkv");
    application::MediaController media_controller(session);
    const auto offline_added = media_controller.commitImported({
        offline_metadata, {}, offline_metadata.display_name, "Unsorted", true});
    require(offline_added.changed(), "Could not add offline test media.");
    const auto offline_add = service.execute(application::AddMediaClipCommand{
        offline_metadata.source_path, first_track_id, 120});
    require(offline_add.reason == application::EditReason::OfflineMedia &&
                service.undoCount() == 1,
            "Offline media changed the timeline or history.");

    const auto invalid_split = service.execute(application::SplitClipCommand{1, 0});
    require(invalid_split.status == application::EditStatus::Rejected &&
                invalid_split.reason == application::EditReason::InvalidBoundary &&
                service.undoCount() == 1,
            "A split at a clip boundary changed state or history.");

    const auto overlap = service.execute(application::AddMediaClipCommand{
        second_path, first_track_id, 50});
    require(overlap.status == application::EditStatus::Rejected &&
                overlap.reason == application::EditReason::Overlap &&
                service.undoCount() == 1,
            "An overlapping media add changed state or history.");

    const auto moved = service.execute(application::MoveClipCommand{
        1, second_track_id, 0});
    require(moved.changed() && session.selection().active_track_id == second_track_id &&
                session.selection().active_clip_id == 1 && service.undoCount() == 2,
            "Moving a clip did not resolve the target track by ID.");
    requireSessionInvariants(session, "Moving a clip broke its parent track ID or selection.");

    const auto split = service.execute(application::SplitClipCommand{1, 60});
    require(split.changed() && split.affected_clip_ids.size() == 2 &&
                session.selection().active_clip_id == 2 && session.playheadFrame() == 0 &&
                service.undoCount() == 3,
            "Splitting did not return and select the new right-hand clip.");
    requireSessionInvariants(session, "Splitting a clip produced duplicate IDs or invalid selection.");
    timeline::TimelineModel restored_model;
    restored_model.restore(model.snapshot());
    require(hasUniqueStableIds(restored_model),
            "Restoring a timeline snapshot did not preserve unique stable IDs.");

    const auto missing_transition = service.execute(application::AddTransitionCommand{
        second_track_id, 1, 999, timeline::TransitionKind::CrossDissolve, 15});
    const auto missing_transition_update = service.execute(application::UpdateTransitionCommand{
        second_track_id, 1, 999, timeline::TransitionKind::FadeToBlack, 15});
    const auto missing_transition_remove = service.execute(application::RemoveTransitionCommand{
        second_track_id, 1, 999});
    require(missing_transition.reason == application::EditReason::InvalidTarget &&
                missing_transition_update.reason == application::EditReason::InvalidTarget &&
                missing_transition_remove.reason == application::EditReason::InvalidTarget &&
                service.undoCount() == 3,
            "Transition commands with missing clip IDs changed state or history.");

    const auto transition = service.execute(application::AddTransitionCommand{
        second_track_id, 1, 2, timeline::TransitionKind::CrossDissolve, 15});
    require(transition.changed() &&
                session.selection().active_transition == timeline::TransitionSelection{
                    second_track_id, 1, 2} && service.undoCount() == 4,
            "Adding a transition did not select it and record one edit.");
    requireSessionInvariants(session, "Adding a transition left an invalid stable selection.");
    const auto duplicate_transition = service.execute(application::AddTransitionCommand{
        second_track_id, 1, 2, timeline::TransitionKind::CrossDissolve, 15});
    require(duplicate_transition.status == application::EditStatus::NoChange &&
                service.undoCount() == 4,
            "Adding an existing transition created another history entry.");

    const auto updated_transition = service.execute(application::UpdateTransitionCommand{
        second_track_id, 1, 2, timeline::TransitionKind::FadeToBlack, 20});
    require(updated_transition.changed() && service.undoCount() == 5,
            "Updating a transition did not create exactly one history entry.");
    const auto unchanged_transition = service.execute(application::UpdateTransitionCommand{
        second_track_id, 1, 2, timeline::TransitionKind::FadeToBlack, 20});
    require(unchanged_transition.status == application::EditStatus::NoChange &&
                service.undoCount() == 5,
            "An unchanged transition update created history.");
    const auto removed_transition = service.execute(application::RemoveTransitionCommand{
        second_track_id, 1, 2});
    require(removed_transition.changed() && !session.selection().active_transition &&
                service.undoCount() == 6,
            "Removing a transition did not clear its selection or record history.");
    const auto already_removed_transition = service.execute(
        application::RemoveTransitionCommand{second_track_id, 1, 2});
    require(already_removed_transition.status == application::EditStatus::NoChange &&
                already_removed_transition.reason == application::EditReason::TransitionNotFound &&
                service.undoCount() == 6,
            "Removing an absent transition created a history entry.");

    const auto trimmed = service.execute(application::TrimClipRangeCommand{2, 70, 50});
    require(trimmed.changed() && service.undoCount() == 7,
            "Range trimming did not create one history entry.");
    const auto invalid_range_trim = service.execute(
        application::TrimClipRangeCommand{2, -1, 50});
    require(invalid_range_trim.reason == application::EditReason::InvalidRange &&
                service.undoCount() == 7,
            "An invalid source range changed state or history.");
    session.setPlayheadFrame(75);
    const auto edge_trimmed = service.execute(application::TrimClipEdgeCommand{
        2,
        timeline::ClipEdge::Right,
        100,
        timeline::ClipEdgeEditMode::Individual,
        75,
        5});
    require(edge_trimmed.changed() && edge_trimmed.playhead_frame == 15 &&
                service.undoCount() == 8,
            "Edge trimming did not return the adjusted playhead and history entry.");

    const auto deleted = service.execute(application::DeleteClipCommand{2});
    require(deleted.changed() && session.selection().active_clip_id == 1 &&
                service.undoCount() == 9,
            "Deleting a clip did not select a surviving clip and record history.");
    const auto undone = service.undo();
    require(undone.changed() && session.selection().active_clip_id == 2 &&
                model.locateClip(2).has_value() && service.canRedo(),
            "Undo did not restore the clip and its selection.");
    requireSessionInvariants(session, "Undo restored an invalid selection or duplicate IDs.");
    const auto redone = service.redo();
    require(redone.changed() && !model.locateClip(2).has_value() && service.canUndo(),
            "Redo did not reapply the clip deletion.");
    requireSessionInvariants(session, "Redo restored an invalid selection or duplicate IDs.");

    application::EditorSession reorder_session;
    application::TimelineCommandService reorder_service(reorder_session);
    auto& reorder_model = reorder_session.legacyTimelineForUi();
    const auto reorder_track_id = reorder_model.tracks().front().track_id;
    addMedia(reorder_session, first_path);
    addMedia(reorder_session, second_path);
    const auto second_media_added = reorder_service.execute(application::AddMediaClipCommand{
        second_path, reorder_track_id, 0});
    const auto first_media_added = reorder_service.execute(application::AddMediaClipCommand{
        first_path, reorder_track_id, 120});
    require(second_media_added.changed() && first_media_added.changed(),
            "Could not prepare the single-track reorder case.");
    const auto reordered = reorder_service.execute(application::ReorderClipCommand{
        first_media_added.affected_clip_ids.front(), 0});
    require(reordered.changed() &&
                reorder_model.tracks().front().clips.front().clip_id ==
                    first_media_added.affected_clip_ids.front(),
            "The legacy single-track reorder command did not reorder by clip identity.");
    requireSessionInvariants(reorder_session,
            "Reordering clips changed stable IDs or invalidated the selection.");
    const auto reorder_undo = reorder_service.undo();
    require(reorder_undo.changed() &&
                reorder_model.tracks().front().clips.front().clip_id ==
                    second_media_added.affected_clip_ids.front(),
            "Undo did not restore a single-track reorder.");
    requireSessionInvariants(reorder_session,
            "Undoing a reorder changed stable IDs or invalidated the selection.");

    const auto text_added = service.execute(application::AddTextClipCommand{
        first_track_id, 240, 60, 30.0});
    require(text_added.changed() && text_added.affected_clip_ids.size() == 1 &&
                session.selection().active_clip_id == text_added.affected_clip_ids[0],
            "Adding a text clip did not return and select its new ID.");
    const auto overlapping_text = service.execute(application::AddTextClipCommand{
        first_track_id, 270, 60, 30.0});
    require(overlapping_text.reason == application::EditReason::Overlap &&
                service.undoCount() == 10,
            "An overlapping text addition changed state or history.");

}

void runInspectorAndTrackCommands() {
    application::EditorSession session;
    application::TimelineCommandService service(session);
    const auto track_id = session.timeline().tracks().front().track_id;
    const auto source = std::filesystem::temp_directory_path() / "command-coverage.mkv";
    addMedia(session, source);
    const auto added_clip = service.execute(application::AddMediaClipCommand{
        source, track_id, 0});
    require(added_clip.changed(), "Could not prepare inspector command coverage.");
    const auto clip_id = added_clip.affected_clip_ids.front();

    const auto empty_name = service.execute(application::AddTrackCommand{"  "});
    require(empty_name.status == application::EditStatus::Rejected &&
                empty_name.reason == application::EditReason::InvalidName &&
                service.undoCount() == 1,
            "An invalid track name changed history.");
    const auto added_track = service.execute(application::AddTrackCommand{"Overlay"});
    require(added_track.changed() && added_track.affected_track_ids.size() == 1 &&
                session.selection().active_track_id == added_track.affected_track_ids.front() &&
                service.undoCount() == 2,
            "Adding a track did not return and select its stable ID.");
    const auto overlay_id = added_track.affected_track_ids.front();
    const auto renamed = service.execute(application::RenameTrackCommand{overlay_id, "Titles"});
    const auto rename_noop = service.execute(application::RenameTrackCommand{overlay_id, "Titles"});
    const auto missing_rename = service.execute(application::RenameTrackCommand{999, "Missing"});
    require(renamed.changed() && rename_noop.status == application::EditStatus::NoChange &&
                missing_rename.reason == application::EditReason::InvalidTarget &&
                service.undoCount() == 3,
            "Track rename did not distinguish changes, no-ops, and missing IDs.");

    const auto moved_track = service.execute(application::MoveTrackCommand{track_id, 0});
    require(moved_track.changed() && session.timeline().tracks().front().track_id == track_id &&
                service.undoCount() == 4,
            "Track movement did not resolve the source by stable ID.");
    requireSessionInvariants(session,
            "Reordering a track changed stable IDs or invalidated its clip selection.");
    const auto missing_track_move = service.execute(application::MoveTrackCommand{999, 0});
    require(missing_track_move.reason == application::EditReason::InvalidTarget &&
                service.undoCount() == 4,
            "Moving a missing track changed history.");
    const auto invalid_track_position = service.execute(
        application::MoveTrackCommand{track_id, 99});
    const auto missing_track_remove = service.execute(
        application::RemoveTrackCommand{999});
    require(invalid_track_position.reason == application::EditReason::InvalidTarget &&
                missing_track_remove.reason == application::EditReason::InvalidTarget &&
                service.undoCount() == 4,
            "Invalid track IDs or positions changed history.");

    const auto nonempty_remove = service.execute(application::RemoveTrackCommand{track_id});
    const auto removed_track = service.execute(application::RemoveTrackCommand{overlay_id});
    require(nonempty_remove.reason == application::EditReason::TrackNotEmpty &&
                removed_track.changed() && service.undoCount() == 5,
            "Track removal did not reject occupied tracks or remove an empty one.");

    const auto batch = service.beginEditBatch();
    const auto audio_first = service.execute(application::SetClipAudioCommand{clip_id, 1.4, false});
    const auto audio_second = service.execute(application::SetClipAudioCommand{clip_id, 0.75, true});
    require(audio_first.changed() && audio_second.changed() &&
                service.undoCount() == 5 && !audio_second.invalidate_playback,
            "Audio changes inside a batch were not applied live or were recorded individually.");
    const auto batch_result = service.finishEditBatch(batch);
    require(batch_result.changed() && service.undoCount() == 6,
            "A changed slider batch did not create exactly one history entry.");
    const auto audio_undo = service.undo();
    require(audio_undo.changed() &&
                session.timeline().tracks()[*session.timeline().locateTrack(track_id)]
                    .clips.front().audio_gain == 1.0 &&
                !session.timeline().tracks()[*session.timeline().locateTrack(track_id)]
                    .clips.front().audio_muted,
            "Undo did not restore the audio state before the batch.");
    const auto no_op_batch = service.beginEditBatch();
    const auto no_op_batch_result = service.finishEditBatch(no_op_batch);
    require(no_op_batch_result.status == application::EditStatus::NoChange &&
                service.undoCount() == 5,
            "An unchanged edit batch created history.");

    const auto invalid_audio = service.execute(application::SetClipAudioCommand{clip_id, 5.0, false});
    const auto missing_audio = service.execute(application::SetClipAudioCommand{999, 1.0, false});
    const auto track_audio = service.execute(application::SetTrackAudioCommand{track_id, 0.5, true});
    require(invalid_audio.reason == application::EditReason::InvalidValue &&
                missing_audio.reason == application::EditReason::InvalidTarget &&
                track_audio.changed() && service.undoCount() == 6,
            "Audio commands did not validate values/IDs or record a valid track edit.");

    const auto text_added = service.execute(application::AddTextClipCommand{
        track_id, 120, 60, 30.0});
    require(text_added.changed(), "Could not prepare a text clip for inspector commands.");
    const auto text_id = text_added.affected_clip_ids.front();
    timeline::TextStyle text_style;
    text_style.content = "Updated title";
    const auto text_changed = service.execute(application::SetClipTextCommand{text_id, text_style});
    const auto text_noop = service.execute(application::SetClipTextCommand{text_id, text_style});
    auto invalid_text_style = text_style;
    invalid_text_style.font_size_pixels = -1.0;
    const auto invalid_text = service.execute(
        application::SetClipTextCommand{text_id, invalid_text_style});
    require(text_changed.changed() && text_changed.invalidate_playback &&
                text_noop.status == application::EditStatus::NoChange &&
                invalid_text.reason == application::EditReason::InvalidValue,
            "Text-style commands did not report effective and no-op edits correctly.");

    const auto base_transform = service.execute(application::SetTransformPropertyCommand{
        clip_id, timeline::TransformProperty::PositionX, 10, 0.25});
    const auto added_key = service.execute(application::ToggleTransformKeyframeCommand{
        clip_id, timeline::TransformProperty::PositionX, 10});
    const auto changed_key = service.execute(application::SetTransformPropertyCommand{
        clip_id, timeline::TransformProperty::PositionX, 10, 0.8});
    const auto removed_key = service.execute(application::ToggleTransformKeyframeCommand{
        clip_id, timeline::TransformProperty::PositionX, 10});
    require(base_transform.changed() && added_key.changed() && changed_key.changed() &&
                removed_key.changed() &&
                session.timeline().tracks()[*session.timeline().locateTrack(track_id)]
                    .clips.front().keyframes.position_x.empty(),
            "Transform property and keyframe commands did not apply through the service.");
    const auto invalid_transform = service.execute(application::SetTransformPropertyCommand{
        999, timeline::TransformProperty::Scale, 0, 1.0});
    const auto invalid_transform_value = service.execute(
        application::SetTransformPropertyCommand{
            clip_id, timeline::TransformProperty::Scale, 0, 0.0});
    const auto invalid_transform_frame = service.execute(
        application::SetTransformPropertyCommand{
            clip_id, timeline::TransformProperty::PositionX, -1, 2.0});
    require(invalid_transform.reason == application::EditReason::InvalidTarget &&
                invalid_transform_value.reason == application::EditReason::InvalidValue &&
                invalid_transform_frame.reason == application::EditReason::InvalidPosition,
            "Transform commands did not reject missing IDs, invalid values, or frames.");

    const auto count_before_clear = service.undoCount();
    const auto cleared = service.execute(application::ClearTimelineCommand{});
    const auto clear_noop = service.execute(application::ClearTimelineCommand{});
    require(cleared.changed() && cleared.invalidate_playback &&
                session.timeline().clipCount() == 0 &&
                clear_noop.status == application::EditStatus::NoChange &&
                service.undoCount() == count_before_clear + 1,
            "Clearing the timeline did not record one effective change.");
    require(service.undo().changed() && session.timeline().clipCount() == 2,
            "Undo did not restore the clips cleared by the typed command.");
}

void runReconnectTimingCommand() {
    application::EditorSession session;
    application::TimelineCommandService service(session);
    timeline::TimelineModel::Snapshot snapshot;
    snapshot.frame_rate = {30, 1};
    timeline::TimelineTrack track;
    track.track_id = 1;
    track.name = "V1";
    timeline::TimelineClip clip;
    clip.timeline_start_frame = 0;
    clip.source_path = std::filesystem::temp_directory_path() / "pending.mkv";
    clip.display_name = "Offline source";
    clip.frame_rate = 24.0;
    clip.frame_count = 48;
    clip.timeline_duration_frames = 24;
    clip.source_duration_frames = 24;
    clip.source_duration_migration_pending = true;
    clip.clip_id = 1;
    clip.track_id = 1;
    clip.kind = timeline::ClipKind::Video;
    track.clips.push_back(clip);
    snapshot.tracks.push_back(track);
    snapshot.next_track_id = 2;
    snapshot.next_clip_id = 2;
    session.legacyTimelineForUi().restore(snapshot);

    media::VideoMetadata metadata;
    metadata.source_path = clip.source_path;
    metadata.display_name = "Restored source";
    metadata.kind = media::MediaKind::Video;
    metadata.duration_seconds = 2.0;
    metadata.frame_rate = 24.0;
    metadata.frame_count = 48;
    require(service.migratePendingMediaTiming(clip.source_path, metadata) ==
                timeline::PendingMediaTimingMigrationResult::Migrated &&
                service.undoCount() == 1 &&
                session.timeline().tracks().front().clips.front().timeline_duration_frames == 30 &&
                !session.timeline().tracks().front().clips.front()
                     .source_duration_migration_pending,
            "The reconnect timing command did not migrate and record an undo state.");
    require(service.undo().changed() &&
                session.timeline().tracks().front().clips.front().timeline_duration_frames == 24 &&
                session.timeline().tracks().front().clips.front()
                    .source_duration_migration_pending,
            "Undo did not restore the pending source timing state.");
    require(service.redo().changed() &&
                session.timeline().tracks().front().clips.front().timeline_duration_frames == 30 &&
                !session.timeline().tracks().front().clips.front()
                     .source_duration_migration_pending,
            "Redo did not reapply the source timing conversion.");
}

} // namespace

int main() {
    try {
        run();
        runInspectorAndTrackCommands();
        runReconnectTimingCommand();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
