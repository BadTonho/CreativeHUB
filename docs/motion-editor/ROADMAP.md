# Motion Studio Roadmap

Status: **2D MVP product scope approved; implementation is substantially
complete, with acceptance and cross-platform validation in progress**. The
standalone Motion Studio shell, in-memory composition/layer model, navigation
timeline, Media Pool, image/video/text/shape layers, CPU preview, versioned
native save/open, and first-pass rendered video export are implemented under
`apps/motion-editor/`.
Configurable preview performance metrics and per-job export summaries are also
written to the Motion Studio diagnostics log.
Its per-layer CPU effects currently include ordered Gaussian Blur and Color
Adjustment stacks shared by preview, playback, and export.
Its initial product scope and
readiness are documented
in [SCOPE_AND_READINESS.md](SCOPE_AND_READINESS.md). C++ and Qt 6 are provisional
implementation choices; no final language, renderer, or codec choices have
been made. Focused reuse libraries are tracked separately in
[REUSE_PLAN.md](REUSE_PLAN.md). The current native document contract is described
in [FORMAT.md](FORMAT.md); `.motion` remains a provisional extension.

Motion Studio may be developed in parallel with the Video Editor and Image
Editor on an independent track. Cross-application integrations still depend on
stable contracts and validated producer/consumer behavior. Motion Studio
focuses on advanced motion design and compositing, while the applications share
media, rendering, animation, and project services when those boundaries are
technically clear.

## Principles

- Keep Video Editor stability as a priority while developing Motion Studio in
  parallel with the other application tracks.
- Scope early Motion Studio work so it can proceed independently of unfinished
  Image Editor milestones; gate shared services and handoffs on stable APIs.
- Reuse shared media, rendering, animation, caching, and recovery services
  instead of building duplicate engines.
- Keep each application's Media Pool UI and document lifecycle independent,
  while sharing neutral media metadata, decoding, and catalog behavior.
- Keep video frames and GPU resources shared or referenced efficiently across
  module boundaries.
- Validate performance, memory use, startup time, licenses, and all three
  target operating systems before recording technical decisions as final.
- Keep this roadmap provisional while technical choices and later release
  gates remain unvalidated.

## Milestones

### 0. Scope and readiness

- [x] Define the intended users, primary workflows, and the first Motion Studio
  release boundary.
- [x] Define the later linked workflow for opening, referencing, and updating
  Motion Studio compositions from Video Editor projects.
- [x] Map the existing Video Editor document, media, rendering, keyframe,
  history, and recovery capabilities to the services Motion Studio needs.
- [x] Identify candidate shared capabilities; document their API
  responsibilities, ownership rules, and extraction gates. Implementations
  are tracked separately, with shared contracts remaining provisional until
  both consumers validate them.
- [x] Define native format separation, versioning, migration, and
  cross-application reference compatibility rules.
- [x] Confirm the first MVP remains standalone 2D composition with opaque,
  silent video export. Set the reference workload to 1080p/30 fps, up to
  10 seconds and five layers on the maintainer's PC: AMD Ryzen 5 3600, 32 GB
  RAM, NVIDIA GeForce GTX 1660 SUPER with 6 GB VRAM, and Windows 11. Record
  test conditions and results during validation. Audio-reactive 2D animation
  is the first expansion to investigate after the MVP is validated; it is not
  part of the MVP.

**Exit criteria:** the MVP and application boundary, later handoff, capability
ownership and extraction gates, and format compatibility policy are documented
in [SCOPE_AND_READINESS.md](SCOPE_AND_READINESS.md). The Video Editor
foundation remains a stability priority; Image Editor milestones do not block
this readiness work.

### 1. Technical validation and gap audit

- [x] Audit the Video Editor's Qt 6, FFmpeg, CPU composition, and OpenGL
  presentation path against the Motion Studio MVP. Record evidence that
  transfers and gaps that remain in
  [SCOPE_AND_READINESS.md](SCOPE_AND_READINESS.md). This is a source-level
  audit; GPU runtime and cross-platform support remain pending validation.
- [x] Document provisional animation and composition contracts against the
  Motion Studio layer and curve workflows; preserve Video Editor regression
  coverage at the shared-library boundary. Motion Studio now has consumer-side
  model and preview coverage; the contracts remain provisional pending
  cross-platform and manual visual validation.
- [x] Extract the FFmpeg playback session behind a neutral observer boundary;
  preserve Video Editor preview metrics in an application adapter.
- [ ] Revalidate the applicable existing paths on Windows, macOS, and Linux;
  record GPU runtime support separately from GPU presentation of CPU-composed
  frames.
- [ ] Measure startup, memory, timeline/seek response, preview latency, and
  render performance for representative small, medium, and heavy compositions.
  Include the approved 1080p/30 fps, 10-second, five-layer reference workload;
  use the maintainer's PC as the reference system, and document measured
  targets and unmet limits. Validate additional systems before generalizing
  hardware requirements.
- [ ] Record dependency and asset licenses, output profile/codec findings, and
  alternatives needed to resolve the identified gaps before choosing a
  renderer or other technology.

**Exit criteria:** existing evidence, Motion-specific gaps, cross-platform
results, performance targets, and remaining alternatives are documented before
the Motion Studio implementation choices are finalized.

### 2. Composition foundation

- [x] Create the in-memory composition document and ordered layer model using
  the shared animation types for transforms and keyframes. Canvas dimensions
  must be explicitly provided; the standalone window starts without a
  composition.
- [x] Add an in-memory canvas viewer with worker-thread preview for visible
  still-image and video layers, then add Motion Studio-owned Qt rasterization
  for text, rectangles, and ellipses on the same preview worker.
  The empty state offers a centered New Composition button alongside the
  existing File menu action.
- [x] Add an explicit exact frame rate and a navigation-only timeline with a
  frame ruler, playhead seeking, and single-frame stepping. Composition
  duration is not fixed during creation.
- [x] Add Motion Studio-owned discrete timeline zoom from 25% to 51,200%,
  defaulting to 100%, with playhead anchoring, visible controls, Ctrl+wheel,
  horizontal navigation, shared ruler/layer frame mapping, and adaptive ruler
  ticks. Zoom changes view state only and does not alter composition timing.
- [x] Add a Time / Frames display selector for ruler ticks and the playhead
  readout. Time is the default and uses elapsed `HH:MM:SS.mmm` calculated from
  the exact composition frame rate; navigation and editing remain frame-based.
- [x] Add an in-memory Media Pool for video and still images, backed by the
  shared media catalog and import processing. Include hierarchical bins,
  cached thumbnails, list and thumbnail views, renaming, offline marking and
  restoration, and a selected-media details panel. The pool clears when a new
  composition replaces the current one. Its import progress dialog is created
  only when a non-empty import request starts, so an empty pool does not open it
  during startup or workspace creation.
- [x] Connect Media Pool image and video items to independent visual timeline
  layers. Support drop-to-insert, front-to-back row ordering, 8-pixel snapping,
  selection, time movement, row reordering, visibility, removal, and right-edge
  duration editing. Still images start at five seconds using the exact
  composition rate; videos use their identified source duration.
- [x] Restore the selected layer's base-transform inspector and render active
  raster layers with the shared CPU compositor. Decode video frames on a
  worker, coalesce rapid seeks, and ignore stale preview generations. The
  inspector edits base transforms or keys at the playhead, while the preview
  evaluates transform keyframes on the worker.
- [x] Add Play/Pause and optional Loop controls for continuous visual preview.
  Advance from a monotonic clock at the exact composition frame rate, stop at
  the furthest layer end or loop to frame 0, and keep the playhead in view.
  Playback decode requests coalesce without cancelling an in-flight playback
  decode; manual seeks still supersede outdated preview work. Audio is omitted.
- [x] Add a resizable Settings dialog for configurable command shortcuts.
  Reuse the shared shortcut manager while keeping Motion Studio command IDs,
  defaults, and dialog behavior application-owned. OK applies a validated
  batch; Cancel discards edits. Time/Frames and Media Pool commands remain
  outside this initial list.
- [x] Match the Video Editor's Help menu pattern with **System** (application
  version and executable path), **Open Log Folder**, and **About Motion Studio**.
- [x] Apply the existing Motion Studio PNG icon to the application window and
  its Windows executable.
- [x] Add manual Open, Save, and Save As for the full composition and Media
  Pool using an atomic, versioned `.motion` JSON document. Reject malformed and
  future-version documents without replacing the current composition or
  rewriting the source file. Missing media reopen as offline references.
- [x] Add transform keyframe editing to image and video layers for Position X/Y,
  Scale, Rotation, and Opacity. Expand a layer's Transform group to reveal its
  property tracks; add or remove keys from the inspector, seek by key marker,
  and drag keys within the layer's duration. New segments default to Linear;
  `.motion` v1 and v2 keys remain linear when migrated to v3.
- [x] Add an expandable Graph Editor below the timeline for the selected
  transform property. Edit segment easing with bounded cubic Bezier handles or
  Linear, Ease In, Ease Out, and Ease In/Out presets. A preset or completed
  handle drag is one Undo action; graph selection and panel visibility are UI
  state. Preview, playback, and export use the same shared animation evaluator.
  Curves were introduced in `.motion` v3; the current v4 format retains them
  and adds per-layer effects. Read v1/v2 curves as Linear, and keep the
  recovery wrapper at v1 while accepting nested documents through v4.
- [x] Make Media Pool, Inspector, Timeline, and Graph Editor movable, resizable,
  tabifiable, floatable, and hideable Qt dock panels around a central Preview.
  Timeline and Graph Editor share a bottom tab group, with Timeline selected by
  default. The Graph Editor button selects its tab; View and the dock close
  control show or hide it. Preserve customized layouts, migrate the previous
  uncustomized default layout, and persist panel state globally in Motion Studio
  settings.
- [x] Organize the timeline UI into separate controls, ruler, and layer-track
  components under `src/ui/timeline/`. Keep navigation and playback state in
  `TimelineNavigator`, and document edits and history in `MainWindow`; preserve
  existing Qt object names and interaction behavior.
- [x] Add bounded Undo/Redo for composition edits, including layer timing,
  order, visibility, transforms, and keyframes. Media Pool operations remain
  outside the history.
- [x] Add configurable autosave and recovery snapshots for the composition and
  Media Pool. Saved-project snapshots use a versioned wrapper beside the
  `.motion` file; untitled snapshots are separated by session under the
  Motion Studio app-data directory. Restore stages media and keeps the
  recovered document dirty. The recovery wrapper stays at version 1 and
  contains the nested native document payload.
- [x] Add native Text, Rectangle, and Ellipse layers with content inspectors,
  static text/shape content, alpha-aware solid colors, and the existing
  transform/keyframe system. Rasterize their RGBA frames on the Motion Studio
  preview worker and composite them through the shared CPU compositor.
  New layers start at the playhead, centered, and last five seconds at the exact
  composition rate. Save them in `.motion` v2; continue opening v1 documents by
  applying default text or rectangle content and upgrading them on save. Keep
  the recovery wrapper at v1 while it accepts nested document versions 1 and 2.
- [x] Add **File > Export Video...** with runtime container and compatible
  encoder discovery, composition/common/custom dimensions, output frame rate,
  and quality/bitrate controls. Export an immutable composition snapshot from
  frame 0 through the furthest layer end, including hidden layers when finding
  the end, with rational frame-rate conversion, opaque black lead-in, progress,
  and cancellation. Verify the staged output before atomic publication; failed
  or canceled jobs preserve an existing destination. The neutral
  `creative-suite::video-encoding` library serves Motion Studio and the Video
  Editor, while the latter keeps its timeline assembly, queue, and optional
  audio export. Motion export contains no audio or alpha, and settings are not
  persisted.
- [x] Add configurable one-second preview performance samples and per-job
  export summaries. The shared system-monitor library provides best-effort
  process CPU and memory readings; Motion Studio records render-stage timing,
  request-to-viewer-paint latency, coalesced/stale work, per-effect application
  counts and timings, and export throughput. Preview metric schema v3 separates
  Gaussian Blur and Color Adjustment timing and records the effective effect
  worker count without recording layer names or content.
  This diagnostic logging does not replace representative-project profiling or
  cross-platform validation.
- [x] Make neutral Color Adjustment an exact byte-preserving no-op while
  retaining per-effect timing records. Replace floating-point Gaussian Blur
  rolling sums with integer accumulation and equivalent rounding, and process
  vertical passes in 32-pixel tiles. Preview, playback, and export share a
  bounded Motion Studio worker pool that parallelizes premultiplication,
  horizontal rows, vertical tiles, and final alpha conversion, with a barrier
  between each blur pass. The pool uses at most eight workers and reserves one
  reported logical core when more than one is available, while keeping at
  least one worker; unknown core counts fall back to one worker. Single- and
  multi-worker differential tests compare bytes against the previous
  algorithm; concurrent calls, cancellation, and per-effect timing are covered.
- [-] Compare effect worker limits on Windows using the same 1920 × 1080,
  60 fps composition with radius-10 Gaussian Blur and neutral Color Adjustment.
  Measure automatic mode and limits 1, 2, 4, and 8 in three 10-second runs each;
  compare blur and total frame-render averages, decode time, rendered/coalesced
  requests, and CPU use. Keep the automatic default unless playback-wide results
  show a repeatable improvement. Confirm pixel equality.
- [-] Extend actionable local error logging and automated coverage for the
  remaining document, rendering, and application boundaries. Save/open and
  export failures are logged before concise user feedback.

The in-memory document/layer model has unit coverage, including explicit canvas
size, exact frame rate, media paths, layer order, timing, movement, and duration
limits. The empty application shell passed its offscreen startup test and a
manual Windows launch/close check. The centered empty-state New Composition
button uses the File menu's creation flow and has offscreen UI regression
coverage. Opening a composition preserves the current window state and geometry;
the app starts maximized unless the user restores it. The canvas viewer,
navigation ruler, timeline zoom and scrolling, layer rows, drag/drop, transforms,
the nested Transform/property tracks and key editing, time/frame display, and
preview have offscreen coverage. The shared media catalog
and importer, plus the Motion Studio Media Pool, have regression coverage for
video and still-image imports, first-frame thumbnails, bins, renaming, offline
restoration, view modes, selection details,
and clearing on composition replacement. The Video Editor retains its
project-media and linked-image regressions. Motion Studio document tests cover
empty and populated round trips, exact fractional rates, stable layer IDs,
transforms, keyframes, bins, renamed and unused media, Unicode and relative or
external paths, malformed/future versions, validation failures, and preserving
an existing file after a rejected save. Offscreen UI tests cover Save As, Open,
dirty title state, Save/Discard/Cancel replacement and close decisions, failed
Open preserving the current document, navigation resets, media-pool changes,
and missing assets reopening offline. Autosave and recovery tests cover dirty
documents, duplicate-state skipping, snapshot retention, invalid snapshots,
startup recovery, saved-project recovery on Open, Settings management, and
retaining the original Save target. The recovery wrapper is separate from the
native `.motion` document schema and remains at wrapper version 1.
Export tests cover fractional-rate conversion, image/video/text/shape
composition, transform keyframes, hidden-layer duration, blank black frames,
decoded output, progress, cancellation, failed-file cleanup, destination
preservation, dialog defaults, and unchanged dirty state. Shared encoder tests
cover capability discovery and encode/decode; the Video Editor export
regression continues to cover its optional audio path.

## Regression coverage index

The automated targets are registered in
[`apps/motion-editor/tests/CMakeLists.txt`](../../apps/motion-editor/tests/CMakeLists.txt).
This map ties current MVP behaviors to their test sources; detailed manual
readiness checks remain in this roadmap and
[`SCOPE_AND_READINESS.md`](SCOPE_AND_READINESS.md).

| Behavior group | Automated evidence | Manual evidence and status |
| --- | --- | --- |
| Composition model, validation, layer data, and timing | `composition_document_test.cpp` (`creative-suite-motion-editor-document`) | Pending manual composition and supported-layer checks remain part of MVP acceptance. |
| Undo/Redo and edit history | `composition_history_test.cpp` (`creative-suite-motion-editor-history`) | Pending manual validation: confirm interactive edit sequences during the broader Windows UI checklist. |
| `.motion` save/open, migrations, and invalid-file preservation | `motion_document_store_test.cpp` (`creative-suite-motion-editor-persistence`), `motion_editor_ui_test.cpp` (`creative-suite-motion-editor-ui`) | Automated round trips and migration coverage exist; Windows workflow acceptance and cross-platform file/path checks remain pending. |
| Autosave and restart recovery | `motion_recovery_store_test.cpp` (`creative-suite-motion-editor-recovery`), `motion_editor_ui_test.cpp` | Automated snapshots and UI recovery paths exist. Manual Windows restart/recovery scenarios remain pending (**P0 validation**). |
| Preview, transforms, curves, and layer effects | `preview_renderer_test.cpp` (`creative-suite-motion-editor-preview`), `motion_editor_ui_test.cpp`; shared evaluator coverage in `libs/tests/animation_test.cpp` and `libs/tests/composition_test.cpp` | Offscreen tests cover preview and interaction behavior; real-hardware visual output and graphics-driver validation remain pending (**P2 validation**). |
| Performance diagnostics | `performance_metrics_test.cpp` (`creative-suite-motion-editor-performance`) | Tests cover metrics aggregation, not actual playback throughput. The 1080p/30 fps, 10-second, five-layer benchmark on the reference PC and other systems remains pending (**P2 validation**). |
| Opaque video export and export controls | `motion_video_export_test.cpp` (`creative-suite-motion-editor-export`), shared `libs/media/tests/video_encoder_test.cpp` | Automated output, cancellation, and failure coverage exists; throughput, installed codecs, output profiles, and cross-platform behavior remain pending (**P1/P2 validation**). |
| Startup and application UI | `motion_editor_startup_test.cpp` (`creative-suite-motion-editor-startup`), `motion_editor_ui_test.cpp` (`creative-suite-motion-editor-ui`) | Startup has offscreen coverage; broader visual, graphics-driver, and platform checks remain pending (**P2 validation**). |
| Motion Studio to Video Editor editable handoff | No current integration test; handoff remains a later roadmap milestone | Planned, not implemented. Define producer/consumer tests when the contract is implemented; not a current regression gap. |

### Current coverage gaps and pending validation

- **P0 — critical paths mapped:** composition validation,
  persistence/migration, startup, and automated recovery each have registered
  test sources. This inventory did not identify a specific P0 behavior with
  neither automated nor documented manual coverage; Windows restart/recovery
  acceptance remains pending.
- **P1 — export acceptance:** automated export behavior is covered; codec
  availability and the supported output profile still require real packaged
  builds and cross-platform validation.
- **P2 — performance and platform acceptance:** metrics tests do not substitute
  for a controlled benchmark. Record the approved workload on the reference PC
  and complete the remaining Windows, macOS, and Linux checks.
- **Planned, not implemented:** audio-reactive 2D animation and linked editing
  from the Video Editor are future work, not missing tests for current
  behavior.

Frame rates are stored as exact rational values from the supported common-rate
list. Creating a composition does not ask for or set its duration. The ruler
starts with a one-hour navigation range, calculated from the selected exact
frame rate; fractional rates round the frame count up. This range is not a
composition end. Seeking and frame stepping stay within the current range.
At 100%, one hour fits in the timeline. Zoom uses the Video Editor's discrete
levels from 25% to 51,200%, preserves the playhead's screen position, and
scrolls horizontally when the current range no longer fits. The ruler and layer
rows share the same viewport mapping; ruler tick spacing adapts to the visible
frame range and the width of labels in the selected display mode. The timeline
defaults to elapsed `HH:MM:SS.mmm` time calculated from the exact rational frame
rate; **Frames** switches the ruler and playhead readout to frame numbers. This
display selection does not affect frame-based navigation or editing and resets
to **Time** for a new composition. Zoom, scroll, and display mode are UI state
and are not persisted. Dragging past the actual range end extends it
by one hour once per gesture and moves the playhead to the new end; scroll to
the end first if it is offscreen. A drag at the visible viewport edge does not
extend a range whose end is still offscreen. The range saturates at the signed
64-bit frame limit, starting at zero.

Image and video layers reference canonical Media Pool paths, and repeated uses
of one source receive distinct layer IDs. Still images last
`ceil(5 * composition FPS)` frames. Video insertion requires a positive source
frame rate plus either a positive frame count or positive duration; its full
source length is converted to composition frames. Rows display front-to-back
while the document stores layers back-to-front. Dropping on a row inserts above
it; dropping in empty space inserts at the top. The eight-pixel snap tolerance
considers frame zero and other layer starts and ends. Stills may be extended;
videos may be shortened and restored up to their source duration. Selection,
insertion, reordering, visibility, and seeking do not alter layer transforms or
keyframes.

Each image or video layer can be expanded to show its **Transform** group; that
group expands to show Position X, Position Y, Scale, Rotation, and Opacity key
tracks. The layer name is omitted from the left header and remains on the clip.
Keys use layer-local frame numbers;
their timeline markers appear at `layer start + local key frame`. Clicking a
marker seeks to it, while dragging moves it to an integer local frame within
the layer duration. A move onto another key for the same property is rejected
without changing either key. Shortening a layer hides keys outside its current
duration but retains them in the document; extending the layer makes them
visible again. New and opened compositions start with layers and Transform
groups collapsed. Inserting a key from the inspector expands its layer and the
Transform group.

The transform inspector edits the base value when a property has no keys. For
an animated property it displays the shared evaluator's interpolated
value and is read-only between keys. Adding a key copies the currently
evaluated value; at an existing key, the field edits that key. Removing the
last key restores the base value. Key editing is available only when the
playhead is within the selected layer. Transform keys are evaluated for both
manual preview and playback, and the selected-layer guide follows the evaluated
position. Seeking and animation do not change layer timing.

The preview displays cached still-image frames or decodes video frames using
the shared `VideoPlaybackSession`, then composites visible layers back-to-front
with `FrameCompositor`. Decode and composition work runs off the UI thread.
Play/Pause follows the exact composition frame rate; Loop is off by default and
restarts from frame 0 when enabled. Playback stops on the last frame of the
furthest layer, including hidden layers, and pressing Play at the end restarts
from frame 0. Manual seeking pauses playback. The monotonic clock remains on
schedule when rendering falls behind; intermediate preview frames may be
skipped. Transform keyframes use the shared curve evaluator in the preview
worker. A completed frame from the current uninterrupted playback may still be
presented; manual seeking or replacing the composition invalidates it. Playback
extends the navigation range by one hour as needed and scrolls to keep the
playhead visible. This does not set a composition duration. Preview remains
CPU-only and Motion Studio-owned. Text, rectangle, and ellipse layers are
rasterized to transparent RGBA8 with Qt painting on the preview worker; the
shared compositor handles their layer order, transforms, opacity, and alpha.
Text glyphs and shape fills/strokes are static; only their existing transform
properties are animated. Source in-points and audio remain unsupported.

During playback, the preview worker decodes forward sequentially for source
frame gaps of two through eight frames when the decoder is already positioned
before the target. One-frame advances keep the decoder's existing fast path;
backward seeks, larger gaps, and interactive scrubbing use timestamp seeking.
An unsuccessful forward decode falls back to timestamp seeking unless it was
cancelled. Export retains its existing decode path. Preview performance schema
v4 reports actual timestamp-seek outcomes and time, forward-decode attempts,
completions and fallbacks, and discarded intermediate frames. These counters
are diagnostics; they do not change the automatic effect-worker policy.

The timeline UI component refactor passed a manual Windows check: playback
controls, ruler seeking, zoom and scrolling, layer editing, and keyframe
interactions worked. The broader manual Windows validation remains pending:
confirm the Motion Studio icon on
the application window and executable; open **Help > System** and confirm
it displays **Beta 0.1.0** and the executable path; open **Help > Open Log
Folder** and confirm the Motion Studio log directory opens, then verify the
About text; verify the centered empty-state
button opens composition creation and disappears after creation; confirm a
composition opens maximized and preserves a restored window size; confirm the
new **File > Save As**, **File > Save**, and **File > Open Composition** actions
use the Windows-native file picker; write and reopen a `.motion` document with
layers, keyframes, and Media Pool bins intact; confirm relative media paths
still resolve after moving the project folder with its media; move a referenced
source away and confirm it reopens offline; verify malformed and future-version
files leave the current document and the source file unchanged; test Save,
Discard, and Cancel before replacing
or closing a dirty composition; and confirm the title's dirty marker clears on
save. Move, resize, tabify, float, and hide the Media Pool and Inspector. Switch
between the Timeline and Graph Editor tabs using the native dock tabs and the
Graph Editor button; use **View > Graph Editor** and its close control to hide
and restore that tab. Restart the app to verify layout restoration, then use
**View > Reset Panel Layout** and confirm Timeline is selected with both tabs
available. Confirm the previous uncustomized default layout migrates to the
tabbed layout while a customized layout remains intact. Then confirm the
one-hour navigation range, stepping, and separate drag extensions; confirm the
timeline controls, ruler, and layer tracks still work after their component
refactor; import
images and videos, organize bins, switch pool views, and restore offline items;
drag media to empty space and existing rows; verify snapping and front-to-back
order; select, move, hide, resize, and remove layers; edit transforms; seek
through image and video previews; exercise zoom buttons, slider, and Ctrl+wheel;
confirm the playhead stays anchored, scroll horizontally to inspect aligned
ruler and layer positions, move and resize clips at multiple zoom levels, and
extend the range only at its actual end, scrolling there if it is offscreen;
resize the viewer, Media Pool, inspector, and timeline; replace the composition
and confirm the pool resets; verify the timeline starts in Time mode, switch to
Frames and back, and confirm the playhead and layer positions do not change;
play and pause a video and confirm a seek pauses it; confirm the left layer
header has no name while the clip retains it; expand a layer to show only
Transform, then expand Transform to show the five properties; collapse and
reopen each level; add Position and Opacity keys, inspect interpolated values,
edit at a key, remove a key, drag a marker, and confirm a colliding move is
rejected; scrub and play through the animation; open the Graph Editor, select a
property and segment, drag each Bezier handle, apply every easing preset, and
verify Undo/Redo, preview, playback, and export use the curve; save and reopen a
v4 project and confirm v1-v3 projects retain empty effect stacks and older
versions retain linear interpolation; create
multiline Unicode text, change its font, size, alignment, box dimensions, and
alpha color; create
rectangles and ellipses, edit their dimensions, fill, optional stroke, and alpha;
use the Effects inspector tab to add Gaussian Blur and Color Adjustment to
different layers, adjust parameters, toggle, drag effects above and below other
rows, and remove effects, then verify preview, playback, export,
Undo/Redo, and save/reopen preserve the stack and order;
animate a text or shape transform, save and reopen it, and confirm content and
keys persist; use **Edit > Undo** and **Edit > Redo** on layer insertion,
content edits, visibility, clip timing, transforms, and keyframes; confirm
inspector edits group into one history step, undoing to
the saved composition clears its dirty marker, Media Pool contents are
unaffected, and new/opened compositions start with empty history; enable Loop and
confirm it restarts at frame 0, then disable Loop and confirm playback stops
on the final layer frame and Play restarts from frame 0; verify playback scrolls and
extends the navigation range for a long clip; open **Settings > Keyboard
Shortcuts**, change Undo and Redo assignments, confirm duplicate assignments are rejected,
check that Cancel discards edits and OK persists them, clear an assignment,
restore defaults with **Reset All**, and confirm timeline commands are disabled
when unavailable; configure autosave interval and retention; create an untitled
composition, allow a snapshot, close the app unexpectedly, then restore it on
startup; open a saved project with a newer recovery snapshot and test Restore
and Ignore; inspect, refresh, delete, restore, and open the folder for snapshots
in **Settings > Autosave & Recovery**; verify missing sources restore offline
and autosave does not overwrite the `.motion` file. Use **Settings > General...**
to disable and re-enable preview metrics; seek and play, then inspect **Help >
Open Log Folder** for active-only samples with CPU/memory, stage timings,
percentiles, and request counters. Confirm idle intervals stay silent and
records contain no paths, layer names, or text content. Export a composition and
inspect its completion summary for elapsed time, frame count, render/write
timings, output size/rate, and achieved speed; also confirm canceled and failed
jobs have summaries. Open **File > Export
Video...**, confirm composition resolution and frame-rate defaults, try a custom
resolution and quality profile, export overlapping image, video, text, and
shape layers with a blank lead-in, and play the result in a media player. Cancel
another export and verify an existing destination remains unchanged; choose an
unavailable encoder if one is offered and confirm a detailed log entry appears.
For effect performance, use the same 1920 × 1080, 60 fps composition with
Gaussian Blur at radius 10 and neutral Color Adjustment. Compare automatic mode
with `CREATIVE_SUITE_MOTION_EFFECT_WORKERS` set to `1`, `2`, `4`, and `8`,
restarting the app for each setting. After warm-up, collect three 10-second runs
per setting. Compare logged blur and full-frame render averages, decode timing,
rendered/coalesced counts, and CPU use; confirm the worker count in each sample
and that preview pixels remain identical. The override is temporary and does
not change the automatic default. For decoder-path validation, use a video layer
with playback metrics enabled. After warming up, compare repeated playback runs
before and after this change; inspect timestamp-seek attempts/successes/failures,
forward-decode completions and fallbacks, decode and full-frame timing, rendered
frames, coalesced requests, and CPU use. Confirm interactive scrubbing remains
responsive and export output is unchanged.
Motion Studio export is opaque and video-only; audio and alpha export remain
open. Then close the application. Overshoot-capable curves, additional
interpolation modes, advanced effects, platform validation, and performance
measurement remain open.

**Exit criteria:** a user can create, save, reopen, and preview a simple
composition without losing its layer or frame-rate data.

### 3. Motion design MVP

- [x] Extend transform keyframes with editable property curves and bounded
  cubic Bezier easing; retain Linear as the default and migrate v1/v2 documents
  as linear segments.
- [x] Add a Motion Studio-owned ordered effect stack for Gaussian Blur and
  Color Adjustment on text, vector-shape, raster-image, and video layers;
  persist it in `.motion` v4 and share evaluation across preview, playback,
  and export.
- [x] Complete the first standalone save/reopen, preview, and rendered-video
  export workflow; Motion Studio currently exports opaque video without audio.
- [ ] Profile representative compositions and validate export throughput,
  memory use, and codec behavior across target platforms.
- [ ] Address measured bottlenecks before expanding the MVP scope.

**Exit criteria:** the agreed MVP workflows pass regression coverage and
manual visual checks, including save/reopen, recovery, and heavy-composition
handling.

### 4. Video Editor integration and release readiness

- [ ] Validate composition handoff and updates between Motion Studio and the
  Video Editor without unnecessary media duplication.
- [ ] Document supported interchange behavior, project compatibility, and
  failure recovery.
- [ ] Validate installation, project paths, fonts, graphics drivers, and
  packaging on Windows, macOS, and Linux.
- [ ] Complete small, medium, and heavy project validation before release.

**Exit criteria:** both applications can exchange supported composition
references reliably and each release build passes its platform regression
gate.

### 5. Future research

- [ ] After the 2D MVP passes its performance and cross-platform validation,
  investigate audio-reactive 2D animation as the first expansion. Define its
  product boundary separately before implementation.
- [ ] Revisit animated masks, advanced effects and effect graphs, nested
  compositions, particles, 3D features, and node-based workflows only after the core 2D motion workflows
  meet their performance targets and a clear use case justifies their added
  complexity.

## Status legend

- `[ ]` Not started
- `[-]` In progress
- `[x]` Completed
- `[!]` Blocked or requiring a decision

Do not add target dates until capacity, platform support, and scope are
validated. Update this roadmap when a milestone, dependency, or decision
changes.
