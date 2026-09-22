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
| Playback session | Sequential frames, forward catch-up without intermediate RGBA materialization, cancellation, reset, bounded frame-cache reuse, seek-free consecutive decoding, optimized random seeking, EOF, segment limits |
| Playback worker | Media activation, generation handling, seek coalescing, absolute-deadline pacing with fractional frame rates, latest-frame mailbox behavior, controlled intermediate-frame skipping, forward decoder catch-up for direct and composed playback, playback completion, separated layer decode/composition, final composition-cache reuse and invalidation, text-raster cache reuse, composition playback without a selected Media Browser source, errors, and no-op seeks without a selected source |
| Timeline model | Tracks, ordering, gaps, overlap rules, movement, split, trim, delete, metadata, history |
| Timeline interaction | Selection without playhead jumps, optional move-to-start selection preference, seek-on-release, configurable clip movement, semitransparent internal-move ghosts with dimmed source clips, red occupied-destination ghosts, media-drop ghosts using optional duration metadata, cancellation cleanup, no pre-release model signal, Blade Tool, trim-on-release, smooth upper-ruler playhead scrubbing, global-to-local seek conversion, stable one-hour horizontal scale, long-content expansion, timecode ruler, adaptive 1/2/5 frame guides with approximately eight-pixel spacing, discrete timeline zoom through 51,200%, frame-level guides confined to the upper ruler, Ctrl + wheel behavior, Shift + wheel row-height adjustment and clamping, vertical scrolling, coordinate anchoring, and viewport-width updates |
| System memory indicator | Deterministic byte-to-MB conversion, rounding, process-memory formatting, zero/invalid handling, and `RAM: N/A` fallback |
| System memory details | Offscreen non-modal dialog, System Memory and Main Editor sections, click-to-open behavior, Working Set, Private Usage, GB/MB formatting, and per-metric `N/A` handling |
| Transform Inspector | Slider and numeric-field synchronization, transform ranges, keyframe-aware edits, live preview updates, and one coalesced history entry per slider drag |
| Inspector audio tabs | Audio tab organization, Clip and Track volume/mute controls, disabled state without a valid video clip, and preserved audio edit behavior |
| Settings dialog | Modal shell, General, Timeline, and Shortcuts tabs, Close action, independent component construction, and editable shortcut preferences |
| Preview performance metrics | Deterministic counter/timing aggregation, bounded p95/p99 timing histograms, decoded/stale-frame counters, playback delivery-rate derivation, failure counters, cache state, workload context, process-resource sampling, reset behavior, disabled behavior, Settings persistence and signal propagation, and offscreen Preview submission instrumentation |
| Shortcut manager | QAction registration and application, QSettings persistence, empty assignments, duplicate blocking, individual reset, and Reset All |
| Project persistence | Versioned JSON, round-trip, timeline zoom and row-height persistence, version 1-6 migration, invalid input, offline media, transactional open |
| Media Browser model | Canonical duplicates, bins, rename, offline and restore behavior |
| Media Browser UI | Media Pool grouping with independent Bins and Media docks, native workspace layout persistence, list/block modes, global mode and icon-scale persistence, bounded 50%-150% icon resizing, seven-character media and folder labels, full-name inline editing, cached thumbnail retention, technical-information role, and preserved selection/drag metadata |
| Media Browser bin organization | Contextual bin creation, media-to-bin drops, bin subtree reparenting, empty-bin preservation, invalid destination rejection, and project bin synchronization |
| Effects UI | Implemented Toolbox categories, current effect catalog, category filtering, stable effect IDs, and visual-only behavior |
| Preview | CPU fallback, valid and invalid frames, resize, grayscale, clean shutdown |

## Manual UI validation

Automated tests do not replace visual validation. The following must be checked
in the running Main Editor after UI or integration changes:

- application startup and clean shutdown;
- Help > System: confirm the dialog shows project version `Beta 0.1.0` and the full
  path of the executable currently running; after a Release build, confirm the
  path points to the intended updated binary rather than an older installed
  copy;
- Settings action: confirm the menu-bar action immediately left of `Help`
  opens a modal dialog with `General`, `Timeline`, and `Shortcuts` tabs, closes
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
  remain available; confirm no Add Text button is shown;
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
  including the draggable Text tool; drag Text to multiple tracks and frames,
  confirm it creates a five-second text clip at the drop position, rejects
  overlap, and participates in Undo/Redo and project dirty state; confirm the
  other effects remain non-draggable and do not change the Preview, Timeline,
  project dirty state, or Undo/Redo; close and reopen the editor to confirm
  layout version 7 restores
  the saved arrangement, and use `View > Restore Default Layout` to restore
  the Media Pool default;
- first launch: confirm the Main Editor opens maximized with Media Pool on the
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
  preservation; reopening a project restores its timeline zoom, uniform track
  height, and starts at
  the beginning of the horizontal scroll;
- confirm that timeline zoom changes the timeline only: preview dimensions,
  playback limits, frame rate, clip data, and Undo/Redo remain unchanged; at
  the highest levels, adjacent frames are visibly separated and the horizontal
  scrollbar remains usable for short and long projects;
- hold Shift and scroll over the Timeline content and ruler at low, medium, and
  maximum row heights; confirm all rows change uniformly, the 30–180 pixel
  limits are respected, and the vertical scrollbar appears when needed;
- confirm that new projects start with 70-pixel Timeline rows, while saved
  `row_height` values remain unchanged and projects without that field migrate
  to 70 pixels;
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
