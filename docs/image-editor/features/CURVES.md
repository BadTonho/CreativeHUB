# Curves adjustment layers

## Editing contract

Layers > New Adjustment Layer > Curves creates the sixth adjustment kind. The
channel selector exposes the RGB master curve and independent Red, Green and
Blue curves. The master curve maps all three encoded-sRGB channels first; the
selected channel curve then maps only its matching output channel. All four
curves default to the identity diagonal, which preserves the original bytes in
Normal mode.

The selected curve permits 2–16 integer points in [0,255]. The first and last
inputs remain 0 and 255; their outputs can change. Interior inputs are strictly
increasing and outputs may reverse direction. Click the graph to add a point,
drag a point, use the Input/Output numeric fields, Add Point (largest available
gap), Remove Point or Reset. These controls affect only the selected curve.
Input is disabled for endpoints. A drag or continuous numeric edit is one Undo;
each button action is atomic. No-op edits preserve Redo. Layer or tab changes
finish an edit.

The graph overlays a combined, 256-bin input RGB histogram. Each nontransparent
sample contributes once to each channel, up to one million regularly spaced
pixels. It captures the actual root, group or clipping-chain input before the
adjustment and its mask. The Image Editor calculates this outside the UI thread,
cancels superseded work and discards stale results. Changes to Curves points do
not recalculate it. The histogram is not persisted or used for color
processing.

## Processing and ownership

`effects::CurvesParameters` owns one master point list and three per-channel
point lists. The Qt-free shared evaluator validates all four and uses
shape-preserving piecewise cubic Hermite interpolation (PCHIP), with zero
tangents at reversals and limited endpoint tangents. A two-point curve is
linear. Every segment stays within its two output values. The CPU builds a
256-byte lookup table for each effective output channel, composing the master
table before that channel's table. The OpenGL raster shader receives those
same tables in 192 `vec4` uniforms. Each straight encoded-sRGB channel indexes
its table at the nearest byte. The existing blend and mask path retains
backdrop alpha and content textures; point edits upload uniforms, not content.
Video/Motion effect registries and document formats do not change.

## Format and compatibility

The `.cimg` v27 writer reads v1–26 and preserves the Curves v26 payload. A Curves adjustment stores the RGB master
in `points` and all individual curves in `channels`:

```json
{
  "type": "curves",
  "points": [{"input": 0, "output": 0}, {"input": 255, "output": 255}],
  "channels": {
    "red": [{"input": 0, "output": 0}, {"input": 255, "output": 255}],
    "green": [{"input": 0, "output": 0}, {"input": 255, "output": 255}],
    "blue": [{"input": 0, "output": 0}, {"input": 255, "output": 255}]
  }
}
```

Each point has exactly two required integer fields in [0,255]. Every curve
requires 2–16 points with strictly increasing input, first input 0 and last
input 255; output may reverse. Version 25 `points` migrate to the master curve,
with the three individual curves initialized to identity. Reject Curves before
v25, missing or extra channel/adjustment/point fields, invalid points, and
incompatible Background/content/outline data. Failed Open preserves the active
document. Recovery envelope v1, application version and linked PNG protocol
remain unchanged. Earlier binaries cannot open v26 Curves documents; v26
binaries reject v27 documents and there is no downgrade writer. Hub accepts
Image v1–29, rejects v30, and preserves all four curves,
masks and media paths in backup and relocated restore.

## Regression and acceptance

`creative-suite-image-editor-curves` covers mapping order, bounds, neutral
bytes, alpha, masks, modes, histogram input, history, mixed stacks, v25
migration, v27/recovery/export and cache reuse.
`creative-suite-image-editor-curves-ui` covers channel selection, independent
editing, point controls, selected-channel Reset, history, persistence and
linked PNG publication. Native `gpuCurves` compares CPU/OpenGL pixels,
per-channel edits, content uploads, groups, clipping, cancellation and resource
limits. Hub backup/restore and Video linked-image tests cover their boundaries.
Record execution results in the roadmap. The [manual checklist](../validation/MANUAL_VALIDATION.md#global-curves-adjustments)
and release/platform acceptance remain pending for the owner.
