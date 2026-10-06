# Image Editor Roadmap

Status: **standalone minimum and multi-document tabs implemented; basic editable text, raster layer masks, linked image layers, canvas resizing, and regression
coverage implemented; manual text validation, packaging, and linked-image
acceptance remain pending; macOS and Linux validation deferred**.
Current application version: **Beta 0.1.3**.
This roadmap covers the independent application under `apps/image-editor/`.
The Video Editor handoff implementation already exists as a bounded prototype,
but its acceptance is gated on passing the standalone manual and Windows
packaging checks in Milestone 1. This gate orders Image Editor acceptance
milestones; the three application tracks may be developed in parallel.
The approved first editing-release direction is thumbnails and social-media
art, with basic editable text. Its reference workload is a 1080p document with
up to five layers and one group, measured on the maintainer's reference PC:
AMD Ryzen 5 3600, 32 GB RAM, NVIDIA GeForce GTX 1660 SUPER with 6 GB VRAM, and
Windows 11. This profile is for validation, not a new canvas-size limit; exact
hardware requirements remain measurement-based. See [scope](SCOPE.md).

## Principles

- Keep the Video Editor's stability work on track while developing the Image
  Editor as a separate executable.
- Preserve linked source images; store editable document operations in a
  versioned format.
- Keep each standalone document to one raster image with a Background,
  editable raster layers and masks, and one-level layer groups. Defer retouching,
  color adjustment, and effect systems until they are justified by validated
  workflows. The application window can hold multiple independent raster
  documents in tabs; this does not change the `.cimg` format or linked-image
  contract.
- Basic editable text is implemented for the first editing release: multiline
  content, font family, pixel size, color, alignment, move/width resize,
  horizontal growth to the canvas edge while typing, wrapping, Undo/Redo,
  `.cimg` save/reopen, recovery, and export. Manual visual
  validation remains pending. Advanced typography, text outlines, and text
  effects remain deferred.
- Use Qt image I/O and deploy the plugins required for the documented input
  formats. Track Qt Image Formats and its codec notices for distribution.
- Keep compatibility with the Video Editor as an independently testable
  milestone after the standalone minimum passes its exit criteria. The current
  implementation refreshes host output after a successful save; unsaved live
  previews remain outside its scope.
- Complete and accept Milestone 1 before accepting Milestone 2. Existing linked
  workflow code remains a prototype during standalone validation; do not
  expand its release scope before the standalone gate passes.
- Consider Windows, macOS, and Linux paths, packaging, and image dimensions
  from the beginning.

## GPU acceleration planning

The [GPU acceleration plan](GPU_ACCELERATION_PLAN.md) separates future work
into the rendering contract/adapter, layer-mask-group composition, interactive
editing/presentation, export/linked PNG publication, and measured acceptance.
It coordinates reuse with the Motion Studio and Video Editor plans while
preserving Image Editor's transparent output and editable operations.
Video Editor is the first consumer of the implemented shared OpenGL adapter;
Image Editor adoption remains planned.

**Status: documentation only; implementation deferred.** Current image
rendering remains on the CPU. No GPU editing, export gain, or driver acceptance
is claimed; each future delivery includes its own coverage and acceptance gate.

## Milestones

### 0. Readiness

- [x] Reserve `apps/image-editor/` and `docs/image-editor/`.
- [x] Select a standalone Qt Widgets executable as the first deliverable.
- [x] Bound the initial editor to crop, quarter-turn rotation, and horizontal
  or vertical flips on one raster image.

**Exit criteria:** a standalone scope and build boundary are agreed.

### 1. Standalone minimum editor

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
  [`MANUAL_VALIDATION.md`](MANUAL_VALIDATION.md).
- [x] Validate Windows Debug and Release build/install deployment and import
  of PNG, JPEG, BMP, WebP, and TIFF; macOS and Linux builds and interaction
  checks are deferred.

**Exit criteria:** the application builds independently, opens and edits a
raster image without changing its source, saves and reopens `.cimg`, exports
PNG/JPEG, and recovers an autosave. Required image plugins are present in the
packaged application. Automated and manual checks pass.

### 2. Video Editor linked-image compatibility

The linked-image implementation is already present as a bounded prototype.
Begin this milestone's acceptance only after Milestone 1 exit criteria pass;
until then, keep the implementation stable and use its existing regression
coverage to catch regressions.

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
- [ ] Validate transparent images, repeated Media Pool uses, clip variants,
  save/reopen, and large files in both applications on Windows. macOS and Linux
  validation is deferred.

**Exit criteria:** automated and manual checks confirm that saving a linked
image refreshes every intended Video Editor use without modifying the original
or leaving stale previews. Unsaved live preview streaming remains out of scope.

### 3. First editing release

Proceed after the compatibility milestone. Keep advanced image workflows
separate from this release.

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
- [ ] Complete the manual visual text checks in
  [`MANUAL_VALIDATION.md`](MANUAL_VALIDATION.md). Their pending status does not
  block continuing implementation work.
- [ ] Validate recovery, export, and linked asset handoff as a complete
  workflow.

**Exit criteria:** the first release workflows pass automated regression
coverage and manual visual validation on supported platforms. Motion Studio
may proceed on a parallel track; cross-application integration depends on
validated contracts and producer/consumer regression coverage.

### Raster layer mask extension (approved 2026-10-02)

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

This implementation extension does not complete the earlier manual or platform
acceptance gates. Its documented UI checklist has no owner-recorded result yet.

### Linked image layer extension (approved 2026-10-02)

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
- [ ] Record platform/manual visual acceptance of these gestures.

### Resizable canvas extension (approved 2026-10-04)

- [x] Add **Image > Canvas Size** with existing canvas presets, custom
  dimensions, and a centered-default 3×3 anchor control.
- [x] Move the source/base image, editable layers, groups, masks, and objects
  without scaling them. Keep original source dimensions for relinking.
- [x] Persist current bounds and base offset in `.cimg` v12; keep v1–v11 visual
  behavior and the recovery wrapper v1.
- [x] Include resized bounds in full export, Quick Export, recovery, and linked
  PNG publication; cover the Video Editor consumer refresh boundary.
- [x] Add core, UI, format, and Video Editor consumer regression coverage.
- [ ] Record the manual anchor, export, save/reopen, and linked refresh checks in
  [`MANUAL_VALIDATION.md`](MANUAL_VALIDATION.md).

Automated tests are registered in the feature-to-verification index below.
Manual and cross-platform acceptance remain pending until their outcomes are
recorded.

### Area Selection tool (approved 2026-10-04)

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
- [ ] Complete the native UI and linked PNG producer/Video Editor consumer
  checks in [`MANUAL_VALIDATION.md`](MANUAL_VALIDATION.md).

The manual visual and linked-image acceptance remains pending; automated
coverage is indexed below.

### Object Selection tool extraction (approved 2026-10-05)

- [x] Move temporary object-selection state, hit testing, marquee gestures,
  transform handles, move/resize/rotation geometry, cancellation, and selection
  overlays into a dedicated UI tool module.
- [x] Keep `ImageCanvas` public signals stable; continue routing selection,
  transient composition, and committed geometry through the existing window
  and document-session flow.
- [x] Add direct tool-state regression coverage and retain canvas/raster UI
  coverage for selection, transforms, cancellation, previews, and history.
- [ ] Complete the owner-run selection and transform checks in
  [`MANUAL_VALIDATION.md`](MANUAL_VALIDATION.md).

The tool extraction does not change the `.cimg` format. Manual visual
acceptance remains with the maintainer; automated coverage is indexed below.

### 4. Future expansion

- [ ] Revisit retouching, color adjustments, and larger effect sets only
  when user workflows and performance measurements justify them.
- [ ] Profile the representative 1080p document on the maintainer's reference
  PC; record test conditions and results before setting numerical hardware
  requirements. Use additional systems before generalizing those requirements.

## Regression coverage index

Automated evidence below is registered in
[`apps/image-editor/tests/CMakeLists.txt`](../../apps/image-editor/tests/CMakeLists.txt).
Manual cases and their recorded status are in
[`MANUAL_VALIDATION.md`](MANUAL_VALIDATION.md).

**Owner-reported Windows status (2026-10-01):** all currently implemented
Image Editor workflows had been exercised on Windows 11. A later WebP import
attempt exposed a runtime deployment regression. Matching Qt image-format
plugins are now copied into the application build and install layouts, and
Debug/Release regression tests pass through the deployed application plugin
path. The Image Editor tests passed 5/5 in both configurations; the full
Release suite passed 61/61. The owner confirmed the affected WebP opens in the
rebuilt Windows UI. Other-platform package validation remains outstanding.

**Automated text and linked-image regression run (2026-10-01):** the Image
Editor application builds in Debug and Release. All five Image Editor CTest
tests and three Video Editor consumer tests passed in both configurations
(8/8 each). This run covers the Image Editor core/UI producer, the deployed
image formats, export UI, linked-image refresh in the Video Editor main window,
application media preparation, and `.csp` persistence. The complete Release
CTest suite also passed 61/61. These automated results do not complete the
pending manual text review or linked-image acceptance checklist.

**Text editor typing, selection, and sizing regression (2026-10-01):** while
the text editor has keyboard focus, a temporary application-level filter keeps
unmodified keys from triggering window shortcuts such as Paint (`B`) or Eraser
(`E`), including shortcut events without text payloads. While typing, the box
grows to fit its longest line up to the canvas edge, then wraps and expands
vertically. Width includes the native editor's rounded font metrics at the
current zoom, viewport frame, document margins, and caret space. Height is
measured from its actual text blocks after applying the width. This prevents
accidental wrapping into a clipped, scrolled line below 100% zoom. The editor's
geometry is updated after the key event completes so resizing does not interrupt
text layout or cursor handling. The native editor
widget draws live text, caret, and selection together; the canvas renderer draws
text after confirmation. This avoids a duplicate live text layer beneath the
native selection highlight. Editing an existing text temporarily hides
its old rendered copy, and cancelling restores it. Clicking an empty canvas
location starts editing with the default box width; a horizontal drag sets a
custom initial width. Reopening unchanged text keeps its saved width. The UI
test verifies live rendered pixels, cursor advancement, native text selection
and highlight after expansion, replacement of selected text, editor visibility
during expansion, cancellation restoration, insertion at the beginning when
reopening existing text, and caret repositioning during click-to-create (`ABC`,
click before `A`, then `DE` produces `DEABC`).

The offscreen test missed the native Windows font/layout difference. A new
`testTextEditorGrowthLayout` regression in `image_editor_ui_test.cpp` reproduced
the failure on Windows after two typed characters before the fix. It checks
each character's layout, visible caret, cursor hit testing, mouse selection and
replacement after expansion, canvas-edge wrapping, and explicit newlines on a
1080p canvas with 48 px text and both narrow and default initial widths.
The [CMake registration](../../apps/image-editor/tests/CMakeLists.txt) also runs
it as `creative-suite-image-editor-native-text` with `QT_QPA_PLATFORM=windows`
on Windows; other platforms retain the offscreen UI test. The owner subsequently
confirmed that normal typing with the default settings works again in the
application. The remaining detailed text checks and platform acceptance are
still tracked in the manual checklist.

`testWindowTextGrowth` also creates a transparent Full HD canvas through the
New Canvas dialog, clicks the Text tool and canvas through the window, and
checks displayed text, caret, mouse selection, replacement, and burst typing
as the frame grows. On Windows it reads the window's displayed pixels without
forcing a QWidget redraw; `--native-keyboard` optionally exercises Windows
keyboard input while verifying that the test window owns foreground focus.
The offscreen fixture uses a desktop-sized window and a shorter heading to
accommodate its wider fallback glyphs without reaching the canvas edge;
wrapping remains covered separately by `testTextEditorGrowthLayout`.

On 2026-10-01, the UI, native text, and export UI CTest cases passed 3/3 in
Debug and Release. The final complete Release suite passed 62/62, with no
failures or skipped tests. Its initial run passed 61/62 because the Video
Editor main-window test executable predated its current activation-wait source;
that target was rebuilt without changing Video Editor code before the final
run. The normal Release Image Editor executable was rebuilt with this fix.

The follow-up run with the full-window scenario and corrected offscreen
fixture passed all three focused Debug tests and the complete Release suite
(62/62, no failures or skips).

**Raster layer mask regression run (2026-10-02):** the Image Editor application
and the full repository build succeeded in Release on Windows. All seven Image
Editor tests and the Video Editor main-window consumer passed (8/8), followed
by the complete Release CTest suite (63/63, no failures or skipped tests).
The core regressions cover grayscale/alpha painting, erasing, history,
mirrored transforms, operation limits, groups, thumbnails, malformed masks,
v1–v9 compatibility, v10 persistence, recovery wrapper v1, and PNG/JPEG exports.
The UI regression exercises mask context actions, thumbnail targeting, gesture
cancellation, Undo/Redo, and saved linked PNG publication. Switching targets
updates selection decorations without rebuilding the tree; existing mixed
group/layer selection and right-clicking another row retain their selections.
The Video Editor consumer refreshes a PNG generated from a real masked v10
document and checks its alpha and existing clip-variant isolation.
The application also built in Debug, where all seven Image Editor tests and
three Video Editor consumer tests (main window, application media, and project
persistence) passed, for 10/10 focused tests. These runs do not record manual
mask acceptance or macOS/Linux results.

Contextual deletion was built in Debug and Release on Windows on 2026-10-02.
All eight focused Image Editor tests passed in Debug; the complete Release
CTest suite passed 64/64 with no failures. The deletion regressions cover
contextual keyboard routing, explicit buttons/menus, batch history, Background
and editor protection, preview cancellation, source preservation, missing
references, thumbnails, persistence, exports, and linked PNG publication.
These automated runs do not record native visual acceptance or macOS/Linux
results.

| Behavior group | Automated evidence | Manual evidence and status |
| --- | --- | --- |
| Contextual object/layer/group deletion | `image_editor_raster_test.cpp` (`creative-suite-image-editor-raster`), `image_editor_deletion_ui_test.cpp` (within `creative-suite-image-editor-ui`), existing layer/group core tests | Automated coverage present for button/menu/Delete focus routing, multiple selection, masks, Background, text/rename/numeric fields, gesture cancellation, Undo/Redo, thumbnails, save/reopen, missing sources, exports, and linked PNG output. Native/platform checks are documented in `MANUAL_VALIDATION.md`; no manual result recorded. |
| Document editing, canvas, layers, groups, shapes, object selection, transforms, undo/redo | `image_editor_ui_test.cpp` (`testShapeToolState`, `testObjectSelectionToolState`, `testGeneralCanvasSelection`; `creative-suite-image-editor-ui`), `image_editor_raster_ui_test.cpp` (`testRasterImagesUi`; same CTest target), and `image_editor_core_test.cpp` (`creative-suite-image-editor-core`) | Automated coverage includes shape preview/geometry, Shift constraints, cancellation, invalid dimensions, topmost hit testing, marquee and Shift selection, mixed-object movement, resize/rotation, confirmation, and history. Windows Release focused Image Editor tests passed 5/5 and full CTest passed 70/70 on 2026-10-05. The existing interaction checklist is in `MANUAL_VALIDATION.md`; visual validation of the Shape Tool extraction remains with the owner. Cross-platform visual checks remain pending. |
| Stateless object edit preparation and session commit boundary | `image_editor_core_test.cpp` (`testImageDocumentObjectEditor`, `testGeneralObjectOperations`, shape/text session tests; `creative-suite-image-editor-core`) | Direct tests cover shape/text creation and editing, style changes, mixed transforms of paint/erase/raster/shape/text objects, group coordinate reversal, deletion, ID and geometry rejection, stack limits, and no mutation during preparation. Existing session tests cover one-edit Undo/Redo, persistence, and rendering. Affected Release targets compiled and the configured Windows CTest suite passed 12/12 on 2026-10-05. No visual UI change is part of this internal refactor. |
| Structural layer/group edits and selection/history | `image_editor_core_test.cpp` (`testImageLayerStackEditor`; `creative-suite-image-editor-core`), plus existing `creative-suite-image-editor-raster` and `creative-suite-image-editor-ui` integration coverage | Direct tests cover adding, deleting, grouping, ungrouping, moving between root and groups, ordering, resulting selection, the 512-item limit, and rejected operations without source mutation. The 11 focused tests and full CTest suite passed (70/70) on Windows Release on 2026-10-05. The existing layer/group interaction checklist remains in `MANUAL_VALIDATION.md`; GUI verification is left to the owner. |
| Undo/Redo snapshot storage and transitions | `image_editor_core_test.cpp` (`testImageDocumentHistory`; `creative-suite-image-editor-core`) and existing session integration coverage for editing, layer/group/mask/raster restoration, selection, and grouped opacity | Direct history tests cover empty state, ordering, redo clearing after a new edit, the 100-entry cap, clear, selected IDs, and implicitly shared raster image references. Session integration remains responsible for validating selections, restoring resources, and invalidating thumbnails. The eight focused Image Editor tests and full CTest suite passed (12/12) on Windows Release on 2026-10-05. Visual UI validation is not applicable. |
| Layer drag reordering and panel row preservation | `image_editor_core_test.cpp` (`testImageLayerStackEditor`; `creative-suite-image-editor-core`) verifies root layers moved in both directions remain in the candidate stack; `image_editor_ui_test.cpp` (`creative-suite-image-editor-ui`) verifies up/down reordering preserves every panel row and selection | The panel retains the dragged item ID and lets the committed document remain authoritative after a drop, avoiding a second Qt-side row removal after refresh. The owner checklist in `MANUAL_VALIDATION.md` checks that rows and visible content remain present when dragging both directions. Automated core/UI coverage is present; native drag verification remains with the owner. |
| Crop gesture, preview, image-edge mapping, document edit and Undo/Redo | `image_editor_ui_test.cpp` (`testCropToolState`, `testCropCanvasMapping`, and crop window integration; `creative-suite-image-editor-ui`) and `image_editor_core_test.cpp` (`applyCrop` bounds/history cases; `creative-suite-image-editor-core`) | Direct coverage checks preview, forward/reverse corner drags, image-bound clipping, cancellation and regions too small to apply. Window integration checks the emitted pixel rectangle marks the document edited and that Undo/Redo updates history state. Cropping layer content keeps the canvas dimensions fixed. Visual crop preview and edge behavior are documented in `MANUAL_VALIDATION.md`; owner verification remains pending. Windows Release focused Image Editor tests passed 6/6 and full CTest passed 70/70 on 2026-10-05. |
| Canvas Size dimensions, all anchors, translation of source/layers/groups/masks, Undo/Redo, and v12 persistence | `image_editor_core_test.cpp` (`creative-suite-image-editor-core`), `image_editor_ui_test.cpp` (`creative-suite-image-editor-ui`) | Automated coverage is present for all nine anchors, extension/reduction, custom background/transparency, edits and persistence. Manual dialog, save/reopen, export, recovery and linked output checks are documented in `MANUAL_VALIDATION.md`; outcome pending. |
| Area Selection geometry, per-tab UI, clipping, masks, groups, resize, export, and stroke Undo/Redo | `image_editor_ui_test.cpp` (`testAreaSelectionToolState`, `testAreaSelectionToolUi`; `creative-suite-image-editor-ui`) and `image_editor_core_test.cpp` (`creative-suite-image-editor-core`) | Automated coverage checks rectangle/ellipse gestures, Add/Subtract previews and results, cancellation, shortcuts, bounded painting, masks, transformed groups, save/reopen, and export. Native brush/eraser and linked PNG checks are documented in `MANUAL_VALIDATION.md`; outcome pending (**P1 validation**). |
| `.cimg` persistence, validation, and backward compatibility | `image_editor_core_test.cpp` (`testImageDocumentCodec`; `creative-suite-image-editor-core`) and `image_editor_mask_test.cpp`; migration fixtures for v1–v12, v9 text validation, v10 masks, v12 canvas bounds, v13 stroke clips, paths, recovery, and write failures | Direct codec coverage checks v13 encode/decode, v3 migration, and rejection of unsupported versions; existing store tests cover legacy migrations, invalid data, relative raster references, recovery, and atomic write failures. Eight focused Image Editor tests and full CTest passed (12/12) on Windows Release on 2026-10-05. Cross-platform validation remains pending. |
| Stateless document rendering, previews, thumbnails, and image export | `image_editor_core_test.cpp` (`testStatelessDocumentRenderer`; `creative-suite-image-editor-core`), `creative-suite-image-editor-raster`, `creative-suite-image-editor-masks`, image-format tests, and `creative-suite-image-editor-export-ui` | Direct core coverage checks composition, selected layer/group output, excluded objects, and cancellation. Existing regression tests cover masks, groups, imported raster resources, thumbnail caching, PNG/JPEG export, and export UI. The affected Release targets compiled; focused tests passed 11/11 and full CTest passed 70/70 on Windows on 2026-10-05. No visual UI change is part of this refactor. |
| Recovery, relinking, and error logging | `image_editor_core_test.cpp` | The owner reports current recovery workflows working on Windows; the per-scenario record is not maintained. Cross-platform validation remains pending. |
| Import formats, decoding, and UI job lifecycle | `image_editor_raster_test.cpp` (`creative-suite-image-editor-raster`), `image_editor_raster_ui_test.cpp` (`creative-suite-image-editor-ui`), `image_editor_format_test.cpp` (`creative-suite-image-editor-image-formats`), and `creative-suite-image-editor-deployed-image-formats` | Core tests cover PNG/JPEG/BMP/WebP/TIFF decoding, corrupt/unsupported files, limits, and cancellation. Direct UI-controller checks cover ready and failed results; integration coverage checks file-dialog import, canvas drop, atomic batches, relinking, cancellation without document mutation, and safe return after the worker finishes. The three focused tests passed and full CTest passed (12/12) on Windows Release on 2026-10-05; other-platform packaging remains pending (**P1 validation**). |
| Flattened export, Quick Export, export dialogs, and UI job lifecycle | `image_editor_core_test.cpp`, `image_editor_export_ui_test.cpp` (`creative-suite-image-editor-export-ui`) | Integration coverage checks JPEG/PNG options and preferences, transparency, selected layer/group scope, progress, and cancellation. Direct controller checks cover success and output failure while worker tests cover rendering/finalization cancellation. The export controller owns worker/thread/progress/cancel lifecycle; the window keeps path, JPEG preference, scope, snapshot, and user feedback. The focused export test and configured Windows Release CTest suite passed (12/12) on 2026-10-05. Cross-platform package checks remain pending. |
| Canvas, tools, layers, shortcuts, UI interactions, and window teardown | `image_editor_ui_test.cpp` (`creative-suite-image-editor-ui`), including `testWindowTeardownWithFocusedTextEditor` | Automated UI coverage now destroys the window while its text editor has focus, exercising the focus-change callback during child teardown. The Windows Release UI test passed ten consecutive runs and the complete CTest suite passed 72/72 on 2026-10-05. The owner reports current UI workflows exercised on Windows 11; cross-platform visual checks remain pending. |
| Tool options toolbar state and shape palette selection | `image_editor_ui_test.cpp` (`creative-suite-image-editor-ui`) directly exercises control synchronization, state updates, emitted requests, and palette selection; existing window integration covers brush, shape, text, and area-selection workflows | The Image Editor app and test targets compiled in Windows Release; the Image Editor CTest suite passed (12/12), and the configured workspace suite passed (70/70) on 2026-10-05. Existing Qt object names and manual tool-options and shape-palette steps in `MANUAL_VALIDATION.md` remain the compatibility checks. |
| Multiple document tabs and per-tab recovery | `image_editor_ui_test.cpp` (`creative-suite-image-editor-ui`), especially `testDocumentTabs` and `testMultiDocumentRecovery`, plus `creative-suite-image-editor-window-close` (`testWindowCloseAcrossDirtyTabs`): current/new-tab creation and opening, duplicate `.cimg` selection, active-tab replacement, reordering and switching with per-tab session/history/selection/zoom isolation, import targeting, Save/Discard/Cancel, failed-save retention, last-tab empty state, multi-snapshot restore and autosave; full-window close across dirty tabs, including cancellation partway through, active-tab restoration, recovery snapshot retention/cleanup, and persisted Save/Discard choices | The isolated Windows Release close test passed five consecutive runs. After the window-teardown fix, the Image Editor UI test passed ten consecutive runs and the complete Release CTest suite passed 72/72 on 2026-10-05. Native visual checks and macOS/Linux validation remain pending (**P2 validation**). |
| Video Editor linked-image producer/consumer workflow | Producer: `image_editor_ui_test.cpp`; consumers: `application_media_services_test.cpp` (`creative-suite-main-editor-application-media`), `main_window_integration_test.cpp` (`creative-suite-main-editor-main-window`), and `project_file_test.cpp` (`creative-suite-main-editor-project`) | Automated producer/consumer regression tests pass in Debug and Release. The owner reports the basic linked edit/save workflow working on Windows, but the full scenario and cross-platform acceptance remain pending (**P1 validation**). |
| Linked image import, geometry, sources, v11 raster references, v12 canvas bounds, v13 clipped strokes, export and publication | `image_editor_raster_test.cpp` (`creative-suite-image-editor-raster`), `image_editor_ui_test.cpp`, Video Editor `main_window_integration_test.cpp` | Existing automated coverage checks PNG publication and host refresh; Area Selection's linked PNG acceptance is documented in `MANUAL_VALIDATION.md`. macOS/Linux and manual acceptance remain pending. |
| Raster layer masks and shared stroke geometry | `image_editor_mask_test.cpp` (`creative-suite-image-editor-masks`), `image_editor_mask_ui_test.cpp` (within `creative-suite-image-editor-ui`); Video Editor `main_window_integration_test.cpp` when both apps are enabled | Automated coverage includes direct `ImageDocumentGeometry` group/clip/bounds tests, direct `ImageLayerMaskEditor` preparation and no-mutation tests, plus editing, transforms, previews, Undo/Redo, persistence, recovery, export, thumbnail targeting, and linked PNG producer/consumer behavior. The Release application and affected targets compiled on Windows; 11 focused tests and full CTest passed 70/70 on 2026-10-05. Mask UI checks are documented in `MANUAL_VALIDATION.md`; no manual result recorded. |
| GPU composition, editing presentation, and export | No GPU implementation or direct GPU boundary tests in this delivery | Planned, not implemented. Required CPU/GPU comparisons, native checks, and PNG producer/consumer gates are recorded in [GPU_ACCELERATION_PLAN.md](GPU_ACCELERATION_PLAN.md). |
| First-release editable text | `image_editor_core_test.cpp` and `image_editor_ui_test.cpp` (`testTextToolState`, `testTextEditorGrowthLayout`, `testWindowTextGrowth`, and editable-text integration); CTest `creative-suite-image-editor-core`, `creative-suite-image-editor-ui`, and Windows `creative-suite-image-editor-native-text` (one-key shortcut interception, focus retention, click-to-create through the full window, displayed pixels without a forced native redraw, caret placement and hit testing after each character, mouse and keyboard selection, selected-text replacement, horizontal growth with native margins and zoomed font metrics, canvas-edge wrapping, and visible multiline height growth) | `testTextToolState` directly covers frame creation by click/drag, preview, style, confirmation, and cancellation. The owner confirmed that normal typing with the default settings works again in the Windows application. Detailed visual editing checks in `MANUAL_VALIDATION.md` and other-platform acceptance remain with the owner (**P2 validation**). Native Windows mouse-drag selection has automated coverage. Linked PNG producer and Video Editor consumer regression tests are present; manual cross-application acceptance remains pending (**P1 validation**). |

### Current coverage gaps and pending validation

- **P0 — current Windows workflows:** the owner reports all currently
  implemented Image Editor flows have been exercised. Detailed per-scenario
  records and other-platform acceptance are not available. The WebP regression
  reported on 2026-10-01 is fixed in build and install deployment; the owner
  confirmed that the affected image opens in the rebuilt Windows UI.
- **P1 — image-format packaging:** Windows CMake configuration now requires
  Qt-version-matched WebP and TIFF plugins, deploys them beside the app and in
  install layouts, and registers an unskippable test against the app runtime.
  Debug and Release tests passed locally; macOS/Linux package validation
  remains pending.
- **P1 — linked-image acceptance:** automated coverage exists on both sides,
  and the owner reports current Image Editor workflows tested on Windows.
  Producer/consumer scenario-level and cross-platform acceptance remain
  tracked separately.
- **P2 — platform and visual validation:** macOS/Linux build, packaging, and
  interaction records remain incomplete.
- **P2 — editable text visual validation:** automated editing, rendering,
  formatting, transform, history, persistence, export, recovery, and publication
  checks are implemented; the manual canvas and visual review is pending.
- **Planned, not implemented:** future retouching, color adjustments, and advanced effects
  are excluded from current coverage until their implementation begins.

## Status legend

- `[ ]` Not started
- `[-]` In progress
- `[x]` Completed
- `[!]` Blocked or requiring a decision

Do not add target dates until project capacity and cross-platform packaging
have been validated. Update this roadmap when a milestone, dependency, or
decision changes.
