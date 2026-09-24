# Image Editor Document Format

Status: **provisional version 3**. The `.cimg` extension is temporary until a
later format review. Version 2 added self-contained blank-canvas documents;
version 3 adds editable paint strokes. Version 1 and version 2 documents remain
readable.

## Document contents

A `.cimg` file is UTF-8 JSON with these top-level fields:

| Field | Type | Meaning |
| --- | --- | --- |
| `format` | string | Must be `creative-suite-image-document`. |
| `version` | integer | Current version is `3`; versions 1 and 2 remain readable. |
| `base` | object | Either a linked source image or a self-contained canvas. |
| `operations` | array | Ordered, non-destructive edits. |

Version 2 source-image bases contain `kind: "source_image"`, `path`, `width`,
and `height`. When the source is in the document directory or one of its
subdirectories, the path is stored relative to the document. External sources
are stored with an absolute path. Relative paths are resolved from the `.cimg`
directory. A missing source keeps the document available for relinking; the
replacement image must have the recorded dimensions so existing crop
coordinates remain valid.

A canvas base contains `kind: "canvas"`, `width`, `height`, and a
`background` color in `#AARRGGBB` notation. The canvas is reconstructed from
these values and does not need an external raster file. Transparent, white,
and custom backgrounds are supported. New canvas dimensions must be positive,
at most 32768 pixels per side, and at most 64 million pixels total.

```json
{
  "format": "creative-suite-image-document",
  "version": 3,
  "base": {
    "kind": "canvas",
    "width": 1920,
    "height": 1080,
    "background": "#00000000"
  },
  "operations": []
}
```

## Operation list

Operations are applied from first to last in base pixel coordinates. Each crop
uses the current image's pixel coordinates and must be a non-empty
rectangle within its bounds. Rotation uses `quarter_turns` of `1` for a right
turn or `-1` for a left turn. Horizontal and vertical flips mirror the current
image at that point in the operation list.

Version 3 source-image example:

```json
{
  "format": "creative-suite-image-document",
  "version": 3,
  "base": {
    "kind": "source_image",
    "path": "media/source.png",
    "width": 1920,
    "height": 1080
  },
  "operations": [
    { "kind": "crop", "x": 120, "y": 80, "width": 1500, "height": 900 },
    { "kind": "rotate", "quarter_turns": 1 },
    { "kind": "flip_horizontal" },
    {
      "kind": "paint_stroke",
      "color": "#FF1E90FF",
      "diameter": 12,
      "points": [{ "x": 120.5, "y": 80.0 }, { "x": 124.0, "y": 83.5 }]
    }
  ]
}
```

Operations are applied in order. A paint stroke's points use floating-point
coordinates in the current image bounds at that point in the operation list;
each point must be finite and satisfy `0 <= x < width` and `0 <= y < height`.
The color uses `#AARRGGBB`, the diameter is an integer from 1 through 512
pixels, and a stroke contains 1 through 100,000 points. Painting uses an
antialiased round brush. Later crop, rotation, or flip operations also transform
earlier strokes because they follow them in the operation list.

Each document has one linked raster base or one self-contained blank canvas.
Paint strokes are editable operations, not independent layers. Layers, masks,
color adjustments, and a persistent undo history are not represented. Save
writes to a temporary file and atomically replaces the destination when the
write succeeds. Export is a separate flattened raster file: PNG preserves
alpha; JPEG uses quality 95 and flattens transparent pixels over white.

Version 1 documents use the legacy `source` object and remain readable. Saving
any document writes version 3; older Image Editor versions do not understand
version 3 documents.

Recovery snapshots use a separate `creative-suite-image-recovery` JSON wrapper
with the document payload, intended `.cimg` destination, and a session identity
for unsaved canvases. They are stored under the user's local application data
directory and are not project assets. Recovery wrapper version 1 accepts
version 1, version 2, and version 3 document payloads.

Unsupported versions and invalid operation data are rejected without
replacing the currently open document.
