#include "application/editor_session.h"
#include "application/media_controller.h"
#include "application/timeline_command_service.h"
#include "ui/workspace/pages/edit/edit_workspace_controller.h"

#include <QCoreApplication>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void run() {
    application::EditorSession session;
    application::TimelineCommandService command_service(session);
    ui::EditWorkspaceController controller(session, command_service);

    int emitted_results = 0;
    int emitted_history_updates = 0;
    QObject::connect(
        &controller,
        &ui::EditWorkspaceController::commandResultProduced,
        [&emitted_results](const application::TimelineEditResult&) {
            ++emitted_results;
        });
    QObject::connect(
        &controller,
        &ui::EditWorkspaceController::historyStateChanged,
        [&emitted_history_updates](bool, bool) {
            ++emitted_history_updates;
        });

    const auto delete_without_selection = controller.deleteSelectedClip();
    require(delete_without_selection.status == application::EditStatus::NoChange &&
                !session.timeline().hasClip() && command_service.undoCount() == 0 &&
                !session.projectDirty(),
            "Deleting without a selection changed the project or its history.");

    const auto track_id = session.timeline().tracks().front().track_id;
    const auto add_text = controller.execute(application::AddTextClipCommand{
        track_id, 0, 90, 30.0});
    require(add_text.changed() && session.timeline().hasClip(),
            "The Edit controller did not delegate an accepted timeline command.");
    require(controller.canUndo() && !controller.canRedo(),
            "The Edit controller did not expose the shared command history.");

    const auto overlap = controller.execute(application::AddTextClipCommand{
        track_id, 30, 90, 30.0});
    require(overlap.status == application::EditStatus::Rejected &&
                overlap.reason == application::EditReason::Overlap &&
                session.timeline().clipCount(0) == 1 && command_service.undoCount() == 1,
            "An occupied timeline position changed the project or history.");

    const auto no_selection = controller.execute(
        application::DeleteClipCommand{999999});
    require(no_selection.status == application::EditStatus::Rejected &&
                command_service.undoCount() == 1,
            "A command without a valid clip selection changed the history.");

    application::MediaController media_controller(session);
    const auto offline_path = std::filesystem::temp_directory_path() /
        ("edit-workspace-offline-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".mkv");
    require(media_controller.commitImported({
                media::VideoMetadata{.source_path = offline_path}, {},
                "Offline media", "Unsorted", true}).changed(),
            "The test could not register offline media in the shared session.");
    const auto offline_add = controller.addMediaClip(
        offline_path, track_id, 300);
    require(offline_add.reason == application::EditReason::OfflineMedia &&
                session.timeline().clipCount(0) == 1 && command_service.undoCount() == 1,
            "Offline media changed the timeline or its history.");

    application::EditorSession offline_drop_session;
    application::TimelineCommandService offline_drop_commands(offline_drop_session);
    application::MediaController offline_drop_library(offline_drop_session);
    ui::EditWorkspaceController offline_drop_controller(
        offline_drop_session, offline_drop_commands);
    const auto isolated_offline_path = std::filesystem::temp_directory_path() /
        ("edit-workspace-offline-drop-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".mkv");
    require(offline_drop_library.commitImported({
                media::VideoMetadata{.source_path = isolated_offline_path}, {},
                "Offline media", "Unsorted", true}).changed(),
            "The test could not register isolated offline media.");
    const auto isolated_offline_add = offline_drop_controller.addMediaClip(
        isolated_offline_path,
        offline_drop_session.timeline().tracks().front().track_id,
        0);
    require(isolated_offline_add.reason == application::EditReason::OfflineMedia &&
                offline_drop_commands.undoCount() == 0 &&
                !offline_drop_session.projectDirty(),
            "An offline media drop changed project state or history.");

    application::EditorSession media_drop_session;
    application::TimelineCommandService media_drop_commands(media_drop_session);
    application::MediaController media_drop_library(media_drop_session);
    ui::EditWorkspaceController media_drop_controller(
        media_drop_session, media_drop_commands);
    const auto online_path = std::filesystem::temp_directory_path() /
        ("edit-workspace-online-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".mkv");
    media::VideoMetadata online_metadata;
    online_metadata.source_path = online_path;
    online_metadata.frame_rate = 30.0;
    online_metadata.frame_count = 300;
    require(media_drop_library.commitImported({
                online_metadata, {}, "Online media", "Unsorted", false}).changed(),
            "The test could not register media for the controller drop test.");
    const auto media_drop_track =
        media_drop_session.timeline().tracks().front().track_id;
    int committed_media_drops = 0;
    QObject::connect(
        &media_drop_controller,
        &ui::EditWorkspaceController::timelineEditCommitted,
        [&committed_media_drops](
            const application::TimelineEditResult&,
            bool,
            bool,
            const QString&) {
            ++committed_media_drops;
        });
    const auto added_media = media_drop_controller.addMediaClip(
        online_path, media_drop_track, 12);
    require(added_media.changed() &&
                media_drop_session.timeline().clipCount(0) == 1 &&
                media_drop_commands.undoCount() == 1 &&
                committed_media_drops == 1,
            "Adding dropped media did not commit through the Edit controller.");
    const auto occupied_media = media_drop_controller.addMediaClip(
        online_path, media_drop_track, 30);
    require(!occupied_media.changed() &&
                occupied_media.reason == application::EditReason::Overlap &&
                media_drop_session.timeline().clipCount(0) == 1 &&
                media_drop_commands.undoCount() == 1 &&
                committed_media_drops == 1,
            "A rejected media drop changed the project or its history.");

    const auto undone = controller.undo();
    require(undone.changed() && !session.timeline().hasClip() &&
                !controller.canUndo() && controller.canRedo(),
            "Undo did not update the shared timeline through the controller.");
    const auto redone = controller.redo();
    require(redone.changed() && session.timeline().clipCount(0) == 1 &&
                controller.canUndo() && !controller.canRedo(),
            "Redo did not restore the timeline through the controller.");
    require(emitted_results == 7 && emitted_history_updates >= emitted_results,
            "The controller did not emit typed result and history integration signals.");

    application::EditorSession track_session;
    application::TimelineCommandService track_commands(track_session);
    ui::EditWorkspaceController track_controller(track_session, track_commands);
    int committed_track_edits = 0;
    QObject::connect(
        &track_controller,
        &ui::EditWorkspaceController::timelineEditCommitted,
        [&committed_track_edits](
            const application::TimelineEditResult&,
            bool,
            bool,
            const QString&) {
            ++committed_track_edits;
        });

    const auto add_track = track_controller.addTrack("Overlay");
    require(add_track.changed() && track_session.timeline().trackCount() == 2,
            "The Edit controller did not add a video track.");
    const auto added_track_id = track_session.timeline().tracks().back().track_id;
    const auto invalid_rename = track_controller.renameTrack(added_track_id, "  ");
    require(invalid_rename.status == application::EditStatus::Rejected &&
                track_commands.undoCount() == 1,
            "A rejected track rename changed the shared history.");
    const auto rename_track = track_controller.renameTrack(added_track_id, "Titles");
    const auto move_track = track_controller.moveTrack(added_track_id, 0);
    const auto remove_track = track_controller.removeTrack(added_track_id);
    require(rename_track.changed() && move_track.changed() && remove_track.changed() &&
                track_session.timeline().trackCount() == 1 &&
                track_commands.undoCount() == 4 && committed_track_edits == 4,
            "Track editing did not preserve shared state, history, or commit signals.");

    application::EditorSession clip_session;
    application::TimelineCommandService clip_commands(clip_session);
    ui::EditWorkspaceController clip_controller(clip_session, clip_commands);
    int committed_clip_edits = 0;
    QObject::connect(
        &clip_controller,
        &ui::EditWorkspaceController::timelineEditCommitted,
        [&committed_clip_edits](
            const application::TimelineEditResult&,
            bool,
            bool,
            const QString&) {
            ++committed_clip_edits;
        });
    const auto clip_track_id = clip_session.timeline().tracks().front().track_id;
    const auto initial_clip = clip_controller.execute(application::AddTextClipCommand{
        clip_track_id, 0, 90, 30.0});
    const auto clip_id = initial_clip.affected_clip_ids.front();
    clip_controller.moveClip(clip_id, clip_track_id, 120);
    require(clip_session.timeline().tracks().front().clips.front().timeline_start_frame == 120 &&
                clip_commands.undoCount() == 2 && committed_clip_edits == 1,
            "A Timeline move request did not commit through the Edit controller.");
    clip_controller.moveClip(clip_id, clip_track_id, 120);
    require(clip_commands.undoCount() == 2 && committed_clip_edits == 1,
            "A no-op Timeline move created a project edit or history entry.");

    application::EditorSession interaction_session;
    application::TimelineCommandService interaction_commands(interaction_session);
    ui::EditWorkspaceController interaction_controller(
        interaction_session, interaction_commands);
    int requested_seeks = 0;
    int committed_inspector_edits = 0;
    qint64 requested_seek_frame = -1;
    QObject::connect(
        &interaction_controller,
        &ui::EditWorkspaceController::timelineEditCommitted,
        [&committed_inspector_edits](
            const application::TimelineEditResult&,
            bool,
            bool,
            const QString&) {
            ++committed_inspector_edits;
        });
    QObject::connect(
        &interaction_controller,
        &ui::EditWorkspaceController::seekTimelineRequested,
        [&requested_seeks, &requested_seek_frame](qint64 frame) {
            ++requested_seeks;
            requested_seek_frame = frame;
        });
    const auto interaction_track =
        interaction_session.timeline().tracks().front().track_id;
    const auto editable_text = interaction_controller.execute(
        application::AddTextClipCommand{interaction_track, 0, 90, 30.0});
    require(editable_text.changed(),
            "The controller test could not create an Inspector test clip.");
    const auto editable_clip = editable_text.affected_clip_ids.front();
    interaction_controller.handleTimelineClipSelectionChanged(
        interaction_track, editable_clip);
    const auto history_before_empty_inspector_command =
        interaction_commands.undoCount();
    interaction_controller.applyTransformProperty(0, 0.25);
    require(interaction_commands.undoCount() ==
                history_before_empty_inspector_command + 1 &&
                committed_inspector_edits == 1,
            "An Inspector transform request did not use shared project history.");
    interaction_controller.toggleTransformKeyframe(0);
    require(interaction_commands.undoCount() ==
                history_before_empty_inspector_command + 2 &&
                committed_inspector_edits == 2,
            "An Inspector keyframe request did not use shared project history.");

    interaction_controller.setPlaybackPresentation(false, false, true);
    interaction_controller.handleTimelineSeekStarted();
    interaction_controller.handleTimelineSeek(75);
    require(requested_seeks == 1 && requested_seek_frame == 75,
            "Timeline seek did not cross the typed playback boundary.");
    interaction_controller.handleTimelineSeekResult(
        75, playback::PlaybackCommandResult::Applied);
    require(interaction_session.playheadFrame() == 75,
            "The applied Timeline seek did not update the shared playhead.");

    interaction_controller.moveActiveTimelineClip(1);
    require(interaction_session.timeline().tracks().front().clips.front()
                    .timeline_start_frame == 1,
            "Timeline clip nudge did not execute through the Edit controller.");
    interaction_controller.handleTimelineClipSplit(editable_clip, 45);
    require(interaction_session.timeline().clipCount(0) == 2 &&
                interaction_commands.undoCount() ==
                    history_before_empty_inspector_command + 4,
            "Timeline split did not update the shared model and history.");
    interaction_controller.deleteActiveTimelineClip();
    require(interaction_session.timeline().clipCount(0) == 1,
            "Deleting the selected text clip did not go through the controller.");
    interaction_controller.clearTimeline();
    require(!interaction_session.timeline().hasClip(),
            "Clearing the Timeline did not go through the controller.");

    application::EditorSession no_selection_session;
    application::TimelineCommandService no_selection_commands(no_selection_session);
    ui::EditWorkspaceController no_selection_controller(
        no_selection_session, no_selection_commands);
    no_selection_controller.deleteActiveTimelineClip();
    no_selection_controller.moveActiveTimelineClip(1);
    no_selection_controller.splitActiveClipAtPlayhead();
    const auto missing_media = no_selection_controller.addMediaClip(
        std::filesystem::temp_directory_path() / "not-imported.mkv",
        no_selection_session.timeline().tracks().front().track_id,
        0);
    require(no_selection_commands.undoCount() == 0 &&
                !no_selection_session.projectDirty() &&
                missing_media.reason == application::EditReason::MediaNotFound,
            "An Edit action without a selection changed the project or history.");

    application::EditorSession effect_drop_session;
    application::TimelineCommandService effect_drop_commands(effect_drop_session);
    ui::EditWorkspaceController effect_drop_controller(
        effect_drop_session, effect_drop_commands);
    const auto effect_drop_track =
        effect_drop_session.timeline().tracks().front().track_id;
    effect_drop_controller.handleTimelineEffectDrop(
        QStringLiteral("unsupported.effect"), effect_drop_track, 0);
    require(!effect_drop_session.timeline().hasClip() &&
                effect_drop_commands.undoCount() == 0,
            "An unsupported effect drop changed the Timeline.");
    effect_drop_controller.handleTimelineEffectDrop(
        QStringLiteral("text.text"), effect_drop_track, 0);
    require(effect_drop_session.timeline().clipCount(0) == 1 &&
                effect_drop_session.timeline().tracks().front().clips.front().kind ==
                    timeline::ClipKind::Text &&
                effect_drop_commands.undoCount() == 1,
            "A text effect drop did not create its clip through the Edit controller.");
    effect_drop_controller.handleTimelineEffectDrop(
        QStringLiteral("text.text"), effect_drop_track, 0);
    require(effect_drop_session.timeline().clipCount(0) == 1 &&
                effect_drop_commands.undoCount() == 1,
            "An occupied text drop changed the project or its history.");
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    try {
        run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
