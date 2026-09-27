# Image Editor Document Format

Status: **provisional version 7**. The `.cimg` extension is temporary until a
later format review. Version 4 added editable raster layers; version 5 adds
eraser strokes; version 6 adds editable line, rectangle, and ellipse shapes;
version 7 adds stable IDs to paint and eraser strokes. Versions 1 through 6
remain readable.

## Document contents

A `.cimg` file is UTF-8 JSON with these top-level fields:

| Field | Type | Meaning |
| --- | --- | --- |
| `format` | string | Must be `creative-suite-image-document`. |
| `version` | integer | Current version is `7`. |
| `base` | object | A linked source image or a self-contained canvas. |
| `operations` | array | Version 1–3 edits retained as Background content. |
| `layers` | array | Version 4 and later layer stack ordered bottom-to-top. |

Source-image bases contain `kind: "source_image"`, `path`, `width`, and
`height`. When the source is in the document directory or one of its
subdirectories, the path is stored relative to the document. External sources
are stored with an absolute path. Relative paths are resolved from the `.cimg`
directory. A missing source keeps the document available for relinking; the
replacement image must have the recorded dimensions.

A canvas base contains `kind: "canvas"`, `width`, `height`, and a `background`
color in `#AARRGGBB` notation. The canvas is reconstructed from these values
without an external raster file. Canvas dimensions must be positive, at most
32768 pixels per side, and at most 64 million pixels total.

## Layer stack

Version 4 and later store every layer's stable UUID, name, type, visibility,
opacity, and ordered operations. There must be exactly one `background` layer at index
zero. It is named `Background`, has 100% opacity, and has no layer operations;
its pixels come from the base and the top-level legacy `operations` array.
Other layers use `kind: "raster"`, have opacity from 0 through 100, and may be
empty. Names are non-empty and at most 128 characters. IDs must be unique
canonical UUIDs. The stack is composited from bottom to top.

```json
{
  "format": "creative-suite-image-document",
  "version": 7,
  "base": {
    "kind": "canvas",
    "width": 1920,
    "height": 1080,
    "background": "#00000000"
  },
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

## Operations

The top-level `operations` array preserves the ordered, non-destructive edits
from versions 1–3 and renders them as part of `Background`. This keeps old
documents visually unchanged when they are opened and later saved as version 7.
New edits are stored in the selected raster layer's `operations` array.

Each layer operation is evaluated on the fixed document canvas. Crop keeps the
selected rectangle in its original canvas coordinates and makes pixels outside
it transparent; it does not resize the document. Rotation is around the canvas
center, and content outside the canvas is clipped. Flips mirror within the
canvas bounds. Paint points use floating-point pixel coordinates in the canvas
at that point in the layer's operation list. A paint color uses `#AARRGGBB`, a
diameter is from 1 through 1024 pixels, and a stroke contains 1 through 100,000
points. Version 5 adds `erase_stroke`, with the same point and diameter limits;
it clears alpha in its raster layer with antialiased edges. It has no color
field and reveals visible content in lower layers. Version 6 adds a `shape`
operation to editable layer sequences. Shape operations retain their position
among paint, erase, crop, rotate, and flip operations. Version 7 gives every
paint and erase stroke a stable canonical UUID in its `id` field. Object IDs
are unique across paint, erase, and shape operations.

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

For legacy top-level operations, crops use the current image bounds and change
the rendered Background size; rotations may swap its dimensions. This behavior
is retained only to read and preserve documents created by versions 1–3.
Version 1 uses the legacy `source` object. Version 2 adds canvas bases. Version
3 adds top-level paint strokes. Version 4 adds layers. Version 5 adds
layer-local eraser strokes. Version 6 adds editable shapes to layer operations.
Version 7 adds IDs to paint and eraser operations. When reading versions 1–6,
the loader generates in-memory IDs for operations that do not contain them;
the next save writes those IDs in version 7. Saving any supported version
writes version 7.

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
matte color. Selecting Background exports the base image and its document-level
operations without the editable layers. The save dialog chooses PNG or JPEG;
Quick Export uses saved JPEG options without showing the options dialog.

Recovery snapshots use a separate `creative-suite-image-recovery` JSON wrapper
with the document payload, intended `.cimg` destination, and a session identity
for unsaved canvases. Recovery wrapper version 1 accepts document payloads in
versions 1 through 7. Autosave and recovery preserve layer order, properties,
IDs, and operations.

Unsupported versions, invalid layer stacks, and invalid operation data are
rejected without replacing the currently open document.
