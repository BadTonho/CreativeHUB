# Image Editor Document Format

Status: **provisional version 12**. The `.cimg` extension is temporary until a
later format review. Version 4 added editable raster layers; version 5 adds
eraser strokes; version 6 adds editable line, rectangle, and ellipse shapes;
version 7 adds stable IDs to paint and eraser strokes; version 8 adds
one-level layer groups; version 9 adds editable text operations; version 10
adds raster layer masks; version 11 adds linked raster image operations;
version 12 adds independently resizable canvas bounds. Versions 1 through 11
remain readable and save as v12.

## Document contents

A `.cimg` file is UTF-8 JSON with these top-level fields:

| Field | Type | Meaning |
| --- | --- | --- |
| `format` | string | Must be `creative-suite-image-document`. |
| `version` | integer | Current version is `12`. |
| `base` | object | A linked source image or a self-contained canvas. |
| `canvas` | object | Version 12 current document bounds and base-image offset. |
| `operations` | array | Version 1–3 edits retained as Background content. |
| `layers` | array | Version 4 and later layer stack ordered bottom-to-top. |

Source-image bases contain `kind: "source_image"`, `path`, `width`, and
`height`. These dimensions always describe the original oriented source image,
even after a canvas resize. Relinking therefore continues to require the
original source dimensions. When the source is in the document directory or one of its
subdirectories, the path is stored relative to the document. External sources
are stored with an absolute path. Relative paths are resolved from the `.cimg`
directory. A missing source keeps the document available for relinking; the
replacement image must have the recorded dimensions.

A canvas base contains `kind: "canvas"`, `width`, `height`, and a `background`
color in `#AARRGGBB` notation. The canvas is reconstructed from these values
without an external raster file. These dimensions retain the original base
bitmap size. Canvas dimensions must be positive, at most 32768 pixels per side,
and at most 64 million pixels total.

## Resizable canvas bounds (version 12)

Version 12 stores the current document dimensions separately from the original
base dimensions. The base image remains at its original pixel size; the offset
positions it inside the current canvas. Editable layer, group, mask, and object
operations are translated by the chosen anchor displacement. The resize is
non-destructive to the source and does not resample layer content.

```json
"canvas": {
  "width": 2400,
  "height": 1080,
  "base_offset_x": 240,
  "base_offset_y": 0
}
```

Offsets are signed integer pixel coordinates bounded to ±1,000,000. Added area
shows the configured base color for canvas documents and transparency for
source-image documents. Content beyond the current bounds is clipped by
composition. Anchored resizing and its layer/group/mask translations are a
single undoable edit. Full export, Quick Export, recovery, and linked PNG
publication use the new bounds too. Original source dimensions remain unchanged
for relink validation.

Versions 1 through 11 have no `canvas` object. They load with the canvas equal
to the dimensions produced by their existing base operations and a zero base
offset, preserving their previous appearance. Saving any supported version
writes version 12. Recovery wrapper version 1 accepts document payloads through
v12.

## Linked raster images (version 11)

Editable raster layers accept the following operation. It is forbidden in
Background, base operations, masks, and group operations.

```json
{
  "kind": "raster_image",
  "id": "7e98eab3-a54a-4f91-8e73-35de86f358cb",
  "path": "assets/photo.png",
  "width": 640,
  "height": 480,
  "transform": [1, 0, 0, 1, 100, 80]
}
```

The UUID is unique across content and masks. Width and height describe the
automatically oriented decoded source, with the same positive dimension and
pixel limits as the canvas. The six finite affine coefficients are
[m11, m12, m21, m22, dx, dy]: x' = m11*x + m21*y + dx,
y' = m12*x + m22*y + dy. The matrix must be invertible; perspective is not
supported. Image coordinates describe pixel edges. Placement can extend beyond
the canvas and composition clips the result.

Paths follow the base-image relative/absolute rules above, including Save As
and recovery. Source pixels are never embedded or automatically copied.
Export destinations cannot overwrite an imported source file.
Missing, unreadable, or dimension-mismatched files preserve their operations on
open. Saving remains available; exports that include an unavailable visible
reference fail before touching their previous output. Relink requires the
recorded dimensions and changes only the selected reference.

Geometry gestures replace only the image matrix. The layer mask and other
operations stay in their existing canvas coordinates. Existing layer/group
crop, quarter-turn, and flip commands continue to transform rendered content
and layer masks. Image pixels render at their position in the operation list,
then the layer mask, layer opacity, and group composition apply.

Recovery retains envelope version 1 and accepts document payloads through v12.
The Video Editor consumes flattened published PNG files; its .csp schema does
not change.

## Raster layer masks (version 10)

An editable raster layer may have an optional `mask` object containing
`enabled` (boolean) and `operations` (array). Omitting `mask` means the layer
has no mask. Background and groups cannot contain this field. A present mask
starts as opaque white over the fixed canvas; an empty operation array is valid.

```json
"mask": {
  "enabled": true,
  "operations": []
}
```

Mask operations reuse `paint_stroke`, `erase_stroke`, `crop`, `rotate`,
`flip_horizontal`, and `flip_vertical`. Shape, text, and raster image operations are rejected.
Paint colors must have equal red, green, and blue components; their alpha
controls blending strength. The editing API converts the selected RGB color using Qt
`qGray` before storing it. Eraser strokes paint opaque black. Stroke IDs are
unique across document content and masks; existing brush, point, and operation
limits also apply to masks.

White reveals, black hides, and gray supplies partial coverage. The renderer
multiplies the layer's premultiplied channels and alpha by mask luminance,
including antialiased coverage and transparent transform borders. Layer
opacity is applied afterward; group opacity is still applied once after child
composition. Disabled masks retain edits but bypass this multiplication.

Layer crop, quarter-turn rotation, and flips append the equivalent transform
to an existing mask's operation sequence. Mask strokes in transformed groups
are mapped back into the child's coordinates. Creating, removing, toggling,
and painting a mask are undoable document edits. Mask editing target selection
is temporary UI state and is not persisted. Versions 1–9 load without masks;
saving upgrades the envelope to v12. A mask in an older envelope is rejected.
Recovery, full export, Quick Export, and linked PNG publication include masks.

## Layer stack

Versions 4 through 7 store a flat layer array ordered bottom-to-top. Version 8
stores the same order as a tree: root entries and group children are each
ordered bottom-to-top. Raster entries retain their stable UUID, name,
visibility, opacity, and ordered operations. There must be exactly one
`background` layer at the bottom of the root stack. It is named `Background`,
has 100% opacity, and has no layer operations; its pixels come from the base
and the top-level legacy `operations` array. Other layers use `kind: "raster"`,
have opacity from 0 through 100, and may be empty. IDs are unique canonical
UUIDs. The 512-item limit counts Background, raster layers, and groups.

A version 8 root entry may instead use `kind: "group"`. A group has a stable
UUID, a non-empty name, visibility, opacity from 0 through 100, ordered
transform operations, and a `children` array of raster layers. Empty groups are
valid; groups cannot contain Background or other groups. Group visibility
hides all children without changing their individual visibility. Children are
composited in order, crop/rotation/flip operations are applied to their
combined pixels, and group opacity is applied once to that result.

```json
{
  "format": "creative-suite-image-document",
  "version": 12,
  "base": {
    "kind": "canvas",
    "width": 1920,
    "height": 1080,
    "background": "#00000000"
  },
  "canvas": {"width": 1920, "height": 1080, "base_offset_x": 0, "base_offset_y": 0},
  "operations": [],
  "layers": [
    {
      "id": "20a31f58-9434-427c-80c6-f1681c98ea32",
      "name": "Background",
      "kind": "background",
      "visible": true,
      "opacity": 100,
      "operations": []
    },
    {
      "id": "0c693567-1bf5-485e-a78f-1b71d95ea73b",
      "name": "Layer 1",
      "kind": "raster",
      "visible": true,
      "opacity": 100,
      "operations": []
    }
  ]
}
```

The active layer is editor session state and is not persisted. Opening a
document selects the topmost editable layer, or `Background` when no editable
layers remain. New documents start with `Background` and a transparent
`Layer 1`. The Background can be hidden, but cannot be renamed, reordered,
deleted, painted, transformed, or given a different opacity.

When a user draws a shape, the editor stores it in a new editable raster layer
named `Shape N`, inserted immediately above the selected layer or inside its
selected group. When a group is selected, the shape layer is inserted in the
root directly above it. Background remains locked. Each generated shape layer
contains only the new shape, so its visibility, opacity, and Quick Export
output can be controlled independently.

The editor can create empty groups or group selected contiguous sibling raster
layers. Ungroup removes the group and preserves children in place; deleting a
group removes it and all of its children as one undoable edit. Dragging can
reorder root items or move raster layers into or out of groups. Groups cannot
be nested. If a raster layer inside a group is selected when an empty group is
created, the new group is inserted in the root directly above the containing
group to preserve the one-level rule.

## Operations

The top-level `operations` array preserves the ordered, non-destructive edits
from versions 1–3 and renders them as part of `Background`. This keeps old
documents visually unchanged when they are opened and later saved as version 12.
New edits are stored in the selected raster layer's `operations` array.

Each layer operation is evaluated on the current fixed document canvas. Crop
keeps the selected rectangle in its canvas coordinates and makes pixels outside
it transparent; it does not resize the document. Rotation is around the recorded
transform bounds, and content outside the canvas is clipped. Flips mirror within
their recorded transform bounds. Version 12 stores optional `bounds_x`,
`bounds_y`, `bounds_width`, and `bounds_height` on fixed-canvas rotate and flip
operations so later canvas resizes do not move historical pivots. Older
versions infer these bounds from the then-current canvas. Paint points use
floating-point pixel coordinates in the canvas at that point in the layer's
operation list. A paint color uses `#AARRGGBB`, a
diameter is from 1 through 1024 pixels, and a stroke contains 1 through 100,000
points. Version 5 adds `erase_stroke`, with the same point and diameter limits;
it clears alpha in its raster layer with antialiased edges. It has no color
field and reveals visible content in lower layers. Version 6 adds a `shape`
operation to editable layer sequences. Shape operations retain their position
among paint, erase, crop, rotate, and flip operations. Version 7 gives every
paint and erase stroke a stable canonical UUID in its `id` field. Version 8
adds the group tree without changing operation IDs. Version 9 adds editable
text. Object IDs are unique across paint, erase, shape, and text operations.

```json
{
  "kind": "erase_stroke",
  "id": "402f47e8-6e6a-452a-80d0-0e37bcd0268d",
  "diameter": 12,
  "points": [{ "x": 48.0, "y": 32.0 }, { "x": 55.0, "y": 32.0 }]
}
```

A shape has a unique canonical UUID, a `shape_type` of `line`, `rectangle`, or
`ellipse`, floating-point `start_x`, `start_y`, `end_x`, and `end_y` canvas
coordinates, stroke and fill enable flags, `#AARRGGBB` colors, and an integer
stroke width from 1 through 1024 pixels. Both endpoints must be finite and
inside the canvas. Lines require a non-zero length, use stroke only, and cannot
have fill enabled. Rectangles and ellipses require non-zero width and height.
At least one of stroke or fill must be enabled. Shape IDs are unique across
editable layers. Background cannot contain shape operations.

```json
{
  "kind": "shape",
  "id": "402f47e8-6e6a-452a-80d0-0e37bcd0268d",
  "shape_type": "ellipse",
  "start_x": 64.0,
  "start_y": 48.0,
  "end_x": 256.0,
  "end_y": 192.0,
  "stroke_enabled": true,
  "stroke_color": "#FF202020",
  "stroke_width": 2,
  "fill_enabled": true,
  "fill_color": "#8060A0FF"
}
```

Version 9 adds a `text` operation to editable raster layers. Text content is a
plain UTF-8 string with newline-separated lines. `font_family` stores only the
requested family name; fonts are not embedded, and the system font fallback is
used if that family is unavailable. `font_pixel_size` is a canvas-pixel size
from 1 through 1024. `color` uses `#AARRGGBB`; new text defaults to opaque
black, 48 pixels, `Sans Serif`, and left alignment. `alignment` is `left`,
`center`, or `right`. The `x` and `y` coordinates and `box_width` are floating
point canvas pixels. While editing, the box grows horizontally to fit the
longest line, up to the canvas's right edge; text wraps at word boundaries or
between characters at that edge, and the layout height grows to fit its
content. A text box reopened without a text or font-size change keeps its saved
width. The text ID is a unique
canonical UUID across all editable object operations. Content is limited to
16,384 characters, font family names to 256 characters, and the text geometry
must fit within the canvas. Text is stored only in version 9 or later and is
not allowed in Background or legacy top-level operations.

```json
{
  "kind": "text",
  "id": "402f47e8-6e6a-452a-80d0-0e37bcd0268d",
  "content": "Video title\nSecond line",
  "font_family": "Sans Serif",
  "font_pixel_size": 48,
  "color": "#FF000000",
  "alignment": "left",
  "x": 96.0,
  "y": 72.0,
  "box_width": 640.0
}
```

For legacy top-level operations, crops use the current image bounds and change
the rendered Background size; rotations may swap its dimensions. This behavior
is retained only to read and preserve documents created by versions 1–3.
Version 1 uses the legacy `source` object. Version 2 adds canvas bases. Version
3 adds top-level paint strokes. Version 4 adds layers. Version 5 adds
layer-local eraser strokes. Version 6 adds editable shapes to layer operations.
Version 7 adds IDs to paint and eraser operations. When reading versions 1–6,
the loader generates in-memory IDs for operations that do not contain them;
the next save writes those IDs in version 12. Versions 1 through 11 remain
visually compatible. Saving any supported version writes version 12. New text
layers are named `Text N` and inserted using the same stack placement rule as
shape layers; they can be grouped, hidden, assigned opacity, selected, moved,
resized by changing their box width, and deleted as editable operations.

Undo and redo history are in memory and are not stored in `.cimg`. A save writes
to a temporary file and atomically replaces the destination. Export is a
separate flattened raster file: visible layers are composited with their
opacity; PNG preserves alpha; JPEG quality is configurable from 0 through 100
and defaults to 95. JPEG transparency is flattened over a selectable opaque
background color that defaults to white. The last accepted JPEG quality and
background color are stored in local application settings and are not included
in `.cimg`. Export writes through `QSaveFile`, so failed or cancelled exports
do not replace the destination with a partial image. The Image Editor renders
and encodes an immutable document snapshot on a worker; cancellation during the
Qt image writer's blocking encode is honored before the temporary file is
committed. **Quick Export** writes only the selected layer at the current
canvas dimensions, respecting its visibility and opacity; a hidden selected
layer therefore produces a transparent PNG or a JPEG filled with the saved
matte color. Selecting a group exports its composed children with the group's
visibility, opacity, and transforms. Selecting Background exports the base
image and its document-level operations without the editable layers. The save
dialog chooses PNG or JPEG; Quick Export uses saved JPEG options without
showing the options dialog.

Recovery snapshots use a separate `creative-suite-image-recovery` JSON wrapper
with the document payload, intended `.cimg` destination, and a session identity
for unsaved canvases. Recovery wrapper version 1 accepts document payloads in
versions 1 through 12. Autosave and recovery preserve root order, group
children, properties, IDs, and operations.

Unsupported versions, invalid layer stacks, and invalid operation data are
rejected without replacing the currently open document.
