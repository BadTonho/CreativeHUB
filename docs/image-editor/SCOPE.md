# Image Editor Scope

Status: **editable text, raster masks, linked image layers, and canvas resizing are implemented;
manual visual validation and release acceptance remain pending**. The Image Editor is a standalone raster
editor in `apps/image-editor/`. Its current behavior and the approved next
release direction are recorded separately below. See the
[roadmap](ROADMAP.md) for milestone order and acceptance gates.

## Implemented Today

The current application supports one raster document at a time, either linked
to an original image or created as a self-contained canvas. It provides a
locked Background, editable raster layers, one-level groups, visibility,
opacity, ordering, crop, quarter-turn rotation, flips, painting, bucket fill,
linear gradients, erasing,
editable line/rectangle/ellipse shapes, editable text, raster layer masks,
imported image layers, object selection and transforms, an RGBA Eyedropper that
samples visible composite pixels with nonzero alpha into the paint color,
Undo/Redo, local autosave and recovery, and bounded technical error logging.
Text supports multiline content, family, pixel size, color, horizontal
alignment, movement, and width resizing. While editing, its box grows
horizontally to fit the longest line up to the canvas edge, then wraps and grows
vertically. Text is kept editable in its own `Text N` layer.

Documents use the provisional `.cimg` version 15 format. The application reads
versions 1 through 14 and writes version 15, migrating older documents on save;
version 1 recovery envelopes accept a version 15 document payload. Source images
remain unchanged, and original source dimensions are retained for relinking.
Area Selection is a separate tool from object Selection. Rectangle and ellipse
gestures can replace, add to, or subtract from the temporary per-tab selection.
New Paint and Eraser strokes, including mask edits, persist their clipping
geometry in `.cimg` v13; bucket fills persist as v14 operations. The Linear
Gradient tool previews and stores a color-to-transparent operation in `.cimg`
v15 on the active layer or selected mask, clipped by the active selection.
Fill detection uses the active layer or selected mask, four-way connectivity,
per-channel RGBA tolerance, brush color and alpha, and the active selection
clip. No-op fills do not add history. The selection itself does not dirty or
serialize with the document. Canvas documents can use standard presets or custom dimensions. Self-contained
canvas documents currently allow up to 32768 pixels per side and 64 million
pixels total. These are format limits, not performance claims.
**Image > Canvas Size** changes current document bounds without resampling layer
content. A 3×3 anchor controls placement; new area uses the configured canvas
background or transparency for source-image documents. The edit is undoable and
is included in full export, Quick Export, recovery, and linked PNG publication.

Flattened PNG and JPEG export are implemented. PNG preserves transparency;
JPEG uses a configurable quality and opaque background. Quick Export can export
the selected layer, group, or Background. Image decoding uses Qt image I/O;
PNG, JPEG, BMP, WebP, and TIFF are on the manual validation checklist, while
packaged plugin validation remains open. The Video Editor linked-image code is
present as a prototype, but acceptance is gated on the standalone checks in the
[roadmap](ROADMAP.md).

## Approved First Editing-Release Direction

The first editing release is aimed at creators making thumbnails and artwork
for videos and social media. Keep the workflow easy to explore and make common
actions convenient. This release extends the current raster editor; it does
not aim for parity with advanced photo-retouching software.

Basic editable text is implemented with these capabilities:

- edit multiline content, font family, pixel size, color, and horizontal
  alignment;
- create a `Text N` layer by clicking an empty canvas location and edit its text
  directly; a horizontal drag can set a custom initial text box width;
- confirm with Ctrl+Enter or a click outside the editor, insert line breaks with
  Enter, cancel with Esc, and reopen an existing text object with a double-click;
- move text and resize its width without changing the font size; while editing,
  the box grows to fit the longest line up to the canvas edge, then text wraps
  and its height grows to fit;
- include text in layer visibility and opacity, groups, selection, deletion,
  Undo/Redo, flattened and Quick Export, save/reopen, and recovery;
- save only the requested font family name, using the system fallback if the
  font is unavailable. The default is 48 px Sans Serif, opaque black, and
  left-aligned. Fonts are not embedded; outlines, effects, and backgrounds are
  outside this scope.

The approved representative document is 1920x1080 with up to five layers and
one group. It is a validation workload, not a maximum canvas size, layer count,
or group count. Keep standard and custom canvas dimensions available.

Use the maintainer's current PC as the reference test machine: AMD Ryzen 5
3600, 32 GB RAM, NVIDIA GeForce GTX 1660 SUPER with 6 GB VRAM, and Windows 11.
Record test conditions and results when profiling. This defines the initial
validation system, not a minimum hardware requirement; set numerical minimums
only after measurements on this and additional systems. No real-time or
performance guarantee is approved yet.

## Compatibility and Release Order

The current implementation preserves the ability to open existing `.cimg`
versions 1 through 14 and writes the current version 15 format on save. Automated
regression tests cover migration, canvas-size persistence, clipped paint/erase,
bucket-fill and linear-gradient operations, and version 15 recovery payload. This implementation does not mark manual text checks or
Video Editor linked-image acceptance complete.

Per project policy, complete and accept the standalone minimum before accepting
Video Editor linked-image compatibility. The first editing release follows the
linked-image acceptance milestone as ordered in the [roadmap](ROADMAP.md).
Cross-application work must validate the producer and consumer contracts and
their regression coverage.

## Approved Layer Mask Extension

On 2026-10-02 the maintainer approved raster layer masks as the next code
feature. Each editable raster layer, including group children, can have one
mask. A white mask initially reveals everything. Selecting its thumbnail
directs the existing Paint/Eraser tools to it; grayscale paint controls coverage
and color alpha controls strength. Eraser writes black. The panel offers
Add Layer Mask, Enable Layer Mask, and Remove Layer Mask. Layer transforms,
Undo/Redo, persistence, recovery, exports, and linked PNG output include masks.
Background and group masks are outside this extension. This code work does not
complete the existing first-release or cross-application acceptance gates.

## Linked image layer extension (approved 2026-10-02)

**File > Import Image as Layer** accepts multiple PNG, JPEG, BMP, WebP, or TIFF
files in an open document. Local files can also be dropped on the canvas.
Decoding runs in the background with cancellation; the complete batch commits
as one Undo edit or inserts nothing. Each layer uses the file name and retains
a link to the original file. Default placement centers and only shrinks images
larger than the canvas; a drop centers at its pointer.

The last imported image is selected with Selection active. Drag to move, use
corners to resize proportionally, hold Alt for independent dimensions, and use
the rotation handle for free rotation. Shift snaps rotation to 15 degrees.
Esc cancels the gesture. Image gestures leave masks and unselected strokes
fixed; layer transform commands continue to transform content and masks.

Loaded pixels remain stable until reopening or relinking. Missing/unreadable
or incompatible sources are marked in Layers. File > Relink Image replaces a
selected image reference only and requires the original oriented dimensions.
Saving and editing other layers remain available, but dependent exports and
linked PNG publication fail while preserving the previous output. No embedded
images, automatic file copies, live external updates, perspective, or color
adjustments are included. Background remains locked and .csp is unchanged.

## Contextual deletion

Delete removes selected objects in the canvas with Selection active. The layer,
mask, and unselected contents remain. In Layers, Delete, the visible Delete
button, and Delete Layer/Delete Group context commands remove the selected
editable stack items in one Undo/Redo edit; groups include their children.
Background is protected, including mixed selections. Text, rename, and numeric
fields keep normal character deletion. Deleting a missing image reference is
allowed and can unblock exports. Original files are preserved. The document
format remains v15 and the contextual shortcut is customizable.

## Outside This Release

Keep retouching, color adjustment, broad effect systems, advanced
typography, text outlines, and text effects in the backlog for later evaluation.
Do not expand the release boundary without validated user workflows and
performance measurements. The Image Editor remains a separate development
track; Video Editor stability remains a suite priority.

## Validation

Automated regression coverage is present for text editing, rendering,
selection, transforms, Undo/Redo, export, save/reopen, recovery, and linked PNG
publication. Manual visual validation of text remains pending; the linked-image
producer/consumer acceptance gate also remains pending until its manual checks
are recorded. Profile the approved 1080p workload on the reference hardware
class. macOS and Linux packaging and interaction checks remain pending. See
[`MANUAL_VALIDATION.md`](MANUAL_VALIDATION.md) and the [roadmap](ROADMAP.md)
for current results and outstanding checks.
