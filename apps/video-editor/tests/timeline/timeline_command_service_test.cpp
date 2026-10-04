#include "application/editor_session.h"
#include "application/media_controller.h"
#include "application/timeline_command_service.h"

#include <creative_suite/effects/effects.h>

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

void addAudioMedia(application::EditorSession& session,
                   const std::filesystem::path& path) {
    application::MediaController controller(session);
    media::VideoMetadata metadata;
    metadata.kind = media::MediaKind::Audio;
    metadata.source_path = path;
    metadata.display_name = path.filename().string();
    metadata.duration_seconds = 2.0;
    metadata.audio = media::AudioMetadata{"pcm_s16le", 48000, 2, 2.0};
    const auto result = controller.commitImported({
        metadata, {}, metadata.display_name, "Unsorted", false});
    require(result.changed(), "Could not add test audio media to the editor session.");
}

void addVideoWithAudioMedia(
    application::EditorSession& session,
    const std::filesystem::path& path,
    double duration_seconds = 4.0) {
    application::MediaController controller(session);
    auto metadata = makeMetadata(path);
    metadata.duration_seconds = duration_seconds;
    metadata.frame_count = static_cast<std::int64_t>(duration_seconds * 30.0);
    metadata.audio = media::AudioMetadata{"aac", 48000, 2, duration_seconds};
    const auto result = controller.commitImported({
        metadata, {}, metadata.display_name, "Unsorted", false});
    require(result.changed(), "Could not add test video-with-audio media.");
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
    const auto undo_transition_remove = service.undo();
    const auto* restored_transition = model.transitionBetween(0, 0, 1);
    require(undo_transition_remove.changed() && service.undoCount() == 5 &&
                restored_transition != nullptr &&
                restored_transition->kind == timeline::TransitionKind::FadeToBlack &&
                service.canRedo(),
            ("Undo did not restore the removed Timeline transition: changed=" +
             std::to_string(undo_transition_remove.changed()) + ", undoCount=" +
             std::to_string(service.undoCount()) + ", restored=" +
             std::to_string(restored_transition != nullptr) + ", kind=" +
             (restored_transition == nullptr
                  ? std::string("missing")
                  : std::to_string(static_cast<int>(restored_transition->kind)))));
    const auto redo_transition_remove = service.redo();
    require(redo_transition_remove.changed() && service.undoCount() == 6 &&
                model.transitionBetween(0, 0, 1) == nullptr,
            "Redo did not remove the restored Timeline transition.");

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

void runVisualEffectCommands() {
    application::EditorSession session;
    application::TimelineCommandService service(session);
    const auto track_id = session.timeline().tracks().front().track_id;
    const auto source_path =
        std::filesystem::temp_directory_path() / "service-effects-video.mkv";
    addMedia(session, source_path);
    const auto insertion = service.execute(application::AddMediaClipCommand{
        source_path, track_id, 0});
    require(insertion.changed() && insertion.selection.active_clip_id.has_value(),
            "Could not create a visual clip for effect command coverage.");
    const auto clip_id = *insertion.selection.active_clip_id;

    auto grayscale = creative_suite::effects::makeDefaultInstance("video.grayscale");
    const auto first_edit = service.execute(application::SetClipEffectsCommand{
        clip_id, {grayscale}});
    require(first_edit.changed() && first_edit.invalidate_playback &&
                service.undoCount() == 2,
            "Adding a visual filter did not create a playback-invalidating history edit.");
    auto location = session.timeline().locateClip(clip_id);
    require(location.has_value() &&
                session.timeline().tracks()[location->track_index]
                    .clips[location->clip_index].effects ==
                    std::vector<creative_suite::effects::EffectInstance>{grayscale},
            "The effect command did not update the visual clip stack.");

    auto brightness = creative_suite::effects::makeDefaultInstance("video.brightness");
    const auto reordered = service.execute(application::SetClipEffectsCommand{
        clip_id, {brightness, grayscale}});
    require(reordered.changed(), "An ordered filter stack could not be changed.");
    require(service.undo().changed(), "Effect stack editing could not be undone.");
    location = session.timeline().locateClip(clip_id);
    require(location.has_value() &&
                session.timeline().tracks()[location->track_index]
                    .clips[location->clip_index].effects ==
                    std::vector<creative_suite::effects::EffectInstance>{grayscale},
            "Undo did not restore the previous filter stack order.");
    require(service.redo().changed(), "Effect stack editing could not be redone.");
    location = session.timeline().locateClip(clip_id);
    require(location.has_value() &&
                session.timeline().tracks()[location->track_index]
                    .clips[location->clip_index].effects ==
                    std::vector<creative_suite::effects::EffectInstance>{
                        brightness, grayscale},
            "Redo did not restore the ordered filter stack.");

    const auto history_before_invalid_edit = service.undoCount();
    auto invalid_brightness = brightness;
    invalid_brightness.parameters.front().value = 201.0;
    const auto invalid_edit = service.execute(application::SetClipEffectsCommand{
        clip_id, {invalid_brightness}});
    require(!invalid_edit.changed() && service.undoCount() == history_before_invalid_edit,
            "An invalid effect value changed the clip or its history.");
    require(service.execute(application::SetClipEffectsCommand{clip_id, {}}).changed(),
            "Removing a filter stack did not produce a Timeline edit.");
    require(service.undo().changed(), "Filter removal could not be undone.");
    location = session.timeline().locateClip(clip_id);
    require(location.has_value() &&
                session.timeline().tracks()[location->track_index]
                    .clips[location->clip_index].effects.size() == 2,
            "Undo did not restore removed filters.");
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

void runAutomaticAudioTrackCommand() {
    application::EditorSession session;
    application::TimelineCommandService service(session);
    const auto video_track_id = session.timeline().tracks().front().track_id;
    const auto first_path = std::filesystem::temp_directory_path() /
        "service-independent-audio-first.wav";
    const auto second_path = std::filesystem::temp_directory_path() /
        "service-independent-audio-second.wav";
    addAudioMedia(session, first_path);
    addAudioMedia(session, second_path);

    const auto first = service.execute(application::AddMediaClipCommand{
        first_path, video_track_id, 45});
    require(first.changed() && first.affected_track_ids.size() == 1 &&
                session.timeline().trackCount() == 3 &&
                session.timeline().tracks().back().kind == timeline::TrackKind::Audio &&
                session.timeline().tracks().back().name == "Audio 2" &&
                session.timeline().tracks().back().clips.front().timeline_start_frame == 45,
            "Dropping audio over a Video track did not create an Audio track at the requested time.");
    const auto audio_track_id = first.affected_track_ids.front();

    const auto second = service.execute(application::AddMediaClipCommand{
        second_path, audio_track_id, 105});
    require(second.changed() && second.affected_track_ids.front() == audio_track_id &&
                session.timeline().trackCount() == 3 &&
                session.timeline().tracks().back().clips.size() == 2,
            "Dropping audio over an Audio track did not reuse that lane.");
    require(service.undo().changed() &&
                session.timeline().tracks().back().clips.size() == 1 &&
                service.redo().changed() &&
                session.timeline().tracks().back().clips.size() == 2,
            "Undo and Redo did not restore an audio drop on its track.");
    const auto crossfade = service.execute(application::AddTransitionCommand{
        audio_track_id, first.affected_clip_ids.front(),
        second.affected_clip_ids.front(),
        timeline::TransitionKind::AudioCrossfade, 15});
    require(crossfade.changed() &&
                session.timeline().tracks().back().clips[1].timeline_start_frame == 90 &&
                session.timeline().transitionBetween(2, 0, 1) != nullptr,
            "The command service could not create an Audio Crossfade on the audio lane.");
    require(service.undo().changed() &&
                session.timeline().tracks().back().clips[1].timeline_start_frame == 105 &&
                session.timeline().transitionBetween(2, 0, 1) == nullptr &&
                service.redo().changed() &&
                session.timeline().tracks().back().clips[1].timeline_start_frame == 90 &&
                session.timeline().transitionBetween(2, 0, 1) != nullptr,
            "Undo and Redo did not restore Audio Crossfade timing and transition state.");
    const auto resized = service.execute(application::UpdateTransitionCommand{
        audio_track_id, first.affected_clip_ids.front(),
        second.affected_clip_ids.front(),
        timeline::TransitionKind::AudioCrossfade, 10});
    require(resized.changed() &&
                session.timeline().tracks().back().clips[1].timeline_start_frame == 95 &&
                service.undo().changed() &&
                session.timeline().tracks().back().clips[1].timeline_start_frame == 90 &&
                service.redo().changed() &&
                session.timeline().tracks().back().clips[1].timeline_start_frame == 95,
            "Updating an Audio Crossfade did not ripple the lane as one Undo/Redo edit.");
    require(service.undo().changed() && service.undo().changed() &&
                service.undo().changed() &&
                service.undo().changed() &&
                session.timeline().trackCount() == 2 &&
                service.redo().changed() &&
                session.timeline().trackCount() == 3 &&
                session.timeline().tracks().back().kind == timeline::TrackKind::Audio &&
                session.timeline().tracks().back().clips.size() == 1,
            "Undo and Redo did not treat automatic Audio track creation as one edit.");
}

void runLinkedVideoAudioCommands() {
    application::EditorSession session;
    application::TimelineCommandService service(session);
    const auto video_track_id = session.timeline().tracks().front().track_id;
    const auto source_path = std::filesystem::temp_directory_path() /
        "service-linked-video-audio.mkv";
    addVideoWithAudioMedia(session, source_path);

    const auto added = service.execute(application::AddMediaClipCommand{
        source_path, video_track_id, 0});
    require(added.changed() && added.affected_clip_ids.size() == 2 &&
                session.timeline().trackCount() == 2 &&
                session.timeline().tracks()[1].kind == timeline::TrackKind::Audio,
            "Adding a video with audio did not create its Audio companion track.");
    const auto video_id = added.affected_clip_ids[0];
    const auto audio_id = added.affected_clip_ids[1];
    auto video_location = session.timeline().locateClip(video_id);
    auto audio_location = session.timeline().locateClip(audio_id);
    require(video_location.has_value() && audio_location.has_value(),
            "The generated video/audio pair could not be located.");
    const auto& video = session.timeline().tracks()[video_location->track_index]
        .clips[video_location->clip_index];
    const auto& audio = session.timeline().tracks()[audio_location->track_index]
        .clips[audio_location->clip_index];
    require(video.linked_clip_id == audio_id && audio.linked_clip_id == video_id &&
                video.audio_extracted && audio.kind == timeline::ClipKind::Audio &&
                audio.timeline_start_frame == video.timeline_start_frame &&
                audio.source_start_time_us == 0 &&
                audio.source_duration_time_us == 4000000 &&
                !audio.frame_rate.has_value(),
            "The generated audio companion did not preserve its link and source timing.");

    require(service.execute(application::MoveClipCommand{
                video_id, video_track_id, 30}).changed(),
            "A linked video clip could not be moved.");
    video_location = session.timeline().locateClip(video_id);
    audio_location = session.timeline().locateClip(audio_id);
    require(session.timeline().tracks()[video_location->track_index]
                    .clips[video_location->clip_index].timeline_start_frame == 30 &&
                session.timeline().tracks()[audio_location->track_index]
                    .clips[audio_location->clip_index].timeline_start_frame == 30,
            "Moving a linked video did not move its audio companion.");

    const auto split = service.execute(application::SplitClipCommand{video_id, 30});
    require(split.changed() && split.affected_clip_ids.size() == 4,
            "Splitting a linked video did not split both synchronized clips.");
    video_location = session.timeline().locateClip(video_id);
    audio_location = session.timeline().locateClip(audio_id);
    const auto right_video_id = split.selection.active_clip_id.value();
    const auto& left_video = session.timeline().tracks()[video_location->track_index]
        .clips[video_location->clip_index];
    const auto& left_audio = session.timeline().tracks()[audio_location->track_index]
        .clips[audio_location->clip_index];
    const auto right_video_location = session.timeline().locateClip(right_video_id);
    require(right_video_location.has_value(), "The right video segment was not created.");
    const auto right_audio_id = *session.timeline().tracks()[right_video_location->track_index]
        .clips[right_video_location->clip_index].linked_clip_id;
    const auto right_audio_location = session.timeline().locateClip(right_audio_id);
    require(right_audio_location.has_value() &&
                left_video.linked_clip_id == audio_id &&
                left_audio.linked_clip_id == video_id &&
                session.timeline().tracks()[right_audio_location->track_index]
                    .clips[right_audio_location->clip_index].linked_clip_id == right_video_id,
            "The two split pairs do not retain reciprocal links.");
    require(service.undo().changed() && session.timeline().clipCount() == 2 &&
                service.redo().changed() && session.timeline().clipCount() == 4,
            "Undo/Redo did not restore both halves of a linked split.");

    const auto audio_edit = service.execute(application::SetClipAudioCommand{
        right_video_id, 0.4, true});
    const auto refreshed_audio = session.timeline().locateClip(right_audio_id);
    require(audio_edit.changed() && refreshed_audio.has_value() &&
                session.timeline().tracks()[refreshed_audio->track_index]
                    .clips[refreshed_audio->clip_index].audio_gain == 0.4 &&
                session.timeline().tracks()[refreshed_audio->track_index]
                    .clips[refreshed_audio->clip_index].audio_muted,
            "Clip audio gain and mute were not shared across a linked pair.");

    const auto undo_count_before_envelope = service.undoCount();
    const auto envelope_batch = service.beginEditBatch();
    require(service.execute(application::SetClipAudioGainKeyframesCommand{
                right_audio_id, {{0, 0.5}, {30, 1.5}}}).changed() &&
                service.execute(application::SetClipAudioGainKeyframesCommand{
                    right_audio_id, {{0, 0.25}, {30, 2.0}}}).changed() &&
                service.finishEditBatch(envelope_batch).changed(),
            "An audio envelope could not be adjusted in an edit batch.");
    require(service.undoCount() == undo_count_before_envelope + 1 &&
                session.timeline().tracks()[refreshed_audio->track_index]
                    .clips[refreshed_audio->clip_index].audio_gain_keyframes ==
                    std::vector<timeline::AudioGainKeyframe>{{0, 0.25}, {30, 2.0}},
            "Multiple curve-drag updates did not become one history item.");
    require(service.undo().changed() &&
                session.timeline().tracks()[refreshed_audio->track_index]
                    .clips[refreshed_audio->clip_index].audio_gain_keyframes.empty() &&
                service.redo().changed() &&
                session.timeline().tracks()[refreshed_audio->track_index]
                    .clips[refreshed_audio->clip_index].audio_gain_keyframes ==
                    std::vector<timeline::AudioGainKeyframe>{{0, 0.25}, {30, 2.0}},
            "Undo/Redo did not restore the audio envelope as one edit.");

    require(service.execute(application::UnlinkAudioCommand{right_video_id}).changed(),
            "The linked pair could not be unlinked.");
    const auto unlinked_video_location = session.timeline().locateClip(right_video_id);
    const auto unlinked_audio_location = session.timeline().locateClip(right_audio_id);
    require(!session.timeline().tracks()[unlinked_video_location->track_index]
                 .clips[unlinked_video_location->clip_index].linked_clip_id.has_value() &&
                !session.timeline().tracks()[unlinked_audio_location->track_index]
                     .clips[unlinked_audio_location->clip_index].linked_clip_id.has_value(),
            "Unlinking did not clear both sides of the relationship.");
    require(service.execute(application::MoveClipCommand{
                right_audio_id,
                session.timeline().tracks()[unlinked_audio_location->track_index].track_id,
                300}).changed() &&
                session.timeline().tracks()[session.timeline().locateClip(right_video_id)->track_index]
                    .clips[session.timeline().locateClip(right_video_id)->clip_index]
                    .timeline_start_frame == 60,
            "An unlinked Audio clip could not move independently.");

    require(service.execute(application::DeleteClipCommand{video_id}).changed() &&
                !session.timeline().locateClip(video_id).has_value() &&
                !session.timeline().locateClip(audio_id).has_value(),
            "Deleting one linked clip did not remove the paired clips together.");
}

void runRippleDeleteCommands() {
    {
        application::EditorSession session;
        application::TimelineCommandService service(session);
        const auto video_track_id = session.timeline().tracks().front().track_id;
        const auto audio_track_id = session.timeline().tracks().back().track_id;
        const auto first_path = std::filesystem::temp_directory_path() /
            "service-ripple-first.mkv";
        const auto second_path = std::filesystem::temp_directory_path() /
            "service-ripple-second.mkv";
        const auto third_path = std::filesystem::temp_directory_path() /
            "service-ripple-third.mkv";
        const auto audio_path = std::filesystem::temp_directory_path() /
            "service-ripple-other-track.wav";
        addMedia(session, first_path);
        addMedia(session, second_path);
        addMedia(session, third_path);
        addAudioMedia(session, audio_path);

        const auto other_track_audio = service.execute(
            application::AddMediaClipCommand{audio_path, audio_track_id, 0});
        const auto first = service.execute(application::AddMediaClipCommand{
            first_path, video_track_id, 0});
        const auto second = service.execute(application::AddMediaClipCommand{
            second_path, video_track_id, 120});
        const auto third = service.execute(application::AddMediaClipCommand{
            third_path, video_track_id, 240});
        require(other_track_audio.changed() && first.changed() && second.changed() &&
                    third.changed(),
                "The Ripple Delete sequence fixture could not be created.");
        const auto first_id = first.affected_clip_ids.front();
        const auto second_id = second.affected_clip_ids.front();
        const auto third_id = third.affected_clip_ids.front();
        const auto other_track_audio_id = other_track_audio.affected_clip_ids.front();
        service.clearHistory();
        const auto before = session.timeline().snapshot();
        const auto invalid_ripple = service.execute(
            application::RippleDeleteClipCommand{999999});
        require(invalid_ripple.status == application::EditStatus::Rejected &&
                    invalid_ripple.reason == application::EditReason::InvalidTarget &&
                    session.timeline().snapshot() == before && service.undoCount() == 0,
                "Ripple Delete accepted a stale clip ID or changed the Timeline on rejection.");

        const auto ripple = service.execute(
            application::RippleDeleteClipCommand{first_id});
        const auto moved_second = session.timeline().locateClip(second_id);
        const auto moved_third = session.timeline().locateClip(third_id);
        const auto fixed_audio = session.timeline().locateClip(other_track_audio_id);
        require(ripple.changed() && ripple.invalidate_playback &&
                    !ripple.stopped_at_collision && service.undoCount() == 1 &&
                    !session.timeline().locateClip(first_id).has_value() &&
                    moved_second.has_value() && moved_third.has_value() &&
                    fixed_audio.has_value() &&
                    session.timeline().tracks()[moved_second->track_index]
                        .clips[moved_second->clip_index].timeline_start_frame == 0 &&
                    session.timeline().tracks()[moved_third->track_index]
                        .clips[moved_third->clip_index].timeline_start_frame == 120 &&
                    session.timeline().tracks()[fixed_audio->track_index]
                        .clips[fixed_audio->clip_index].timeline_start_frame == 0 &&
                    ripple.affected_track_ids.size() == 1 &&
                    session.selection().active_clip_id == second_id,
                "Ripple Delete did not close the selected track's gap while preserving other tracks and selecting the remaining clip.");
        require(service.undo().changed(),
                "Ripple Delete could not be undone.");
        const auto after_undo = session.timeline().snapshot();
        require(after_undo == before,
                "Undo did not restore the complete pre-ripple Timeline state.");
        require(service.redo().changed(),
                "Ripple Delete could not be redone.");
        const auto redone_second = session.timeline().locateClip(second_id);
        require(redone_second.has_value() &&
                    session.timeline().tracks()[redone_second->track_index]
                        .clips[redone_second->clip_index].timeline_start_frame == 0,
                "Redo did not restore the Ripple Delete positions.");
        require(service.undo().changed() &&
                    service.execute(application::DeleteClipCommand{first_id}).changed() &&
                    session.timeline().tracks()[session.timeline().locateClip(second_id)->track_index]
                        .clips[session.timeline().locateClip(second_id)->clip_index]
                        .timeline_start_frame == 120,
                "Ordinary Delete stopped leaving the following clip at its original position.");
    }

    {
        application::EditorSession session;
        application::TimelineCommandService service(session);
        const auto track_id = session.timeline().tracks().front().track_id;
        const auto first = service.execute(application::AddTextClipCommand{
            track_id, 0, 30, 30.0});
        const auto second = service.execute(application::AddTextClipCommand{
            track_id, 30, 30, 30.0});
        const auto third = service.execute(application::AddTextClipCommand{
            track_id, 60, 30, 30.0});
        require(first.changed() && second.changed() && third.changed(),
                "The text Ripple Delete fixture could not be created.");
        const auto first_id = first.affected_clip_ids.front();
        const auto second_id = second.affected_clip_ids.front();
        const auto third_id = third.affected_clip_ids.front();
        service.clearHistory();
        const auto ripple = service.execute(
            application::RippleDeleteClipCommand{first_id});
        const auto second_location = session.timeline().locateClip(second_id);
        const auto third_location = session.timeline().locateClip(third_id);
        require(ripple.changed() && second_location.has_value() &&
                    third_location.has_value() &&
                    session.timeline().tracks()[second_location->track_index]
                        .clips[second_location->clip_index].timeline_start_frame == 0 &&
                    session.timeline().tracks()[third_location->track_index]
                        .clips[third_location->clip_index].timeline_start_frame == 30,
                "Ripple Delete did not close the gap between text clips.");
        require(service.undo().changed() &&
                    session.timeline().locateClip(first_id).has_value() &&
                    service.redo().changed() &&
                    !session.timeline().locateClip(first_id).has_value(),
                "Text Ripple Delete did not share a single Undo/Redo action.");
    }

    {
        application::EditorSession session;
        application::TimelineCommandService service(session);
        const auto audio_track_id = session.timeline().tracks().back().track_id;
        const auto audio_path = std::filesystem::temp_directory_path() /
            "service-ripple-audio-only.wav";
        addAudioMedia(session, audio_path);
        const auto first = service.execute(application::AddMediaClipCommand{
            audio_path, audio_track_id, 0});
        const auto second = service.execute(application::AddMediaClipCommand{
            audio_path, audio_track_id, 60});
        const auto third = service.execute(application::AddMediaClipCommand{
            audio_path, audio_track_id, 120});
        require(first.changed() && second.changed() && third.changed(),
                "The audio Ripple Delete fixture could not be created.");
        const auto first_id = first.affected_clip_ids.front();
        const auto second_id = second.affected_clip_ids.front();
        const auto third_id = third.affected_clip_ids.front();
        service.clearHistory();
        const auto ripple = service.execute(
            application::RippleDeleteClipCommand{second_id});
        const auto third_location = session.timeline().locateClip(third_id);
        require(ripple.changed() && !session.timeline().locateClip(second_id) &&
                    third_location.has_value() &&
                    session.timeline().tracks()[third_location->track_index]
                        .clips[third_location->clip_index].timeline_start_frame == 60 &&
                    session.timeline().tracks()[
                        session.timeline().locateClip(first_id)->track_index]
                        .clips[session.timeline().locateClip(first_id)->clip_index]
                        .timeline_start_frame == 0,
                "Ripple Delete did not close a gap between audio-only clips.");
    }

    {
        application::EditorSession session;
        application::TimelineCommandService service(session);
        const auto video_track_id = session.timeline().tracks().front().track_id;
        const auto audio_track_id = session.timeline().tracks().back().track_id;
        const auto target_path = std::filesystem::temp_directory_path() /
            "service-ripple-linked-target.mkv";
        const auto follower_path = std::filesystem::temp_directory_path() /
            "service-ripple-linked-follower.mkv";
        const auto tail_path = std::filesystem::temp_directory_path() /
            "service-ripple-linked-tail.mkv";
        const auto blocker_path = std::filesystem::temp_directory_path() /
            "service-ripple-audio-blocker.wav";
        addVideoWithAudioMedia(session, target_path);
        addVideoWithAudioMedia(session, follower_path);
        addVideoWithAudioMedia(session, tail_path);
        addAudioMedia(session, blocker_path);
        const auto blocker = service.execute(application::AddMediaClipCommand{
            blocker_path, audio_track_id, 0});
        const auto target = service.execute(application::AddMediaClipCommand{
            target_path, video_track_id, 0});
        const auto follower = service.execute(application::AddMediaClipCommand{
            follower_path, video_track_id, 120});
        const auto tail = service.execute(application::AddMediaClipCommand{
            tail_path, video_track_id, 240});
        require(target.changed() && blocker.changed() && follower.changed() &&
                    tail.changed() && target.affected_clip_ids.size() == 2 &&
                    follower.affected_clip_ids.size() == 2 &&
                    tail.affected_clip_ids.size() == 2,
                "The linked collision fixture could not be created.");
        const auto target_id = target.affected_clip_ids.front();
        const auto target_audio_id = target.affected_clip_ids.back();
        const auto follower_video_id = follower.affected_clip_ids.front();
        const auto follower_audio_id = follower.affected_clip_ids.back();
        const auto tail_video_id = tail.affected_clip_ids.front();
        const auto tail_audio_id = tail.affected_clip_ids.back();
        const auto blocker_id = blocker.affected_clip_ids.front();
        service.clearHistory();
        const auto before = session.timeline().snapshot();
        const auto ripple = service.execute(
            application::RippleDeleteClipCommand{target_id});
        const auto follower_video_location = session.timeline().locateClip(follower_video_id);
        const auto follower_audio_location = session.timeline().locateClip(follower_audio_id);
        const auto tail_video_location = session.timeline().locateClip(tail_video_id);
        const auto tail_audio_location = session.timeline().locateClip(tail_audio_id);
        const auto blocker_location = session.timeline().locateClip(blocker_id);
        require(ripple.changed() && ripple.stopped_at_collision &&
                    service.undoCount() == 1 &&
                    !session.timeline().locateClip(target_audio_id).has_value() &&
                    follower_video_location.has_value() &&
                    follower_audio_location.has_value() &&
                    tail_video_location.has_value() && tail_audio_location.has_value() &&
                    blocker_location.has_value() &&
                    session.timeline().tracks()[follower_video_location->track_index]
                        .clips[follower_video_location->clip_index].timeline_start_frame == 60 &&
                    session.timeline().tracks()[follower_audio_location->track_index]
                        .clips[follower_audio_location->clip_index].timeline_start_frame == 60 &&
                    session.timeline().tracks()[tail_video_location->track_index]
                        .clips[tail_video_location->clip_index].timeline_start_frame == 240 &&
                    session.timeline().tracks()[tail_audio_location->track_index]
                        .clips[tail_audio_location->clip_index].timeline_start_frame == 240 &&
                    session.timeline().tracks()[blocker_location->track_index]
                        .clips[blocker_location->clip_index].timeline_start_frame == 0,
                "Ripple Delete did not stop both linked sequences at the audio blocker and leave the remaining gap.");
        require(service.undo().changed() &&
                    session.timeline().snapshot() == before &&
                    service.redo().changed() &&
                    session.timeline().tracks()[session.timeline().locateClip(tail_video_id)->track_index]
                        .clips[session.timeline().locateClip(tail_video_id)->clip_index]
                        .timeline_start_frame == 240,
                "A collision-stopped Ripple Delete did not undo and redo as one edit.");
    }

    {
        application::EditorSession session;
        application::TimelineCommandService service(session);
        const auto audio_track_id = session.timeline().tracks().back().track_id;
        const auto audio_path = std::filesystem::temp_directory_path() /
            "service-ripple-crossfade.wav";
        addAudioMedia(session, audio_path);
        const auto first = service.execute(application::AddMediaClipCommand{
            audio_path, audio_track_id, 0});
        const auto second = service.execute(application::AddMediaClipCommand{
            audio_path, audio_track_id, 60});
        const auto third = service.execute(application::AddMediaClipCommand{
            audio_path, audio_track_id, 120});
        require(first.changed() && second.changed() && third.changed(),
                "The Ripple Delete transition fixture could not be created.");
        const auto first_id = first.affected_clip_ids.front();
        const auto second_id = second.affected_clip_ids.front();
        const auto third_id = third.affected_clip_ids.front();
        require(service.execute(application::AddTransitionCommand{
                    audio_track_id, first_id, second_id,
                    timeline::TransitionKind::AudioCrossfade, 15}).changed() &&
                    service.execute(application::AddTransitionCommand{
                        audio_track_id, second_id, third_id,
                        timeline::TransitionKind::AudioCrossfade, 15}).changed(),
                "The Ripple Delete transition fixture could not add adjacent crossfades.");
        service.clearHistory();
        const auto ripple = service.execute(
            application::RippleDeleteClipCommand{first_id});
        const auto second_location = session.timeline().locateClip(second_id);
        const auto third_location = session.timeline().locateClip(third_id);
        require(ripple.changed() && second_location.has_value() &&
                    third_location.has_value() &&
                    session.timeline().transitionBetween(
                        second_location->track_index,
                        second_location->clip_index,
                        third_location->clip_index) != nullptr &&
                    session.timeline().transitionBetween(
                        second_location->track_index, 0, 1)->kind ==
                        timeline::TransitionKind::AudioCrossfade &&
                    session.timeline().tracks()[second_location->track_index]
                        .clips[second_location->clip_index].timeline_start_frame == 0 &&
                    session.timeline().tracks()[third_location->track_index]
                        .clips[third_location->clip_index].timeline_start_frame == 45,
                "Ripple Delete did not discard the removed clip's transition while preserving and shifting the remaining crossfade.");
        require(service.undo().changed() &&
                    session.timeline().locateClip(first_id).has_value() &&
                    service.redo().changed() &&
                    !session.timeline().locateClip(first_id).has_value(),
                "Ripple Delete did not restore affected audio transitions through Undo/Redo.");
    }
}

void runClipAttributeCommands() {
    application::EditorSession session;
    application::TimelineCommandService service(session);
    const auto video_track_id = session.timeline().tracks().front().track_id;
    const auto source_path = std::filesystem::temp_directory_path() /
        "service-attribute-source.mkv";
    const auto short_path = std::filesystem::temp_directory_path() /
        "service-attribute-short.mkv";
    const auto long_path = std::filesystem::temp_directory_path() /
        "service-attribute-long.mkv";
    const auto audio_path = std::filesystem::temp_directory_path() /
        "service-attribute-target.wav";
    addVideoWithAudioMedia(session, source_path);
    addVideoWithAudioMedia(session, short_path, 1.0);
    addVideoWithAudioMedia(session, long_path, 5.0);
    addAudioMedia(session, audio_path);

    const auto source = service.execute(application::AddMediaClipCommand{
        source_path, video_track_id, 0});
    const auto short_target = service.execute(application::AddMediaClipCommand{
        short_path, video_track_id, 120});
    const auto long_target = service.execute(application::AddMediaClipCommand{
        long_path, video_track_id, 150});
    require(source.changed() && short_target.changed() && long_target.changed() &&
                source.affected_clip_ids.size() == 2 &&
                short_target.affected_clip_ids.size() == 2 &&
                long_target.affected_clip_ids.size() == 2,
            "The clip-attribute test could not create linked video/audio pairs.");
    const auto source_video_id = source.affected_clip_ids[0];
    const auto source_audio_id = source.affected_clip_ids[1];
    const auto short_video_id = short_target.affected_clip_ids[0];
    const auto short_audio_id = short_target.affected_clip_ids[1];
    const auto long_video_id = long_target.affected_clip_ids[0];

    require(service.execute(application::SetClipAudioCommand{
                source_video_id, 0.6, true}).changed() &&
                service.execute(application::SetClipAudioGainKeyframesCommand{
                    source_audio_id, {{0, 0.0}, {60, 1.0}, {120, 2.0}}}).changed() &&
                service.execute(application::SetClipEffectsCommand{
                    source_video_id, {creative_suite::effects::makeDefaultInstance(
                        "video.grayscale")}}).changed() &&
                service.execute(application::SetTransformPropertyCommand{
                    source_video_id, timeline::TransformProperty::PositionX, 0, 0.2}).changed() &&
                service.execute(application::ToggleTransformKeyframeCommand{
                    source_video_id, timeline::TransformProperty::PositionX, 0}).changed() &&
                service.execute(application::ToggleTransformKeyframeCommand{
                    source_video_id, timeline::TransformProperty::PositionX, 60}).changed() &&
                service.execute(application::SetTransformPropertyCommand{
                    source_video_id, timeline::TransformProperty::PositionX, 60, 0.8}).changed() &&
                service.execute(application::ToggleTransformKeyframeCommand{
                    source_video_id, timeline::TransformProperty::PositionX, 120}).changed() &&
                service.execute(application::SetTransformPropertyCommand{
                    source_video_id, timeline::TransformProperty::PositionX, 119, 0.4}).changed(),
            "The clip-attribute source could not prepare audio and transform animation.");
    require(service.execute(application::SetClipAudioCommand{
                short_video_id, 1.5, false}).changed() &&
                service.execute(application::SetClipAudioGainKeyframesCommand{
                    short_audio_id, {{0, 1.5}, {30, 1.5}}}).changed(),
            "The short linked target could not prepare distinct audio attributes.");

    const auto source_video_location = session.timeline().locateClip(source_video_id);
    const auto source_audio_location = session.timeline().locateClip(source_audio_id);
    require(source_video_location.has_value() && source_audio_location.has_value(),
            "The clip-attribute source pair could not be located.");
    const auto& source_video = session.timeline().tracks()[source_video_location->track_index]
        .clips[source_video_location->clip_index];
    const auto& source_audio = session.timeline().tracks()[source_audio_location->track_index]
        .clips[source_audio_location->clip_index];
    timeline::TimelineClipAttributes attributes;
    attributes.source_kind = source_video.kind;
    attributes.transform = source_video.transform;
    attributes.transform_keyframes = source_video.keyframes;
    attributes.audio_gain = source_video.audio_gain;
    attributes.audio_muted = source_video.audio_muted;
    attributes.audio_volume_envelope = source_audio.audio_gain_keyframes;

    const auto short_location = session.timeline().locateClip(short_video_id);
    require(short_location.has_value(), "The short paste target could not be located.");
    const auto before_short_paste = session.timeline().tracks()[short_location->track_index]
        .clips[short_location->clip_index];
    const auto history_before_short_paste = service.undoCount();
    const auto pasted_short = service.execute(application::ApplyClipAttributesCommand{
        short_video_id, attributes,
        timeline::ClipAttributeOptions{false, true, true, true, false}});
    const auto short_video_after = session.timeline().locateClip(short_video_id);
    const auto short_audio_after = session.timeline().locateClip(short_audio_id);
    require(pasted_short.changed() && short_video_after.has_value() &&
                short_audio_after.has_value() &&
                service.undoCount() == history_before_short_paste + 1,
            "Pasting transform and audio attributes did not create one atomic history entry.");
    const auto& pasted_video = session.timeline().tracks()[short_video_after->track_index]
        .clips[short_video_after->clip_index];
    const auto& pasted_audio = session.timeline().tracks()[short_audio_after->track_index]
        .clips[short_audio_after->clip_index];
    require(pasted_video.linked_clip_id == short_audio_id &&
                pasted_audio.linked_clip_id == short_video_id,
            "Short-target paste did not preserve the linked pair.");
    require(pasted_video.audio_gain == 0.6 && pasted_video.audio_muted &&
                pasted_audio.audio_gain == 0.6 && pasted_audio.audio_muted,
            "Short-target paste did not apply gain and mute to the linked pair.");
    require(pasted_audio.audio_gain_keyframes ==
                std::vector<timeline::AudioGainKeyframe>{{0, 0.0}, {30, 0.5}},
            "Short-target paste did not sample the audio envelope at the target end.");
    require(pasted_video.transform.position_x == 0.2,
            "Short-target paste did not preserve the transform base value.");
    require(pasted_video.keyframes.position_x.size() == 2,
            "Short-target paste did not limit transform keyframes to the target duration.");
    require(pasted_video.keyframes.position_x.back().frame == 29,
            "Short-target paste did not add a transform keyframe at the target endpoint.");
    require(std::abs(pasted_video.keyframes.position_x.back().value - 0.49) < 1e-9,
            "Short-target paste did not sample the transform curve at the target endpoint.");
    require(service.undo().changed() &&
                session.timeline().tracks()[short_video_after->track_index]
                    .clips[short_video_after->clip_index] == before_short_paste &&
                service.redo().changed() &&
                session.timeline().tracks()[session.timeline().locateClip(short_audio_id)->track_index]
                    .clips[session.timeline().locateClip(short_audio_id)->clip_index]
                    .audio_gain_keyframes ==
                    std::vector<timeline::AudioGainKeyframe>{{0, 0.0}, {30, 0.5}},
            "Undo/Redo did not restore/reapply all short-target attributes as one edit.");

    const auto long_paste = service.execute(application::ApplyClipAttributesCommand{
        long_video_id, attributes,
        timeline::ClipAttributeOptions{false, true, false, true, false}});
    const auto long_video_location = session.timeline().locateClip(long_video_id);
    const auto long_audio_id = *session.timeline().tracks()[long_video_location->track_index]
        .clips[long_video_location->clip_index].linked_clip_id;
    const auto long_audio_location = session.timeline().locateClip(long_audio_id);
    require(long_paste.changed() && long_audio_location.has_value() &&
                session.timeline().tracks()[long_video_location->track_index]
                    .clips[long_video_location->clip_index].keyframes.position_x ==
                    attributes.transform_keyframes.position_x &&
                session.timeline().tracks()[long_audio_location->track_index]
                    .clips[long_audio_location->clip_index].audio_gain_keyframes ==
                    *attributes.audio_volume_envelope &&
                timeline::evaluateAudioGainEnvelope(
                    *attributes.audio_volume_envelope, 150.0) == 2.0,
            "Long-target paste scaled local frame positions instead of preserving offsets and the final value.");

    const auto audio_target = service.execute(application::AddMediaClipCommand{
        audio_path, video_track_id, 350});
    require(audio_target.changed() && audio_target.affected_clip_ids.size() == 1,
            "The clip-attribute test could not add a standalone audio destination.");
    const auto audio_clip_id = audio_target.affected_clip_ids.front();
    const auto text_target = service.execute(application::AddTextClipCommand{
        video_track_id, 500, 90, 30.0});
    require(text_target.changed(),
            "The clip-attribute test could not add a mixed-type destination.");
    const auto text_clip_id = text_target.affected_clip_ids.front();
    const auto audio_paste = service.execute(application::ApplyClipAttributesCommand{
        audio_clip_id, attributes,
        timeline::ClipAttributeOptions{false, false, true, true, false}});
    const auto audio_location = session.timeline().locateClip(audio_clip_id);
    require(audio_paste.changed() && audio_location.has_value() &&
                session.timeline().tracks()[audio_location->track_index]
                    .clips[audio_location->clip_index].audio_gain == 0.6 &&
                session.timeline().tracks()[audio_location->track_index]
                    .clips[audio_location->clip_index].audio_muted &&
                session.timeline().tracks()[audio_location->track_index]
                    .clips[audio_location->clip_index].audio_gain_keyframes ==
                    std::vector<timeline::AudioGainKeyframe>{{0, 0.0}, {60, 1.0}},
            "Audio attributes could not be pasted from a linked video into an audio-only clip.");

    const auto audio_before_invalid = session.timeline().tracks()[audio_location->track_index]
        .clips[audio_location->clip_index];
    const auto history_before_invalid = service.undoCount();
    const auto invalid_audio_attributes = [&]() {
        auto invalid = attributes;
        invalid.audio_gain = 8.0;
        return invalid;
    }();
    const auto rejected = service.execute(application::ApplyClipAttributesCommand{
        short_video_id, invalid_audio_attributes,
        timeline::ClipAttributeOptions{false, true, true, false, false}});
    require(rejected.status == application::EditStatus::Rejected,
            "An invalid audio gain was accepted during a multi-group paste.");
    require(service.undoCount() == history_before_invalid,
            "A rejected multi-group paste added an undo entry.");
    require(session.timeline().tracks()[audio_location->track_index]
                .clips[audio_location->clip_index] == audio_before_invalid,
            "A rejected multi-group paste changed another clip's model state.");

    const auto incompatible = service.execute(application::ApplyClipAttributesCommand{
        audio_clip_id, attributes,
        timeline::ClipAttributeOptions{true, false, false, false, false}});
    require(incompatible.status == application::EditStatus::Rejected &&
                service.undoCount() == history_before_invalid,
            "Effects were accepted when pasted from a video clip onto an audio-only clip.");

    attributes.effects = {
        creative_suite::effects::makeDefaultInstance("video.grayscale")};
    const std::vector<timeline::ClipId> mixed_targets{
        short_video_id, audio_clip_id, text_clip_id};
    const auto history_before_batch = service.undoCount();
    const auto batch_paste = service.execute(
        application::ApplyClipAttributesBatchCommand{
            mixed_targets, attributes,
            timeline::ClipAttributeOptions{true, true, true, true, true}});
    const auto short_video_batch_location = session.timeline().locateClip(short_video_id);
    const auto short_audio_batch_location = session.timeline().locateClip(short_audio_id);
    const auto audio_batch_location = session.timeline().locateClip(audio_clip_id);
    const auto text_batch_location = session.timeline().locateClip(text_clip_id);
    require(batch_paste.changed() && service.undoCount() == history_before_batch + 1 &&
                short_video_batch_location.has_value() &&
                short_audio_batch_location.has_value() &&
                audio_batch_location.has_value() && text_batch_location.has_value(),
            "Mixed-compatible attribute paste did not create one atomic history entry.");
    const auto& batch_video = session.timeline().tracks()[
        short_video_batch_location->track_index].clips[
            short_video_batch_location->clip_index];
    const auto& batch_video_audio = session.timeline().tracks()[
        short_audio_batch_location->track_index].clips[
            short_audio_batch_location->clip_index];
    const auto& batch_audio = session.timeline().tracks()[
        audio_batch_location->track_index].clips[audio_batch_location->clip_index];
    const auto& batch_text = session.timeline().tracks()[
        text_batch_location->track_index].clips[text_batch_location->clip_index];
    require(batch_video.effects == attributes.effects &&
                batch_video.transform.position_x == attributes.transform.position_x &&
                batch_video_audio.audio_gain_keyframes ==
                    std::vector<timeline::AudioGainKeyframe>{{0, 0.0}, {30, 0.5}} &&
                batch_audio.effects.empty() && batch_audio.audio_gain == 0.6 &&
                batch_audio.audio_muted &&
                batch_audio.audio_gain_keyframes ==
                    std::vector<timeline::AudioGainKeyframe>{{0, 0.0}, {60, 1.0}} &&
                batch_text.transform.position_x == attributes.transform.position_x &&
                batch_text.effects.empty() && batch_text.audio_gain == 1.0,
            "Batch paste did not skip incompatible groups per destination or route linked audio attributes.");
    const auto before_invalid_batch = session.timeline().snapshot();
    const auto history_before_invalid_batch = service.undoCount();
    const auto invalid_batch = service.execute(
        application::ApplyClipAttributesBatchCommand{
            {audio_clip_id, 999999}, attributes,
            timeline::ClipAttributeOptions{false, false, true, false, false}});
    require(invalid_batch.status == application::EditStatus::Rejected &&
                invalid_batch.reason == application::EditReason::InvalidTarget &&
                service.undoCount() == history_before_invalid_batch &&
                session.timeline().snapshot() == before_invalid_batch,
            "A stale batch destination caused partial attribute changes.");
    require(service.undo().changed() &&
                session.timeline().tracks()[short_video_batch_location->track_index]
                    .clips[short_video_batch_location->clip_index].effects.empty() &&
                service.redo().changed() &&
                session.timeline().tracks()[session.timeline().locateClip(short_video_id)->track_index]
                    .clips[session.timeline().locateClip(short_video_id)->clip_index]
                    .effects == attributes.effects,
            "Batch Paste Attributes did not undo and redo as a single edit.");
}

} // namespace

int main() {
    try {
        run();
        runVisualEffectCommands();
        runInspectorAndTrackCommands();
        runReconnectTimingCommand();
        runAutomaticAudioTrackCommand();
        runLinkedVideoAudioCommands();
        runRippleDeleteCommands();
        runClipAttributeCommands();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
