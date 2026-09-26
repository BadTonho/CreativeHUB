# Playback Controller

Status: **provisional**. This boundary is the Stage 6 implementation recorded in the [refactoring risk audit](REFACTORING_RISK_AUDIT.md).

## Ownership

`playback::PlaybackController` is a Qt Core application boundary with no dependency on widgets. It owns the playback thread and worker lifetime, the active generation, pending clip activation, composition snapshots, and the one-slot latest-frame mailbox. It reads the timeline, media library, stable selection, and playhead from `application::EditorSession`.

The controller accepts typed commands for clip activation, play, pause, frame stepping, timeline seeking, composition refresh, audio parameters, monitor volume, invalidation, and shutdown. Timeline clips are addressed by `ClipId` in the application boundary. Track and clip vector indexes are derived only when building the current worker adapter request.

`PlaybackWorker` remains the thread-bound execution adapter. Video decoding, audio playback, composition, transitions, and pacing continue to use the existing media and rendering components. A lightweight precise timer on the controller's UI thread advances a monotonic global Timeline frame using the project's persisted rational Timeline rate (30/1 FPS for new projects). The worker uses the same global frame domain and the full composition snapshot's global start/end bounds for composed playback. Activating another clip passes the current global Timeline frame; the worker derives that clip's local frame only for source-frame and audio-sample mapping. The end of a short text, image, or video layer therefore cannot stop the composed clock while other layers continue. Each composed video layer maps its local Timeline frame to a source frame using that clip's source rate and in-point. Standalone media playback continues to use the source rate. The worker may lag or skip preview frames, but media activation does not reset or change the Timeline clock.

## Event and thread flow

Worker state and completion signals are queued to the controller's UI-thread affinity. Frame signals use a direct connection only to publish a shared frame pointer into `PlaybackFrameMailbox`; a queued controller callback drains the latest packet. The controller checks the generation both before publishing and before emitting a frame event, so an old frame cannot replace a newer preview after an activation or edit.

The controller validates pending activation against the current `ClipId`, canonical media path, and online library item before committing it. It updates the session playhead and stable selection, then emits a typed event. `MainWindow` projects accepted events to the preview, timeline, media browser, playback controls, status bar, and technical error log. It keeps only a loading presentation flag derived from activation events; pending activation details remain in the controller.

Generation-tagged media, render, and seek requests are rejected when an activation or edit supersedes them. Composition snapshots have their own revision: a queued snapshot remains reusable across clip activation generations, but a newer edit invalidates it. The worker preserves its prepared composition decoder sessions during ordinary media changes and switches the active composition clip without reopening every layer. In composed playback, those prepared sessions supply video frames; a separate direct-decoding session is not required. Events and frames from older generations are discarded before they can update session state or the preview.

When the clock enters another clip, the controller requests activation while keeping `Playing` active and updating the global playhead. The worker uses the matching prepared decoder from the current composition snapshot, or opens the source for direct playback when composition is unavailable, then seeks to the latest local frame before committing the first frame and resuming worker playback. In composed mode, seeks and clip activation update the global frame without changing the composition-wide playback bounds; the active layer's duration is not a playback endpoint. The controller remains responsible for Timeline gaps and the project end. Frames behind the clock are discarded; pending seeks are coalesced to the current position. Pause, Stop, timeline seek, and project invalidation stop the clock and cancel pending activations. Per-clip and per-track audio gain and mute values are reapplied on media activation.

On shutdown, the controller stops the worker on its thread, quits and joins the thread, and clears the frame mailbox. Calls made after shutdown are ignored.

## Regression coverage

`creative-suite-main-editor-playback-controller` runs without constructing `MainWindow` and uses a substitutable worker factory to exercise controller commands, delayed activation, a nonzero source in-point, global-frame propagation across a cut, seek-to-current-position before resuming, and dirty-state preservation. `creative-suite-main-editor-playback-worker-errors` also runs a deterministic 491-frame composition with a 150-frame text layer beginning at frame 294, switches the active clip during playback, and verifies that the worker reaches the composition end rather than finishing at the text layer's end. `creative-suite-main-editor-main-window` verifies that an accepted activation updates the visible timeline and media-browser selection. Existing playback worker tests continue to cover decoding, audio, composition, and pacing.

## Manual validation

1. Open a project with adjacent video clips, including a trimmed source in-point, and play through the cut. Confirm the Timeline playhead moves continuously while the next source opens, then the preview seeks to the current position and playback resumes without restarting at source frame zero.
2. Repeat with a deliberately slow-to-open or large next clip. Confirm the playhead continues, stale frames are skipped, and the last valid preview frame may remain briefly while loading.
3. While playback is active, seek rapidly between clips and edit the selected clip. Confirm a late frame or completion from an earlier activation does not replace the current preview. Pause or Stop during a pending activation and confirm playback remains stopped; Play retries the selected clip from its current position.
4. Play a video with a text overlay that starts around Timeline frame 294 and lasts about 150 frames. Confirm the underlying video continues changing through the full text clip and after it ends; the worker must not stop or jump to the text clip's final frame. Repeat with image clips and a gap in the Timeline. Confirm the preview follows the visible composition and the controller reports the gap.
5. Close the editor during playback. Confirm shutdown completes without leaving audio, preview updates, or a worker thread active.
