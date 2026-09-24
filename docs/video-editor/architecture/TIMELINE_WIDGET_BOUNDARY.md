# Timeline Widget Boundary

Status: **provisional**. This document records the Stage 7 implementation in the [refactoring risk audit](REFACTORING_RISK_AUDIT.md).

## Ownership

`timeline::TimelineGeometry` owns timeline rectangles, frame-to-pixel conversion, and ruler mapping. `TimelineHitTester` resolves pointer positions to tracks, clips, and adjacent transition pairs. Hit-test results use local vector locations because they describe the current widget projection.

`TimelineDropValidator` checks placement overlap and computes magnetic snapping. `TimelineInteractionController` owns ruler seeking, clip movement, trim, blade, clip seeking, and drag-and-drop preview state. Completed move, split, and seek gestures return typed requests; move requests carry stable `ClipId` and `TrackId` values. The controller has no `MainWindow` dependency and does not mutate the model or history.

`TimelineInteractionPainter` draws move ghosts, media drop ghosts, invalid targets, snap guides, and drop markers from a read-only paint state. `TimelineWidget` retains Qt event delivery, mouse capture, clip and ruler drawing, context menus, and conversion between local hit-test positions and stable IDs. Its edit signals use `TrackId` and `ClipId`; the former index-based edit signals and their unused `MainWindow` handlers have been removed.

`application::TimelineCommandService` owns timeline mutations for clip operations, tracks, transitions, and inspector values. `MainWindow` routes requests to this service, applies accepted results to selection, playhead, playback composition, history controls, and other widgets, and presents domain rejections. Audio and transform slider previews use edit batches so a completed gesture creates at most one undo entry. `MediaController` rename updates displayed clip labels through the session without adding a timeline history entry.

## Limits

The widget remains a Qt presentation adapter. It still owns Qt mouse and drag event dispatch, drawing of the timeline's tracks and clips, context-menu presentation, and local selection indexes used while painting. The interaction controller receives hit-tested context from the widget and returns requests; it does not own Qt widgets, dialogs, application logging, project serialization, playback workers, or timeline history.

The `.csp` format, keyboard shortcuts, and expected editing behavior are unchanged. `EditorSession::legacyTimelineForUi()` remains available for test fixture construction; production application handlers do not use it to mutate the timeline.

## Regression coverage

- `creative-suite-main-editor-timeline-geometry` checks coordinate conversion, ruler bounds, clip and transition hit testing, overlap validation, snapping, and interaction painting without constructing `MainWindow`.
- `creative-suite-main-editor-timeline-interaction-controller` checks move thresholds and stable-ID results, split clicks, seeking, ruler previews, drop preview state, and gesture cleanup.
- `creative-suite-main-editor-timeline-widget` covers mouse and drag integration, snapping, drop behavior, drawing, and stable-ID signals.
- `creative-suite-main-editor-timeline-command-service` covers track and inspector commands, invalid targets and values, no-op behavior, undo batches, and playback invalidation.
- `creative-suite-main-editor-main-window` verifies that service results update the visible selection, playhead, and timeline projection.
- `creative-suite-main-editor-application-media` verifies that media rename also updates clip labels without timeline history.

## Manual visual validation

1. Open a project with two tracks and clips. Drag a clip to another track and confirm its ghost, overlap warning, snap guide, and final placement.
2. Trim both edges of a clip, including a shared cut, then split a clip with the blade tool. Confirm selection and preview follow the edited clip.
3. Drag media from the media browser to an empty position and near an existing clip edge. Confirm duration, snapping, and overlap feedback.
4. Change clip audio and transform sliders, release each gesture, then undo once per gesture. Confirm each gesture produces one undo step.
5. Repeat the checks with text clips, a gap, and a project with multiple tracks.

This manual validation was not performed in the current environment.
