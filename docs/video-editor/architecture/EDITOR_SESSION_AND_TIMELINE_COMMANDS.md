# Editor Session and Timeline Commands

Status: **provisional**. This document records the application boundary introduced by Stages 4 and 5 of the [refactoring risk audit](REFACTORING_RISK_AUDIT.md).

## Ownership

`application::EditorSession` owns the working timeline model and undo history, the media library and bins, stable timeline selection, the playhead, the current project path, the saved project baseline, and the dirty flag. The project file format is unchanged. Project lifecycle, media mutation, asynchronous import, and transactional open are described in [Project and Media Controllers](PROJECT_AND_MEDIA_CONTROLLERS.md).

`MainWindow` composes widgets and dialogs, sends edit requests to application services, and projects accepted results to the interface and `PlaybackController`. Timeline indices remain short-lived presentation coordinates; edit requests identify tracks and clips by stable IDs.

## Command boundary

`application::TimelineCommandService` applies typed commands for clip movement and reorder, split, edge and range trim, deletion, media and text clip addition, transition add, update, and removal, track add, rename, reorder, removal, and clearing, plus inspector edits for audio, text style, transforms, and keyframes. The service resolves IDs, validates requests through the timeline model, updates selection and playhead state, and records history only after an effective mutation.

Results carry a status and domain reason, affected track and clip IDs, the resulting selection and playhead, and whether playback must be invalidated. Expected rejections and no-ops do not create history or log entries. The service has no Qt, widget, dialog, or logger dependency; the application layer handles playback invalidation, UI refresh, and technical error logging.

Undo and redo are coordinated by the service using session snapshots. Slider gestures use edit batches: previews update the model while the gesture is active, then one history entry is committed on release if the value changed. A no-op gesture does not add history.

## Internal invariants

Debug assertions check that track and clip IDs are nonzero and unique, and that each clip's stored track ID matches its owning track after structural timeline edits and snapshot restoration. Session checks verify that selected track and clip IDs resolve consistently and that a selected transition still exists between adjacent clips on its selected track. A selected media path is independent because the media browser can select an item without selecting a timeline clip.

`ProjectController` derives the dirty flag from the mapped project document and its saved baseline. Save completion and dirty-state updates check that relationship. Recovery may temporarily install a document whose saved baseline differs; the UI recomputes the canonical dirty state once presentation state is available.

These assertions diagnose internal programming errors in debug builds. Project files continue to be checked by the project document validator and report invalid IDs as domain errors.

## Related boundaries and limits

`MainWindow` still owns presentation concerns such as dialogs, status messages, widget composition, and translating hit-tested selection into visible controls. `TimelineWidget` emits stable `TrackId` and `ClipId` requests; its indexes are local coordinates for geometry and painting. The explicitly named `EditorSession::legacyTimelineForUi()` accessor remains for test fixture construction, but application handlers no longer use it to mutate the timeline.

Project serialization, parsing, migration, and validation use focused codec modules behind the compatible `project::load/save` API. Media probing, first-frame decoding, asynchronous import, and transactional project preparation run through the Stage 5 services documented in [Project and Media Controllers](PROJECT_AND_MEDIA_CONTROLLERS.md). Timeline component ownership and the manual interaction checklist are documented in [Timeline Widget Boundary](TIMELINE_WIDGET_BOUNDARY.md).
