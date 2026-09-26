# Regression Testing

This document defines the local regression gate for the Video Editor. Every
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

The GitHub Actions workflow runs the same build and CTest gate on Windows,
macOS, and Linux. Local results only validate the operating system on which
they were run; cross-platform support is validated when all matrix jobs pass.

## Automated coverage

| Area | Test coverage |
| --- | --- |
| Structured logging | File creation, required fields, escaping, rotation, retention limit |
| Media probing and decoding | Missing files, invalid inputs, reference metadata, frame dimensions, PNG/JPEG/BMP/WebP/TIFF still-image probing, RGBA transparency, 150-frame defaults, and animated-GIF rejection |
| Playback session | Sequential frames, forward catch-up without intermediate RGBA materialization, cancellation, reset, bounded frame-cache reuse, seek-free consecutive decoding, optimized random seeking, EOF, segment limits |
| Playback worker | Media activation, generation handling, seek coalescing, absolute-deadline pacing with fractional frame rates, latest-frame mailbox behavior, controlled intermediate-frame skipping, forward decoder catch-up for direct and composed playback, playback completion, separated layer decode/composition, composition decoder-session reuse across media activation and playback of consecutive activated clips, final composition-cache reuse and invalidation, text-raster cache reuse, static-image frame reuse without FFmpeg/audio sessions, composition playback without a selected Media Browser source, global monitoring-volume updates, errors, and no-op seeks without a selected source |
| Playback controller | Monotonic Timeline clock using the first valid clip rate or 30 fps; continuous playhead during delayed media activation; trimmed source in-point seek; current-position seek before playback resumes; stale-frame rejection; pending activation cancellation on Pause, Stop, and seek; and clean project dirty state |
| Playback transition plan | Cross Dissolve held outgoing frame and incoming blend at its first, middle, and final frames; Fade to Black on both sides of the cut; one-frame durations; inactive and invalid transitions; unaffected layers on other tracks |
| Frame-step navigation | Worker steps within a clip; forward/backward activation at contiguous junctions, one-frame clips, gaps and Timeline limits, media overlaps and cross-track priority, transitions, and missing or invalid active clip locations |
| Timeline model | Tracks, ordering, gaps, overlap rules, movement, split, rolling and individual edge trims, one-sided media overlap and top-clip priority, video source limits, still-image/text extension, delete, metadata, canonical multi-track snapshots, history, Undo, and Redo |
| Timeline edge-trim command | Rolling and individual trim outcomes for video, image, and text, edited-clip identity after reordering, local playback frame and preserved global playhead, no-change and invalid requests, and Undo/Redo snapshot compatibility |
| Timeline edge-trim gesture | Pending transition selection versus valid shared-cut drag, rolling and individual previews, final release boundary, retained preview after an invalid pointer boundary, no-op and invalid requests, legacy trim range, signal order and single commit, and cancellation on track replacement or clearing |
| Timeline workspace selectors | Edit, blank Fusion, and Render button order, visible labels/icons, dimensions, exclusive checked state, tooltips, and accessible names |
| Workspace page switching | Edit startup state; FusionWorkspace-provided Viewer title, Node Editor, and Inspector; Render settings, shared Preview, and queue columns; responsive horizontal/vertical layout switching at 1100 px; constrained Settings fields with accessible Browse action and no horizontal scrolling; fixed Add to Queue footer outside the Settings scroll area; wheel scrolling over closed selectors and numeric controls without changing their values; runtime FFmpeg output discovery; project-derived defaults; prepared-job snapshots; controller-coordinated Edit → Fusion → Render transitions; exclusive selectors and lower dock titles; replacement of the Timeline with the Node Editor in the same lower dock; shared Timeline identity; Preview transfer into the middle Render column and restoration to the Edit/Fusion central stack; hidden Timeline controls/footer and blocked Timeline input in Render; project dirty-state preservation when preparing jobs; preservation of mixed prior dock visibility across repeated Render selection and exit, including a previously hidden Timeline; prepare-for-close restoration; and MainWindow close/reopen layout persistence |
| Render queue model | Runtime container/encoder compatibility filtering; stable job IDs and status/progress/error roles; project-document snapshot isolation; append, remove, and reorder behavior; retry reset; structure locking during execution; invalid operation rejection; and a new session starting with an empty queue |
| Offline Render export | CPU composition at configured dimensions; Timeline-to-output FPS conversion; black gaps; text clips and transform keyframes; Cross Dissolve; output file reopen and stream validation; embedded video-audio mix with clip/track gain and mute; monotonic progress; cancellation and failure preserving an existing destination and cleaning temporary files; ordered queue continuation after failure; cancellation leaving later jobs unstarted; and no change to the project snapshot |
| Edit workspace controller | Shared-session clip selection and playhead state; typed playback, media-drop, and seek requests; track creation, renaming, reordering, and removal; media and text insertion, clip movement, nudge, split, trim, delete, and clear; Inspector transform and keyframe commands; command-result, committed-edit, and history signals; rejected and no-op edits; occupied positions; offline or unregistered media; Undo/Redo; and unchanged project state for rejected commands |
| Functions window shortcut | Offscreen Shift+Space registration, WindowShortcut context, empty non-modal floating window, opening and toggling while focused, inside/outside click behavior, close and destruction through Escape/title bar/deactivation, fresh recreation without duplicates, and regular Space playback shortcut preservation |
| Timeline interaction | Selection without playhead jumps, row-local clip hit testing, gap deselection for Timeline and Media Browser items, no-op drags from empty rows, optional move-to-start selection preference, seek-on-release, configurable clip movement, checked-by-default Magnetic Snap with eight-pixel tolerance, clip-edge and Timeline-boundary snapping, aligned snap guides, enable/disable behavior, semitransparent internal-move ghosts with dimmed source clips, red occupied-destination ghosts, media-drop ghosts using optional duration metadata, one-frame fallback metadata, cancellation cleanup, no pre-release model signal, Blade Tool, edge-hover resize cursor and reset behavior, live left/right edge extension previews and trim-on-release, distinct rolling-center and one-sided shared-cut handles while preserving junction selection on click, smooth upper-ruler playhead scrubbing, global-to-local seek conversion, stable one-hour horizontal scale, long-content expansion, frozen track-header overlay during horizontal scrolling, vertical header alignment during vertical scrolling, timecode ruler, adaptive 1/2/5 frame guides with approximately eight-pixel spacing, discrete timeline zoom through 51,200%, frame-level guides confined to the upper ruler, Ctrl + wheel behavior, Shift + wheel row-height adjustment and clamping, vertical scrolling, coordinate anchoring, and viewport-width updates |
| System memory indicator | Deterministic byte-to-MB conversion, rounding, process-memory formatting, zero/invalid handling, and `RAM: N/A` fallback |
| System memory details | Offscreen non-modal dialog, System Memory and Video Editor sections, click-to-open behavior, Working Set, Private Usage, GB/MB formatting, and per-metric `N/A` handling |
| Transform Inspector | Slider and numeric-field synchronization, transform ranges, keyframe-aware edits, live preview updates, and one coalesced history entry per slider drag |
| Inspector audio tabs | Audio tab organization, Clip and Track volume/mute controls, disabled state without a valid video clip, and preserved audio edit behavior |
| Settings dialog | Modal shell, General, Autosave, Timeline, and Shortcuts tabs, empty and populated autosave snapshot table, refresh/restore/delete/open-folder requests, Close action, independent component construction, and editable shortcut preferences |
| Preview performance metrics | Deterministic counter/timing aggregation, bounded p95/p99 timing histograms, decoded/stale-frame counters, playback delivery-rate derivation, failure counters, cache state, workload context, process-resource sampling, reset behavior, disabled behavior, Settings persistence and signal propagation, and offscreen Preview submission instrumentation |
| Shortcut manager | QAction registration and application, QSettings persistence, empty assignments, duplicate blocking, individual reset, and Reset All |
| Project persistence | Versioned JSON v9, persisted stable track/clip IDs, canonical multi-track video/image/text kind round-trip, media overlap round-trip with text-over-text rejection, timeline zoom and row-height persistence, version 1-8 migration with legacy flat clips converted to `timeline_tracks`, duplicate/zero ID rejection, invalid input, offline media, transactional open |
| MainWindow integration | Offscreen multi-track project open, preservation of tracks, clips, and stable IDs, clean dirty state immediately after opening, selection initialized by ID, equality using only the canonical loaded document, save/reopen round-trip, stale pending activation rejection, EditWorkspace-built Inspector and Timeline plus FusionWorkspace-built panels shared with WorkspaceHost, one shared Preview/Timeline/EditorSession, unchanged selection/playhead/playback/history/dirty state across workspace changes, workspace-only layout behavior, and controlled MainWindow construction and shutdown |
| Project validation | Out-of-range JSON integers, overflowing timeline ranges, and overflowing media-source ranges are rejected before reaching editing code |
| Autosave and recovery | Retention, Unicode project paths, recovery filtering, and actionable log entries for malformed snapshots |
| Media Browser model | Canonical duplicates, bins, rename, offline and restore behavior |
| Media Browser UI | Media Pool grouping with independent Bins and Media docks, native workspace layout persistence, list/block modes, global mode and icon-scale persistence, bounded 50%-150% icon resizing, seven-character media and folder labels, full-name inline editing, cached thumbnail retention, technical-information role, and preserved selection/drag metadata |
| Media Browser bin organization | Contextual bin creation, media-to-bin drops, bin subtree reparenting, empty-bin preservation, invalid destination rejection, and project bin synchronization |
| Effects UI | Implemented Toolbox categories, current effect catalog, category filtering, stable effect IDs, Text and transition drag MIME, and visual-only Grayscale/Gain behavior |
| Preview | CPU fallback, valid and invalid frames, resize, grayscale, clean shutdown |

## Manual UI validation

Automated tests do not replace visual validation. The following must be checked
in the running Video Editor after UI or integration changes:

- application startup and clean shutdown;
- while a project is being prepared, confirm File, Edit, View, and Help remain
  available, project-changing commands are disabled, and the progress dialog
  does not block the rest of the application;
- Help > System: confirm the dialog shows project version `Beta 0.1.3` and the full
  path of the executable currently running; after a Release build, confirm the
  path points to the intended updated binary rather than an older installed
  copy;
- Settings action: confirm the menu-bar action immediately left of `Help`
  opens a modal dialog with `General`, `Autosave`, `Timeline`, and `Shortcuts` tabs, closes
  without changing project dirty state, and leaves the existing Edit menu
  preferences available;
- Settings > General: confirm preview performance metrics are disabled by
  default, can be enabled immediately, persist globally after reopening the
  editor, write aggregated numeric `preview/performance_metrics` samples about
  once per second while Preview is active, and stop logging when disabled;
  confirm this preference does not modify project dirty state, `.csp` data, or
  Undo/Redo;
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
  remain available; confirm no Add Text button is shown; confirm the far right
  of the top workspace toolbar shows the active `Edit` button, blank Fusion
  button with no text or icon, and labeled `Render` button;
- Timeline construction (F1): confirm the control row, scrolling viewport,
  fixed track headers, and footer retain their layout. Check the saved monitor
  volume at startup, zoom slider and buttons, checked initial Snap state,
  Selection/Blade switching, and each add/rename/move/remove track action.
  Switch Edit/Fusion and back; check footer status updates and playback buttons
  and shortcuts. Each action should respond once, with the same preview and
  playhead behavior; Timeline selection, project dirty state, and Undo/Redo
  history should change only when the corresponding edit requires it. The
  existing widget, workspace selector, and Timeline end-button tests cover
  those components, but do not instantiate the application `MainWindow`;
- Playback across clip boundaries: play adjacent clips with different source
  in-points and confirm the Timeline playhead advances continuously while each
  next clip opens. Confirm the preview switches to the current Timeline position
  instead of restarting at source frame zero; if opening takes longer than one
  frame, older preview frames may be skipped. Pause, Stop, seek, and edit during
  a pending activation and confirm stale frames do not reappear and the project
  dirty state changes only for the actual edit;
- Workspace pages: confirm startup selects Edit; click the blank Fusion button
  and confirm the existing Preview is labeled `Viewer`, the bottom dock title
  changes to `Node Editor`, the Timeline is hidden, and the Inspector shows the
  Fusion placeholder while Bins and Media remain available. Select Render and
  confirm the central page has output settings on the left, the same live
  project Preview in the middle, and the render queue on the right. Confirm the
  Preview follows the current playhead and playback. At central widths of at
  least 1100 px, resize the three columns and verify the Preview starts wider
  than Settings and Queue. Shrink the window below 1100 px and confirm Settings,
  Preview, and Queue stack vertically in that order. Verify Settings controls
  fit without horizontal scrolling and the Browse button remains visible; long
  codec names should be available from the selector and its tooltip. Widen the
  window again and confirm the horizontal layout returns without losing queue
  jobs or settings. Confirm **Add to Queue** remains fixed below the Settings
  scroll area, is the only add action, and adds a job when the configuration is
  valid. With Settings scrolled to the top, hover each closed selector and
  numeric field and use the mouse wheel; confirm the Settings panel scrolls but
  the selected options and numbers do not change. Confirm clicking a selector
  option, using the keyboard, and editing numeric fields still work. Confirm output
  formats and encoders come from the active
  FFmpeg build and incompatible codec/container combinations are absent. Check
  project-size resolution and first-clip FPS defaults (30 fps when no clip
  provides a rate), custom dimensions, and Low, Standard, High, and Custom
  bitrate behavior. Add two jobs with different output settings, change the
  project or form, and confirm the earlier job retains its snapshot. Reorder
  and remove jobs, confirm preparing a job does not dirty the project, and
  confirm the queue starts empty in a new application session. Add two jobs and
  use **Start Queue**: confirm rows show progress and finish as Completed, the
  output files open and play, and the project remains clean. While the queue is
  running, confirm Add, Remove, and Move controls are disabled while Settings
  remain editable and do not alter queued snapshots. Try adding jobs with the
  same destination and confirm the queue is rejected; add jobs targeting
  existing files and confirm a single grouped replacement prompt appears.
  Cancel during a long job and confirm its previous destination stays intact,
  the active row becomes Canceled, and later rows remain Prepared. Retry and
  confirm canceled and failed jobs run again while completed jobs are skipped.
  Cause one job to fail with offline media and confirm the failure is logged
  with useful job and media context, later jobs still run, and the failed row
  can be retried. The Timeline dock is the only visible workspace
  dock, its title remains `Timeline`, and its tracks, clips, ruler, and playhead
  are visible without the control row or footer. Try selecting a
  clip, seeking on the ruler, editing or dragging a clip, dropping media or an
  effect, opening a context menu, and changing zoom or track height; confirm
  none changes the project, playhead, selection, history, dirty state, or
  playback. Confirm the horizontal and vertical scrollbars still navigate the
  project. Return to Fusion and Edit and confirm the previous visibility of
  every dock is restored and the Timeline controls and interactions return.
  Repeat with the Timeline dock hidden before entering Render; it must be shown
  in Render and hidden again on exit. Close the application from Render and
  reopen it to confirm it starts in Edit with the previous dock layout. Resize
  the bottom dock in Edit and Fusion. Click all selectors and confirm
  selection, playhead, playback, Timeline contents, Undo/Redo, and project
  dirty state remain unchanged; the Node Editor and Fusion Inspector must not
  provide composition operations;
- Functions window: press Shift + Space with focus in the Timeline, Media
  Browser, and Preview, in both Edit and Fusion, and confirm the empty
  floating `Functions` window opens centered over the editor and receives
  focus. Click inside the window and confirm it stays open. Click any Main
  Editor panel outside it and confirm the window closes while the panel still
  receives the click; also switch to another application and confirm the
  window closes. Press Shift + Space again to confirm a fresh empty window
  opens. Verify Escape, the title-bar close button, and Shift + Space while the
  window is focused all close it. Confirm it contains no controls or function
  entries, Space alone still controls playback, and opening or closing the
  window does not change project dirty state, Timeline selection, playhead,
  playback, or Undo/Redo. Change the shortcut in `Settings > Shortcuts`, verify
  the new assignment applies, then reset it to Shift + Space;
- Timeline selection and empty-row behavior: select a clip, click an empty
  content area, and confirm both the Timeline clip and Media Browser item are
  deselected; press and drag from that empty area and confirm no clip moves,
  no ghost appears, and no project or Undo/Redo state changes; when clips on
  different rows overlap in time, confirm clicking each row selects only the
  clip in that row, while starting a drag on an actual clip still moves it;
- Timeline monitor volume: confirm the `Volume` slider and percentage indicator
  are visible, start at 100%, accept 0%-200%, restore the global `QSettings`
  value after reopening the editor, and apply changes while playback continues
  without changing the project dirty state, clip/track gains, `.csp` data, or
  the operating-system volume; test 0%, 50%, 100%, 150%, and 200% with media
  that has audio, media without audio, and static images;
- Timeline track height, maximum row height, vertical scrolling, stable
  one-hour horizontal scale, horizontal scrolling for longer content, zoom
  controls from 25% through 51,200%, progressively denser adaptive frame guides
  in the upper ruler, one guide per frame at frame-level density, no per-frame
  text over clips, and no vertical grid lines crossing clip content,
  clips filling the track row vertically without top or bottom margins,
  Ctrl + wheel playhead anchoring, button playhead anchoring, timecode labels in
  `HH:MM:SS.mmm`, click-and-drag
  playhead scrubbing on the upper time ruler, live playhead movement after a
  seek even when an intermediate frame is skipped, empty gaps without overlays,
  no Media Browser selection while scrubbing, selecting a clip without moving
  the playhead, playback playhead movement while a bin or different Media
  Browser item is selected, and the optional Edit > Move Playhead to Selected Clip Start
  preference, visual order, and release of any active Timeline mouse grab when
  clips are deleted or the track model is refreshed; drag a clip between rows
  and within the same row to confirm that the source is dimmed, the ghost
  follows the cursor, an occupied target is red, and no project change occurs
  before release; cancel the gesture and confirm the ghost disappears; drag a
  media item from both Media Browser modes and confirm that its duration-sized
  ghost follows the cursor, invalid areas show a red marker, folders remain
  rejected, and the existing drop creates exactly one clip only on release;
  confirm the checked-by-default Magnetic Snap button, place clip edges side
  by side within and beyond the eight-pixel tolerance, move between tracks,
  verify the guide line and Timeline-boundary snapping, then disable the tool
  and confirm the raw cursor frame is preserved; confirm Text drops keep their
  cursor marker and transition drops highlight a contiguous cut within the
  current hit area; confirm the snap toggle does not dirty the project;
- Media Browser list/block toggles, restoration of the last global mode and icon
  scale, slider adjustment from 50% to 150% in 10% steps, default 100% sizing,
  cached thumbnails, seven-character labels with ellipses, no technical second
  line, full-name inline editing, hover over the information icon for the
  complete technical tooltip, selection, clicking bins without
  losing the selected path or expanded branches, bins, context actions,
  folder items shown alongside media, folder icons, inline renaming with
  double-click and F2, automatic New Bin naming without dialogs through the
  context menu, protection of
  All Media/Unsorted, and preventing folder items from producing media drag data,
  native drag previews showing the cached media thumbnail or folder icon with
  the compact name, pressing and moving a media item with the left mouse button
  starts the native drag preview in both list and block modes, and rejecting
  folder previews at the Timeline,
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
  drops, project dirty state, persistence after save/reopen, drag-and-drop
  through the scrollable Timeline viewport in both list and block modes,
  horizontal-scroll coordinate conversion, rejection of the track header and
  ruler, and creation of exactly one clip; confirm that invalid inline names
  restore the previous
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
  including draggable Text and transition tools; drag Text to multiple tracks
  and frames, confirm it creates a five-second text clip at the drop position,
  rejects overlap, and participates in Undo/Redo and project dirty state; drag
  Cross Dissolve and Fade to Black onto contiguous clip junctions on multiple
  tracks, confirm the target junction is highlighted, the transition appears
  only on release, playback and frame seeking before, during, and after the
  transition reflect it, and Undo/Redo and project dirty state
  update; confirm drops away from a valid junction are rejected; confirm
  Grayscale and Gain remain non-draggable and do not change the Preview,
  Timeline, project dirty state, or Undo/Redo; close and reopen the editor to
  confirm layout version 7 restores the saved arrangement, and use
  `View > Restore Default Layout` to restore the Media Pool default;
- first launch: confirm the Video Editor opens maximized with Media Pool on the
  left, Inspector on the right, Preview in the center, and Timeline across the
  bottom; resize or rearrange the docks, close the editor, and confirm the
  window geometry and dock arrangement are restored without changing project
  dirty state;
- playback controls, keyboard shortcuts, seeking, trimming, Blade Tool, and
  clip movement; clear both the Media Browser and Timeline item selections,
  place the playhead over a valid clip, and confirm Play resolves that clip
  and starts playback; also confirm that playback crosses a text-to-video
  boundary without an out-of-range-frame error, and that `Project opened.`,
  `Loading timeline clip...`, and other transient status messages appear beside
  the frame in one compact footer line without a separate global status row;
- use Previous Frame and Next Frame buttons and their existing keyboard
  shortcuts within video, image, and text clips and at contiguous junctions
  in both directions; check the Preview, playhead, active clip, gap and Timeline
  limit messages, and transitions on different tracks without changing project
  dirty state or history;
- hover over both edges of a clip and confirm the horizontal resize cursor
  appears in the edge hit area, returns to the default cursor inside the clip,
  and disappears outside its edge or when the pointer leaves the Timeline;
  split a video, drag the 8-pixel strip centered on the shared cut, and confirm
  the preview moves both sides while preserving the cut; then drag each
  8-pixel side handle and confirm only that clip changes, the neighbor stays
  fixed, and extending into it creates an overlap whose edited edge remains
  marked in the preview while the later-starting clip stays visible above;
  play through the overlap and confirm audio switches to the
  visible clip, then switches back if the underlying clip continues; verify
  one-frame minimums, source limits, transition selection on a simple click,
  and Undo/Redo for both gesture modes; save and reopen the overlapping
  project; also extend an outer edge into a gap and confirm video source limits,
  still-image frame holding, and text duration extension; repeat with the
  playhead inside and outside the edited clip, confirming the selected clip,
  playhead, Preview frame, and one Undo/Redo entry after each valid release;
- for edge-gesture validation, release the mouse at a different frame from its
  last drag event and confirm the final frame is used; click a shared cut
  without moving and confirm it selects the transition without editing;
  repeat these checks with video, still-image, and text clips where applicable;
- use the default-enabled Preview performance metrics (or enable them in
  Settings) and compare a simple 1080p playback run
  with the metrics disabled: confirm the one-second summaries include decode,
  composition, decoded-frame cache hits, text-raster cache hits, and final
  composition-cache hits, `metrics_schema_version="4"`, p95/p99 timings,
  delivery FPS, window-local `first_frame_ms`, lifecycle timings for media
  open, audio setup, composition setup, activation, playback start, and seek,
  cache bytes, and process-resource fields;
  verify that a sequential run does not seek for every frame, that composition
  remains on the CPU, and that the optimized path does not change the Preview
  output, frame rate, project dirty state, or Undo/Redo;
- with Preview metrics enabled, compare `decode_avg_ms` with
  `decode_packet_avg_ms`, `decode_receive_avg_ms`, `pixel_conversion_avg_ms`,
  `frame_cache_copy_avg_ms`, and `decode_discarded_frames`; confirm that
  sequential playback reuses the pixel converter, forward catch-up materializes
  only its final target frame, reports zero `frame_cache_copy_count`, keeps the
  same frame counts, and does not add overwritten frames or visual differences;
- with Preview metrics enabled during playback, compare `playback_ticks`,
  `pacing_skipped_frames`, `pacing_coalesced_frames`, `pacing_lag_avg_ms`,
  `pacing_lag_max_ms`, `pacing_lag_p95_ms`, `pacing_lag_p99_ms`,
  `pacing_audio_catchup_frames`, `pacing_deadline_catchup_frames`,
  `audio_clock_drift_samples`, `audio_clock_drift_avg_ms`,
  `audio_clock_drift_max_abs_ms`, and `audio_buffered_ms`,
  `emitted_frames`, `received_frames`, `submitted_frames`,
  `cpu_presented_frames`, `gpu_presented_frames`, `presented_fps`,
  `presentation_ratio_percent`, and `overwritten_frames`; confirm that a
  simple run stays close to the source FPS, that intentional catch-up reports
  skipped frames instead of emitting a burst, stale frames are counted after a
  seek/generation change, and the one-slot mailbox prevents unnecessary UI
  queue growth; for audio playback, confirm that a one-frame drift does not
  immediately skip video frames, that audio catch-up starts only after three
  consecutive ticks above the tolerance, and that no more than one additional
  audio catch-up frame is selected per tick;
- with Preview metrics enabled, activate a media item and perform seeks in a
  composition with text and video layers; confirm `activation_events`,
  `playback_start_events`, `seek_requests`, and `seek_operations` distinguish
  requested and executed lifecycle work, `seek_to_presentation_*` is populated
  for composition seeks, and `first_frame_ms` is not interpreted as the
  playback-start latency;
- with Preview metrics enabled, exercise decode, seek, composition, and GPU
  failures; confirm their counters increase in the next aggregate sample while
  the detailed technical error remains in its normal error log entry;
- with Preview metrics enabled, verify `preview_backend` changes between
  `opengl` and `cpu_fallback` when GPU preview is disabled or fails, and verify
  unsupported `gpu_utilization_percent` and `gpu_memory_used_bytes` values are
  written as `N/A` rather than guessed;
- with Preview metrics enabled, verify source width/height/FPS/codec/container,
  preview dimensions, composition layer/text/transition counts, audio state,
  and active generation/clip context contain no media paths or frame data;
- with Preview metrics enabled, confirm every log entry contains numeric
  `process_id` and `thread_id` values plus a stable `process_instance_id`;
  confirm `playback/worker_ready` identifies `thread_role="playback_worker"`,
  `preview/performance_metrics` identifies `thread_role="ui_logger"`, and
  its `playback_worker_thread_id`, `playback_generation`, active track and
  clip indices, and playback frame index correlate with the active playback
  session; verify missing track or clip selections are recorded as `-1` and
  no media paths are added to performance samples;
- the Timeline footer RAM indicator: confirm it is aligned to the right, uses
  the `RAM: <megabytes> MB` format, refreshes approximately once per
  second, reports only the Video Editor process, and does not affect playback,
  Timeline state, project dirty state, or Undo/Redo;
- clicking the Timeline footer RAM indicator: confirm the non-modal `Memory
  Usage` window opens and can remain open during playback and editing; verify
  System Memory shows total, used, and available values, Video Editor shows
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
  preservation; reopening a project restores its timeline zoom, uniform track
  height, and starts at
  the beginning of the horizontal scroll;
- Open Media: select multiple video and still-image files together; confirm
  valid files are imported when another file fails, duplicate paths are
  ignored, PNG/JPEG/BMP/WebP/TIFF transparency and original dimensions are
  preserved, animated GIF is rejected, and the summary names the failures;
  drag an image from list and block modes to the Timeline, confirm it creates
  a five-second static clip with no audio, plays the same frame across seeks,
  participates in snapping, trim, transforms, transitions, and save/reopen,
  and reopens as offline when its source is unavailable;
- confirm that timeline zoom changes the timeline only: preview dimensions,
  playback limits, frame rate, clip data, and Undo/Redo remain unchanged; at
  the highest levels, adjacent frames are visibly separated and the horizontal
  scrollbar remains usable for short and long projects;
- scroll the Timeline horizontally at normal and high zoom; confirm that the
  track names, clip counts, and active-track highlight remain fixed on the
  left, while the ruler and clips move; scroll vertically and confirm that the
  frozen header rows remain aligned with their tracks; verify selection,
  playhead, clip movement, snapping, and viewport media drops still work;
- hold Shift and scroll over the Timeline content and ruler at low, medium, and
  maximum row heights; confirm all rows change uniformly, the 30–180 pixel
  limits are respected, and the vertical scrollbar appears when needed;
- confirm that new projects start with 70-pixel Timeline rows, while saved
  `row_height` values remain unchanged and projects without that field migrate
  to 70 pixels;
- project autosave: with a dirty saved project, confirm that the default
  30-second timer creates snapshots in the sibling `<project>.autosave`
  directory without changing the `.csp` file, dirty indicator, or playback;
  repeat with an unsaved project and confirm snapshots use the application
  data recovery directory; verify Settings changes for enablement, 10–300
  second interval, and 5–20 snapshot retention;
- Settings > Autosave: confirm the current project's and unsaved-project
  snapshots appear with project/type/date/name information, the newest entry
  is selected, Refresh reloads the list, Delete Selected asks for confirmation,
  and Open Folder opens the containing recovery directory;
- Settings > Autosave recovery: with a dirty project, confirm Restore Selected
  asks for confirmation, leaves the main `.csp` untouched, loads the selected
  snapshot as dirty working data, closes Settings only after success, and
  clears the restored project's or session's snapshot set;
- recovery: leave a newer snapshot, restart the editor, and confirm the
  dialog lists snapshots by date; Restore opens dirty working data without
  replacing the original `.csp`, Ignore leaves the snapshots available, and
  Delete removes only the selected snapshot; malformed snapshots must be
  ignored and logged without blocking project open;
- confirm that Ctrl + scroll still changes only horizontal zoom and normal
  scrolling still moves the scroll area;
- GPU preview, CPU fallback, grayscale, aspect-ratio preservation, and logs;
- GPU playback frame handoff: with metrics enabled, confirm normal GPU playback
  does not repeatedly update the hidden CPU surface, Preview submission does
  not retain stale frames after clear, and the shared frame handoff preserves
  the same visual output; repeat with `CREATIVE_SUITE_DISABLE_GPU_PREVIEW=1`
  to confirm the lazy CPU fallback, grayscale, and invalid-frame behavior.
- playback pacing: run a video with and without audio, then add a text layer
  and repeat; confirm the Preview follows the newest target frame, audio stays
  synchronized when available, intermediate frames are skipped only when the
  worker is late, `decode_discarded_frames` increases without a matching rise
  in RGBA pixel conversions during catch-up,
  `pacing_coalesced_frames` identifies UI pressure, and `overwritten_frames`
  is not confused with mailbox coalescing. Verify seek,
  Previous Frame, Next Frame, Blade Tool, selection, playback completion, and
  project dirty state remain unchanged.
- composed video activation: play a video clip whose decoder session was prepared
  by the Timeline composition, then continue into another prepared video clip.
  Confirm playback completes both activations without a “Playback session is
  not available” error and the Preview keeps showing composed frames.
- absolute-deadline pacing: run a continuous 24 fps and 25 fps playback for at
  least 30 seconds, with Preview performance metrics enabled. Confirm that
  normal playback does not show a periodic frame-loss pattern, that the
  effective frame rate stays close to the source rate with and without audio,
  and that `pacing_lag` increases only during real worker delays. With audio
  enabled, confirm the worker does not collapse to a lower cadence when the
  audio-selected target is ahead. Introduce a temporary decode or composition
  delay and confirm that catch-up skips due intermediate frames, then remove
  the delay and confirm playback resumes without accumulating timer drift or
  changing Timeline, project, GPU, or Undo/Redo state.

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
