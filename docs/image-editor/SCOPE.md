# Image Editor Scope

Status: **editable text, raster masks, linked image layers, canvas resizing, layer outlines, clipping masks, six blend modes and Gradient Map, Brightness/Contrast, Exposure / Offset / Gamma, global Hue/Saturation, global Levels and global Curves adjustments are implemented;
manual visual validation and release acceptance remain pending**. The Image Editor is a standalone raster
editor in `apps/image-editor/`. Its current behavior and the approved next
release direction are recorded separately below. See the
[roadmap](ROADMAP.md) for milestone order and acceptance gates.

Interactive editing uses an incremental CPU raster cache and optional shared GPU
composition with direct texture presentation. Settings > Rendering offers Automatic
and CPU, with complete CPU recovery on capacity/technical failures. Automatic GPU
activation is governed by the correctness/performance gate and reference Windows
[evidence](performance/INTERACTIVE_RENDERING_RESULTS_WINDOWS_2026-10-12.md). Tools, export and
published PNG rasterization remain CPU. Native macOS/Linux GPU acceptance is pending.

## Implemented Today

The current application supports one raster document at a time, either linked
to an original image or created as a self-contained canvas. It provides a
locked Background, editable raster layers, one-level groups, visibility,
opacity, ordering, crop, quarter-turn rotation, flips, painting, bucket fill,
Magic Wand selection, linear gradients, blur, erasing,
editable line/rectangle/ellipse shapes, editable text, raster layer masks,
imported image layers, object selection and transforms, an RGBA Eyedropper that
samples visible composite pixels with nonzero alpha into the paint color,
Undo/Redo, local autosave and recovery, and bounded technical error logging.
Text supports multiline content, family, pixel size, color, horizontal
alignment, movement, and width resizing. While editing, its box grows
horizontally to fit the longest line up to the canvas edge, then wraps and grows
vertically. Text is kept editable in its own `Text N` layer.

Documents use the provisional `.cimg` version 27 format. The application reads
versions 1 through 26 and writes version 27, migrating older documents on save;
version 1 recovery envelopes accept a version 27 document payload. Source images
remain unchanged, and original source dimensions are retained for relinking.
Area Selection is a separate tool from object Selection. Its Rectangle and
Ellipse gestures can replace, add to, or subtract from the temporary per-tab
selection. Lasso is a separate toolbar tool with its own implementation under
`src/ui/tools/lasso/`; it shares the selection and Replace/Add/Subtract
controls. Lasso follows the pointer without smoothing, closes on release, and
rejects degenerate or oversized contours without replacing the previous
selection.
New Paint and Eraser strokes, including mask edits, persist their clipping
geometry in `.cimg` v13; bucket fills persist as v14 operations. The Linear
Gradient tool previews and stores a color-to-transparent operation in `.cimg`
v15 on the active layer or selected mask, clipped by the active selection.
The Blur tool previews a stroke with independent brush size and radius controls,
and stores one ordered `.cimg` v16 operation on the active layer or mask. It
uses only that target's preceding pixels, respects the active selection, and
supports Undo/Redo and cancellation during a gesture. Radius zero and unchanged
results do not create edits.
Fill detection uses the active layer or selected mask, four-way connectivity,
per-channel RGBA tolerance, brush color and alpha, and the active selection
clip. No-op fills do not add history. The selection itself does not dirty or
serialize with the document. Canvas documents can use standard presets or
custom dimensions. Self-contained canvas documents currently allow up to
32768 pixels per side and 64 million pixels total. These are format limits,
not performance claims.

The Magic Wand uses the same raw active-layer or mask target, but makes a
temporary four-connected selection with per-channel RGBA tolerance. It does
not clip detection to an existing selection; Replace/Add/Subtract combines the
new region afterward. Its tolerance is session-only and starts at zero after
launch. Selections remain per-tab UI state and do not change pixels, history,
or `.cimg` data.
**Image > Canvas Size** changes current document bounds without resampling layer
content. A 3×3 anchor controls placement; new area uses the configured canvas
background or transparency for source-image documents. The edit is undoable and
is included in full export, Quick Export, recovery, and linked PNG publication.

Flattened PNG and JPEG export are implemented. PNG preserves transparency;
JPEG uses a configurable quality and opaque background. Quick Export can export
the selected layer, group, or Background. All export scopes reject the original
base image and imported raster sources as output destinations, including
existing symbolic/hard-link aliases. Source
files and document state remain unchanged when the destination is rejected.
Image decoding uses Qt image I/O; PNG, JPEG, BMP, WebP, and TIFF are on the
manual validation checklist, while packaged plugin validation remains open.
The Video Editor linked-image code is
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
  left-aligned. Fonts are not embedded. The text operation does not contain effects or
  backgrounds; the separate layer-outline property can outline editable text.

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
versions 1 through 26 and writes the current version 27 format on save. Automated
regression tests cover migration, canvas-size persistence, clipped paint/erase,
bucket-fill, linear-gradient, blur, adjustment channels, and version 27 recovery payload. This implementation does not mark manual text checks or
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
format is v21 and the contextual shortcut is customizable.

## Approved Blur Tool

On 2026-10-06 the maintainer approved an editable Blur tool. It applies a
premultiplied-RGBA blur to the active raster layer or selected mask with a
1–1024 px brush size (default 12) and 0–100 px radius (default 10). The active
Area Selection clips writes. A live preview is temporary; Escape cancels, and
each changed gesture becomes one ordered, undoable `.cimg` v16 operation.
General retouching and broad effect systems remain outside this feature.

## Editable layer outlines (2026-10-20)

Editable layers, including group children, offer Outline controls below the
layer list: enable, thickness (1-1024 px, initially 2), RGBA color (initially
opaque black), Inside/Outside placement (initially Outside), and removal.
Background, groups, and multiple selections cannot receive an outline.
Disabling retains settings; removing resets them. The outline follows the
layer alpha after its raster mask, before layer/group opacity, and includes
transparent holes. It is clipped to canvas bounds and does not change raw
paint/fill/selection targets or source files. Object transforms keep the
configured thickness in document pixels. Text remains editable.

Live editing, history, v21 save/reopen, recovery, thumbnails, composite sampling,
full/Quick Export, and linked PNG publication include the outline. The
[feature contract](features/LAYER_OUTLINE.md) records rendering and coverage.
Manual/native and macOS/Linux visual acceptance remains pending.

## Outside This Release

The [roadmap implementation sequence](ROADMAP.md#4-feature-expansion--implementation-sequence),
updated at the maintainer's request on 2026-10-20, records implemented layer outlines and clipping masks,
then plans additional blend modes, adjustments, styles, selections/vectors, brushes,
deformation, layout/color, automation, and assisted editing. The remaining
items are planned extensions and are not automatically added to the first
editing release. Existing shape strokes, linked images, basic object selection, and
linear gradients are partial building blocks rather than their advanced
equivalents.

Implementation can proceed while manual acceptance is pending; the existing
standalone, linked-image, and first-release acceptance order still applies.
Assign release scope separately, based on bounded workflows and performance
evidence. The Image Editor remains a separate development track; Video Editor
stability remains a suite priority.

## Validation

Automated regression coverage is present for text editing, rendering,
selection, transforms, Undo/Redo, export, save/reopen, recovery, and linked PNG
publication. Manual visual validation of text remains pending; the linked-image
producer/consumer acceptance gate also remains pending until its manual checks
are recorded. Profile the approved 1080p workload on the reference hardware
class. macOS and Linux packaging and interaction checks remain pending. See
[`MANUAL_VALIDATION.md`](validation/MANUAL_VALIDATION.md) and the [roadmap](ROADMAP.md)
for current results and outstanding checks.

## Editable clipping masks

Sibling layers can clip to their shared base alpha without baking editable
content. Layers commands, customizable shortcuts, stack/history, masked alpha,
outline preservation, thumbnails, CPU/GPU preview and export are implemented.
See [contract and verification](features/CLIPPING_MASKS.md). Six blend modes are implemented; visual and release acceptance remain pending.

## Layer and group blend modes

Normal, Multiply, Screen, Overlay, Add and Difference are editable per layer and
isolated group. Clipped children blend within their chain; base outlines remain
protected internally. Undo/Redo, CPU/OpenGL preview, v21 persistence/recovery,
exports and PNG publication retain editable content. See [contract](features/BLEND_MODES.md).


## Gradient Map adjustment layers

The first non-destructive adjustment modifies lower siblings or its clipping
chain, with editable RGB points, masks captured from area selections, history,
preserved alpha, six blend modes and CPU/OpenGL composition. Full/group exports
include it; isolated adjustment Quick Export is unavailable. See
[contract and verification](features/GRADIENT_MAP.md). Native visual and
macOS/Linux acceptance remain pending. Brightness/Contrast and Exposure / Offset / Gamma are also implemented.

## Brightness/Contrast adjustments

The second editable adjustment uses signed integer controls, selection-seeded masks,
alpha-preserving CPU/OpenGL processing and v21 persistence. See
[contract](features/BRIGHTNESS_CONTRAST.md). Native visual/platform acceptance remains pending.

## Exposure / Offset / Gamma adjustments

The third editable adjustment provides decimal controls, linear-sRGB processing,
selection-seeded masks, atomic Reset, history, CPU/OpenGL preview, exports and
PNG publication. Native format v22 retains supported older documents and Hub
backup/restore. See [contract](features/EXPOSURE_OFFSET_GAMMA.md). Visual/platform
acceptance is separate; global Hue/Saturation is also implemented.

## Global Hue/Saturation adjustments

The fourth adjustment provides integer Hue, Saturation and Lightness controls,
HSL processing on encoded sRGB, chromatic thumbnails, masks, atomic Reset,
history, CPU/OpenGL preview and document/group exports. Native v23 and Hub
backup/restore retain supported older documents. See [contract](features/HUE_SATURATION.md).
Colorize and per-color targeting remain planned.
Native visual/platform acceptance is tracked independently.

## Global Levels adjustments

The fifth adjustment provides an RGB master and independent Red, Green and Blue
input black/white, midtone gamma and output black/white controls. Its combined
input histogram is calculated asynchronously from the stack below the
adjustment. Masks, history, CPU/OpenGL preview, document/group exports, linked
PNG and native v27/Hub backups retain editable content. See
[contract](features/LEVELS.md). Native visual/platform acceptance is separate.

## Global Curves adjustments

The sixth adjustment provides an RGB master curve and independent Red, Green,
and Blue curves. The master maps first, followed by the matching channel curve.
A combined input histogram is calculated asynchronously and shown behind the
graph. Masks, history, CPU/OpenGL preview, document/group exports, linked PNG,
and v27/Hub backups retain editable points. See [contract](features/CURVES.md).
Native visual/platform acceptance remains pending.
