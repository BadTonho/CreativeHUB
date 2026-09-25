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
    const auto offline_add = controller.execute(
        application::AddMediaClipCommand{offline_path, track_id, 300});
    require(offline_add.reason == application::EditReason::OfflineMedia &&
                session.timeline().clipCount(0) == 1 && command_service.undoCount() == 1,
            "Offline media changed the timeline or its history.");

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
