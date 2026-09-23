#include "application/editor_session.h"
#include "application/media_controller.h"
#include "application/timeline_command_service.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
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

    const auto split = service.execute(application::SplitClipCommand{1, 60});
    require(split.changed() && split.affected_clip_ids.size() == 2 &&
                session.selection().active_clip_id == 2 && session.playheadFrame() == 0 &&
                service.undoCount() == 3,
            "Splitting did not return and select the new right-hand clip.");

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
    const auto redone = service.redo();
    require(redone.changed() && !model.locateClip(2).has_value() && service.canUndo(),
            "Redo did not reapply the clip deletion.");

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
    const auto reorder_undo = reorder_service.undo();
    require(reorder_undo.changed() &&
                reorder_model.tracks().front().clips.front().clip_id ==
                    second_media_added.affected_clip_ids.front(),
            "Undo did not restore a single-track reorder.");

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

} // namespace

int main() {
    try {
        run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
