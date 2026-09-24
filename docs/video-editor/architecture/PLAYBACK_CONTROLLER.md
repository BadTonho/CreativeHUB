# Playback Controller

Status: **provisional**. This boundary is the Stage 6 implementation recorded in the [refactoring risk audit](REFACTORING_RISK_AUDIT.md).

## Ownership

`playback::PlaybackController` is a Qt Core application boundary with no dependency on widgets. It owns the playback thread and worker lifetime, the active generation, pending clip activation, composition snapshots, and the one-slot latest-frame mailbox. It reads the timeline, media library, stable selection, and playhead from `application::EditorSession`.

The controller accepts typed commands for clip activation, play, pause, frame stepping, timeline seeking, composition refresh, audio parameters, monitor volume, invalidation, and shutdown. Timeline clips are addressed by `ClipId` in the application boundary. Track and clip vector indexes are derived only when building the current worker adapter request.

`PlaybackWorker` remains the thread-bound execution adapter. Video decoding, audio playback, composition, transitions, and pacing continue to use the existing media and rendering components. The worker reports errors and state through its existing typed signals; `PlaybackController` rejects events from older generations before delivering application events.

## Event and thread flow

Worker state and completion signals are queued to the controller's UI-thread affinity. Frame signals use a direct connection only to publish a shared frame pointer into `PlaybackFrameMailbox`; a queued controller callback drains the latest packet. The controller checks the generation both before publishing and before emitting a frame event, so an old frame cannot replace a newer preview after an activation or edit.

The controller validates pending activation against the current `ClipId`, canonical media path, and online library item before committing it. It updates the session playhead and stable selection, then emits a typed event. `MainWindow` projects accepted events to the preview, timeline, media browser, playback controls, status bar, and technical error log. It keeps only a loading presentation flag derived from activation events; pending activation details remain in the controller.

Generation-tagged composition, media, render, and seek requests retain the generation captured when the request is queued. Debug checks verify this association at the worker boundary; events and frames from older generations are discarded before they can update session state or the preview.

On shutdown, the controller stops the worker on its thread, quits and joins the thread, and clears the frame mailbox. Calls made after shutdown are ignored.

## Regression coverage

`creative-suite-main-editor-playback-controller` runs without constructing `MainWindow` and uses a substitutable worker factory to exercise controller commands and delayed worker events. `creative-suite-main-editor-main-window` verifies that an accepted activation updates the visible timeline and media-browser selection. Existing playback worker tests continue to cover decoding, audio, composition, and pacing.

## Manual validation

1. Open a project with adjacent video clips and play through the clip boundary. Confirm the preview and playhead continue into the next clip.
2. While playback is active, seek rapidly between clips and edit the selected clip. Confirm a late frame or completion from an earlier activation does not replace the current preview.
3. Repeat with text and image clips, including a gap in the timeline. Confirm the preview follows the visible composition and reports the gap.
4. Close the editor during playback. Confirm shutdown completes without leaving audio, preview updates, or a worker thread active.
