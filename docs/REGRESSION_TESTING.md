# Regression Testing

This document defines the local regression gate for the Main Editor. Every
implemented rule should have either an automated test or a documented manual
validation step before the related change is considered complete.

## Local gate

From the repository root, run:

```powershell
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
git diff --check
```

The CTest suite is the required automated gate. A failed test blocks the
change until the cause is understood and fixed or the expected behavior is
updated intentionally.

## Automated coverage

| Area | Test coverage |
| --- | --- |
| Structured logging | File creation, required fields, escaping, rotation, retention limit |
| Media probing and decoding | Missing files, invalid inputs, reference metadata, frame dimensions |
| Playback session | Sequential frames, reset, optimized seeking, frame cache, EOF, segment limits |
| Playback worker | Media activation, generation handling, seek coalescing, playback completion, composition playback without a selected Media Browser source, errors, and no-op seeks without a selected source |
| Timeline model | Tracks, ordering, gaps, overlap rules, movement, split, trim, delete, metadata, history |
| Timeline interaction | Selection without playhead jumps, optional move-to-start selection preference, seek-on-release, configurable clip movement, Blade Tool, trim-on-release, smooth upper-ruler playhead scrubbing, global-to-local seek conversion, stable one-hour horizontal scale, long-content expansion, timecode ruler, discrete timeline zoom, Ctrl + wheel behavior, coordinate anchoring, and viewport-width updates |
| System memory indicator | Deterministic byte-to-MB conversion, rounding, process-memory formatting, zero/invalid handling, and `RAM: N/A` fallback |
| System memory details | Offscreen non-modal dialog, System Memory and Main Editor sections, click-to-open behavior, Working Set, Private Usage, GB/MB formatting, and per-metric `N/A` handling |
| Transform Inspector | Slider and numeric-field synchronization, transform ranges, keyframe-aware edits, live preview updates, and one coalesced history entry per slider drag |
| Inspector audio tabs | Audio tab organization, Clip and Track volume/mute controls, disabled state without a valid video clip, and preserved audio edit behavior |
| Settings dialog | Modal shell, General, Timeline, and Shortcuts tabs, Close action, independent component construction, and editable shortcut preferences |
| Shortcut manager | QAction registration and application, QSettings persistence, empty assignments, duplicate blocking, individual reset, and Reset All |
| Project persistence | Versioned JSON, round-trip, timeline zoom persistence, version 1-5 migration, invalid input, offline media, transactional open |
| Media Browser model | Canonical duplicates, bins, rename, offline and restore behavior |
| Media Browser UI | Media Pool grouping with independent Bins and Media docks, native workspace layout persistence, list/block modes, global mode persistence, compact item data, cached thumbnail retention, technical-information role, and preserved selection/drag metadata |
| Media Browser bin organization | Contextual bin creation, media-to-bin drops, bin subtree reparenting, empty-bin preservation, invalid destination rejection, and project bin synchronization |
| Effects UI | Implemented Toolbox categories, current effect catalog, category filtering, stable effect IDs, and visual-only behavior |
| Preview | CPU fallback, valid and invalid frames, resize, grayscale, clean shutdown |

## Manual UI validation

Automated tests do not replace visual validation. The following must be checked
in the running Main Editor after UI or integration changes:

- application startup and clean shutdown;
- Settings action: confirm the menu-bar action immediately left of `Help`
  opens a modal dialog with `General`, `Timeline`, and `Shortcuts` tabs, closes
  without changing project dirty state, and leaves the existing Edit menu
  preferences available;
- Settings > Shortcuts: edit a shortcut, confirm it applies immediately and
  persists after reopening the editor, clear a shortcut to disable it, confirm
  duplicate combinations are rejected and the previous value is restored, and
  validate both individual `Reset` and confirmed `Reset All`;
- dock resizing, floating, re-docking, and restoration;
- Timeline: confirm the dock shows only its official Timeline title, without
  a duplicate internal title or the former Click to select interaction hint,
  while the playback controls, ruler, clips, and footer remain available;
  confirm Previous Frame, Play/Pause, and Next Frame show only media icons,
  update the Play/Pause icon correctly, and retain working tooltips; confirm
  the mouse Selection Tool icon is checked initially, the Blade Tool is an
  icon-only mutually exclusive mode, and both accessible names and tooltips
  remain available; confirm no Add Text button is shown;
- Timeline track height, maximum row height, vertical scrolling, stable
  one-hour horizontal scale, horizontal scrolling for longer content, zoom
  controls from 25% through 800%, Ctrl + wheel playhead anchoring, button
  playhead anchoring, timecode labels in `HH:MM:SS.mmm`, click-and-drag
  playhead scrubbing on the upper time ruler, empty gaps without overlays,
  no Media Browser selection while scrubbing, selecting a clip without moving
  the playhead, and the optional Edit > Move Playhead to Selected Clip Start
  preference, and visual order;
- Media Browser list/block toggles, restoration of the last global mode,
  cached thumbnails, compact item descriptions, hover over the information
  icon for the complete technical tooltip, selection, clicking bins without
  losing the selected path or expanded branches, bins, context actions,
  folder items shown alongside media, folder icons, inline renaming with
  double-click and F2, automatic New Bin naming without dialogs through the
  context menu, protection of
  All Media/Unsorted, and preventing folder items from producing media drag data,
  the `Media Pool` text button below the menu bar next to the `Effects` group
  button, switching between the Media Pool and Effects groups, the `Media Pool`
  submenu with independent `Bins` and `Media` actions, moving, resizing,
  floating, closing, re-docking, and tabifying each dock,
  restoring both docks through `View > Media Pool`, restoration of the
  complete workspace layout after restarting the editor, and `View > Restore
  Default Layout` without marking the project dirty,
  offline media, right-click New Bin in the media area and bin tree, creation
  of child bins, dragging media to bins, dragging bins into bins, preservation
  of empty sub-bins, rejection of All Media/blank/self/descendant/collision
  drops, project dirty state, persistence after save/reopen, and drag-and-drop
  to the Timeline; confirm that invalid inline names restore the previous
  label and report a concise status message, and confirm that no Add to
  Timeline or New Bin buttons, redundant status row, or excessive top/bottom
  spacing is shown while media drag-and-drop remains available; confirm that
  branch lines make nested bins visually distinguishable at one or more levels;
- Effects workspace: confirm that `Toolbox` and `Favorites` appear as a
  vertical pair in the left column, with `Favorites` below `Toolbox`, and that
  `Effects` is beside them while `Bins` and `Media` are hidden; confirm all
  three effects docks can be moved, resized, floated, closed, re-docked, and
  tabified independently; confirm `Favorites` starts empty and receives no
  effects automatically; drag the visible separators to resize the column and
  the Effects list, confirm the 20 px Toolbox/Favorites and 30 px Effects
  minimums, and verify the chosen sizes return after restarting;
  confirm the three docks are individually available in
  `View > Effects`; click `Media Pool` to return to `Bins` above `Media`, and
  confirm the toolbar actions synchronize their checked state; select every
  Toolbox category and confirm the Effects list shows only the implemented
  entries (`Grayscale`, `Gain`, `Cross Dissolve`, `Fade to Black`, and `Text`),
  including the draggable Text tool; drag Text to multiple tracks and frames,
  confirm it creates a five-second text clip at the drop position, rejects
  overlap, and participates in Undo/Redo and project dirty state; confirm the
  other effects remain non-draggable and do not change the Preview, Timeline,
  project dirty state, or Undo/Redo; close and reopen the editor to confirm
  layout version 6 restores
  the saved arrangement, and use `View > Restore Default Layout` to restore
  the Media Pool default;
- playback controls, keyboard shortcuts, seeking, trimming, Blade Tool, and
  clip movement; clear both the Media Browser and Timeline item selections,
  place the playhead over a valid clip, and confirm Play resolves that clip
  and starts playback; also confirm that playback crosses a text-to-video
  boundary without an out-of-range-frame error, and that `Project opened.`,
  `Loading timeline clip...`, and other transient status messages appear beside
  the frame in one compact footer line without a separate global status row;
- the Timeline footer RAM indicator: confirm it is aligned to the right, uses
  the `RAM: <megabytes> MB` format, refreshes approximately once per
  second, reports only the Main Editor process, and does not affect playback,
  Timeline state, project dirty state, or Undo/Redo;
- clicking the Timeline footer RAM indicator: confirm the non-modal `Memory
  Usage` window opens and can remain open during playback and editing; verify
  System Memory shows total, used, and available values, Main Editor shows
  Working Set and Private Usage, values refresh approximately once per second,
  failed metrics show `N/A`, and closing the window leaves project state
  unchanged;
- Transform Inspector sliders for Position X/Y, Scale, Rotation, and Opacity;
  confirm that the numeric fields remain editable, values stay within their
  property ranges, keyframe edits still target the current frame, and one
  slider drag creates one Undo/Redo entry;
- Inspector tabs: switch between `Inspector` and `Audio`, close and reopen the
  application to confirm the last active tab is restored, and select clips
  without an automatic tab change;
- Audio tab: confirm the vertical Clip and Track blocks expose volume and mute
  controls, edits update playback, and Undo/Redo restores both properties;
  confirm all four controls are disabled for text clips, gaps, and no
  selection, and that the Timeline no longer contains an audio-control row;
- project prompts, Save/Open behavior, dirty-state title, and failed-open
  preservation; reopening a project restores its timeline zoom and starts at
  the beginning of the horizontal scroll;
- confirm that timeline zoom changes the timeline only: preview dimensions,
  playback limits, frame rate, clip data, and Undo/Redo remain unchanged;
- GPU preview, CPU fallback, grayscale, aspect-ratio preservation, and logs.

Record a manual result in the task or commit description when a milestone
changes one of these behaviors.

## Rules for adding coverage

- Add a deterministic CTest case when a behavior can be exercised without a
  real window or external service.
- Keep intentional user outcomes out of error-log assertions; test that
  unexpected technical failures are logged with actionable context.
- Keep tests independent, use temporary files, and do not depend on private
  media or machine-specific paths.
- Update this matrix when a new subsystem, user-facing rule, or keyboard
  shortcut is introduced.
