# Image Editor Document Format

Status: **provisional version 2**. The `.cimg` extension is temporary until a
later format review. Version 2 adds self-contained blank-canvas documents;
version 1 source-image documents remain readable.

## Document contents

A `.cimg` file is UTF-8 JSON with these top-level fields:

| Field | Type | Meaning |
| --- | --- | --- |
| `format` | string | Must be `creative-suite-image-document`. |
| `version` | integer | Current version is `2`; version 1 remains readable. |
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
  "version": 2,
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

Version 2 source-image example:

```json
{
  "format": "creative-suite-image-document",
  "version": 2,
  "base": {
    "kind": "source_image",
    "path": "media/source.png",
    "width": 1920,
    "height": 1080
  },
  "operations": [
    { "kind": "crop", "x": 120, "y": 80, "width": 1500, "height": 900 },
    { "kind": "rotate", "quarter_turns": 1 },
    { "kind": "flip_horizontal" }
  ]
}
```

Each document has one linked raster base or one self-contained blank canvas.
Layers, masks, color edits, and a persistent undo history are not represented.
Save writes to a temporary file and atomically replaces the destination when
the write succeeds. Export is a separate flattened raster file: PNG preserves
alpha; JPEG uses quality 95 and flattens transparent pixels over white.

Version 1 documents use the legacy `source` object and remain readable. Saving
any document writes version 2.

Recovery snapshots use a separate `creative-suite-image-recovery` JSON wrapper
with the document payload, intended `.cimg` destination, and a session identity
for unsaved canvases. They are stored under the user's local application data
directory and are not project assets. Recovery wrapper version 1 accepts both
version 1 and version 2 document payloads.

Unsupported versions and invalid operation data are rejected without
replacing the currently open document.
