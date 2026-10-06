# Image Editor Architecture

Status: **initial standalone boundary, provisional**.

The Image Editor is an independent Qt Widgets application. Its application
core lives under `apps/image-editor/src/` and links Qt Core and Qt Gui without
depending on Qt Widgets. The UI owns dialogs, dock widgets, and window state;
the core owns each open image document, layer stack, edit operations,
persistence, and recovery.

## Source layout

- `src/app/` contains the executable entry point.
- `src/core/document/` contains the editable document session, stateless image
  renderer, image exporter, and `.cimg` serialization.
- `src/core/recovery/` contains local recovery snapshot persistence.
- `src/core/diagnostics/` contains bounded technical error logging and the
  optional performance collector and JSON Lines summary writer.
- `src/ui/import/` contains the UI controller for cancellable raster decode
  jobs; image format decoding remains in the core document module.
- `src/ui/canvas/`, `src/ui/diagnostics/`, `src/ui/dialogs/`, `src/ui/layers/`,
  `src/ui/tools/`, and `src/ui/windows/` contain the canvas widget, performance
  panel, creation dialogs, layer dock panel, tool sidebar and tool families,
  and main application window respectively.
- `src/ui/tools/brush/brush_tool.*` shares stroke collection, cursor rendering,
  and stroke-overlay drawing between the brush tools. `paint_tool.*` and
  `eraser_tool.*` own their gesture and preview rules; they return preview or
  edit requests for `ImageCanvas` to forward through its existing signals.
- `src/ui/tools/crop/crop_tool.*` owns the temporary crop gesture, normalized
  and image-bounded geometry, selection preview, and pixel-region result.
  `ImageCanvas` supplies positions in image-edge coordinates and the view
  context, then forwards the existing `cropSelected` signal. `ImageEditorWindow`
  still applies the crop through the document session and history.
- `src/ui/tools/selection/area_selection_tool.*` owns each canvas's temporary
  Area Selection path, gesture geometry, Replace/Add/Subtract operations,
  cancellation, bounds clipping, complexity limit, and preview overlay.
  `ImageCanvas` maps pointer positions to image coordinates, forwards the
  existing selection signals, and supplies the active clip path to brush tools.
  The selection remains UI state and is not stored in the document or history.
- `src/ui/tools/selection/object/object_selection_tool.*` owns temporary object
  selection, hit testing, marquee gestures, transform handles, move/resize/
  rotation geometry, cancellation, and selection overlays. It reports selected
  IDs and proposed geometry through internal events; it never edits the
  document. `ImageCanvas` maps pointer events, preserves its public signals,
  and renders transformed previews. `ImageEditorWindow` coordinates the active
  layer, requests transient compositions from the session, and commits released
  geometry through the document history. The tool also provides hit testing to
  the canvas for entering text editing; `TextTool` owns the inline editing UI.
- `src/ui/tools/shapes/shape_tool.*` owns the in-progress shape gesture,
  Shift-constrained geometry, invalid-geometry rejection, and shape preview.
  Its shared painter renders both the preview and committed shape overlays.
  `ImageCanvas` maps pointer positions to image coordinates and emits the
  existing `shapeCreated` signal; `ImageEditorWindow` continues to own shared
  shape settings and commits completed shapes through the document session.
- `src/ui/tools/options/image_tool_options_bar.*` owns the tool-options toolbar
  controls and their presentation state. It emits user requests; the window
  keeps shared tool settings and coordinates updates with the active canvas and
  document. `src/ui/tools/shapes/shape_palette.*` owns the floating shape
  picker and reports the selected kind for the window to apply.
- `src/ui/tools/text/text_tool.*` owns text-frame creation, the hosted inline
  editor, its live layout and keyboard handling, and committed-text rendering.
  It reports start, commit, and cancellation through internal signals;
  `ImageCanvas` maps pointer coordinates, supplies the image/view context, and
  forwards its stable public signals. `ImageEditorWindow` retains shared text
  options and commits confirmed edits through the document session.
- Tool families use subdirectories such as `brush/`, `crop/`, `selection/`,
  `shapes/`, and `text/` to keep the tools area navigable as it grows.

## Linked image resources and geometry

`prepareRasterImport` decodes PNG, JPEG, BMP, WebP, and TIFF through the existing
Qt reader with automatic orientation. `ImageImportController` runs it on a
worker thread behind the shared cancellable modal progress dialog and waits
for safe thread completion. Cancellation is checked between files and after
each blocking decode; Qt's decode call itself cannot be interrupted. The
window keeps import/relink validation and applies only a complete successful
batch to the active session as one Undo/Redo edit.

`RasterImage` stores a UUID, path, oriented source size, and an invertible
affine matrix. `ImageDocumentSession` caches implicitly shared QImages by path.
Repeated imports share loaded pixels; operations contain no pixel buffers.
Relink can retain a per-object shared image handle to refresh a selected
reference without refreshing other references to the same path. History and
export snapshots retain shared resource handles, allowing Undo to restore
same-path relinks. Reopening recreates the cache from disk. No external file
watcher is used.

The session exposes batch insertion, raster lookup, geometry editing through
`visibleObjects`/`updateObjectsRendered`, temporary core composition through
`renderedImageWithObjects`, source diagnostics, and compatible relinking.
Import commits above the active layer, inside its parent group if applicable;
an active group inserts above that group at root. Placement starts centered,
fits down without enlargement, and maps canvas placement back through an
existing parent group transform.

`ImageDocumentObjectEditor` prepares candidate documents for object creation,
editing, styling, transformation, and deletion without mutating the source
document or accessing session state. It handles paint and erase strokes,
linked raster images, shapes, and text; shape and text creation also prepare
their new layer and resulting selection. The editor shares object-geometry
mapping with the session's visible-object projection and raster placement.
`ImageDocumentSession` checks loaded-image availability for creation, then
commits a successful candidate as one history edit and invalidates thumbnails.
Object queries and transient previews remain on the session; raster import and
relink remain in their existing resource workflow.

Selection manipulates only the raster matrix, including positions outside
the canvas. Oriented corners resize in image-local axes, Alt permits independent
scales, and the rotation handle rotates around the image center with optional
15-degree Shift snapping. Esc discards the temporary composition. The core
preview includes masks, operation order, opacity, crop, and group transforms.
The existing layer transform commands retain their content-and-mask behavior.

Source problems are reported in the layer panel and logged with path/object
context. They do not prevent document save or other layer edits. Export
snapshots check only references participating in their visible scope before
rendering or opening an output file. Full export, Quick Export, and linked PNG
publication retain their previous output on failure.

`ImageDocumentSession` remains the public document façade. It owns document
state, edit history, imported-image references, selection, and the mutable
layer-thumbnail cache. `ImageDocumentRenderer` receives const document and
image references and produces composites, selected-layer/group renders,
object-excluded previews, and thumbnail pixels; it owns no cache and does not
mutate the document. `image_exporter.cpp` validates the export request, asks
the renderer for the requested pixels, then handles JPEG flattening,
cancellation, progress, and atomic file writing. This split keeps rendering
and file encoding independent of session history while preserving the session
and export entry points used by the UI.

`ImageExportController` in `ui/export/` owns the UI-side export job lifecycle:
it runs `ImageExportWorker` on a background thread, connects cancellation and
progress, waits for safe thread completion, and returns the core export result.
`ImageEditorWindow` keeps destination selection, JPEG preferences, Quick Export
scope, snapshot capture, and user-facing status or error reporting. The shared
progress dialog remains in `ui/dialogs/` because image import also uses it.

## Runtime boundaries

- `ImageDocumentSession` owns either a decoded, linked source image or a
  self-contained canvas base, the locked Background, editable raster layers,
  one-level groups, and the active layer or group identity. Layer and group
  visibility, opacity, order, names, transforms, and group membership are
  document edits with undo/redo. The active selection is session state and
  does not make the document dirty. `ImageDocumentHistory` owns the in-memory
  Undo/Redo stacks and their snapshots; the session restores documents,
  validates the saved selection, retains loaded raster resources, and
  invalidates thumbnails after a history transition. A new edit clears Redo,
  and the history retains at most 100 Undo entries.
- Version 1–3 operation sequences remain attached to Background so legacy
  documents render unchanged. New layer operations render on the fixed canvas;
  layer crops clear pixels outside the selected rectangle, while rotations and
  flips clip to the canvas bounds. The renderer composites visible items from
  bottom to top. A group composites its children first, applies crop, rotation,
  and flips to the combined pixels, then applies group opacity once. Export and
  recovery use that same composition; linked source files remain unchanged.
- `ImageDocumentStore` reads and atomically writes `.cimg` documents and
  recovery snapshots. `ImageDocumentCodec` translates document data to and
  from versioned JSON, validates it, and migrates supported older versions.
  Version 4 stores layer UUIDs and properties; version 5 adds layer-local
  eraser strokes; version 6 adds editable shape operations; version 7 adds
  stable UUIDs to paint and eraser strokes; version 8 adds one-level groups.
  Version 9 adds text, version 10 adds raster layer masks, version 11 adds
  linked raster images, version 12 adds independent current canvas bounds with
  a base-image offset, and version 13 adds persisted selection clips on paint
  and eraser strokes. Area Selection itself remains temporary per tab. The
  original base dimensions remain available for relink validation. The codec
  continues to accept versions 1–12 and generates in-memory IDs for older
  strokes. The data format is specified in [`FORMAT.md`](FORMAT.md).
- `ImageExportSnapshot` captures the current source image, document data, and
  selected layer and group IDs; its implicitly shared image and document
  buffers exclude undo history and thumbnail caches. The default composite
  scope renders all visible layers and groups. **Quick Export** uses the
  selected layer or group scope and preserves the full canvas bounds; selected
  groups include their visibility, opacity, and transforms. Selecting
  Background exports the base image with its document operations. Standalone
  PNG/JPEG exports render this immutable snapshot and write it on a worker
  thread. Quick Export reuses the saved JPEG quality (0–100, default 95) and
  opaque matte (default white) without opening the options dialog. PNG preserves
  alpha. A modal progress dialog keeps the document stable while export runs;
  cancellation is checked during rendering and before the atomic output commit.
  Qt's image writer cannot be interrupted during its blocking encode call, so
  cancellation requested in that phase discards the temporary output after
  encoding returns. Linked image publication continues to use its synchronous
  composite PNG path and default options.
- The Layers dock is a tree with Ctrl/Shift multi-selection, collapsed groups,
  visibility controls, and drag reordering or reparenting. **Group Selected**
  accepts only contiguous sibling raster layers. Add Group creates an empty
  group. Ungroup preserves its children; Delete removes the group and children
  as one undoable edit. Background remains fixed at the root bottom, and
  subgroups are rejected. Group opacity is applied once to its composite.
- The Layers dock places a **Quick Export** button above the tree. It invokes
  the same selected-item export action as **File > Quick Export** and stays
  disabled until an image is open.
- `ImageEditorWindow` starts with an empty workspace and owns a tab context for
  every open document. Each context is the source of truth for its
  `ImageDocumentSession`, canvas and page, active layer, multi-selection in the
  Layers dock, canvas object selection, linked-image metadata, and source
  diagnostics; each canvas keeps its own zoom and pan. The window resolves
  document-specific commands through the active context instead of swapping a
  second copy of that state when tabs change. Tool settings remain shared. The
  **+** menu creates a canvas, opens
  an image, or opens a `.cimg` in a new tab. File-menu Open and New Canvas
  replace the active tab after the save/discard/cancel prompt; opening an
  already-open `.cimg` selects its tab.
  Failed saves leave the affected tab open. Closing the last tab leaves the
  window empty. Linked Video Editor mode stays in one tab and continues to
  publish its PNG after a successful `.cimg` save. Tabs do not change the
  `.cimg` schema or the Video Editor handoff contract.
- `RecoveryStore` writes a local snapshot every 60 seconds for each dirty
  document with a renderable base. Unsaved canvases use a persisted session
  identity so they remain recoverable without a source path. On the next launch,
  each available snapshot can be restored into its own tab. The default store
  uses the application's local data directory; tests may inject an isolated
  data directory when constructing `ImageEditorWindow`.
- `NewCanvasDialog` offers fixed pixel presets or custom dimensions and
  requires a transparent, white, or custom-color background. Canvas documents
  store this base metadata without generating a companion raster file.
- `ImageCanvas` handles fit, zoom, middle-button panning, crop selection, and a
  checkerboard behind transparent pixels. It converts pointer positions and
  passes a lightweight context to `PaintTool` and `EraserTool`; each tool owns
  its stroke gesture, cursor, and preview overlay. Tool requests return through
  the existing canvas signals, while `ImageEditorWindow` and the document
  session continue to compose previews and commit edits. Paint and Eraser commit
  one image-space operation when released. Eraser's default live preview
  renders a temporary composite with the selected layer erased but does not
  mutate the document or history; the optional overlay preview draws a
  translucent mark instead. Escape cancels an in-progress erase and mask-paint
  gesture; regular paint keeps its existing cancellation behavior. The eraser
  clears alpha only in the selected editable layer, revealing visible lower
  layers; when targeting a mask it writes black instead, and Background cannot
  be painted or erased. For either active tool, `Ctrl+Alt` plus a left-button
  drag over the image adjusts that tool's size from signed horizontal
  displacement at the press point: right increases and left decreases at 1 px
  per screen pixel. Vertical movement is ignored. The tool outline stays
  anchored at the press point during the drag, including when the pointer leaves
  the image. On release, the system pointer returns to the press point; normal
  hover tracking resumes on subsequent mouse movement. The gesture updates the
  window controls without changing the document or history.
- `AreaSelectionTool` keeps the temporary selection and draws its live
  Replace/Add/Subtract preview. `ImageCanvas` continues to own mode exclusivity,
  coordinate conversion, and canvas-resize translation, while its public
  methods and signals remain the integration boundary used by the window.
- `ImageCanvas` previews line, rectangle, and ellipse operations while drawing.
  **Selection** hit-tests paint strokes, erase strokes, and shapes across visible
  editable layers from top to bottom. Click selects the topmost object; Shift
  click toggles objects in the selection, and a marquee selects objects whose
  visible geometry it intersects. Selecting an object activates its layer.
  Selected objects move and resize as a group. Corner handles preserve the
  group's proportions by default; holding Alt during resize allows independent
  horizontal and vertical scaling. Stroke and brush widths follow the geometric
  mean of the two scale factors. Shape style controls apply to all selected
  shapes; paint and erase strokes keep the style set when drawn. Eraser
  operations remain in their layer's ordered sequence, so moving one changes
  the region erased when that sequence is replayed. Escape cancels shape
  creation or an active selection gesture. Each object transform, shape style
  edit, or object deletion is one Undo/Redo edit.
- `ToolSidebar` contains mutually exclusive, checkable, icon-only Paint,
  Eraser, Shapes, and Selection tools in a compact rail; all can be inactive.
  Selection uses a mouse-pointer icon.
  The always-visible color swatch remains specific to Paint. `ImageEditorWindow`
  owns a persistent top tool options bar; it is empty when no tool is active and
  shows synchronized size controls (1–1024 pixels) for Paint and Eraser, plus
  stroke, fill, colors, and width controls for Shapes and selected shapes.
  The Shapes sidebar button opens a movable, non-modal palette next to the
  button on first use. Icon-only Line, Rectangle, and Ellipse choices set the
  current shape type and activate Shapes; tooltips and accessible names identify
  each choice. The palette stays open while drawing and is hidden by its close
  button. Reopening it retains its position and current type. The Shapes
  shortcut activates the current type without opening the palette. Each
  completed shape creates and selects a new editable `Shape N` layer directly
  above the selected layer, including Background; the new layer contains only
  that shape. Shape creation and layer insertion are one Undo/Redo edit. Shapes
  remains available with Background selected, while Paint and Eraser require
  an editable layer. When a child layer is selected, a new shape is inserted
  inside that group. When a group is selected, it is inserted at the root above
  the group. Stroke and fill controls edit all selected shapes.
  Paint and Eraser sizes are independent and start at 12 px. Shape defaults are
  Rectangle, enabled stroke and fill using the current Paint color, and a 2 px
  stroke. Shape options are session-only and are not stored in `.cimg`. The
  Eraser-only Preview option starts off and is session state, not document data.
  The sidebar tools and crop action cannot be active at the same time.
- `LayerPanel` is hosted by a resizable, dockable right-side `QDockWidget`. It
  presents the stack as a top-to-bottom tree with groups, Ctrl/Shift
  multi-selection, and drag reordering or reparenting. A drag retains its source
  layer ID from the start of the gesture through the drop. The document owns the
  reordered stack, so the custom drag does not let Qt remove the source row a
  second time after the panel refreshes from the committed document. Group
  Selected accepts only contiguous sibling raster layers. Ungroup preserves
  children; Delete removes a group and its children together. Groups cannot
  nest, and group thumbnails show the transformed composite. Each raster row
  has an isolated, aspect-fitted thumbnail on the left, the layer name, and an
  eye visibility button on the right.
  Thumbnails use the canvas checkerboard colors behind transparent pixels and
  remain visible when a layer is hidden or has zero opacity. The session
  renders operations at thumbnail resolution and caches small per-layer
  previews by source and content, so selection and visibility changes do not
  rerender them. Background remains fixed at the root bottom, with visibility
  as its only editable property.
  Selecting Background disables Paint, Eraser, and layer transforms. Its hint
  explains that Shapes creates a separate editable layer for each object.
  Selecting a group enables its opacity and transforms; a selected child layer
  receives new layers and shapes inside its group.
  Shapes and Selection remain available; shapes created with Background
  selected are inserted immediately above it. Opacity slider drags are grouped
  into one undo entry.
- `ImageLayerStackEditor` prepares structural stack edits from the document and
  current selection, returning a candidate document and resulting selection
  without mutating the source or accessing history. It owns layer/group
  creation and deletion, grouping, ungrouping, moves between root and existing
  groups, stack item counts, and reconstruction of the flattened layer order.
  `ImageDocumentSession` commits successful results as one history edit, updates
  selection, and invalidates thumbnails. It also uses the shared count/order
  helpers when importing raster layers and creating shape or text layers.
  Renaming, visibility, opacity, masks, and opacity drag grouping remain in the
  session.
- `ImageDocumentSession::deleteStackItems` delegates structural removal to
  `ImageLayerStackEditor`; a batch ignores Background/unknown/duplicate entries
  and removes selected groups and their children in one history edit. Surviving
  groups keep their other children; selection moves to a surviving layer when
  its target is removed. `deleteLayer` and `deleteGroup` delegate to this method.
  The panel emits one batch for its Delete button and context commands, and
  excludes Background from the selection. Object deletion still uses
  `deleteObjects`, leaving masks and unselected layer operations intact.
  Refreshing the canvas cancels pending gestures/previews so a later mouse
  release cannot commit a deleted object's transform. Source resources remain
  shared with history; deleting a reference never deletes its original file.
- `ImageEditorWindow` routes menu and sidebar actions, prompts before discarding
  edits, and projects session state into the window. A completed paint gesture
  is one undoable document operation; changing tools does not modify the image.
- `ImageEditorWindow` owns the command-action registry and the **Settings >
  Keyboard Shortcuts** dialog. The shared registration and persistence logic is
  provided by `creative-suite::shortcuts`; the Image Editor keeps its existing
  stable action names and `ImageEditor/KeyboardShortcuts` QSettings group,
  separately from editable documents. Defaults use Qt standard
  sequences plus `B` for Paint, `E` for Eraser, and `Esc` to cancel crop or an
  in-progress shape. Shapes and Selection have no default shortcut. **Delete
  Selection** defaults to Delete and targets objects with canvas focus or the
  stack selection with Layers focus. Focus changes disable it in text, rename,
  and numeric fields. The explicit Delete Selected Objects menu command and
  Selection options button remain available for every selected object kind,
  including imported raster images. Only the contextual command is registered
  as a deletion shortcut, preventing duplicate Delete bindings. The Selection
  and contextual delete actions keep their existing
  settings keys so user-assigned shortcuts survive the rename. Default conflicts
  are avoided when migrating an older saved Delete assignment:
  if no deletion preference exists, the new contextual binding starts cleared
  and the older command keeps Delete. Reset All restores canonical defaults.
  Duplicate assignments are rejected before acceptance. The dialog stages edits until OK;
  Cancel discards them, and accepted bindings are persisted as one validated
  batch. Paint and Eraser require an editable layer; Shapes
  creates a new layer above the selected layer, and Selection remains
  available with Background selected. The fixed
  tool-size mouse gesture is documented separately and is not part of the
  keyboard shortcut preferences.
- In standalone mode, the window opens and saves `.cimg` documents normally. A
  linked launch accepts `--linked-source`, `--linked-document`, and
  `--publish-output`. It opens an existing linked document or creates one from
  the source, pins Save to the linked document, and disables source-changing
  and Save As actions. Save writes `.cimg` first, then uses `QSaveFile` to
  atomically publish the flattened PNG. Linked saves use a per-document
  `QLockFile` plus a SHA-256 baseline check to reject concurrent Image Editor
  revisions before replacing the document. The source image is never written.
  The `.cimg` schema is version 13; host links live in the Video Editor's
  `.csp` document.
- `ImageEditorLogger` writes bounded JSON Lines error entries under the local
  application data directory. Technical failures are logged before a message
  is shown; expected dialog cancellation is not an error.
- Performance collection is an opt-in runtime diagnostic controlled by
  **Settings > Collect Performance Metrics**. It is disabled by default. The
  core `ImageEditorPerformanceMetrics` singleton records replay and operation
  types, masks, layer/group composition, thumbnails and cache results, canvas
  paint, and export rendering/encoding stages. Each stage retains at most the
  latest 2,048 timing samples while keeping total count, total time, and maximum
  for the active collection session; snapshots report average, p50, p95, and
  maximum. A modeless Performance Metrics dock refreshes once per second and
  samples process CPU, working set, and private memory through the existing
  `system-monitor` library. Unavailable resource readings are shown as `N/A`.
- Every five seconds with new measured activity, the window appends a
  cumulative summary to `logs/image-editor-performance.jsonl` in the
  application-local data directory. It also flushes a final summary when
  collection is disabled or the application closes. This is separate from the
  error log and rotates at 2 MiB per file with three files retained. Records
  contain stage timings, process resource values, OS/build metadata, and only
  active-document dimensions and item counts; they exclude paths, document
  names, and pixels. Disabling collection stops further timing, sampling, and
  summary writes.
- `creative-suite-image-editor-benchmark` is a development executable and has
  no install rule. It creates deterministic synthetic documents for the
  `reference`, `mask-heavy`, `stroke-heavy`, `large-image`, `repeated-source`,
  and `export` profiles. It writes synthetic PNGs and a `.cimg` fixture only
  to a temporary directory, then opens the fixture through
  `ImageDocumentSession`. Each profile measures the core view-refresh path
  (`renderedImage`, layer/group thumbnails, and mask thumbnails) in cold and
  warm modes. Cold mode opens a fresh session for each measured iteration;
  warm mode primes one session, performs the configured warmups, then reuses
  that session. The report also verifies that cold and warm output pixels
  match. The session caches one group thumbnail per group and requested size;
  render-affecting edits, opacity changes, document/resource replacement, and
  history restoration invalidate that cache. Selection changes do not.
  Group cache lookups use the existing thumbnail hit/miss metrics, and the
  `group_thumbnail` stage is recorded only when the renderer runs. Mask
  thumbnails remain uncached. PNG/JPEG export is measured separately for the
  `export` profile.
- Benchmark schema version 2 reports wall time and each stage's total elapsed
  time per measured iteration, with average, p50, p95, maximum, iteration
  count, and total stage-call count. Percentiles use the same complete set of
  measured iterations as the average; stage timings are nested and must not be
  summed across stages. Resource samples span each profile run, including
  warmups and cache priming.
  Defaults remain three warmups and 30 iterations; `--profile`, `--warmup`,
  `--iterations`, and `--output` select the workload and destination. It reads
  no personal media and changes neither `.cimg` nor shared application
  interfaces. The [schema v1 report](performance-baseline-windows-2026-10-06.json)
  is retained as a historical direct-renderer result and is not directly
  comparable to v2. The first Windows reference-PC
  report using the v2 method is
  [`performance-baseline-windows-2026-10-06-v2.json`](performance-baseline-windows-2026-10-06-v2.json).
  The same-PC post-cache comparison is recorded in
  [`performance-after-group-thumbnail-cache-windows-2026-10-06.json`](performance-after-group-thumbnail-cache-windows-2026-10-06.json);
  its before/after summary is in `GPU_ACCELERATION_PLAN.md`.

## Layer mask ownership and rendering

The future [GPU acceleration plan](GPU_ACCELERATION_PLAN.md) preserves this
operation/alpha contract while proposing a shared backend adapter. It is
documentation only; the CPU renderer described here remains the implementation.

`ImageLayerData` owns optional `ImageLayerMaskData` with an enabled flag and a
separate operation vector. Undo snapshots share operation buffers rather than
copying a canvas-sized bitmap per stroke. `ImageDocumentSession` exposes mask
creation/removal, enablement, painting, erasing, transient previews, and small
mask thumbnails. It delegates mask-stroke validation and operation preparation
to the stateless `ImageLayerMaskEditor`; the session still applies the returned
operation and records it in Undo/Redo. Mask previews append a prepared operation
to a temporary document value and use the existing renderer without changing
the session or history. Masks begin white and replay grayscale strokes and
fixed-canvas transforms. Mask luminance multiplies premultiplied layer pixels
before opacity and group composition. Empty white masks avoid a full-size
allocation. Exports and linked publication use the same raster-layer renderer.

`ImageDocumentGeometry` centralizes point transforms, mapping stroke points and
selection clips through parent-group operations, clip validation, and image
bounds used by Paint, Eraser, and mask editing. Layer transforms are appended to
both content and existing mask operations. Painting in a transformed parent
group uses inverse group geometry. Object selection transforms affect content
objects while the layer mask remains in the layer canvas. Masks are not
independently selectable canvas objects.

The Layers dock displays the grayscale mask beside the content thumbnail;
the active target has a highlighted border and a disabled mask has a slash.
Clicking either thumbnail changes the Paint/Eraser target without a document
edit. Mask painting previews the composed result and Escape cancels the gesture.
Context actions add, enable/disable, or remove one mask per editable raster
layer. Background and groups reject masks. Technical mask edit and document
errors use the window's existing structured logging path.

## Image I/O and current limits

The application reads PNG, JPEG, BMP, WebP, and TIFF through Qt's image I/O
system. WebP and TIFF require the optional Qt Image Formats plugins, which are
tracked by the repository's vcpkg manifest. Windows CMake deployment requires
configuration-matched WebP/TIFF plugins and copies them to application build
and install layouts. The deployed-format regression checks the application
plugin path. macOS/Linux packaging remains pending. The official
[Qt Image Formats module documentation](https://doc.qt.io/qt-6/qtimageformats-index.html)
describes the plugin model and its bundled codec notices. The module is
available under LGPLv3 or GPLv2; its bundled TIFF codec uses the libtiff
license, and its WebP codec uses the BSD 3-Clause license. Distribution
packages must include the plugins and preserve the applicable license notices
for the codecs actually shipped.

Decoded images and transparent layer buffers are currently rendered
synchronously as `QImage`. Very large images or many painted layers can use
substantial memory or pause the UI; profile representative documents before
expanding this workflow. Undo and redo retain up to 100 in-memory document
snapshots and are not stored in the `.cimg` file.

The `.cimg` format is application-specific and does not change or embed into
the Video Editor's `.csp` format. The first linked-image handoff is implemented
as a saved PNG plus a native `.cimg` companion. The Video Editor owns shared
Media Pool links and clip-specific variants in `.csp` v10; the current
behavior and remaining validation are documented in the
[cross-application proposal](../CROSS_APPLICATION_COMPATIBILITY.md).
