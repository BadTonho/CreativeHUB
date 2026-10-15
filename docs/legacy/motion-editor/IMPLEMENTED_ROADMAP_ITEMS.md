# Archived completed roadmap items — motion-editor

This file preserves completed checklist entries moved from [the active roadmap](../../motion-editor/ROADMAP.md).
The active roadmap now focuses on open work, release gates, and required feature-to-verification coverage.
This archive is historical context; current behavior and compatibility contracts remain in the application documentation.

## Motion Studio Roadmap / Milestones / 0. Scope and readiness

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

## Motion Studio Roadmap / Milestones / 1. Technical validation and gap audit

- [x] Audit the Video Editor's Qt 6, FFmpeg, CPU composition, and OpenGL
  presentation path against the Motion Studio MVP. Record evidence that
  transfers and gaps that remain in
  [SCOPE_AND_READINESS.md](../../motion-editor/SCOPE_AND_READINESS.md). This is a source-level
  audit; GPU runtime and Windows validation remain pending.

- [x] Document provisional animation and composition contracts against the
  Motion Studio layer and curve workflows; preserve Video Editor regression
  coverage at the shared-library boundary. Motion Studio now has consumer-side
  model and preview coverage; the contracts remain provisional pending Windows
  and manual visual validation.

- [x] Extract the FFmpeg playback session behind a neutral observer boundary;
  preserve Video Editor preview metrics in an application adapter.

## Motion Studio Roadmap / Milestones / 2. Composition foundation

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
  Curves were introduced in `.motion` v3; v4 adds per-layer effects and v5
  adds video source-in frames plus saved revisions for linked writes. Read
  v1/v2 curves as Linear, and keep the recovery wrapper at v1 while accepting
  nested documents through v5.

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
  preview worker and composite them through the shared CPU compositor by
  default; an experimental opt-in OpenGL path can compose the same raster data.
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
  Shared Windows FFmpeg now includes experimental NVENC capability discovery.
  Motion Studio displays those choices but keeps experimental encoders out of
  its initial selection; its RGBA export boundary and CPU decoding are unchanged.

- [x] Add configurable one-second preview performance samples and per-job
  export summaries. The shared system-monitor library provides best-effort
  process CPU and memory readings; Motion Studio records render-stage timing,
  request-to-viewer-paint latency, coalesced/stale work, per-effect application
  counts and timings, and export throughput. Preview metric schema v3 separates
  Gaussian Blur and Color Adjustment timing and records the effective effect
  worker count without recording layer names or content.
  This diagnostic logging does not replace representative-project profiling or
  later platform validation.

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

## Motion Studio Roadmap / Regression coverage index / 3. Motion design MVP

- [x] Extend transform keyframes with editable property curves and bounded
  cubic Bezier easing; retain Linear as the default and migrate v1/v2 documents
  as linear segments.

- [x] Add a Motion Studio-owned ordered effect stack for Gaussian Blur and
  Color Adjustment on text, vector-shape, raster-image, and video layers;
  persist it in `.motion` v4 and share evaluation across preview, playback,
  and export. Version 5 adds video source-in frames and saved revisions.

- [x] Reuse `creative-suite::effects` for fused Color Adjustment processing
  while retaining Motion's effect model, Gaussian Blur, Inspector, history,
  and `.motion` schema.

- [x] Complete the first standalone save/reopen, preview, and rendered-video
  export workflow; Motion Studio currently exports opaque video without audio.

- [x] Implement shared Rectangle/Ellipse masks with geometry animation and
  editable Fusion interchange; see [Masks](../../motion-editor/MASKS.md). Manual/platform acceptance
  and representative performance measurements remain separate.

## Motion Studio Roadmap / Regression coverage index / 4. Video Editor integration and release readiness

- [x] Implement editable handoff from video Timeline clips and image/video
  Media Pool items. Saved Motion compositions publish a new video render;
  unsaved changes are not streamed.

- [x] Store links in Video Editor `.csp` v24, migrate `.motion` v1-v4 with
  zero source-in frames, and preserve linked clip position, duration, and
  original audio.

- [x] Document supported interchange behavior, project compatibility, and
  publication and missing-media recovery.


## Historical implementation and verification note

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
video and still-image imports, runtime-based image decoding, single-frame GIF
acceptance, multi-frame rejection, first-frame thumbnails, bins, renaming,
offline restoration, view modes, selection details,
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
