# Gradient Map adjustment layers

Implementation: 2026-10-20. Introduced in `.cimg` v20; current writer v29 reads v1-28. Readers accept v1-19 without
adjustments. Recovery envelope v1 and linked PNG publication are unchanged.
Application versions remain unchanged. Native visual and macOS/Linux acceptance
remain pending.

## Editing

Layers > New Adjustment Layer > Gradient Map creates a visible, Normal, 100%
adjustment with a white mask and black/white endpoints. No shortcut is added.
Creation requires a document and one selected item, including Background. Insert
above the selected sibling (above a selected root group), or above its entire
clipping chain. Group children keep their parent. An active area selection is
captured once as an editable mask, including holes and antialiasing; inverse
group geometry converts it to local mask coordinates.

The panel edits 2-32 opaque RGB points on a 0.1% position grid. Endpoint positions
are fixed at 0/100 and cannot be removed. Interior points cannot cross neighbors;
the minimum gap is 0.1%. Left/Right on the focused bar selects adjacent points,
including points too close to separate with the mouse. Add selects the midpoint of the largest gap (leftmost
on ties), rounded to the grid, with its interpolated color. Remove deletes only
an interior point. Colors interpolate linearly in the existing encoded RGB.

Drags/position edits/color dialogs each form one continuous history entry.
Cancelling the color dialog restores its complete previous parameters; unchanged
values preserve history/Redo. Selection/tab changes finish edits and canvas
transients are cancelled. Mask selection still exposes the owner's properties.
Names, visibility, masks, opacity, six blend modes and stack commands remain
editable. Content painting, outlines and individual object transforms are
unavailable on adjustments. Mask tools remain available; groups/canvas geometry
move the mask. Text/shapes still create their own raster layers.

## Composition and tools

An unclipped adjustment processes accumulated lower visible siblings; isolated
groups do not import an external backdrop. Root adjustments see finished lower
groups. Clipped adjustments process accumulated chain colors at their own position,
before base outline/opacity. Later clipped raster children follow normally.
Adjustments never serve as clipping bases. An unclipped adjustment is a barrier;
structurally orphaned runs are released atomically with the edit, including when
an adjustment is released from clipping. Undo restores all associations.

Unpremultiply accumulated RGB, calculate `t=.2126R+.7152G+.0722B`, and interpolate
the validated points. Round the mapped color to RGB8 before blending. With
`w=maskCoverage*opacity`, output straight RGB is
`(1-w)*original + w*Blend(original,mapped)`; re-premultiply with the **original
alpha byte**, which never changes. Source alpha does not multiply w again.
Transparent input remains transparent. Adjustments execute bottom-to-top.

The composite Eyedropper sees the finished result; editable selection/paint/fill
targets remain raw. Object transforms use the worker-composed preview. Adjustment
thumbnails show their gradient; group thumbnails show internal adjustment results.
Quick Export on an adjustment is disabled and explained because it owns no pixels.
Full/group export includes internal adjustments; isolated raster Quick Export
excludes external adjustment colors and retains existing clipping matte rules.
The exporter also refuses a programmatic adjustment-only export before writing.

## Boundaries and storage

`ImageLayerData::adjustment` is an optional shared typed adjustment payload; absence
retains legacy raster behavior. Session creation/set/begin/end operations own
history. Content-editability and mask-editability are separate capabilities.
`ImageLayerMaskData::initial_selection` optionally seeds a black mask filled white
inside the captured path, before all existing mask operations. No selection means
the existing white seed. Paths follow canvas offsets and participate in resource,
thumbnail, raster and history dependencies.

The Qt-independent `effects/gradient_map.h` owns validated normalized stops and
RGB interpolation. It does not add an effect to Video/Motion UI registries.
Reference/export/CPU preview share the Image alpha-preserving adapter. Clipping
chains reuse available raw member/mask rasters. The shared OpenGL raster record
adds an optional trailing adjustment; native root/group draws copy the accumulated
backdrop solely on GPU and evaluate the same stops in the raster shader. No
readback is used to apply the adjustment. Default absent fields retain Normal
rendering. Documents containing adjustments accumulate in premultiplied RGBA8;
non-Normal group opacity is quantized before the external blend to match CPU
rounding before subsequent adjustments. Content plus adjustment in one raster
record is invalid.

CPU raster/thumbnail storage stays within 64 MiB and worker GPU resources within
256 MiB. Backdrop copies share that budget; parameter edits do not upload unchanged
source rasters. Cancellation publishes no incomplete result. Capacity recovery is
informational and technical failures are logged through the existing controller.

v20 writes layer `kind: "adjustment"` and `adjustment: {type: "gradient_map",
stops: [{position, color}]}`; positions are normalized and colors are `#RRGGBB`.
Adjustments cannot be Background, carry raster operations or have an outline.
Masks optionally write `initial_selection: {fill_rule, path}` using existing path
elements and Qt fill-rule identifiers. Reject invalid point counts/order/grid,
unknown types, incompatible properties and these fields before v20. Failed Open
preserves the existing session. Hub preserves both payloads while rebasing media,
accepts Image through v29 and rejects v30. Recovery stays envelope v1.

## Verification

- `creative-suite-image-editor-gradient-map`: independent numeric references,
  transparency/coverage/modes, masks, scope, history/no-ops/cancel, chain barriers,
  caches, export rejection/equivalence, v20/invalid Open/recovery and CPU 1080p.
- `creative-suite-image-editor-gradient-map-ui` and full UI: creation, selected
  mask capture, point controls/history/color cancellation, tool restrictions,
  multi-selection and Background, linked publication/source preservation.
- Interactive/native GPU: CPU/OpenGL parity, clipping/group transforms, point/color
  edits with zero source uploads/rasterization, 1080p resources and constrained CPU
  recovery. Shared effects/composition and Video/Motion gates protect consumers.
- Hub backup format/manager: actual Image reader, v1-21 round trips, gradient points
  and initial masks through relocation/restoration; future v22 refusal.

Windows Release evidence (2026-10-20): 50 distinct scoped CTest gates passed:
32 Image, four shared effects/composition, eight Video, four Motion and two Hub.
The native raster run passed all 17 QtTest cases with at most two levels per RGB
channel and exact alpha for Gradient Map scenes. The initial full Image run
exposed a misplaced shader line; the two affected native gates and shared OpenGL
gate passed after its correction. The other 30 Image gates passed unchanged.

The [1080p profile](../performance/gradient-map-windows-2026-10-20.json) records
CPU cold/warm 49/41 ms, GPU cold/warm including readback 66/7 ms, and point/color
edits 5/4 ms with zero source uploads or raster reconstruction. Retained CPU cache
was 49,766,400 bytes; warm GPU textures were 82,944,000 bytes. A separate 64 MiB
capacity test recovered through CPU and matched the reference. These are single
local observations, not general performance guarantees.

Canonical Image Release/Debug, Hub Debug and affected Video/Motion Release builds
completed without installers; exact paths and modification times are in the
profile. Native visual checks and macOS/Linux builds/runtime acceptance remain
**pending**; see [manual checks](../validation/MANUAL_VALIDATION.md#gradient-map-adjustments).

The v21 Brightness/Contrast type shares this stack/mask contract; see
[second adjustment](BRIGHTNESS_CONTRAST.md). Historical v20 evidence above is unchanged.
