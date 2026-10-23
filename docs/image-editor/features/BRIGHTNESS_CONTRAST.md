# Brightness/Contrast adjustment layers

## Editing and scope

Layers > New Adjustment Layer > Brightness/Contrast creates a visible, unclipped,
Normal adjustment at 100% opacity with both controls at zero. No shortcut is added.
One selected layer, Background or group is required. Insert above the selected
sibling or entire clipping chain; group children retain their parent. Capture an
active area selection as the initial independent mask, otherwise use white.

The panel offers integer sliders and numeric fields from -100 through +100.
Each drag or continuous numeric edit is one Undo. Reset returns both values to
zero atomically. Equal values preserve history and Redo. Layer/tab switches end
edits and cancel canvas transients. Mask selection keeps owner properties visible.
Masks, visibility, opacity, six modes and stack operations remain editable.
Adjustments cannot own painted content, outlines, individual transforms or an
isolated Quick Export. Full/group export and linked PNG publication include them.
The adjustment thumbnail has a half-light/half-dark symbol; groups show results.

## Shared processing

`effects::AdjustmentParameters` is a typed variant of Gradient Map,
Brightness/Contrast, Exposure / Offset / Gamma and Hue/Saturation parameters. Image owns stack scope and history. Existing
Gradient Map session APIs remain; typed creation/set operations are added for
the second type and cannot change a layer's type. No new Video/Motion UI effect
is registered. The shared color-adjustment RGB evaluator retains existing
brightness/contrast/saturation rounding and consumer behavior.

For encoded, straight normalized RGB channel v, calculate
`clamp((v + brightness/100 - .5) * (1 + contrast/100) + .5)`.
Round the adjusted RGB to bytes before the existing blend/intensity operation.
Mask coverage times opacity sets intensity; source alpha does not multiply it
again. Preserve each original alpha byte. Zero/zero in Normal preserves pixels
exactly; other modes still blend the original and adjusted colors.

Reuse the [adjustment scope and clipping rules](GRADIENT_MAP.md): lower siblings,
isolated groups, clipped modifiers before base outline/opacity, and adjustment
barriers that cannot become clipping bases. Consecutive types execute in order.
CPU reference, preview, sampling, thumbnails and exports use the same adapter.
OpenGL evaluates the new type on the GPU-only backdrop copy with exact alpha;
no readback executes the effect. Preserve the existing Gradient Map shader path.

Parameter changes reuse raw content/masks and trigger dependent chains/groups.
CPU caches remain bounded to 64 MiB and GPU resources to 256 MiB. Cancellation
publishes no incomplete frame; capacity recovery remains distinct from logged
technical failures. Raw content tools and isolated raster exports keep targets.

## Format and compatibility

Introduced in v21; the current v27 writer reads versions 1-26, including v20
Gradient Map layers. Write
`kind: "adjustment"`, with `adjustment: {type: "brightness_contrast", brightness,
contrast}`; both values are required integers in [-100,100]. The optional typed
payload remains absent on ordinary layers. Reject the new type before v21,
unknown types, invalid values, Background adjustments and incompatible raster
content/outline. Invalid Open preserves the current document. Selection mask
encoding and recovery envelope v1 are unchanged.

Hub accepts Image v1-28, refuses v29 and retains all six adjustment payloads/masks
through backup, relocation and restoration, verified by the actual Image reader.
Application versions and linked PNG protocol are unchanged.

## Verification and acceptance

Core tests cover independent numeric references, identity, all six modes,
partial alpha/coverage, history/no-ops, mixed types, clipped barriers, transforms,
exports, cache reuse, v21 invalid loads and recovery. UI tests exercise sliders,
continuous numeric edits, atomic Reset, masks/tools, tabs and PNG publication.
Native raster tests compare CPU/OpenGL within two RGB levels and exact alpha,
including groups/chains, parameter edits without uploads, 1080p and constrained
capacity recovery. Shared effects/composition and affected Video/Motion and Hub
regressions protect existing contracts.

Windows Release evidence (2026-10-20): 52 distinct scoped CTest gates passed:
34 Image, four shared effects/composition, eight Video, four Motion and two Hub.
The native raster run passed 18 QtTest cases with at most two RGB levels and exact
adjustment alpha. The full run passed 51/52: Video render/export reported an output
replacement failure, then passed in isolation without code changes. This
intermittent failure is recorded and needs investigation if it recurs.

The [1080p profile](../performance/brightness-contrast-windows-2026-10-20.json)
records CPU cold/warm 51/38 ms and brightness/contrast edits 37/38 ms. GPU first
1080p/warm frame including readback measured 49/3 ms; each parameter edit measured
3 ms, zero uploads and no source reconstruction. Retained CPU cache was 49,766,400
bytes, warm GPU textures 82,944,000 bytes. Forced 64 MiB GPU capacity recovered
through CPU in 41 ms with exact reference equality. These are local observations;
see the profile's warm-context and process-memory limitations.

Canonical Image Release/Debug, Hub Debug and affected Video/Motion Release builds
completed without installers or application version changes. Exact executable
paths/modification times are in the profile. Visual checks and macOS/Linux
builds/native runtime acceptance remain **pending**; follow the
[manual checklist](../validation/MANUAL_VALIDATION.md#brightnesscontrast-adjustments).
Exposure / Offset / Gamma is implemented; see [contract](EXPOSURE_OFFSET_GAMMA.md).
Global Hue/Saturation is implemented; see [contract](HUE_SATURATION.md).
Levels is implemented; see [contract](LEVELS.md).
