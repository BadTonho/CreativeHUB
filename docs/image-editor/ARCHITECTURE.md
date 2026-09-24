# Image Editor Architecture

Status: **initial standalone boundary, provisional**.

The Image Editor is an independent Qt Widgets application. Its application
core lives under `apps/image-editor/src/` and links Qt Core and Qt Gui without
depending on Qt Widgets. The UI owns dialogs and window state; the core owns
the active image document, ordered edit operations, persistence, and recovery.

## Runtime boundaries

- `ImageDocumentSession` owns the decoded source image and the document's
  ordered crop, rotation, and flip operations. The source file remains
  unchanged. Rendering applies operations in sequence to an owning `QImage`.
- `ImageDocumentStore` reads and atomically writes versioned `.cimg` documents
  and recovery snapshots. Its data format is specified in
  [`FORMAT.md`](FORMAT.md).
- `RecoveryStore` writes a local snapshot every 60 seconds while a dirty image
  with an available source is open. On the next launch, the UI offers the
  newest available snapshot for restoration.
- `ImageCanvas` handles fit, zoom, middle-button panning, and crop selection.
  `ImageEditorWindow` routes menu and toolbar actions, prompts before discarding
  edits, and projects session state into the window.
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
