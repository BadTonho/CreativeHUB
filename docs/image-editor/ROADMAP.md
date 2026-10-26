# Image Editor Roadmap

This roadmap records open work, release gates, and feature-to-verification
coverage. Completed implementation checklists are archived in
[legacy documentation](../legacy/image-editor/IMPLEMENTED_ROADMAP_ITEMS.md).

See the [documentation index](README.md) for organized references by topic.

## Documentation map

| Question | Canonical document |
| --- | --- |
| What can the editor do today? | [Current scope](SCOPE.md) |
| What should be implemented next? | [Next code step](#next-code-step) and [implementation sequence](#4-feature-expansion--implementation-sequence) |
| How are documents stored and migrated? | [Document format](architecture/FORMAT.md) |
| How is the editor structured? | [Architecture](architecture/ARCHITECTURE.md) |
| Which behaviors still need hands-on checks? | [Manual validation](validation/MANUAL_VALIDATION.md) |
| Which tests and manual checks cover each feature? | [Regression coverage index](#regression-coverage-index) in this roadmap |
| Which shortcuts are available? | [Shortcuts](SHORTCUTS.md) |

Use the linked scope and format documents for current behavior; this roadmap
keeps open implementation and acceptance work plus its required coverage index.

Status: **standalone minimum and multi-document tabs implemented; basic
editable text, raster layer masks, linked image layers, canvas resizing, the
RGBA Eyedropper, editable layer outlines, clipping masks, six blend modes, Gradient Map, Brightness/Contrast, Exposure / Offset / Gamma, global and per-color-range Hue/Saturation, global and per-channel Levels, global and per-channel Curves adjustments, and regression coverage implemented; manual text validation,
packaging, and linked-image
acceptance remain pending; macOS and Linux validation deferred**.
Current application version: **Beta 0.1.0**.
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

## Next code step

**Hue/Saturation per-color-range controls are implemented.** Select the next
bounded feature from the provisional sequence in section 4. Colorize is an
independent mode; visual and platform acceptance remains pending. Levels and Curves provide RGB master and
independent channels; see their [Levels](features/LEVELS.md) and
[Curves](features/CURVES.md) contracts.
Global Hue/Saturation is implemented; see its [contract](features/HUE_SATURATION.md).
Exposure / Offset / Gamma is implemented; see its [contract](features/EXPOSURE_OFFSET_GAMMA.md).
The adjustment-layer contract and Gradient Map are implemented; see
[Gradient Map](features/GRADIENT_MAP.md). Outlines, clipping masks and six layer/
group blend modes are implemented; see [blend modes](features/BLEND_MODES.md).
Manual and platform acceptance remains pending.

The maintainer requested this feature expansion plan on **2026-10-20**, based
on the Photoshop feature comparison in the local owner-planning reference
`ideia/PHOTOSHOP_FEATURES.md`. The self-contained sequence in section 4 records
the missing and partial capabilities from that comparison. Its order is
provisional; the simple outline workflow highlighted in the reference has now
been implemented. Each unchecked item is **planned, not implemented**.

Implementation can proceed while existing manual acceptance remains pending.
The acceptance order in Milestones 1-3 still applies to release readiness;
section 4 defines code work and does not assign features to an application
version or expand the first editing release automatically.

## Principles

The shared GPU/dependency regression gate on 2026-10-10 corrected unqualified
`ImageDocumentStore` references in the core test's persistence assertions. This
is a test compilation fix; raster save/recovery assertions also use the current
document-version constant while retaining older migration fixtures. The Magic
Wand UI geometry-limit fixture uses alternating connected spans that exceed the
path limit after adjacent equal spans are merged. Document versions and application behavior retain
their existing contracts. Its execution result is tracked with the shared
[Windows/NVIDIA pipeline evidence](../video-editor/gpu/GPU_PIPELINE.md).

- Keep the Video Editor's stability work on track while developing the Image
  Editor as a separate executable.
- Preserve linked source images; store editable document operations in a
  versioned format.
- The current document model has a Background, editable raster layers and
  masks, and one-level groups. Multiple independent documents use tabs.
  Section 4 plans extensions to this baseline; introduce new hierarchy,
  artboard, color, or effect contracts deliberately and preserve older
  supported documents and linked-image consumers.
- Basic editable text is implemented for the first editing release: multiline
  content, font family, pixel size, color, alignment, move/width resize,
  horizontal growth to the canvas edge while typing, wrapping, Undo/Redo,
  `.cimg` save/reopen, recovery, and export. Manual visual
  validation remains pending. Layer outlines and clipping masks are separate
  implemented layer properties; richer typography remains planned in section 4.
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

## Interactive GPU rendering

Incremental content/mask caching, bounded worker scheduling, shared raster GPU
composition and direct texture presentation are implemented. Settings > Rendering
provides Automatic / CPU. Tools and PNG/JPEG export/publication remain CPU; formats
and history are unchanged. The [GPU plan](gpu/GPU_ACCELERATION_PLAN.md) documents
ownership, fallback and measured activation requirements. Windows evidence is in
[the delivery results](performance/INTERACTIVE_RENDERING_RESULTS_WINDOWS_2026-10-12.md);
native macOS/Linux validation remains pending independently of the CI matrix.

## Shared composition mask-operation regression

The shared engine adds straight-RGBA ApplyMask/MaskedBlend for Video Editor
Fusion masks. Existing application rendering semantics and formats are unchanged.
The Windows Debug consumer gate includes Image interactive/GPU raster and Motion
preview/GPU composition/export, plus shared composition CPU/OpenGL tests.
Native macOS/Linux acceptance remains separate and pending.

## Milestones

### 1. Standalone minimum editor — acceptance pending

The core implementation checklist is archived. Its release gate remains open:
complete the packaging and manual checks in [MANUAL_VALIDATION.md](validation/MANUAL_VALIDATION.md)
and record platform acceptance before accepting linked-image compatibility.

**Exit criteria:** the application builds independently, opens and edits a
raster image without changing its source, saves and reopens `.cimg`, exports
PNG/JPEG, and recovers an autosave. Required image plugins are present in the
packaged application. Automated and manual checks pass.

### 2. Video Editor linked-image compatibility

The linked-image implementation is already present as a bounded prototype.
Begin this milestone's acceptance only after Milestone 1 exit criteria pass;
until then, keep the implementation stable and use its existing regression
coverage to catch regressions.

- [ ] Validate transparent images, repeated Media Pool uses, clip variants,
  save/reopen, and large files in both applications on Windows. macOS and Linux
  validation is deferred.

**Exit criteria:** automated and manual checks confirm that saving a linked
image refreshes every intended Video Editor use without modifying the original
or leaving stale previews. Unsaved live preview streaming remains out of scope.

### 3. First editing release

Proceed after the compatibility milestone. Keep advanced image workflows
separate from this release.

- [ ] Complete the manual visual text checks in
  [`MANUAL_VALIDATION.md`](validation/MANUAL_VALIDATION.md). Their pending status does not
  block continuing implementation work.
- [ ] Validate recovery, export, and linked asset handoff as a complete
  workflow.

**Exit criteria:** the first release workflows pass automated regression
coverage and manual visual validation on supported platforms. Motion Studio
may proceed on a parallel track; cross-application integration depends on
validated contracts and producer/consumer regression coverage.

### Raster layer mask extension (approved 2026-10-02)

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

- [ ] Record platform/manual visual acceptance of these gestures.

### Resizable canvas extension (approved 2026-10-04)

- [ ] Record the manual anchor, export, save/reopen, and linked refresh checks in
  [`MANUAL_VALIDATION.md`](validation/MANUAL_VALIDATION.md).

Automated tests are registered in the feature-to-verification index below.
Manual and cross-platform acceptance remain pending until their outcomes are
recorded.

### Area Selection tool (approved 2026-10-04)

- [ ] Complete the native UI and linked PNG producer/Video Editor consumer
  checks in [`MANUAL_VALIDATION.md`](validation/MANUAL_VALIDATION.md).

### Lasso tool (approved 2026-10-06)

The manual visual and linked-image acceptance remains pending; automated
coverage is indexed below.

### Bucket Fill tool (approved 2026-10-06)

- [ ] Complete manual checks for layer/mask targeting, tolerance, selection,
  Undo/Redo, and save/reopen in [`MANUAL_VALIDATION.md`](validation/MANUAL_VALIDATION.md).

### Magic Wand tool (approved 2026-10-06)

- [ ] Complete native visual checks for layer/mask targets and transformed
  groups, including a large transparent canvas, in
  [`MANUAL_VALIDATION.md`](validation/MANUAL_VALIDATION.md).

### Linear Gradient tool (approved 2026-10-06)

- [ ] Complete the manual preview, direction, selection, mask, Undo/Redo, and
  save/reopen checks in [`MANUAL_VALIDATION.md`](validation/MANUAL_VALIDATION.md).

### Blur tool (approved 2026-10-06)

- [ ] Complete the manual layer/mask, selection, preview/cancel, history, and
  save/reopen checks in [`MANUAL_VALIDATION.md`](validation/MANUAL_VALIDATION.md).

### Object Selection tool extraction (approved 2026-10-05)

- [ ] Complete the owner-run selection and transform checks in
  [`MANUAL_VALIDATION.md`](validation/MANUAL_VALIDATION.md).

The tool extraction does not change the `.cimg` format. Manual visual
acceptance remains with the maintainer; automated coverage is indexed below.

### Eyedropper tool (approved 2026-10-06)

- [ ] Record native visual validation with masks, layer opacity, and transparent
  canvas pixels in [`MANUAL_VALIDATION.md`](validation/MANUAL_VALIDATION.md).

### 4. Feature expansion — implementation sequence

Planning baseline: **2026-10-20**. The numbered stages below give a provisional
code order, not release dates or a promise of full Photoshop parity. Later
stages need their own bounded implementation contracts before code changes.
Preserve the lightweight, local-first workflow and reuse shared processing
where its semantics match; document models and editing workflows remain owned
by the Image Editor.

Already implemented and excluded from new-feature tasks: raster layers and
one-level groups, layer opacity, raster layer masks, rectangular/elliptical and
freehand selections, Magic Wand, paint/erase, bucket fill, linear
color-to-transparent gradients, blur, basic editable text and shapes, linked
raster imports, basic transforms, document tabs, and PNG/JPEG export including
selected-layer/group Quick Export, and editable layer outlines. Partial equivalents must not be mistaken
for the complete features below: object selection selects existing editable
objects rather than detecting subjects in photos; linked images are not full
Smart Objects; linear gradients are not Gradient Maps; shape strokes are not
general layer outlines, which now have their own layer property.

#### 4.1. Instant layer outline — implemented

Editable layer outlines provide thickness, RGBA color and Inside/Outside
placement, with enable/disable and removal in Layers. They follow masked alpha,
preserve editable content, and participate in history, v17 persistence/recovery,
CPU/GPU preview, thumbnails, export and linked PNG publication. See the
[implementation contract, regression coverage and Windows evidence](features/LAYER_OUTLINE.md).
Native visual and macOS/Linux acceptance remains pending.

#### 4.2. Clipping masks and blend modes

- [x] Clip sibling layers to a shared base alpha with Layers commands, a customizable
  shortcut, stack-following chains, group barriers and atomic release on base deletion.
  See [clipping contract](features/CLIPPING_MASKS.md).
- [x] Expose Normal, Multiply, Screen, Overlay, Add, and Difference on layers
  and isolated groups; see [contract](features/BLEND_MODES.md).
- [ ] Add further darken/lighten,
  contrast, comparison, and hue/saturation/color/luminosity modes incrementally.
- [x] Preserve preview/export equivalence, history, persistence, recovery, and
  linked output for clipping and the six implemented blend modes.

#### 4.3. Non-destructive adjustments and fill layers

- [x] Establish an application-owned adjustment-layer contract for affected
  lower layers, masks, groups, clipping, and operation order; reuse compatible
  shared effect processing without importing Video Editor timeline models.
- [x] Implement Gradient Map, mapping luminance to editable gradient colors.
  This is separate from the existing linear painting tool.
- [x] Add editable brightness/contrast; see [contract](features/BRIGHTNESS_CONTRAST.md).
- [x] Add Exposure / Offset / Gamma; see [contract](features/EXPOSURE_OFFSET_GAMMA.md).
- [x] Add global Hue/Saturation; see [contract](features/HUE_SATURATION.md).
- [x] Add global RGB Levels; see [contract](features/LEVELS.md).
- [x] Add global RGB Curves; see [contract](features/CURVES.md).
- [x] Add individual Red, Green and Blue curves alongside the RGB master curve;
  see the [Curves contract](features/CURVES.md).
- [x] Add individual Red, Green and Blue Levels alongside the RGB master;
  see the [Levels contract](features/LEVELS.md).
- [x] Extend Hue/Saturation with Colorize and six fixed color-range controls.
- [ ] Add color balance, Black & White channel controls, LUT loading, selective
  color, invert, posterize, and threshold.
- [ ] Add editable solid-color, gradient, and pattern fill layers and reusable
  adjustment/fill presets.

#### 4.4. Layer styles and organization

- [ ] Extend outline styling with center placement and gradient/pattern fills
  after the simple outline is implemented.
- [ ] Add drop/inner shadows, outer/inner glow, bevel/emboss, and
  color/gradient/pattern overlays with reeditable parameters and reusable styles.
- [ ] Add Fill separately from Opacity, defining its interaction with each
  blend mode and style rather than treating it as a second opacity slider.
- [ ] Add masks on groups, nested groups, configurable layer locks, linked
  layer transforms, layer filtering, and saved layer compositions.
- [ ] Extend linked raster resources into embedded/linked object containers
  with controlled relink/refresh, preserved source data through transforms,
  and reeditable filter stacks. Current linked imports remain the baseline.

#### 4.5. Selection refinement, paths, and vector design

- [ ] Add polygonal/magnetic lasso, Quick Selection brush, color-range
  selection including skin-tone targeting, fixed-size/aspect marquee controls,
  and selection intersection.
- [ ] Add selection transforms, expand/contract, smooth, feather, contrast,
  edge shifting, saved selections, and selection/mask/alpha-channel conversion.
- [ ] Add an edge-refinement workspace with transparent/black/white/onion-skin
  views, refine-edge brush, adaptive radius, and color decontamination.
- [ ] Add editable Bezier paths, vector masks, path editing, and a channel
  panel for component and alpha channels as supported by the color model.
- [ ] Extend shapes with triangles/polygons, independently rounded rectangle
  corners, and union/subtract/intersect/exclude operations.
- [ ] Add richer paragraph/character formatting, variable-font axis and
  OpenType controls, text on paths, and reusable typography styles.

#### 4.6. Brush engine and retouching

- [ ] Extend paint/erase with hardness, independent opacity/flow, smoothing,
  pressure/tilt input, custom tips, size/angle dynamics, scattering, texture,
  dual brushes, color dynamics, and brush presets.
- [ ] Add Mixer Brush wet/load/mix controls and horizontal/vertical/radial
  painting symmetry.
- [ ] Add Clone Stamp and source rotation/flip/scale controls, then Spot
  Healing, sampled Healing Brush, and Patch workflows.
- [ ] Add Dodge/Burn with tonal-range targeting, Sponge saturation controls,
  Sharpen, and Smudge. Extend the existing Blur tool only where needed.

#### 4.7. Advanced transforms and deformation

- [ ] Add a configurable transform pivot, skew, corner distortion, perspective,
  and editable warp meshes with custom rows/columns.
- [ ] Add linked-plane Perspective Warp and pin-based deformation. Static
  deformation belongs to Image Editor; pose recording/keyframes require a
  separate Motion Studio contract and producer/consumer coverage.
- [ ] Add Liquify-style forward warp, reconstruct, smooth, twirl, pucker, and
  bloat tools. Face-aware deformation belongs to the assisted-editing stage.

#### 4.8. Layout, precision, and color-managed photography

- [ ] Add rulers, guides, grids, snapping/alignment, measurement tools,
  physical units, and configurable document DPI/PPI.
- [ ] Add multiple artboards per document, with independent sizes and export.
  Existing document tabs are not artboards.
- [ ] Add explicit ICC profile assignment/conversion, black-point
  compensation, and soft proofing; define display and export color contracts.
- [ ] Extend the 8-bit RGBA editing pipeline to 16-bit and 32-bit floating-point
  workflows, plus explicit grayscale/bitmap, CMYK, and Lab document modes in
  separately bounded increments. Define operation/format availability per mode.
- [ ] Add non-destructive RAW development with exposure, color, and optical
  controls and preserved original camera data.

#### 4.9. Automation and asset delivery

- [ ] Add recorded actions/macros, batch processing, reusable presets, and
  batch/multi-document/artboard export.
- [ ] Add WebP export and SVG export for compatible vector content; importing
  WebP today does not provide WebP export.
- [ ] Add controlled layer-based asset generation and a documented
  scripting/extension API with explicit ownership and error boundaries.

#### 4.10. Assisted editing and content reconstruction

- [ ] Add photo object/subject/sky isolation; do not conflate this with existing
  editable-object selection or the connected-color Magic Wand.
- [ ] Evaluate content-aware fill, patch/move, multi-source reconstruction,
  and content-aware scaling, with separately editable results where applicable.
- [ ] Evaluate generative fill/expand, image/background generation, variants,
  upscaling, harmonization, and prompt-guided editing.
- [ ] Evaluate neural restoration, skin/portrait tools, colorization, artifact
  removal, and face-aware deformation. Distinguish local and online processing
  and experimental capabilities. Choose concrete implementations only after
  reviewing workload, dependencies/licenses, privacy, and service boundaries.

#### Delivery requirements for each implementation

Every item above remains **planned, not implemented** until delivered with its
regression coverage. Cover model behavior, cancellation/no-ops where relevant,
history, persistence/migration, recovery, rendering/export, and error logging.
Update the feature-to-verification index with actual test sources/targets when
implementation starts; planned features are not existing coverage gaps.
Changes to shared processing or linked output require affected
producer/consumer regressions. Retain documented native/manual checks where
automation cannot cover the interaction. Measure representative workloads
before complex optimizations or broader hardware/performance claims; use
additional systems before setting numerical hardware requirements.

## Regression coverage index

Automated evidence below is registered in
[`apps/image-editor/tests/CMakeLists.txt`](../../apps/image-editor/tests/CMakeLists.txt).
Manual cases and their recorded status are in
[`MANUAL_VALIDATION.md`](validation/MANUAL_VALIDATION.md).

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
| Editable layer outline, masked alpha, Inside/Outside, history, v17 persistence/recovery, preview/cache/export and PNG publication | `image_editor_outline_test.cpp` (`creative-suite-image-editor-outline`), `image_editor_outline_ui_test.cpp` (`creative-suite-image-editor-outline-ui` and full UI), interactive/GPU raster suites, Hub backup format/manager tests and Video consumer gates | Windows Release automated evidence and 1080p measurements are in [the feature contract](features/LAYER_OUTLINE.md). Native visual/manual and macOS/Linux acceptance remain pending. |
| Startup loads the per-app selected downloaded/bundled Qt catalog before the editor window; Settings > Language persists System language, English, or pt-BR for next launch | `creative-suite-image-editor-translation-catalog`, `creative-suite-image-editor-ui` (`tests/image_editor_ui_test.cpp`), and shared `creative-suite-translations` cover startup catalog loading, language control persistence, regional locale resolution, manifest validation, update integrity, and fallback. App integration is in `apps/image-editor/src/app/main.cpp`. | Automated checks are present. Native Windows/macOS/Linux startup, locale, and visual checks remain pending in [translation acceptance](../TRANSLATIONS_AND_LANGUAGE_PACKS.md#regression-and-acceptance). |
| Hub snapshot references and restoration across versions 1–19 | `apps/hub/tests/test_backup_formats.cpp` (`hub-backup-format-test`), registered in `apps/hub/tests/CMakeLists.txt`; actual Image Editor persistence producer and consumer | Automated coverage present for original and grouped raster paths, unchanged source bytes, and opening vault/relocated snapshots. Native UI and macOS/Linux checks remain pending in [Hub backups](../hub/BACKUPS.md). |
| Source preservation in all image export scopes | `image_editor_core_test.cpp` (`testExportPreservesSourceImages`; `creative-suite-image-editor-core`) and `image_editor_export_ui_test.cpp` (`creative-suite-image-editor-export-ui`) | Automated coverage verifies PNG/JPEG base-image preservation, imported sources, Composite/SelectedLayer/SelectedGroup scopes, relative and normalized paths, Windows case aliases, rejection before rendering, unchanged source bytes/document state, worker failure propagation, and successful export to a separate destination. Filesystem alias cases run when link creation is available. The core test failed against the previous exporter before the fix. The Release application and affected targets built; core, raster, export UI, and application UI tests passed 4/4 on Windows on 2026-10-10. An unrestricted core run also passed hard-link cases; symbolic-link creation lacked the required Windows privilege and remains pending, along with native manual and macOS/Linux validation. The checklist is in `MANUAL_VALIDATION.md`. |
| Contextual object/layer/group deletion | `image_editor_raster_test.cpp` (`creative-suite-image-editor-raster`), `image_editor_deletion_ui_test.cpp` (within `creative-suite-image-editor-ui`), existing layer/group core tests | Automated coverage present for button/menu/Delete focus routing, multiple selection, masks, Background, text/rename/numeric fields, gesture cancellation, Undo/Redo, thumbnails, save/reopen, missing sources, exports, and linked PNG output. Native/platform checks are documented in `MANUAL_VALIDATION.md`; no manual result recorded. |
| Document editing, canvas, layers, groups, shapes, object selection, transforms, undo/redo | `image_editor_ui_test.cpp` (`testShapeToolState`, `testObjectSelectionToolState`, `testGeneralCanvasSelection`; `creative-suite-image-editor-ui`), `image_editor_raster_ui_test.cpp` (`testRasterImagesUi`; same CTest target), and `image_editor_core_test.cpp` (`creative-suite-image-editor-core`) | Automated coverage includes shape preview/geometry, Shift constraints, cancellation, invalid dimensions, topmost hit testing, marquee and Shift selection, mixed-object movement, resize/rotation, confirmation, and history. Windows Release focused Image Editor tests passed 5/5 and full CTest passed 70/70 on 2026-10-05. The existing interaction checklist is in `MANUAL_VALIDATION.md`; visual validation of the Shape Tool extraction remains with the owner. Cross-platform visual checks remain pending. |
| Stateless object edit preparation and session commit boundary | `image_editor_core_test.cpp` (`testImageDocumentObjectEditor`, `testGeneralObjectOperations`, shape/text session tests; `creative-suite-image-editor-core`) | Direct tests cover shape/text creation and editing, style changes, mixed transforms of paint/erase/raster/shape/text objects, group coordinate reversal, deletion, ID and geometry rejection, stack limits, and no mutation during preparation. Existing session tests cover one-edit Undo/Redo, persistence, and rendering. Affected Release targets compiled and the configured Windows CTest suite passed 12/12 on 2026-10-05. No visual UI change is part of this internal refactor. |
| Structural layer/group edits and selection/history | `image_editor_core_test.cpp` (`testImageLayerStackEditor`; `creative-suite-image-editor-core`), plus existing `creative-suite-image-editor-raster` and `creative-suite-image-editor-ui` integration coverage | Direct tests cover adding, deleting, grouping, ungrouping, moving between root and groups, ordering, resulting selection, the 512-item limit, and rejected operations without source mutation. The 11 focused tests and full CTest suite passed (70/70) on Windows Release on 2026-10-05. The existing layer/group interaction checklist remains in `MANUAL_VALIDATION.md`; GUI verification is left to the owner. |
| Undo/Redo snapshot storage and transitions | `image_editor_core_test.cpp` (`testImageDocumentHistory`; `creative-suite-image-editor-core`) and existing session integration coverage for editing, layer/group/mask/raster restoration, selection, and grouped opacity | Direct history tests cover empty state, ordering, redo clearing after a new edit, the 100-entry cap, clear, selected IDs, and implicitly shared raster image references. Session integration remains responsible for validating selections, restoring resources, and invalidating thumbnails. The eight focused Image Editor tests and full CTest suite passed (12/12) on Windows Release on 2026-10-05. Visual UI validation is not applicable. |
| Layer drag reordering and panel row preservation | `image_editor_core_test.cpp` (`testImageLayerStackEditor`; `creative-suite-image-editor-core`) verifies root layers moved in both directions remain in the candidate stack; `image_editor_ui_test.cpp` (`creative-suite-image-editor-ui`) verifies up/down reordering preserves every panel row and selection | The panel retains the dragged item ID and lets the committed document remain authoritative after a drop, avoiding a second Qt-side row removal after refresh. The owner checklist in `MANUAL_VALIDATION.md` checks that rows and visible content remain present when dragging both directions. Automated core/UI coverage is present; native drag verification remains with the owner. |
| Crop gesture, preview, image-edge mapping, document edit and Undo/Redo | `image_editor_ui_test.cpp` (`testCropToolState`, `testCropCanvasMapping`, and crop window integration; `creative-suite-image-editor-ui`) and `image_editor_core_test.cpp` (`applyCrop` bounds/history cases; `creative-suite-image-editor-core`) | Direct coverage checks preview, forward/reverse corner drags, image-bound clipping, cancellation and regions too small to apply. Window integration checks the emitted pixel rectangle marks the document edited and that Undo/Redo updates history state. Cropping layer content keeps the canvas dimensions fixed. Visual crop preview and edge behavior are documented in `MANUAL_VALIDATION.md`; owner verification remains pending. Windows Release focused Image Editor tests passed 6/6 and full CTest passed 70/70 on 2026-10-05. |
| Canvas Size dimensions, all anchors, translation of source/layers/groups/masks, Undo/Redo, and v12 persistence | `image_editor_core_test.cpp` (`creative-suite-image-editor-core`), `image_editor_ui_test.cpp` (`creative-suite-image-editor-ui`) | Automated coverage is present for all nine anchors, extension/reduction, custom background/transparency, edits and persistence. Manual dialog, save/reopen, export, recovery and linked output checks are documented in `MANUAL_VALIDATION.md`; outcome pending. |
| Area Selection and standalone Lasso geometry, per-tab UI, clipping, masks, groups, resize, export, and stroke Undo/Redo | `tools/lasso/lasso_tool.*`; `image_editor_ui_test.cpp` (`testAreaSelectionToolState`, `testAreaSelectionToolUi`; `creative-suite-image-editor-area-selection-ui`) and `image_editor_core_test.cpp` (`creative-suite-image-editor-core`) | Automated coverage checks rectangle/ellipse and standalone Lasso gestures, live contour preview, Replace/Add/Subtract results, Escape cancellation, oversized and degenerate contours, bounds clipping, the dedicated toolbar button, Paint clipping, shortcuts, masks, transformed groups, save/reopen, and export. Native brush/eraser and linked PNG checks are documented in `MANUAL_VALIDATION.md`; outcome pending (**P1 validation**). |
| `.cimg` persistence, validation, and backward compatibility | `image_editor_core_test.cpp` (`testImageDocumentCodec`; `creative-suite-image-editor-core`), `image_editor_bucket_fill_test.cpp` (`creative-suite-image-editor-bucket-fill`), `image_editor_gradient_test.cpp` (`creative-suite-image-editor-gradient`), `image_editor_blur_test.cpp` (`creative-suite-image-editor-blur`), and `image_editor_mask_test.cpp`; migration fixtures for v1–v15, v9 text validation, v10 masks, v12 canvas bounds, v13 stroke clips, v14 bucket fills, v15 gradients, v16 blur, paths, recovery, and write failures | Direct codec coverage checks current-version encode/decode, v3 migration, bucket-fill, gradient, and blur layer/mask serialization, v15 compatibility, and rejection of unsupported versions; existing store tests cover legacy migrations, invalid data, relative raster references, recovery, and atomic write failures. Focused Windows Release verification is recorded after this implementation. Cross-platform validation remains pending. |
| Bucket Fill algorithm, session edit, mask/selection behavior, tolerance, no-op history, Undo/Redo, persistence and UI interaction | `image_editor_bucket_fill_test.cpp` (`creative-suite-image-editor-bucket-fill`) covers four-way connectivity, per-channel RGBA tolerance, selection clipping, layer/mask rendering, grayscale mask conversion, no-op history, Undo/Redo and current-version save/reopen; `image_editor_ui_test.cpp` (`testBucketFillTool`; `creative-suite-image-editor-ui`) covers activation, tolerance control, inside/outside clicks | Automated focused coverage is present. The owner-facing manual layer/mask visual checklist is in `MANUAL_VALIDATION.md`; native visual validation and macOS/Linux acceptance remain pending. |
| Magic Wand region detection, tolerance, active layer/mask pixels, group mapping, temporary selection combination and UI | `src/ui/tools/magic_wand/`; `image_editor_ui_test.cpp` (`testMagicWandTool`; `creative-suite-image-editor-magic-wand-ui`) | Focused coverage checks four-way connectivity, RGBA tolerance, seed and group-path mapping, complexity rejection and preservation, mask pixels without document/history changes, toolbar/tolerance UI, Replace/Add/Subtract, and ignored outside clicks. The focused test passed 1/1 on 2026-10-07 before the responsiveness follow-up; the latest code still needs that focused test rerun. The owner manual check now includes a large transparent canvas and a responsive interface. |
| Linear Gradient algorithm, composed preview, layer/mask selection, Undo/Redo, persistence and UI interaction | `image_editor_gradient_test.cpp` (`creative-suite-image-editor-gradient`) covers premultiplied endpoints, selection, no-op gestures, preview/commit pixel equality, grayscale masks, history, current-version round-trip, and v14 reading; `image_editor_ui_test.cpp` (`testLinearGradientTool`; `creative-suite-image-editor-gradient-ui`) covers activation, live gesture signals, outside clicks, and zero-length drags | Focused automated coverage is present. The manual layer/mask preview and visual checklist is in `MANUAL_VALIDATION.md`; native visual validation and macOS/Linux acceptance remain pending. |
| Blur algorithm, layer/mask source isolation, asynchronous latest-state preview, cursor repaint, selection clipping, cancellation, Undo/Redo, current-version persistence and UI interaction | `image_editor_blur_test.cpp` (`creative-suite-image-editor-blur`) covers premultiplied RGBA, radius zero, selection clipping, layer/mask editing, preview/commit equality, grayscale masks, history, current-version round-trip and v15 reading; `image_editor_ui_test.cpp` (`testBlurTool`; `creative-suite-image-editor-blur-ui`) covers activation, controls, live preview, Escape cancellation and ignored outside clicks | Existing algorithm and UI coverage remains applicable; the focused blur gate passed in the 2026-10-20 outline regression selection. The manual checklist now verifies cursor tracking and responsiveness during preview. Native visual acceptance and macOS/Linux checks remain pending. |
| Stateless document rendering, previews, thumbnails, and image export | `image_editor_core_test.cpp` (`testStatelessDocumentRenderer`; `creative-suite-image-editor-core`), `creative-suite-image-editor-raster`, `creative-suite-image-editor-masks`, image-format tests, and `creative-suite-image-editor-export-ui` | Direct core coverage checks composition, selected layer/group output, excluded objects, and cancellation. Existing regression tests cover masks, groups, imported raster resources, thumbnail caching, PNG/JPEG export, and export UI. The affected Release targets compiled; focused tests passed 11/11 and full CTest passed 70/70 on Windows on 2026-10-05. No visual UI change is part of this refactor. |
| Recovery, relinking, and error logging | `image_editor_core_test.cpp` | The owner reports current recovery workflows working on Windows; the per-scenario record is not maintained. Cross-platform validation remains pending. |
| Import formats, decoding, and UI job lifecycle | `image_editor_raster_test.cpp` (`creative-suite-image-editor-raster`), `image_editor_raster_ui_test.cpp` (`creative-suite-image-editor-ui`), `image_editor_format_test.cpp` (`creative-suite-image-editor-image-formats`), and `creative-suite-image-editor-deployed-image-formats` | Core tests cover PNG/JPEG/BMP/WebP/TIFF decoding, corrupt/unsupported files, limits, and cancellation. Direct UI-controller checks cover ready and failed results; integration coverage checks file-dialog import, canvas drop, atomic batches, relinking, cancellation without document mutation, and safe return after the worker finishes. The three focused tests passed and full CTest passed (12/12) on Windows Release on 2026-10-05; other-platform packaging remains pending (**P1 validation**). |
| Flattened export, Quick Export, export dialogs, and UI job lifecycle | `image_editor_core_test.cpp`, `image_editor_export_ui_test.cpp` (`creative-suite-image-editor-export-ui`) | Integration coverage checks JPEG/PNG options and preferences, transparency, selected layer/group scope, progress, and cancellation. Direct controller checks cover success and output failure while worker tests cover rendering/finalization cancellation. The export controller owns worker/thread/progress/cancel lifecycle; the window keeps path, JPEG preference, scope, snapshot, and user feedback. The focused export test and configured Windows Release CTest suite passed (12/12) on 2026-10-05. Cross-platform package checks remain pending. |
| Canvas, tools, layers, shortcuts, UI interactions, and window teardown | `image_editor_ui_test.cpp` (`creative-suite-image-editor-ui`), including `testWindowTeardownWithFocusedTextEditor` | Automated UI coverage now destroys the window while its text editor has focus, exercising the focus-change callback during child teardown. The Windows Release UI test passed ten consecutive runs and the complete CTest suite passed 72/72 on 2026-10-05. The owner reports current UI workflows exercised on Windows 11; cross-platform visual checks remain pending. |
| Paint/Eraser diameter propagation, Blur size isolation and resize gesture, tool options toolbar state, and shape palette selection | `image_editor_ui_test.cpp` (`creative-suite-image-editor-brush-size-ui`) verifies Paint's slider, numeric field, Ctrl+Alt drag, maximum diameter, and applied stroke size; `creative-suite-image-editor-blur-ui` verifies Blur's controls, Ctrl+Alt drag, updated field, and diameter on a subsequent blur stroke; `creative-suite-image-editor-ui` covers state updates and shape palette selection | Paint and Blur size are independently stored. The focused Paint and Blur UI tests passed 2/2 in Windows Release on 2026-10-06. The Image Editor Release app compiled successfully at `build/apps/image-editor/Release/creative-suite-image-editor.exe`. The manual cross-tool check is in `MANUAL_VALIDATION.md`. |
| Eyedropper color sampling | `eyedropper/eyedropper_tool.*` owns pixel lookup; `image_editor_ui_test.cpp` (`testEyedropperTool`; `creative-suite-image-editor-ui`) covers RGBA preservation, fractional-coordinate flooring, empty/transparent-pixel rejection, transparent-swatch color-picker recovery, persistent tool activation, ignored outside clicks, and paint-color handoff | Manual composite sampling with masks, opacity, and transparency is documented in `MANUAL_VALIDATION.md`; owner visual validation remains pending. |
| Multiple document tabs and per-tab recovery | `image_editor_ui_test.cpp` (`creative-suite-image-editor-ui`), especially `testDocumentTabs` and `testMultiDocumentRecovery`, plus `creative-suite-image-editor-window-close` (`testWindowCloseAcrossDirtyTabs`): current/new-tab creation and opening, duplicate `.cimg` selection, active-tab replacement, reordering and switching with per-tab session/history/selection/zoom isolation, import targeting, Save/Discard/Cancel, failed-save retention, last-tab empty state, multi-snapshot restore and autosave; full-window close across dirty tabs, including cancellation partway through, active-tab restoration, recovery snapshot retention/cleanup, and persisted Save/Discard choices | The isolated Windows Release close test passed five consecutive runs. After the window-teardown fix, the Image Editor UI test passed ten consecutive runs and the complete Release CTest suite passed 72/72 on 2026-10-05. Native visual checks and macOS/Linux validation remain pending (**P2 validation**). |
| Video Editor linked-image producer/consumer workflow | Producer: `image_editor_ui_test.cpp`; consumers: `application_media_services_test.cpp` (`creative-suite-main-editor-application-media`), `main_window_integration_test.cpp` (`creative-suite-main-editor-main-window`), and `project_file_test.cpp` (`creative-suite-main-editor-project`) | Automated producer/consumer regression tests pass in Debug and Release. The owner reports the basic linked edit/save workflow working on Windows, but the full scenario and cross-platform acceptance remain pending (**P1 validation**). |
| Motion Studio linked Image Editor layer workflow | Image Editor producer: existing `image_editor_ui_test.cpp` linked-mode publication coverage (`creative-suite-image-editor-ui`). Motion Studio consumer: `motion_editor_ui_test.cpp`, `motion_document_store_test.cpp`, and `motion_video_export_test.cpp` (`creative-suite-motion-editor-ui`, `creative-suite-motion-editor-persistence`, and `creative-suite-motion-editor-export`). | The producer interface is reused without changing `.cimg` or Image Editor launch arguments. Automated Motion Studio refresh, corrupt-publication recovery, per-layer path isolation, export lookup, and Save As copy coverage is present. Manual duplicate-layer edits, missing-link recovery, and cross-app reopen remain pending; integration acceptance stays behind the standalone Image Editor gate (**P1 validation**). |
| Linked image import, geometry, sources, v11 raster references, v12 canvas bounds, v13 clipped strokes, v14 bucket fills, v15 gradients, v16 blur, export and publication | `image_editor_raster_test.cpp` (`creative-suite-image-editor-raster`), `image_editor_ui_test.cpp`, Video Editor `main_window_integration_test.cpp` | Existing automated coverage checks PNG publication and host refresh; Area Selection's linked PNG acceptance is documented in `MANUAL_VALIDATION.md`. macOS/Linux and manual acceptance remain pending. |
| Raster layer masks and shared stroke geometry | `image_editor_mask_test.cpp` (`creative-suite-image-editor-masks`), `image_editor_mask_ui_test.cpp` (within `creative-suite-image-editor-ui`); Video Editor `main_window_integration_test.cpp` when both apps are enabled | Automated coverage includes direct `ImageDocumentGeometry` group/clip/bounds tests, direct `ImageLayerMaskEditor` preparation and no-mutation tests, plus editing, transforms, previews, Undo/Redo, persistence, recovery, export, thumbnail targeting, and linked PNG producer/consumer behavior. The Release application and affected targets compiled on Windows; 11 focused tests and full CTest passed 70/70 on 2026-10-05. Mask UI checks are documented in `MANUAL_VALIDATION.md`; no manual result recorded. |
| Optional performance collection, privacy, benchmark report, and session raster cache | `image_editor_performance_test.cpp` (`creative-suite-image-editor-performance`) covers disabled collection, aggregation/percentiles, bounded samples, serialization, JSONL rotation, unchanged render/export pixels, group-thumbnail caching, layer-cache pixel equality, selective invalidation after paint/eraser/mask/resource edits, Undo/Redo, canvas/document replacement, preview/export bypass, and the 64 MiB budget; `image_editor_ui_test.cpp` (`testPerformanceMetricsUi`; `creative-suite-image-editor-ui`) covers Settings persistence, panel refresh, final flush, stop behavior, and path omission; `verify_image_editor_benchmark.cmake` (`creative-suite-image-editor-benchmark-report`) checks schema v2, cold/warm thumbnail and layer-cache behavior, hit/miss/bypass counters, memory limit, distributions, and matching pixels | The Image Editor Release performance test passed 12/12 and the configured full CTest suite passed 27/27 on Windows on 2026-10-06. The initial pre-cache baseline remains in [performance-baseline-windows-2026-10-06-v2.json](performance/performance-baseline-windows-2026-10-06-v2.json); the post-group-cache run remains [performance-after-group-thumbnail-cache-windows-2026-10-06.json](performance/performance-after-group-thumbnail-cache-windows-2026-10-06.json); the new same-PC layer-cache report is [performance-after-layer-raster-cache-windows-2026-10-06.json](performance/performance-after-layer-raster-cache-windows-2026-10-06.json). The v1 renderer-only report remains [available historically](performance/performance-baseline-windows-2026-10-06.json). Native manual panel inspection and macOS/Linux validation remain pending (**P2 validation**); measurements are observations, with no hardware target set. |
| Incremental CPU/GPU composition, presentation, cancellation, context/allocation failure, and resource budgets | `image_editor_interactive_test.cpp` (`creative-suite-image-editor-interactive`, `creative-suite-image-editor-gpu-raster`); `image_editor_ui_test.cpp` (`creative-suite-image-editor-native-rendering-ui`: native presentation, CPU/Automatic switching, stable panel items and logged recovery); existing UI/export/model suites; shared `creative-suite-composition-opengl` and Video/Motion consumer tests | Automated coverage present. Exact CPU incremental parity and mask coverage; final GPU premultiplied-channel tolerance <=2. Windows acceptance and measured gate are recorded in [the delivery results](performance/INTERACTIVE_RENDERING_RESULTS_WINDOWS_2026-10-12.md). Native macOS/Linux pending. GPU tool rasterization/export/publication remains deferred. |
| First-release editable text | `image_editor_core_test.cpp` and `image_editor_ui_test.cpp` (`testTextToolState`, `testTextEditorGrowthLayout`, `testWindowTextGrowth`, and editable-text integration); CTest `creative-suite-image-editor-core`, `creative-suite-image-editor-ui`, and Windows `creative-suite-image-editor-native-text` (one-key shortcut interception, focus retention, click-to-create through the full window, displayed pixels without a forced native redraw, caret placement and hit testing after each character, mouse and keyboard selection, selected-text replacement, horizontal growth with native margins and zoomed font metrics, canvas-edge wrapping, and visible multiline height growth) | `testTextToolState` directly covers frame creation by click/drag, preview, style, confirmation, and cancellation. The owner confirmed that normal typing with the default settings works again in the Windows application. Detailed visual editing checks in `MANUAL_VALIDATION.md` and other-platform acceptance remain with the owner (**P2 validation**). Native Windows mouse-drag selection has automated coverage. Linked PNG producer and Video Editor consumer regression tests are present; manual cross-application acceptance remains pending (**P1 validation**). |

### Windows updater coverage

The shared updater tests in `libs/updater/tests/` cover catalog validation,
version comparison, download resume/cancellation, network failure, and hash
rejection (`updater-catalog-test`, `updater-service-test`). The Image Editor
also shares `updater-runtime-test` coverage for pending schema-1 persistence,
independent editor waits, self-update shutdown, and per-app launch failures. It
must update only its own installation. The packaged Windows checklist is in
[`MANUAL_VALIDATION.md`](validation/MANUAL_VALIDATION.md); install-path reuse, user-data
preservation, rollback, and Hub restore remain **pending** packaged-installer
validation.

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
- **Planned, not implemented:** additional section 4.2 modes and unchecked section 4.3 items onward are excluded from current coverage until implementation begins.
  Add actual regression mappings as each feature is delivered.

## Status legend

- `[ ]` Not started
- `[-]` In progress
- `[!]` Blocked or requiring a decision

Do not add target dates until project capacity and cross-platform packaging
have been validated. Update this roadmap when a milestone, dependency, or
decision changes.

## Shared affine compositor regression boundary

Image raster defaults remain unchanged by the optional shared Motion affine contract. Shared CPU/OpenGL/Vulkan tests and Image raster/mask/core/export-UI regressions protect composition and Transform consumers. Native visual/platform acceptance remains separate.

The standalone export-UI test target explicitly links the translations library
used by the shared window source; this keeps its scoped Release build complete.

The selected consumer and shared gates passed in the 2026-10-17 Release run;
see [Scale boundary evidence](../motion-editor/features/SCALE_AXES.md#automated-windows-evidence--2026-10-17).

## Shared static blend-mode maintenance

Motion originally introduced the six shared composition modes. Image now uses
them for its own layer/group UI and v19 persistence, as specified in
[Blend modes](features/BLEND_MODES.md). Existing Normal and image-operation
semantics remain unchanged. Shared
composition/OpenGL/Vulkan and Image core/raster/mask/UI/export/GPU-raster gates
protect this boundary. Native visual/platform/GPU acceptance remains separate
from portable fallback coverage. See [shared contract](../CROSS_APPLICATION_COMPATIBILITY.md#shared-static-blend-mode-composition).

## Shared Track Matte maintenance

Shared CPU/OpenGL gain an additive alpha/luma image operation for Motion;
Vulkan refuses it explicitly. This application gains no matte UI or persisted
fields. Its existing composition/raster, masks, preview/UI and export gates
protect unchanged operations and defaults. See [contracts and pending acceptance](../motion-editor/features/TRACK_MATTES.md).

Shared logical mask-raster maintenance for Motion Bezier paths: existing composition, `creative-suite-image-editor-raster`, `creative-suite-image-editor-masks`, UI and export suites protect unchanged defaults. No Image path control or format field is added; native acceptance remains separate.

## Drop Shadow contract maintenance

Motion Drop Shadow extends shared CPU effects/GPU image operations only; Image adds no selector or persisted field. Existing core/raster/mask/blur, UI/export, interactive and GPU-raster regressions protect consumers. See [shared Motion contract](../motion-editor/features/DROP_SHADOW.md); native/platform acceptance remains pending.

## Clipping mask regression coverage

| Behavior | Automated coverage | Acceptance |
| --- | --- | --- |
| Clipping alpha/styles/opacity, stack/history, v18/recovery, cache/export | `image_editor_clipping_test.cpp` (`creative-suite-image-editor-clipping`) | Automated coverage present; execution evidence in [feature contract](features/CLIPPING_MASKS.md). |
| Layers command, restrictions, tabs, shortcut migration and linked PNG | `image_editor_clipping_ui_test.cpp` (`creative-suite-image-editor-clipping-ui`, full UI) | [Manual visual checklist](validation/MANUAL_VALIDATION.md#clipping-masks) pending. |
| CPU/OpenGL parity and Hub restoration | `image_editor_interactive_test.cpp` (`creative-suite-image-editor-gpu-raster`), `hub-backup-format-test`, `hub-backup-manager-test` | Native macOS/Linux acceptance pending. |

## Blend mode regression coverage

| Behavior | Automated coverage | Acceptance |
| --- | --- | --- |
| Six layer/group modes, alpha, clipping, history/v19/recovery/export/cache | `image_editor_blend_test.cpp` (`creative-suite-image-editor-blend`), shared `composition_test.cpp` | Coverage present; evidence in [feature contract](features/BLEND_MODES.md). |
| Selector restrictions, mask ownership, tabs, history, linked PNG | `image_editor_blend_ui_test.cpp` (`creative-suite-image-editor-blend-ui`, full UI), Video consumer tests | [Visual checklist](validation/MANUAL_VALIDATION.md#blend-modes) pending. |
| CPU/OpenGL parity, uploads, budget/cancellation and Hub restoration | Interactive/native GPU, shared composition/OpenGL and Hub backup format/manager tests; Motion/Video consumer gates | Native macOS/Linux and manual acceptance pending. |


## Gradient Map regression coverage

| Behavior | Automated coverage | Acceptance |
| --- | --- | --- |
| RGB mapping, preserved alpha, masks/selection seed, history/scope/stack, v20/recovery/export/cache | `creative-suite-image-editor-gradient-map`, shared effects tests | [Contract and execution evidence](features/GRADIENT_MAP.md); native visual pending. |
| Creation, multi-point UI/history/color cancel, mask owner/tools, restrictions and PNG publication | `creative-suite-image-editor-gradient-map-ui`, full UI | [Manual checklist](validation/MANUAL_VALIDATION.md#gradient-map-adjustments) pending. |
| CPU/OpenGL, source upload reuse, budgets, GPU recovery and Hub restore | Interactive/native GPU, shared composition, Hub backup format/manager, Video/Motion consumer gates | Native macOS/Linux pending. |

## Brightness/Contrast regression coverage

| Behavior | Automated coverage | Acceptance |
| --- | --- | --- |
| Integer parameters, shared RGB math, preserved alpha, mixed adjustment stacks, history, v21/recovery/export/cache | `creative-suite-image-editor-brightness-contrast`, shared effects | [Contract and evidence](features/BRIGHTNESS_CONTRAST.md); manual/platform checks pending. |
| Creation, sliders/numeric controls, Reset, no-ops/Redo, mask tools, tabs and PNG publication | `creative-suite-image-editor-brightness-contrast-ui`, full UI | Native visual checklist pending. |
| Native OpenGL equivalence, budgets, zero-upload parameter edits and 1080p | `creative-suite-image-editor-gpu-raster` | Maximum two RGB levels, exact alpha; [Windows profile](performance/brightness-contrast-windows-2026-10-20.json). |

## Exposure / Offset / Gamma regression coverage

| Behavior | Automated coverage | Acceptance |
| --- | --- | --- |
| Linear-sRGB math, preserved alpha, mixed stacks, history, v22/recovery/export/cache | `creative-suite-image-editor-exposure-offset-gamma`, shared effects | [Contract and execution evidence](features/EXPOSURE_OFFSET_GAMMA.md); visual/platform acceptance recorded separately. |
| Creation, decimal controls, atomic Reset, masks, tabs and linked PNG | `creative-suite-image-editor-exposure-offset-gamma-ui`, full UI | [Manual checklist](validation/MANUAL_VALIDATION.md#exposure--offset--gamma-adjustments) pending. |
| CPU/OpenGL parity, zero-upload edits, budgets/cancellation and Hub restoration | Interactive `gpuExposureOffsetGamma`, shared composition/OpenGL, Hub backup format/manager, Video/Motion consumers | Native and reference-PC evidence remains separate from offscreen checks. |

The 2026-10-21 Linux scoped run includes 40 passing CTest gates, six broader
failures and one offscreen OpenGL skip, plus a passing focused Video handoff.
Native Image interactive cases passed 19/19 and native shared OpenGL passed.
See [execution evidence and unresolved gates](features/EXPOSURE_OFFSET_GAMMA.md#linux-execution-evidence--2026-10-21)
and the [Linux measurements](performance/exposure-offset-gamma-linux-2026-10-21.json).
All six failures reproduced in the pre-feature baseline on the same environment.
These results do not complete the existing release/manual acceptance gates.

## Hue/Saturation regression coverage

The native GPU raster path distinguishes single-channel grayscale masks from
RGB masks when calculating coverage. `gpuBudgetsGeometryAndCoverage` checks all
256 grayscale levels; `gpuHueSaturation` also exercises adjustment masks.

| Behavior | Automated coverage | Acceptance |
| --- | --- | --- |
| Normal, Colorize and fixed range HSL, grayscale/chromatic pixels, transitions, alpha, independent values, history, v28→29/recovery/export/cache and thumbnails | `creative-suite-image-editor-hue-saturation`, shared effects | [Contract and execution evidence](features/HUE_SATURATION.md); visual/platform acceptance remains separate. |
| Menu, range selector, mode-specific values, gestures, selected-range Reset, Undo/Redo, masks, tabs and linked PNG | `creative-suite-image-editor-hue-saturation-ui`, full UI, translation catalog | [Manual checklist](validation/MANUAL_VALIDATION.md#huesaturation-color-range-acceptance) pending. |
| CPU/OpenGL parity, zero-upload edits, budgets/cancellation, handoff and restore | Interactive `gpuHueSaturation`, native shared OpenGL, Video linked-image, Hub v29 backup gates and Motion consumers | Record native versus offscreen execution separately; compare known baseline failures. |

## Levels regression coverage

| Behavior | Automated coverage | Acceptance |
| --- | --- | --- |
| Master-then-channel mapping, intermediate byte rounding, independent sets, bounds, identity, alpha, modes, histogram, history, v24–27 migration, v28/recovery/export/cache | `creative-suite-image-editor-levels`, shared effects | [Levels contract](features/LEVELS.md); owner visual acceptance pending. |
| Channel selector and switching, five controls, independent paired bounds, gestures/selected-channel Reset, Undo/Redo, persistence, selection mask and combined histogram | `creative-suite-image-editor-levels-ui`, translation catalog | [Manual checklist](validation/MANUAL_VALIDATION.md#rgb-and-per-channel-levels-adjustments) pending. |
| CPU/OpenGL parity, unchanged uploads, groups/clipping, cancellation/capacity, v28 Hub backup/restore and flattened PNG consumers | Interactive `gpuLevels`, Hub backup format/manager, Video linked-image producer/consumer and Motion linked-PNG gates | Native and platform acceptance tracked separately. |

**Prior Windows Release baseline (2026-10-22, Curves and global Levels):** all 17 focused CTest cases passed:
shared effects; Image Editor core, image formats, full UI, translation catalog,
Gradient Map, blend, clipping, masks, Exposure, Hue/Saturation and Levels
(model/UI); Video Editor linked-image; and Motion Studio persistence, GPU
composition and export. The existing clipping test needs
`QT_QPA_FONTDIR=C:/Windows/Fonts` in this test tree to render its text fixture.
Hub backup manager and format tests both passed; the manager test needs access
to its user data directory outside the workspace sandbox. Native Windows
`gpuLevels` passed (3 QtTest cases, including setup and cleanup) with CPU/GL
pixel parity, zero content uploads for parameter edits, and resource budget
checks. The Release executable was built at the canonical Image Editor path.
The [owner's manual checklist](validation/MANUAL_VALIDATION.md#rgb-and-per-channel-levels-adjustments)
and release/platform acceptance remain pending.

**Per-channel Levels Windows Release execution (2026-10-22):** the Image
Editor Release application, affected test targets, Video linked-image producer,
Motion linked-PNG consumer and Hub Debug application built successfully. All
10 selected CTest cases passed: Image Editor UI, translation catalog,
interactive CPU/OpenGL, Levels model/UI, Curves model/UI, Video linked-image,
Motion UI and Hub backup format. The Hub format test exercises backup of the
v27 Levels data and rejects v28. The shared Effects suite still reaches the
previously recorded Outer Glow assertion; the Hub backup-manager suite stops
at its AppLocalData fixture file open because the workspace sandbox does not
grant that path. Neither is recorded as a passing gate. Native visual and
macOS/Linux acceptance remain pending.

## Curves regression coverage

| Behavior | Automated coverage | Acceptance |
| --- | --- | --- |
| PCHIP mapping order, independent channels, bounds, neutral bytes, alpha, masks, modes, histogram, history, v25 migration, v27 recovery/export/cache | `creative-suite-image-editor-curves`, shared effects | [Curves contract](features/CURVES.md); owner visual acceptance pending. |
| Channel selector, independent graph/numeric edits, add/remove, gestures/Reset-per-channel, history, selection mask and linked PNG publication | `creative-suite-image-editor-curves-ui`, full UI and translation catalog | [Manual checklist](validation/MANUAL_VALIDATION.md#global-curves-adjustments) pending. |
| CPU/OpenGL parity for distinct channels, unchanged content uploads, mixed stacks, clipping/groups, cancellation/capacity and handoff | Native `gpuCurves`, Hub backup gates, Video linked-image and affected Motion gates | Native and platform acceptance tracked separately. |

**Windows Release execution (2026-10-22):** Image Editor Curves model, Curves
UI, translation catalog, and native `gpuCurves` passed. The GPU case compared
CPU/OpenGL pixels and exercised point-edit uploads, clipping/groups, cancel,
and the resource budget. Video linked-image publication/refresh and GPU
timeline composition passed, as did Motion Studio's UI linked-image consumer
gate. Hub backup format and restore passed, including the v26 Curves data. The
Hub backup-manager test reached and passed its future
Image v27 rejection checks, then stopped at its later AppLocalData fixture
write because that path is outside the workspace sandbox. The shared Effects
test reached a failing Outer Glow assertion after the Curves checks. These two
unrelated failures are not recorded as passing gates. The Image Editor Release
app was built at `build/apps/image-editor/Release/creative-suite-image-editor.exe`
(2,512,896 bytes; 2026-10-22 22:42:53 -03:00). The [owner's manual checklist](validation/MANUAL_VALIDATION.md#global-curves-adjustments)
and macOS/Linux acceptance remain pending.

Raster cache collision invalidation is covered by
`creative-suite-image-editor-performance`
(`layerRasterCacheInvalidatesCollidingEntriesSafely`). The regression fails
before the erase-iterator fix; retained memory and hit/miss behavior are checked
after partial and complete removal. See [HSL delivery evidence](features/HUE_SATURATION.md#raster-cache-invalidation-fix).

The full `creative-suite-image-editor-ui` gate checks raster-import cancellation
before the queued worker completion is delivered, so fast decoding cannot skip
the cancellation request. Production import behavior is unchanged.

Linux Release evidence (2026-10-21): 50 scoped CTest gates selected, 43 passed,
six known pre-HSL failures and one offscreen shared-OpenGL skip. Native Image
interactive passed 21/21, and native shared OpenGL passed separately. The
offscreen Image interactive gate contains 9 passed and 12 skipped QtTest cases.
See [execution evidence](features/HUE_SATURATION.md#linux-execution-evidence--2026-10-21)
and [1080p measurements](performance/hue-saturation-linux-2026-10-21.json).
These results do not complete the existing release/manual acceptance gates.
