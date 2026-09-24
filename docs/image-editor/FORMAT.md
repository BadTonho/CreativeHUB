# Image Editor Document Format

Status: **provisional version 1**. The `.cimg` extension is temporary until a
later format review.

## Document contents

A `.cimg` file is UTF-8 JSON with these top-level fields:

| Field | Type | Meaning |
| --- | --- | --- |
| `format` | string | Must be `creative-suite-image-document`. |
| `version` | integer | Current version is `1`. |
| `source` | object | Original image path and its decoded width and height. |
| `operations` | array | Ordered, non-destructive edits. |

The source object contains `path`, `width`, and `height`. When the source is in
the document directory or one of its subdirectories, the path is stored
relative to the document. External sources are stored with an absolute path.
Relative paths are resolved from the `.cimg` directory. A missing source keeps
the document available for relinking; the replacement image must have the
recorded dimensions so existing crop coordinates remain valid.

## Operation list

Operations are applied from first to last in decoded source pixel coordinates.
Each crop uses the current image's pixel coordinates and must be a non-empty
rectangle within its bounds. Rotation uses `quarter_turns` of `1` for a right
turn or `-1` for a left turn. Horizontal and vertical flips mirror the current
image at that point in the operation list.

```json
{
  "format": "creative-suite-image-document",
  "version": 1,
  "source": {
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

Only one raster source is supported. Layers, masks, color edits, and a
persistent undo history are not represented. Save writes to a temporary file
and atomically replaces the destination when the write succeeds. Export is a
separate flattened raster file: PNG preserves alpha; JPEG uses quality 95 and
flattens transparent pixels over white.

Recovery snapshots use a separate `creative-suite-image-recovery` JSON wrapper
with the document payload and the intended `.cimg` destination. They are
stored under the user's local application data directory and are not project
assets.

Unsupported versions and invalid operation data are rejected without
replacing the currently open document.
