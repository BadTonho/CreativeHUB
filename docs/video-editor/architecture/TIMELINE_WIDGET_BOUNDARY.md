# Timeline Widget Boundary

Status: **provisional**. This document describes the current Timeline widget
boundary and its regression coverage.

## Ownership

`timeline::TimelineGeometry` owns timeline rectangles, frame-to-pixel conversion, and ruler mapping. It projects Video and Audio rows into separate viewports while preserving each kind's model-relative order and applying independent temporary scroll offsets. `TimelineHitTester` resolves pointer positions to tracks, clips, and adjacent transition pairs only inside the matching group viewport. Hit-test results use local vector locations because they describe the current widget projection.

`TimelineDropValidator` checks placement overlap and computes magnetic snapping. `TimelineInteractionController` owns ruler seeking, clip movement, trim, blade, clip seeking, and drag-and-drop preview state. Completed move, split, and seek gestures return typed requests; move requests carry stable `ClipId` and `TrackId` values. The controller has no `MainWindow` dependency and does not mutate the model or history.

`TimelineInteractionPainter` draws move ghosts, media drop ghosts, empty-group drop previews, invalid targets, snap guides, and drop markers from a read-only paint state. `TimelineWidget` retains Qt event delivery, mouse capture, clip and ruler drawing, context menus, the adjustable group divider, independent scroll offsets, and conversion between local hit-test positions and stable IDs. At the default split, pane viewports fit their rows when possible, eliminating unused vertical space between short Video and Audio groups; overflow uses the available height, while an explicitly resized divider honors its saved ratio. Its centered 120-by-18-pixel divider grip scrolls both groups by the same pointer delta with independent range clamping; the outer divider edges resize the panes. The grip and edges expose distinct hover cursors and visual states. Its edit signals use `TrackId` and `ClipId`; empty-group drop requests carry `TrackKind` until the command service creates the first track.

`application::TimelineCommandService` owns timeline mutations for clip operations, tracks, transitions, and inspector values. `MainWindow` routes requests to this service, applies accepted results to selection, playhead, playback composition, history controls, and other widgets, and presents domain rejections. Audio and transform slider previews use edit batches so a completed gesture creates at most one undo entry. `MediaController` rename updates displayed clip labels through the session without adding a timeline history entry.

## Limits

The widget remains a Qt presentation adapter. It still owns Qt mouse and drag event dispatch, drawing of the timeline's tracks and clips, context-menu presentation, and local selection indexes used while painting. The interaction controller receives hit-tested context from the widget and returns requests; it does not own Qt widgets, dialogs, application logging, project serialization, playback workers, or timeline history.

The `.csp` format and keyboard shortcuts are unchanged. The divider ratio is a local `QSettings` preference shared across projects; scroll offsets are not persisted. `TimelineCommandService` stages empty-group track creation and the complete media batch before committing one edit state, so rejection leaves both the track list and history unchanged. Render uses the shared Timeline widget and retains navigation while setting it read-only for clip operations. `EditorSession::legacyTimelineForUi()` remains available for test fixture construction; production application handlers do not use it to mutate the timeline.

## Regression coverage

- `creative-suite-main-editor-timeline-geometry` checks coordinate conversion, ruler bounds, clip and transition hit testing, overlap validation, snapping, and interaction painting without constructing `MainWindow`.
- `creative-suite-main-editor-timeline-interaction-controller` checks move thresholds and stable-ID results, split clicks, seeking, ruler previews, drop preview state, and gesture cleanup.
- `creative-suite-main-editor-timeline-widget` covers mouse and drag integration, snapping, drop behavior, drawing, stable-ID signals, contiguous default layout for short Video/Audio groups, independent group scrolling by scrollbar and wheel, linked center-grip scrolling with per-group clamping and no-overflow groups, scrolled-row hit testing, the expanded divider hit area, grip/edge cursor and visual states, edge resizing and split bounds, and typed drops into empty groups.
- `creative-suite-main-editor-timeline-command-service` covers track and inspector commands, invalid targets and values, no-op behavior, undo batches, playback invalidation, empty-group track creation, incompatible-drop rollback, linked audio companions, and one-step Undo/Redo.
- `creative-suite-main-editor-main-window` verifies that service results update the visible selection, playhead, and timeline projection.
- `creative-suite-main-editor-application-media` verifies that media rename also updates clip labels without timeline history.

## Manual visual validation

1. Open a project with one or two Video and Audio tracks. Confirm the Audio rows sit directly after the Video group instead of leaving a large empty band between them. Add enough tracks to overflow both groups and confirm the default divider splits the usable height evenly. Scroll each pane independently with its scrollbar and the wheel; confirm the matching headers, clips, selection, and edit hit targets stay aligned. Drag the centered grip and confirm both groups move by the same delta, stopping independently at their own limits. Confirm horizontal scrolling, ruler, zoom, and playhead remain shared.
2. Hover the centered grip and confirm its highlight and open-hand cursor; press it and confirm the closed-hand cursor. Hover and drag either divider edge and confirm the resize cursor and pane-height adjustment. Close and reopen the application and confirm the ratio is restored across projects while both panes remain usable. Change track row height and confirm both groups use the same height.
3. Remove all tracks from one group. Confirm its pane and drop hint remain visible, compatible media creates the first track and clip together, incompatible media leaves the group empty, and one Undo/Redo removes or restores the complete operation. Repeat with an external file drop and with video containing audio.
4. Switch to Render with many tracks and an empty group. Confirm both scrollbars, linked center-grip scrolling, and edge resizing still work while clip selection and edits remain blocked.
5. Open a project with two tracks and clips. Drag a clip to another track and confirm its ghost, overlap warning, snap guide, and final placement.
6. Trim both edges of a clip, including a shared cut, then split a clip with the blade tool. Confirm selection and preview follow the edited clip.
7. Drag media from the media browser to an empty position and near an existing clip edge. Confirm duration, snapping, and overlap feedback.
8. Change clip audio and transform sliders, release each gesture, then undo once per gesture. Confirm each gesture produces one undo step.
9. Repeat the checks with text clips, a gap, and a project with multiple tracks.

This manual validation was not performed in the current environment.
