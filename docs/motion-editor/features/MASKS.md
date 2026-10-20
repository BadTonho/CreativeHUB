# Editable shape masks

Rectangle Mask and Ellipse Mask are edited in the Inspector's Masks tab. Choose
Content, Transformation, or a specific ordered effect, then create a mask or
select an existing mask by type and ID. Each destination accepts one mask.
Changing a shared definition updates all its users. Choosing None removes only
that association; its last association removes the unused definition in the
same history operation. Effect reordering keeps the mask with its effect.

Center X/Y default to 50% (range -200% to 300%), Width/Height to 50% (0% to
400%), Rotation to zero (-3600 to 3600 degrees), Soft Edge to zero (0% to 100%
of the shorter receiver dimension), and Invert to off. Zero dimensions produce
empty coverage; inversion then reveals all. Coordinates are relative to the
receiving stage, which can differ between source and project-sized transform.
Apply Settings commits one edit. Animated geometry edits insert/update a key
at the composition playhead; K adds/removes a key. Soft Edge and Invert are
static. Choose a property/key in the list to edit linear/Bezier easing in the
existing Graph Editor. Mask curves use composition frames, so moving a layer
does not move a shared mask's keys. There are no new shortcuts or Viewer handles.

View Mask displays opaque grayscale coverage for the selected destination,
including softness/inversion, without an edit or saved Viewer state. It can
show coverage even when the selected layer is inactive. Leaving that mask or
removing its association restores normal preview.

Content masks multiply alpha before effects. Effect masks mix that effect's
input and processed output; Transformation masks mix the untransformed and
transformed images in the project canvas. Mixing uses premultiplied alpha and
returns straight RGBA. Native color adjustment and Gaussian Blur retain their
ordered processing. Gaussian Blur is editable through composition interchange v3; it retains
its own ordered stage and mask reference.

The document owns mask IDs, associations and history. Preview/export snapshots
share immutable definitions; the renderer evaluates them at an explicit
composition frame. Shared composition owns the existing 2x2/signed-distance
coverage generator, capped to four cached entries/64 MiB per evaluation. It
never retains animated frame history. Existing CPU/GPU ApplyMask/MaskedBlend
operations and the blur kernel are reused; image intermediates remain on GPU
until presentation/export readback. Resource exhaustion recovers on CPU without
permanently disabling later GPU frames. Technical failures are logged before
feedback; cancellations and refused edits are ordinary control flow.

Motion schema 8 persists masks and destination references in saved documents,
autosave and recovery. Versions 1-7 retain their prior behavior. Independent
composition copies retain internal sharing without sharing mutable definitions
with the original. See [interchange](../../COMPOSITION_INTERCHANGE.md) for v2
conversion and [format](../architecture/FORMAT.md) for native persistence.

## Regression and pending acceptance

`creative-suite-mask-interchange` covers model edits/sharing, Inspector controls,
geometry, stage conversion, raster parity, curves, native persistence and
recovery. Existing Motion document/history/UI/preview/export suites and shared
composition/OpenGL consumers protect the surrounding behavior. Hub backup
fixtures reopen native v8 masks with the actual Motion reader.

Windows Debug evidence (2026-10-14): all 29 selected consumer suites passed,
with the final mask/Inspector rerun also passing. Synthetic Fusion/Motion RGBA
mask comparisons observed zero channel difference; native GPU comparisons
passed the maximum tolerance of two. Export clock, bounded-pool CPU recovery
and subsequent GPU reuse were exercised. Full environment/build evidence is
in the [interchange guide](../../COMPOSITION_INTERCHANGE.md).

Manual checks pending: use both File menus, edit a mask shared across two
layers/effects, seek/play, move a layer without shifting mask keys, edit Bezier
easing, toggle View Mask, remove each association, undo/redo, save/recover, and
transfer back. Compare rotated, transparent media with source and transform
masks on native drivers. Windows automated coverage does not establish native
macOS/Linux acceptance. Pen/polygon masks and direct path editing were outside that initial delivery;
Bezier authoring is now documented below. Combinations, tracking, Merge/Output
masks and animated Soft Edge/Invert remain planned.


Layer hierarchy resolves inherited geometry and the animated [Anchor Point](ANCHOR_POINT.md) before this existing Transform
stage. Transform masks keep their composition-space coverage and continue to
limit the complete resolved transform. Parent masks/effects are not copied to
children; Null controls cannot own masks. See [Layer hierarchy](HIERARCHY.md).

Independent [Scale X/Y](SCALE_AXES.md) extends the same Transform stage with a
source-pixel affine override, retaining inherited shear. CPU and precise OpenGL
apply it once; Vulkan refuses it for fallback. Source/effect coverage remains in
its prior space and Transform coverage remains in composition pixels; masks do
not inherit a parent's masks or link state. `creative-suite-motion-editor-scale`
compares animated affine effects and all mask stages with independent reference
sampling and encoded export frames. Native visual/GPU acceptance remains pending.

Bezier masks and direct path editing are now implemented in native v15; see [Pen paths](BEZIER_PATHS.md). Existing primitive-mask stages/coordinates retain their exact default path. Path masks have complete-geometry keys in composition time and preserve sharing by ID. Combinations and tracking remain planned.

Trim Paths is a Bezier-shape stroke operator, not a mask operator. Existing
source/effect masks retain full-path logical reference dimensions while its
stroke changes. Transformation masks remain in composition coordinates; matte
consumers see the trimmed layer's normal prepared pixels. See [Trim Paths](TRIM_PATHS.md).
