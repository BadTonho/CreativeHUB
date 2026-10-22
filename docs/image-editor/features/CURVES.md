# Global Curves adjustment layers

## Editing contract

Layers > New Adjustment Layer > Curves creates the sixth adjustment kind. One
curve maps encoded-sRGB red, green and blue channels independently.
The default diagonal preserves the original bytes in Normal mode. The layer
uses the existing adjustment placement, group, clipping, selection-seeded mask,
opacity, visibility, six blend modes, history, thumbnails, export and linked PNG
contracts. It has no painted content, outline, transform or isolated Quick
Export. No shortcut or per-channel control is added.

The graph permits 2–16 integer points in [0,255]. The first and last inputs
remain 0 and 255; their outputs can change. Interior inputs are strictly
increasing and outputs may reverse direction. Click the graph to add a point,
drag a point, use the Input/Output numeric fields, Add Point (largest available
gap), Remove Point or Reset. Input is disabled for endpoints. A drag or
continuous numeric edit is one Undo; each button action is atomic. No-op edits
preserve Redo. Layer or tab changes finish an edit.

The graph overlays a combined, 256-bin input RGB histogram. Each nontransparent
sample contributes once to each channel, up to one million regularly spaced
pixels. It captures the actual root, group or clipping-chain input before the
curve and its mask. The Image Editor calculates this outside the UI thread,
cancels superseded work and discards stale results. Changes only to Curves
points do not recalculate it. The histogram is not persisted or used for color
processing.

## Processing and ownership

`effects::CurvesParameters` contains ordered integer `{input, output}` points.
The Qt-free shared evaluator uses shape-preserving piecewise cubic Hermite
interpolation (PCHIP), with zero tangents at reversals and limited endpoint
tangents. A two-point curve is linear. Every segment stays within its two
output values. A 256-byte lookup table rounds and clamps the result. The CPU
builds it once per adjustment operation; the OpenGL raster shader receives the
same table as 64 `vec4` uniforms. Each straight RGB channel indexes the table
at its nearest encoded-sRGB byte. The existing blend and mask path retains
backdrop alpha and content textures; point edits upload uniforms, not content.
Video/Motion effect registries and document formats do not change.

## Format and compatibility

The `.cimg` v25 writer reads v1–24. A Curves adjustment stores
`{"type":"curves","points":[{"input":0,"output":10},{"input":128,"output":180},{"input":255,"output":245}]}`.
Both fields are required integers in [0,255]. Reject this kind before v25,
invalid count/order/endpoints, extra point or adjustment fields and incompatible
Background/content/outline data. Failed Open preserves the active document.
Recovery envelope v1, application version and linked PNG protocol remain
unchanged. Earlier binaries cannot open v25; there is no downgrade writer. Hub
accepts Image v1–25, rejects v26, and preserves points, masks and media paths in
backup and relocated restore.

## Regression and acceptance

`creative-suite-image-editor-curves` covers mapping, bounds, neutral bytes,
alpha, masks, modes, histogram input, history, mixed stacks, v25/recovery/export
and cache reuse. `creative-suite-image-editor-curves-ui` covers creation,
selection masks, point controls, Reset and editing history. Native `gpuCurves`
compares CPU/OpenGL pixels, content uploads, groups, clipping, cancellation and
resource limits. Hub and linked-image consumer tests cover their boundaries.
Record execution results in the roadmap. The [manual checklist](../validation/MANUAL_VALIDATION.md#global-curves-adjustments)
and release/platform acceptance remain pending for the owner.
