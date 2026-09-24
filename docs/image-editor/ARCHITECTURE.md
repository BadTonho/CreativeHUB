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
  and recovery snapshots. Version 4 stores layer UUIDs and properties, and the
  reader continues to accept versions 1–3. Its data format is specified in
  [`FORMAT.md`](FORMAT.md).
- `RecoveryStore` writes a local snapshot every 60 seconds while a dirty
  document with a renderable base is open. Unsaved canvases use a persisted
  session identity so they remain recoverable without a source path. On the
  next launch, the UI offers the newest available snapshot for restoration.
- `NewCanvasDialog` offers fixed pixel presets or custom dimensions and
  requires a transparent, white, or custom-color background. Canvas documents
  store this base metadata without generating a companion raster file.
- `ImageCanvas` handles fit, zoom, middle-button panning, crop selection, and a
  checkerboard behind transparent pixels. It previews a round paint stroke
  during a drag and emits one image-space stroke when the gesture ends.
- `ToolSidebar` currently contains one checkable, icon-only Paint tool in a
  compact rail and an always-visible color swatch at the bottom. The swatch opens
  an alpha-capable color picker. `ImageEditorWindow` owns a persistent top tool
  options bar; it is empty when no tool is active and shows synchronized slider
  and numeric brush-size controls (1–512 pixels) while Paint is active. Paint
  starts inactive. The options are visible only while both the Paint button and
  canvas paint mode are active; the sidebar and crop menu action cannot be
  active at the same time.
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
the Main Editor's `.csp` format. The linked-image compatibility contract is a
later milestone documented in the
[cross-application proposal](../CROSS_APPLICATION_COMPATIBILITY.md).
