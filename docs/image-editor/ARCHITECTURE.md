# Image Editor Architecture

Status: **initial standalone boundary, provisional**.

The Image Editor is an independent Qt Widgets application. Its application
core lives under `apps/image-editor/src/` and links Qt Core and Qt Gui without
depending on Qt Widgets. The UI owns dialogs and window state; the core owns
the active image document, ordered edit operations, persistence, and recovery.

## Source layout

- `src/app/` contains the executable entry point.
- `src/core/document/` contains the editable document session and `.cimg`
  serialization.
- `src/core/recovery/` contains local recovery snapshot persistence.
- `src/core/diagnostics/` contains bounded technical error logging.
- `src/ui/canvas/`, `src/ui/dialogs/`, `src/ui/tools/`, and `src/ui/windows/`
  contain the canvas widget, creation dialogs, tool sidebar, and main
  application window respectively.

## Runtime boundaries

- `ImageDocumentSession` owns either a decoded, linked source image or a
  self-contained canvas base, plus the document's ordered crop, rotation, flip,
  and paint-stroke operations. Linked source files remain unchanged. Rendering
  applies operations in sequence to an owning `QImage`; paint strokes are stored
  in `.cimg`, included in recovery, and flattened only for raster export.
- `ImageDocumentStore` reads and atomically writes versioned `.cimg` documents
  and recovery snapshots. Its data format is specified in
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
  compact rail. The standard tooltip names the tool on hover, and activating the
  tool expands the rail to show its brush controls. Those brush controls
  provide an alpha-capable color picker and a 1–512 pixel diameter setting. The
  tool starts inactive, and the sidebar and crop menu action cannot be active at
  the same time.
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

Decoded images are currently loaded into memory as `QImage`, and edit
operations are rendered synchronously. Very large images can therefore use
substantial memory or pause the UI; profile representative image sizes before
expanding this workflow. Undo and redo retain up to 100 in-memory operation
snapshots and are not stored in the `.cimg` file.

The `.cimg` format is application-specific and does not change or embed into
the Main Editor's `.csp` format. The linked-image compatibility contract is a
later milestone documented in the
[cross-application proposal](../CROSS_APPLICATION_COMPATIBILITY.md).
