# Motion Studio Roadmap

Status: **provisional; implementation started**. The standalone Motion Studio
shell, in-memory composition/layer model, navigation timeline, Media Pool,
raster clip layers, and CPU preview are wired into CMake under
`apps/motion-editor/`. Its initial product scope and readiness are documented
in [SCOPE_AND_READINESS.md](SCOPE_AND_READINESS.md). C++ and Qt 6 are provisional
implementation choices; no final language, renderer, or codec choices have
been made. Focused reuse libraries are tracked separately in
[REUSE_PLAN.md](REUSE_PLAN.md).

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
  still-image and video layers. Text and shape content are not rasterized yet.
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
  worker, coalesce rapid seeks, and ignore stale preview generations. Keyframes
  remain stored but are not edited or evaluated.
- [ ] Add keyframe editing interactions to the timeline.
- [ ] Add project save/load, versioned formats, undo/redo, autosave, and
  recovery for the first supported composition workflow.
- [-] Extend actionable local error logging and automated coverage for the
  remaining document, rendering, and application boundaries. Media insertion
  and preview failures are logged; persistence and export failures are not
  implemented yet.

The in-memory document/layer model has unit coverage, including explicit canvas
size, exact frame rate, media paths, layer order, timing, movement, and duration
limits. The empty application shell passed its offscreen startup test and a
manual Windows launch/close check. The centered empty-state New Composition
button uses the File menu's creation flow and has offscreen UI regression
coverage. Opening a composition preserves the current window state and geometry;
the app starts maximized unless the user restores it. The canvas viewer,
navigation ruler, timeline zoom and scrolling, layer rows, drag/drop, transforms,
time/frame display, and preview have offscreen coverage. The shared media catalog
and importer, plus the Motion Studio Media Pool, have regression coverage for
video and still-image imports, first-frame thumbnails, bins, renaming, offline
restoration, view modes, selection details,
and clearing on composition replacement. The Video Editor retains its
project-media and linked-image regressions.

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

The current preview displays the cached still-image frame or decodes the nearest
video frame using the shared `VideoPlaybackSession`, then composites visible
layers back-to-front with `FrameCompositor`. Decode and composition work runs
off the UI thread. Newer seek requests supersede queued work, stale generations
are ignored, and decode errors are logged with source path context without
stopping the worker. Preview is CPU-only and omits text, shape, audio,
continuous-playback, source-in-point, and keyframe evaluation.

Manual Windows validation remains pending: verify the centered empty-state
button opens composition creation and disappears after creation; confirm a
composition opens maximized and preserves a restored window size; confirm the
one-hour navigation range, stepping, and separate drag extensions; import
images and videos, organize bins, switch pool views, and restore offline items;
drag media to empty space and existing rows; verify snapping and front-to-back
order; select, move, hide, resize, and remove layers; edit transforms; seek
through image and video previews; exercise zoom buttons, slider, and Ctrl+wheel;
confirm the playhead stays anchored, scroll horizontally to inspect aligned
ruler and layer positions, move and resize clips at multiple zoom levels, and
extend the range only at its actual end, scrolling there if it is offscreen;
resize the viewer, Media Pool, inspector, and timeline; replace the composition
and confirm the pool resets; verify the timeline starts in Time mode, switch to
Frames and back,
and confirm the playhead and layer positions do not change; then close the
application. Keyframe editing, document
persistence, undo/redo, and export remain open.

**Exit criteria:** a user can create, save, reopen, and preview a simple
composition without losing its layer or frame-rate data.

### 3. Motion design MVP

- [ ] Add keyframes and editable property curves with documented interpolation
  behavior.
- [ ] Support ordered text, vector-shape, raster-image, and video layers with
  basic 2D transforms and a simple effect set.
- [ ] Complete the standalone save/reopen, preview, and rendered-video
  workflows and profile representative compositions.
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
