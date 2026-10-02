# Image Editor Architecture

Status: **initial standalone boundary, provisional**.

The Image Editor is an independent Qt Widgets application. Its application
core lives under `apps/image-editor/src/` and links Qt Core and Qt Gui without
depending on Qt Widgets. The UI owns dialogs, dock widgets, and window state;
the core owns the active image document, layer stack, edit operations,
persistence, and recovery.

## Source layout

- `src/app/` contains the executable entry point.
- `src/core/document/` contains the editable document session and `.cimg`
  serialization.
- `src/core/recovery/` contains local recovery snapshot persistence.
- `src/core/diagnostics/` contains bounded technical error logging.
- `src/ui/canvas/`, `src/ui/dialogs/`, `src/ui/layers/`, `src/ui/tools/`, and
  `src/ui/windows/` contain the canvas widget, creation dialogs, layer dock
  panel, tool sidebar, and main application window respectively.

## Linked image resources and geometry

`prepareRasterImport` decodes PNG, JPEG, BMP, WebP, and TIFF through the existing
Qt reader with automatic orientation. The window runs it on a worker thread
behind a cancellable modal progress dialog. Cancellation is checked between
files and after each blocking decode; Qt's decode call itself cannot be
interrupted. The document remains stable while the worker runs. Only a
complete successful batch enters the session, as one Undo/Redo edit.

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

## Runtime boundaries

- `ImageDocumentSession` owns either a decoded, linked source image or a
  self-contained canvas base, the locked Background, editable raster layers,
  one-level groups, and the active layer or group identity. Layer and group
  visibility, opacity, order, names, transforms, and group membership are
  document edits with undo/redo. The active selection is session state and
  does not make the document dirty.
- Version 1–3 operation sequences remain attached to Background so legacy
  documents render unchanged. New layer operations render on the fixed canvas;
  layer crops clear pixels outside the selected rectangle, while rotations and
  flips clip to the canvas bounds. The renderer composites visible items from
  bottom to top. A group composites its children first, applies crop, rotation,
  and flips to the combined pixels, then applies group opacity once. Export and
  recovery use that same composition; linked source files remain unchanged.
- `ImageDocumentStore` reads and atomically writes versioned `.cimg` documents
  and recovery snapshots. Version 4 stores layer UUIDs and properties; version
  5 adds layer-local eraser strokes; version 6 adds editable shape operations;
  version 7 adds stable UUIDs to paint and eraser strokes; version 8 adds
  one-level groups. Version 9 adds text, version 10 adds raster layer masks, and version 11 adds linked
  raster images. The reader
  continues to accept versions 1–10 and generates
  in-memory IDs for older strokes. Its data format is specified in
  [`FORMAT.md`](FORMAT.md).
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
- `RecoveryStore` writes a local snapshot every 60 seconds while a dirty
  document with a renderable base is open. Unsaved canvases use a persisted
  session identity so they remain recoverable without a source path. On the
  next launch, the UI offers the newest available snapshot for restoration.
- `NewCanvasDialog` offers fixed pixel presets or custom dimensions and
  requires a transparent, white, or custom-color background. Canvas documents
  store this base metadata without generating a companion raster file.
- `ImageCanvas` handles fit, zoom, middle-button panning, crop selection, and a
  checkerboard behind transparent pixels. Paint and Eraser preview round strokes
  during a drag and commit one image-space operation when released. Eraser's
  default live preview renders a temporary composite with the selected layer
  erased but does not mutate the document or history; the optional overlay
  preview draws a translucent mark instead. Escape cancels an in-progress erase.
  The eraser clears alpha only in the selected editable layer, revealing visible
  lower layers; Background cannot be painted or erased. For either active tool,
  `Ctrl+Alt` plus a left-button drag over the image adjusts that tool's size from
  signed horizontal displacement at the press point: right increases and left
  decreases at 1 px per screen pixel. Vertical movement is ignored. The tool
  outline stays anchored at the press point during the drag, including when the
  pointer leaves the image. On release, the system pointer returns to the press
  point; normal hover tracking resumes on subsequent mouse movement. The gesture
  updates the window controls without changing the document or history.
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
  multi-selection, and drag reordering or reparenting. Group Selected accepts
  only contiguous sibling raster layers. Ungroup preserves children; Delete
  removes a group and its children together. Groups cannot nest, and group
  thumbnails show the transformed composite. Each raster row has an isolated,
  aspect-fitted thumbnail on the left, the layer name, and an eye visibility
  button on the right.
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
- `ImageDocumentSession::deleteStackItems` normalizes a batch of
  `ImageStackItemData` references, ignores Background/unknown/duplicate entries,
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
  The `.cimg` schema is version 11; host links live in the Video Editor's
  `.csp` document.
- `ImageEditorLogger` writes bounded JSON Lines error entries under the local
  application data directory. Technical failures are logged before a message
  is shown; expected dialog cancellation is not an error.

## Layer mask ownership and rendering

The future [GPU acceleration plan](GPU_ACCELERATION_PLAN.md) preserves this
operation/alpha contract while proposing a shared backend adapter. It is
documentation only; the CPU renderer described here remains the implementation.

`ImageLayerData` owns optional `ImageLayerMaskData` with an enabled flag and a
separate operation vector. Undo snapshots share operation buffers rather than
copying a canvas-sized bitmap per stroke. `ImageDocumentSession` exposes mask
creation/removal, enablement, painting, erasing, transient previews, and small
mask thumbnails. Masks begin white and replay grayscale strokes and fixed
canvas transforms. Mask luminance multiplies premultiplied layer pixels before
opacity and group composition. Empty white masks avoid a full-size allocation.
Exports and linked publication use the same raster-layer renderer.

Layer transforms are appended to both content and existing mask operations.
Painting in a transformed parent group uses inverse group geometry. Object
selection transforms affect content objects while the layer mask remains in
the layer canvas. Masks are not independently selectable canvas objects.

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
