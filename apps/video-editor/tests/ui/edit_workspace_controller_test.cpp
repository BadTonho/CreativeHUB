#include "application/editor_session.h"
#include "application/media_controller.h"
#include "application/timeline_command_service.h"
#include "workspaces/edit/controllers/edit_workspace_controller.h"

#include <creative_suite/effects/effects.h>

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

    const auto visual_clip_id = added_media.affected_clip_ids.front();
    bool effects_target_available = false;
    QObject::connect(
        &media_drop_controller,
        &ui::EditWorkspaceController::effectTargetAvailabilityChanged,
        [&effects_target_available](bool available) {
            effects_target_available = available;
        });
    media_drop_controller.handleTimelineClipSelectionChanged(
        media_drop_track, visual_clip_id);
    require(media_drop_controller.selectedClipSupportsEffects() &&
                effects_target_available,
            "Selecting a video clip did not enable visual filters.");
    media_drop_controller.handleTimelineEffectDrop(
        QStringLiteral("video.grayscale"), media_drop_track, 12);
    media_drop_controller.addEffectToSelectedClip(QStringLiteral("video.brightness"));
    auto effect_stack = media_drop_session.timeline().tracks().front().clips.front().effects;
    require(effect_stack.size() == 2 &&
                effect_stack[0].id == "video.grayscale" &&
                effect_stack[1].id == "video.brightness" &&
                effect_stack[0].enabled && effect_stack[1].enabled,
            "Effects-panel drop and Functions application did not append filters to the selected clip.");

    media_drop_controller.selectClipEffect(0);
    const auto history_before_effect_toggle = media_drop_commands.undoCount();
    media_drop_controller.setSelectedClipEffectEnabled(0, false);
    effect_stack = media_drop_session.timeline().tracks().front().clips.front().effects;
    require(!effect_stack[0].enabled && effect_stack[1].enabled &&
                media_drop_commands.undoCount() == history_before_effect_toggle + 1,
            "Disabling one effect did not create exactly one history command or preserve its stack position.");
    require(media_drop_controller.undo().changed() &&
                media_drop_session.timeline().tracks().front().clips.front().effects[0].enabled &&
                media_drop_controller.redo().changed() &&
                !media_drop_session.timeline().tracks().front().clips.front().effects[0].enabled,
            "Effect enable state did not participate in Undo/Redo.");

    const auto history_before_effect_parameter = media_drop_commands.undoCount();
    media_drop_controller.beginEffectEdit();
    media_drop_controller.applySelectedClipEffectParameter(55.0);
    media_drop_controller.applySelectedClipEffectParameter(35.0);
    media_drop_controller.finishEffectEdit();
    effect_stack = media_drop_session.timeline().tracks().front().clips.front().effects;
    require(!effect_stack[0].enabled && effect_stack[0].parameters.front().value == 35.0 &&
                media_drop_commands.undoCount() == history_before_effect_parameter + 1,
            "Inspector parameter edits did not remain available while disabled or use one grouped history entry.");

    media_drop_controller.moveSelectedClipEffect(1);
    effect_stack = media_drop_session.timeline().tracks().front().clips.front().effects;
    require(effect_stack[0].id == "video.brightness" &&
                effect_stack[1].id == "video.grayscale",
            "Inspector reordering did not preserve the requested effect order.");
    media_drop_controller.removeSelectedClipEffect();
    require(media_drop_session.timeline().tracks().front().clips.front().effects.size() == 1 &&
                media_drop_commands.undo().changed() &&
                media_drop_session.timeline().tracks().front().clips.front().effects.size() == 2 &&
                media_drop_commands.redo().changed() &&
                media_drop_session.timeline().tracks().front().clips.front().effects.size() == 1,
            "Inspector effect removal did not participate in Undo/Redo.");

    auto copied_stack = std::vector<creative_suite::effects::EffectInstance>{
        creative_suite::effects::makeDefaultInstance("video.grayscale"),
        creative_suite::effects::makeDefaultInstance("video.saturation"),
        creative_suite::effects::makeDefaultInstance("video.saturation")};
    require(creative_suite::effects::setParameterValue(
                copied_stack[0], "amount", 73.0) &&
                creative_suite::effects::setParameterValue(
                    copied_stack[1], "amount", 125.0) &&
                creative_suite::effects::setParameterValue(
                    copied_stack[2], "amount", 42.0),
            "The effect-copy fixture could not prepare non-default parameters.");
    copied_stack[0].enabled = false;
    copied_stack[2].enabled = false;
    const auto source_clip_id = added_media.affected_clip_ids.front();
    require(media_drop_controller.execute(application::SetClipEffectsCommand{
                source_clip_id, copied_stack}).changed(),
            "The effect-copy fixture could not set its source stack.");

    const auto added_effect_target_track = media_drop_controller.addTrack(
        "Effects destination");
    require(added_effect_target_track.changed(),
            "The effect-copy fixture could not add a destination track.");
    const auto effect_target_track_id =
        added_effect_target_track.affected_track_ids.front();
    const auto added_effect_target = media_drop_controller.addMediaClip(
        online_path, effect_target_track_id, 12);
    require(added_effect_target.changed(),
            "The effect-copy fixture could not add a destination clip.");
    const auto target_clip_id = added_effect_target.affected_clip_ids.front();
    const auto alternate_path = std::filesystem::temp_directory_path() /
        ("edit-workspace-alternate-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".mkv");
    auto alternate_metadata = online_metadata;
    alternate_metadata.source_path = alternate_path;
    require(media_drop_library.commitImported({
                alternate_metadata, {}, "Alternate media", "Unsorted", false}).changed(),
            "The test could not register alternate media for selection-independent copy.");
    const auto alternate_track = media_drop_controller.addTrack("Media selection fixture");
    require(alternate_track.changed(),
            "The test could not add a separate Timeline track for the alternate selection.");
    const auto alternate_clip = media_drop_controller.addMediaClip(
        alternate_path, alternate_track.affected_track_ids.front(), 0);
    require(alternate_clip.changed(),
            "The test could not add alternate media to distinguish panel selection from Timeline selection.");
    require(media_drop_controller.execute(application::SetTransformPropertyCommand{
                alternate_clip.affected_clip_ids.front(),
                timeline::TransformProperty::PositionX, 0, 0.95}).changed(),
            "The test could not distinguish the alternate media's attributes.");
    auto target_effects = std::vector<creative_suite::effects::EffectInstance>{
        creative_suite::effects::makeDefaultInstance("video.brightness"),
        creative_suite::effects::makeDefaultInstance("video.contrast")};
    require(creative_suite::effects::setParameterValue(
                target_effects[0], "amount", -25.0) &&
                creative_suite::effects::setParameterValue(
                    target_effects[1], "amount", 160.0),
            "The effect-copy fixture could not prepare the destination stack.");
    target_effects[1].enabled = false;
    require(media_drop_controller.execute(application::SetClipEffectsCommand{
                target_clip_id, target_effects}).changed(),
            "The effect-copy fixture could not set the destination stack.");

    require(media_drop_controller.execute(application::SetTransformPropertyCommand{
                source_clip_id, timeline::TransformProperty::PositionX, 0, 0.25}).changed() &&
                media_drop_controller.execute(application::ToggleTransformKeyframeCommand{
                    source_clip_id, timeline::TransformProperty::PositionX, 120}).changed() &&
                media_drop_controller.execute(application::SetTransformPropertyCommand{
                    source_clip_id, timeline::TransformProperty::PositionX, 120, 0.75}).changed() &&
                media_drop_controller.execute(application::SetClipAudioCommand{
                    source_clip_id, 0.5, true}).changed() &&
                media_drop_controller.execute(application::SetTransformPropertyCommand{
                    target_clip_id, timeline::TransformProperty::PositionX, 0, 0.9}).changed() &&
                media_drop_controller.execute(application::SetClipAudioCommand{
                    target_clip_id, 1.5, false}).changed(),
            "The clip-attribute fixture could not prepare visual and audio values.");

    media_drop_controller.handleTimelineClipSelectionChanged(media_drop_track, source_clip_id);
    require(media_drop_controller.canCopySelectedClipAttributes() &&
                !media_drop_controller.canPasteCopiedClipAttributes(),
            "Copy Attributes must be available for any selected Timeline clip before copying.");
    const auto history_before_copy = media_drop_commands.undoCount();
    const auto dirty_before_copy = media_drop_session.projectDirty();
    media_drop_controller.copySelectedClipAttributes();
    require(media_drop_commands.undoCount() == history_before_copy &&
                media_drop_session.projectDirty() == dirty_before_copy &&
                media_drop_controller.canPasteCopiedClipAttributes(),
            "Copying clip attributes must fill the in-memory clipboard without editing the project.");

    media_drop_session.selectionForUi().selected_source_path = alternate_path;
    media_drop_controller.copySelectedClipAttributes();
    media_drop_controller.handleTimelineClipSelectionChanged(
        effect_target_track_id, target_clip_id);
    require(media_drop_controller.applyCopiedClipAttributes(
                timeline::ClipAttributeOptions{true, false, false, false, false}).changed() &&
                media_drop_session.timeline().tracks()[
                    media_drop_session.timeline().locateClip(target_clip_id)->track_index]
                    .clips[media_drop_session.timeline().locateClip(target_clip_id)->clip_index]
                    .effects == copied_stack,
            "Copy Attributes followed Media Browser selection instead of the selected Timeline clip.");
    require(media_drop_controller.undo().changed(),
            "The selection-independent copy fixture could not restore the destination stack.");
    media_drop_controller.handleTimelineClipSelectionChanged(media_drop_track, source_clip_id);
    media_drop_controller.copySelectedClipAttributes();

    media_drop_controller.handleTimelineClipSelectionChanged(
        effect_target_track_id, target_clip_id);
    const auto history_before_paste = media_drop_commands.undoCount();
    const auto pasted_visual = media_drop_controller.applyCopiedClipAttributes(
        timeline::ClipAttributeOptions{true, true, true, false, false});
    auto target_location = media_drop_session.timeline().locateClip(target_clip_id);
    require(pasted_visual.changed() && target_location.has_value() &&
                media_drop_session.timeline().tracks()[target_location->track_index]
                        .clips[target_location->clip_index].effects == copied_stack &&
                media_drop_session.timeline().tracks()[target_location->track_index]
                        .clips[target_location->clip_index].transform.position_x == 0.25 &&
                media_drop_session.timeline().tracks()[target_location->track_index]
                        .clips[target_location->clip_index].keyframes.position_x.size() == 1 &&
                media_drop_session.timeline().tracks()[target_location->track_index]
                        .clips[target_location->clip_index].keyframes.position_x.front().value == 0.75 &&
                media_drop_session.timeline().tracks()[target_location->track_index]
                        .clips[target_location->clip_index].audio_gain == 0.5 &&
                media_drop_session.timeline().tracks()[target_location->track_index]
                        .clips[target_location->clip_index].audio_muted &&
                media_drop_commands.undoCount() == history_before_paste + 1,
            "Paste Attributes did not apply the selected visual and audio groups as one edit.");
    require(media_drop_controller.undo().changed() &&
                media_drop_session.timeline().tracks()[target_location->track_index]
                        .clips[target_location->clip_index].effects == target_effects &&
                media_drop_session.timeline().tracks()[target_location->track_index]
                        .clips[target_location->clip_index].transform.position_x == 0.9 &&
                media_drop_session.timeline().tracks()[target_location->track_index]
                        .clips[target_location->clip_index].audio_gain == 1.5 &&
                media_drop_controller.redo().changed() &&
                media_drop_session.timeline().tracks()[target_location->track_index]
                        .clips[target_location->clip_index].effects == copied_stack,
            "Pasting clip attributes did not restore and reapply all selected groups through Undo/Redo.");

    media_drop_controller.handleTimelineClipSelectionChanged(media_drop_track, source_clip_id);
    require(media_drop_controller.execute(application::SetClipEffectsCommand{
                source_clip_id, {}}).changed(),
            "The empty effects source fixture could not be prepared.");
    media_drop_controller.copySelectedClipAttributes();
    media_drop_controller.handleTimelineClipSelectionChanged(
        effect_target_track_id, target_clip_id);
    require(media_drop_controller.applyCopiedClipAttributes(
                timeline::ClipAttributeOptions{true, false, false, false, false}).changed() &&
                media_drop_session.timeline().tracks()[target_location->track_index]
                    .clips[target_location->clip_index].effects.empty(),
            "Pasting an empty effects group did not clear the destination stack.");

    const auto incompatible_text = media_drop_controller.execute(
        application::AddTextClipCommand{effect_target_track_id, 700, 90, 30.0});
    require(incompatible_text.changed(),
            "The attribute-copy fixture could not add an incompatible text clip.");
    media_drop_controller.handleTimelineClipSelectionChanged(
        effect_target_track_id, incompatible_text.affected_clip_ids.front());
    const auto history_before_rejected_paste = media_drop_commands.undoCount();
    const auto rejected_paste = media_drop_controller.applyCopiedClipAttributes(
        timeline::ClipAttributeOptions{true, false, false, false, false});
    require(rejected_paste.status == application::EditStatus::Rejected &&
                media_drop_commands.undoCount() == history_before_rejected_paste,
            "The command service accepted effects on an incompatible text clip.");

    const auto source_text = media_drop_controller.execute(
        application::AddTextClipCommand{effect_target_track_id, 820, 90, 30.0});
    const auto target_text = media_drop_controller.execute(
        application::AddTextClipCommand{effect_target_track_id, 940, 90, 30.0});
    require(source_text.changed() && target_text.changed(),
            "The attribute-copy fixture could not create two text clips.");
    timeline::TextStyle source_style;
    source_style.content = "Copied title";
    source_style.font_size_pixels = 80.0;
    timeline::TextStyle target_style;
    target_style.content = "Original title";
    target_style.font_size_pixels = 24.0;
    require(media_drop_controller.execute(application::SetClipTextCommand{
                source_text.affected_clip_ids.front(), source_style}).changed() &&
                media_drop_controller.execute(application::SetClipTextCommand{
                    target_text.affected_clip_ids.front(), target_style}).changed(),
            "The text attribute fixture could not set source and destination styles.");
    media_drop_controller.handleTimelineClipSelectionChanged(
        effect_target_track_id, source_text.affected_clip_ids.front());
    media_drop_controller.copySelectedClipAttributes();
    media_drop_controller.handleTimelineClipSelectionChanged(
        effect_target_track_id, target_text.affected_clip_ids.front());
    require(media_drop_controller.applyCopiedClipAttributes(
                timeline::ClipAttributeOptions{false, false, false, false, true}).changed(),
            "Text attributes could not be pasted between text clips.");
    const auto target_text_location = media_drop_session.timeline().locateClip(
        target_text.affected_clip_ids.front());
    require(target_text_location.has_value() &&
                media_drop_session.timeline().tracks()[target_text_location->track_index]
                    .clips[target_text_location->clip_index].text == source_style,
            "Paste Attributes did not transfer text content and formatting.");

    const auto history_before_occupied_drop = media_drop_commands.undoCount();
    const auto committed_before_occupied_drop = committed_media_drops;
    const auto occupied_media = media_drop_controller.addMediaClip(
        online_path, media_drop_track, 30);
    const auto source_location_after_rejected_drop =
        media_drop_session.timeline().locateClip(source_clip_id);
    require(!occupied_media.changed() &&
                occupied_media.reason == application::EditReason::Overlap &&
                source_location_after_rejected_drop.has_value() &&
                media_drop_session.timeline().clipCount(
                    source_location_after_rejected_drop->track_index) == 1 &&
                media_drop_commands.undoCount() == history_before_occupied_drop &&
                committed_media_drops == committed_before_occupied_drop,
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
    require(add_track.changed() && track_session.timeline().trackCount() == 3,
            "The Edit controller did not add a video track.");
    const auto added_track_id = add_track.affected_track_ids.front();
    const auto invalid_rename = track_controller.renameTrack(added_track_id, "  ");
    require(invalid_rename.status == application::EditStatus::Rejected &&
                track_commands.undoCount() == 1,
            "A rejected track rename changed the shared history.");
    const auto rename_track = track_controller.renameTrack(added_track_id, "Titles");
    const auto move_track = track_controller.moveTrack(added_track_id, 1);
    const auto remove_track = track_controller.removeTrack(added_track_id);
    require(rename_track.changed() && move_track.changed() && remove_track.changed() &&
                track_session.timeline().trackCount() == 2 &&
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
