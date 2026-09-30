# Motion Studio Roadmap

Status: **provisional; implementation started**. The standalone Motion Studio
shell, in-memory composition/layer model, navigation timeline, Media Pool,
image/video/text/shape layers, CPU preview, versioned native save/open, and
first-pass rendered video export are implemented under `apps/motion-editor/`.
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
  render performance for representative small, medium, and heavy compositions;
  document targets and any unmet limits.
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
  Save curves in `.motion` v3 while reading v1/v2 as Linear; keep the recovery
  wrapper at v1 and accept nested documents through v3.
- [x] Make Media Pool, Inspector, Timeline, and Graph Editor movable, resizable,
  tabifiable, floatable, and hideable Qt dock panels around a central Preview.
  Keep the Inspector tabs together, restore the first-run layout on request,
  and persist customized panel state globally in Motion Studio settings.
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

Manual Windows validation remains pending: verify the centered empty-state
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
save. Move, resize, tabify, float, and hide the Media Pool, Inspector, Timeline,
and Graph Editor; confirm the Graph Editor timeline button and **View** actions
stay synchronized, restart the app to verify layout restoration, then use
**View > Reset Panel Layout**. Then confirm the one-hour navigation range,
stepping, and separate drag extensions; import
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
v3 project and confirm v1/v2 projects retain linear interpolation; create
multiline Unicode text, change its font, size, alignment, box dimensions, and
alpha color; create
rectangles and ellipses, edit their dimensions, fill, optional stroke, and alpha;
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
and autosave does not overwrite the `.motion` file. Open **File > Export
Video...**, confirm composition resolution and frame-rate defaults, try a custom
resolution and quality profile, export overlapping image, video, text, and
shape layers with a blank lead-in, and play the result in a media player. Cancel
another export and verify an existing destination remains unchanged; choose an
unavailable encoder if one is offered and confirm a detailed log entry appears.
Motion Studio export is opaque and video-only; audio and alpha export remain
open. Then close the application. Overshoot-capable curves, additional
interpolation modes, effects, platform validation, and performance profiling
remain open.

**Exit criteria:** a user can create, save, reopen, and preview a simple
composition without losing its layer or frame-rate data.

### 3. Motion design MVP

- [x] Extend transform keyframes with editable property curves and bounded
  cubic Bezier easing; retain Linear as the default and migrate v1/v2 documents
  as linear segments.
- [ ] Add a simple effect set to the supported ordered text, vector-shape,
  raster-image, and video layers.
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

- [ ] Revisit animated masks, chained effects, nested compositions, particles,
  3D features, and node-based workflows only after the core 2D motion workflows
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
