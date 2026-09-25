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

## Runtime boundaries

- `ImageDocumentSession` owns either a decoded, linked source image or a
  self-contained canvas base, the locked Background, editable raster layers,
  and the active layer identity. Layer visibility, opacity, order, names,
  painting, and transforms are document edits with undo/redo. The active layer
  selection is session state and does not make the document dirty.
- Version 1–3 operation sequences remain attached to Background so legacy
  documents render unchanged. New layer operations render on the fixed canvas;
  layer crops clear pixels outside the selected rectangle, while rotations and
  flips clip to the canvas bounds. The renderer composites visible layers from
  bottom to top. Export and recovery use that same composition; linked source
  files remain unchanged.
- `ImageDocumentStore` reads and atomically writes versioned `.cimg` documents
  and recovery snapshots. Version 4 stores layer UUIDs and properties; version
  5 adds layer-local eraser strokes. The reader continues to accept versions
  1–4. Its data format is specified in
  [`FORMAT.md`](FORMAT.md).
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
- `ToolSidebar` contains mutually exclusive, checkable, icon-only Paint and
  Eraser tools in a compact rail; both can be inactive. The always-visible color
  swatch remains specific to Paint. `ImageEditorWindow` owns a persistent top
  tool options bar; it is empty when no tool is active and shows synchronized
  size controls (1–1024 pixels) for the active tool. Paint and Eraser sizes are
  independent and start at 12 px. The Eraser-only Preview option starts off and
  is session state, not document data. The sidebar tools and crop action cannot
  be active at the same time.
- `LayerPanel` is hosted by a resizable, dockable right-side `QDockWidget`. It
  presents the stack top-to-bottom with an isolated, aspect-fitted thumbnail
  on the left, the layer name, and an eye visibility button on the right.
  Thumbnails use the canvas checkerboard colors behind transparent pixels and
  remain visible when a layer is hidden or has zero opacity. The session
  renders operations at thumbnail resolution and caches small per-layer
  previews by source and content, so selection and visibility changes do not
  rerender them. Background
  remains fixed at the bottom, with visibility as its only editable property.
  Selecting Background disables painting and transforms and explains that an
  editable layer is required. Opacity slider drags are grouped into one undo
  entry.
- `ImageEditorWindow` routes menu and sidebar actions, prompts before discarding
  edits, and projects session state into the window. A completed paint gesture
  is one undoable document operation; changing tools does not modify the image.
- `ImageEditorWindow` owns the command-action registry and the **Settings >
  Keyboard Shortcuts** dialog. Stable action names identify preferences stored
  with `QSettings`, separately from editable documents. Defaults use Qt standard
  sequences plus `B` for Paint, `E` for Eraser, and `Esc` to cancel crop;
  duplicate assignments are rejected, and tool actions stay disabled without an
  editable layer. The fixed tool-size mouse gesture is documented separately
  and is not part of the keyboard shortcut preferences.
- In standalone mode, the window opens and saves `.cimg` documents normally. A
  linked launch accepts `--linked-source`, `--linked-document`, and
  `--publish-output`. It opens an existing linked document or creates one from
  the source, pins Save to the linked document, and disables source-changing
  and Save As actions. Save writes `.cimg` first, then uses `QSaveFile` to
  atomically publish the flattened PNG. Linked saves use a per-document
  `QLockFile` plus a SHA-256 baseline check to reject concurrent Image Editor
  revisions before replacing the document. The source image is never written.
  The `.cimg` schema remains version 5; host links live in the Main Editor's
  `.csp` document.
- `ImageEditorLogger` writes bounded JSON Lines error entries under the local
  application data directory. Technical failures are logged before a message
  is shown; expected dialog cancellation is not an error.

## Image I/O and current limits

The application reads PNG, JPEG, BMP, WebP, and TIFF through Qt's image I/O
system. WebP and TIFF require the optional Qt Image Formats plugins, which are
listed in the vcpkg manifest and deployed with the application. The official
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
the Main Editor's `.csp` format. The first linked-image handoff is implemented
as a saved PNG plus a native `.cimg` companion. The Main Editor owns shared
Media Pool links and clip-specific variants in `.csp` v10; the current
behavior and remaining validation are documented in the
[cross-application proposal](../CROSS_APPLICATION_COMPATIBILITY.md).
