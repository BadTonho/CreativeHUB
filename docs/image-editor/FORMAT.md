# Image Editor Document Format

Status: **provisional version 5**. The `.cimg` extension is temporary until a
later format review. Version 4 added editable raster layers; version 5 adds
eraser strokes. Versions 1 through 4 remain readable.

## Document contents

A `.cimg` file is UTF-8 JSON with these top-level fields:

| Field | Type | Meaning |
| --- | --- | --- |
| `format` | string | Must be `creative-suite-image-document`. |
| `version` | integer | Current version is `5`. |
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
  "version": 5,
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
documents visually unchanged when they are opened and later saved as version 5.
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
field and reveals visible content in lower layers.

```json
{
  "kind": "erase_stroke",
  "diameter": 12,
  "points": [{ "x": 48.0, "y": 32.0 }, { "x": 55.0, "y": 32.0 }]
}
```

For legacy top-level operations, crops use the current image bounds and change
the rendered Background size; rotations may swap its dimensions. This behavior
is retained only to read and preserve documents created by versions 1–3.
Version 1 uses the legacy `source` object. Version 2 adds canvas bases. Version
3 adds top-level paint strokes. Version 4 adds layers. Version 5 adds
layer-local eraser strokes. Saving any supported version writes version 5.

Undo and redo history are in memory and are not stored in `.cimg`. A save writes
to a temporary file and atomically replaces the destination. Export is a
separate flattened raster file: visible layers are composited with their
opacity; PNG preserves alpha; JPEG uses quality 95 and flattens transparent
pixels over white.

Recovery snapshots use a separate `creative-suite-image-recovery` JSON wrapper
with the document payload, intended `.cimg` destination, and a session identity
for unsaved canvases. Recovery wrapper version 1 accepts document payloads in
versions 1 through 5. Autosave and recovery preserve layer order, properties,
IDs, and operations.

Unsupported versions, invalid layer stacks, and invalid operation data are
rejected without replacing the currently open document.
