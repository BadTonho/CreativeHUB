# Archived completed roadmap items — image-editor

This file preserves completed checklist entries moved from [the active roadmap](../../image-editor/ROADMAP.md).
The active roadmap now focuses on open work, release gates, and required feature-to-verification coverage.
This archive is historical context; current behavior and compatibility contracts remain in the application documentation.

## Image Editor Roadmap / Milestones / 0. Readiness

- [x] Reserve `apps/image-editor/` and `docs/image-editor/`.

- [x] Select a standalone Qt Widgets executable as the first deliverable.

- [x] Bound the initial editor to crop, quarter-turn rotation, and horizontal
  or vertical flips on one raster image.

## Image Editor Roadmap / Milestones / 1. Standalone minimum editor

- [x] Add an independently buildable `creative-suite-image-editor` target.

- [x] Add a versioned `.cimg` editable document that references its original
  image and stores an ordered list of non-destructive operations.

- [x] Keep undo and redo in memory, preserve the original source, and support
  explicit relinking when the source path is unavailable.

- [x] Export flattened PNG and JPEG images, preserving PNG transparency and
  flattening JPEG output over a selectable background (white by default) with
  configurable quality. Add Quick Export for only the selected layer, retaining
  canvas bounds and respecting visibility and opacity.

- [x] Add local autosave snapshots, recovery, and bounded structured error
  logging.

- [x] Add self-contained blank canvases with standard and custom dimensions,
  selectable backgrounds, `.cimg` v2 persistence, and v1 document compatibility.

- [x] Add a locked Background and editable raster layers with visibility,
  opacity, ordering, rename, delete, painting, erasing, fixed-canvas transforms, and
  `.cimg` v5 persistence while retaining v1–v4 compatibility.

- [x] Add editable line, rectangle, and ellipse operations with creation,
  selection across visible layers, movement, resizing, style editing, deletion,
  Undo/Redo, and `.cimg` v6 persistence while retaining v1–v5 compatibility.
  New shapes each receive a dedicated `Shape N` layer using the existing
  layer structure; Background can be the insertion anchor without becoming
  editable.

- [x] Add general multi-selection for paint, eraser, and shape operations,
  including marquee selection, grouped movement and scaling, and `.cimg` v7
  persistent object IDs while retaining visual compatibility with v1–v6.

- [x] Add one-level layer groups with multi-selection, reordering and
  reparenting, combined transforms, single-pass group opacity, group Quick
  Export, undoable grouping/ungrouping/deletion, and `.cimg` v8 persistence
  while retaining v1-v7 compatibility.

- [x] Add canvas fit, zoom, pan, and drag-to-crop controls.

- [x] Add independent document tabs, new-tab creation/opening, active-tab
  replacement and close prompts, per-tab recovery snapshots, and linked-mode
  single-tab enforcement without changing `.cimg` or the host contract.

- [x] Pass Release build and automated tests for documents, edits, relinking,
  export, recovery, logging, and the UI boundary.

- [x] User-confirmed Windows validation of the Release build, Paint/Eraser
  switching after Shapes, export, `.cimg` save/reopen, editable shapes, general
  object selection, and layer groups.

- [x] Reopen the affected WebP image in the rebuilt Windows application UI;
  the owner confirmed it opened successfully. See
  [`MANUAL_VALIDATION.md`](../../image-editor/validation/MANUAL_VALIDATION.md).

- [x] Validate Windows Debug and Release build/install deployment and import
  of PNG, JPEG, BMP, WebP, and TIFF; macOS and Linux builds and interaction
  checks are deferred.

## Image Editor Roadmap / Milestones / 2. Video Editor linked-image compatibility

- [x] Add Media Pool and timeline actions to create or reopen linked documents.

- [x] Keep the source unchanged; store linked documents and PNG outputs beside
  the source. Timeline variants use an independent initial image copy.

- [x] Persist optional shared and per-clip references in `.csp` v10 and retain
  v1-v9 project compatibility.

- [x] Publish PNG after a successful `.cimg` save and refresh affected host
  images asynchronously. Unsaved changes are not streamed.

- [x] Resolve the Image Editor from a configured path, sibling installation or
  development build, or `PATH`; allow the user to locate it if unavailable.

- [x] Reject stale linked document saves using a lock and saved revision hash.

- [x] Cover project open, shared output, clip
  variants, stale project generations, and publication failures.

- [x] Confirm the basic linked edit/save workflow manually: saving in the
  Image Editor refreshed the Video Editor (user-confirmed; platform unspecified).

## Image Editor Roadmap / Milestones / 3. First editing release

- [x] Confirm the first-release workflow and validation profile: create
  thumbnails and social-media art, including editable text, using a
  representative 1920x1080 document with up to five layers and one group.
  Keep custom canvas sizes; this profile is not a maximum-size policy.

- [x] Implement basic editable text with multiline content, font family, size,
  color, alignment, move/width resize, horizontal growth while typing,
  Undo/Redo, and `.cimg` save/reopen.
  Preserve reading versions 1 through 8; the initial text release saved v9,
  and the mask extension now saves v10 while also reading v9.

- [x] Add automated regression coverage for text editing, rendering,
  selection, transforms, export, persistence, recovery, and linked PNG
  publication in `image_editor_core_test.cpp` and `image_editor_ui_test.cpp`.

## Image Editor Roadmap / Milestones / Raster layer mask extension (approved 2026-10-02)

- [x] Add one optional grayscale mask to each editable raster layer, including
  children of groups, with white default coverage and grayscale brush strength.

- [x] Add mask thumbnails and Add/Enable/Remove Layer Mask context actions;
  selecting a thumbnail directs Paint/Eraser to the mask without changing pixels.

- [x] Include masks in layer transforms, Undo/Redo, thumbnails, full and Quick
  Export, recovery, and linked PNG output.

- [x] Persist mask operations in `.cimg` v10, read v1–v9 unchanged, and retain
  recovery wrapper v1.

- [x] Add core/UI regressions and a Video Editor consumer regression for the
  published masked PNG. Background and group masks remain outside scope.

## Image Editor Roadmap / Milestones / Linked image layer extension (approved 2026-10-02)

- [x] Import a cancellable atomic batch from a dialog or canvas drop, with
  auto-oriented Qt decoding and file-name layers.

- [x] Add raster selection, movement, proportional/Alt scaling, free rotation,
  Shift snapping, core preview, gesture cancellation, and one history entry.

- [x] Keep masks and unselected operations fixed for image gestures; retain
  existing content-and-mask layer transforms.

- [x] Share decoded source pixels across previews, thumbnails, history, and
  export snapshots without embedding/copying original files.

- [x] Persist .cimg v11, migrate v1–v10 on save, and keep recovery wrapper v1.

- [x] Preserve unavailable references and support compatible selected-image
  relink; block dependent outputs without replacing previous output files.

- [x] Add core/UI and Video Editor PNG refresh regressions.

## Image Editor Roadmap / Milestones / Resizable canvas extension (approved 2026-10-04)

- [x] Add **Image > Canvas Size** with existing canvas presets, custom
  dimensions, and a centered-default 3×3 anchor control.

- [x] Move the source/base image, editable layers, groups, masks, and objects
  without scaling them. Keep original source dimensions for relinking.

- [x] Persist current bounds and base offset in `.cimg` v12; keep v1–v11 visual
  behavior and the recovery wrapper v1.

- [x] Include resized bounds in full export, Quick Export, recovery, and linked
  PNG publication; cover the Video Editor consumer refresh boundary.

- [x] Add core, UI, format, and Video Editor consumer regression coverage.

## Image Editor Roadmap / Milestones / Area Selection tool (approved 2026-10-04)

- [x] Add a separate Area Selection tool with Rectangle/Ellipse shapes and
  Replace/Add/Subtract modes; keep its temporary selection per document tab.

- [x] Clip new Paint and Eraser strokes, including mask strokes, and preserve
  clipping in transformed groups without changing object Selection behavior.

- [x] Keep selection state outside document dirty state and history; translate
  and crop it with anchored canvas resizing and restore it through resize
  Undo/Redo.

- [x] Persist per-stroke clipping geometry in `.cimg` v13; open v1–v12 with
  unrestricted legacy strokes.

- [x] Cover selection geometry and UI gestures, clipped rendering, masks,
  groups, save/reopen, legacy compatibility, export, and invalid complexity.

## Image Editor Roadmap / Milestones / Lasso tool (approved 2026-10-06)

- [x] Add Lasso as its own sidebar tool and implementation in
  `src/ui/tools/lasso/lasso_tool.*`; keep its contour logic out of the
  Area Selection shape menu and share the temporary selection/combine modes.

- [x] Capture unsmoothed canvas points, close the contour on release, clip the
  path to image bounds, and preserve the previous selection for degenerate or
  oversized gestures.

- [x] Verify the dedicated button, live preview, Replace/Add/Subtract modes,
  cancellation, geometry limits, and Paint clipping in the focused UI test;
  keep the manual visual check in [`MANUAL_VALIDATION.md`](../../image-editor/validation/MANUAL_VALIDATION.md).

## Image Editor Roadmap / Milestones / Bucket Fill tool (approved 2026-10-06)

- [x] Add a dedicated Bucket Fill tool and a 0–255 tolerance control that starts
  at 0; keep the tool active after a fill and add no keyboard shortcut.

- [x] Detect four-connected regions on the active layer or selected mask using
  maximum per-channel RGBA difference; apply brush color/alpha and restrict
  traversal and output to the active Area Selection.

- [x] Store changed fills as one undoable operation, replay them in order, and
  persist them in `.cimg` v14 while retaining v1–v13 read compatibility.

- [x] Cover exact/tolerant and alpha matching, diagonal disconnection,
  selection clipping, mask conversion, no-op history, Undo/Redo, and v14
  save/reopen in the focused regression tests.

## Image Editor Roadmap / Milestones / Magic Wand tool (approved 2026-10-06)

- [x] Add a dedicated Magic Wand tool and `magic_wand_config.h` with a
  session-only tolerance that defaults to zero, spans 0–255, and uses
  four-way connectivity.

- [x] Detect regions from raw editable layer or selected-mask pixels using
  maximum-channel RGBA distance; map selected regions through parent-group
  transforms and combine them with Area Selection's Replace/Add/Subtract modes.

- [x] Keep selection state temporary per tab, preserve the previous selection
  when geometry is rejected, and avoid document pixels, history, and `.cimg`
  changes. No keyboard shortcut or saved preference is added.

- [x] Cover algorithm limits, RGBA connectivity, toolbar interaction, masks,
  and selection combination in the focused Magic Wand UI verification.

- [x] Coalesce identical scanline runs, simplify their union to remove internal
  row edges from the selection outline, and move layer rendering and region
  detection off the UI thread for large uniform regions.

## Image Editor Roadmap / Milestones / Linear Gradient tool (approved 2026-10-06)

- [x] Add a dedicated Linear Gradient tool that drags from the current brush
  color to transparent, previews the composed canvas during the gesture, and
  clamps the drag endpoint to the canvas edge.

- [x] Apply gradients as one ordered operation on the active layer or selected
  mask, clipped by Area Selection, with premultiplied RGBA interpolation and
  no-op detection.

- [x] Persist gradients in `.cimg` v15 while preserving v1–v14 read support;
  add focused algorithm/session/persistence and canvas gesture regressions.

## Image Editor Roadmap / Milestones / Blur tool (approved 2026-10-06)

- [x] Add an independent Blur brush with a live composed preview, Escape
  cancellation, brush size 1–1024 px (default 12), and radius 0–100 px
  (default 10).

- [x] Blur only the active raster layer or selected mask in premultiplied RGBA,
  clip writes to brush coverage and Area Selection, and avoid history entries
  for radius-zero or pixel-identical gestures.

- [x] Persist changed strokes as ordered `.cimg` v16 operations; retain reading
  support for v1–v15 and cover preview/commit equality, mask grayscale,
  selection, Undo/Redo, and save/reopen in focused regressions.

## Image Editor Roadmap / Milestones / Object Selection tool extraction (approved 2026-10-05)

- [x] Move temporary object-selection state, hit testing, marquee gestures,
  transform handles, move/resize/rotation geometry, cancellation, and selection
  overlays into a dedicated UI tool module.

- [x] Keep `ImageCanvas` public signals stable; continue routing selection,
  transient composition, and committed geometry through the existing window
  and document-session flow.

- [x] Add direct tool-state regression coverage and retain canvas/raster UI
  coverage for selection, transforms, cancellation, previews, and history.

## Image Editor Roadmap / Milestones / Eyedropper tool (approved 2026-10-06)

- [x] Add an Eyedropper sidebar tool that samples RGBA from the committed
  visible composite, ignores fully transparent pixels, and updates the paint
  color while remaining active.

- [x] Ignore clicks outside the image and keep sampling separate from document
  edits, Undo/Redo, and the `.cimg` format.

- [x] Cover RGBA handoff to Paint and confirm sampling emits no edit request.

## Image Editor Roadmap / Milestones / 4. Future expansion

- [x] Add opt-in Image Editor timing/resource collection and a deterministic
  synthetic benchmark without changing `.cimg` or adding release dependencies.

- [x] Run the reference, mask-heavy, stroke-heavy, large-image,
  repeated-source, and export profiles on the maintainer's Windows reference
  PC; see the version 2 [baseline report](../../image-editor/performance/performance-baseline-windows-2026-10-06-v2.json).


## Historical implementation and verification note

The shared GPU/dependency regression gate on 2026-10-10 corrected unqualified
`ImageDocumentStore` references in the core test's persistence assertions. This
is a test compilation fix; raster save/recovery assertions also use the current
document-version constant while retaining older migration fixtures. The Magic
Wand UI geometry-limit fixture uses alternating connected spans that exceed the
path limit after adjacent equal spans are merged. Document versions and application behavior retain
their existing contracts. Its execution result is tracked with the shared
[Windows/NVIDIA pipeline evidence](../../video-editor/gpu/GPU_PIPELINE.md).


## Historical implementation and verification note

**Automated run (2026-10-02, Windows):** the Image Editor compiled in Debug
and Release. The final Debug focused run passed 11/11 tests: the eight Image
Editor tests plus Video Editor main-window, application-media, and workspace
switch tests. The complete Release CTest suite passed 64/64 tests. New coverage
includes all five formats, EXIF orientation, shared duplicate sources, atomic
batch history, fit/drop placement, transformed groups, object deletion,
fixed masks, geometry previews, file-dialog/drop gestures, Alt scaling, Shift
rotation, Esc, cancellation, v11 persistence, v10 migration, Save As, recovery,
source diagnostics, compatible and same-path relink history, protected outputs,
and producer/consumer PNG refresh. Existing fixtures cover v1–v9 migration.
`git diff --check` passed. No new manual or macOS/Linux acceptance is recorded.

- [x] Import a cancellable atomic batch from a dialog or canvas drop, with
  auto-oriented Qt decoding and file-name layers.
- [x] Add raster selection, movement, proportional/Alt scaling, free rotation,
  Shift snapping, core preview, gesture cancellation, and one history entry.
- [x] Keep masks and unselected operations fixed for image gestures; retain
  existing content-and-mask layer transforms.
- [x] Share decoded source pixels across previews, thumbnails, history, and
  export snapshots without embedding/copying original files.
- [x] Persist .cimg v11, migrate v1–v10 on save, and keep recovery wrapper v1.
- [x] Preserve unavailable references and support compatible selected-image
  relink; block dependent outputs without replacing previous output files.
- [x] Add core/UI and Video Editor PNG refresh regressions.
