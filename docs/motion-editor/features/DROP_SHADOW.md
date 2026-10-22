# Animated Drop Shadow

Status: implementation with automated coverage. Manual visual, platform, native
Viewer presentation and benchmark acceptance remain pending.

## Authoring

Add **Drop Shadow** in the Inspector's ordered Effects stack. Images, videos,
text, primitive shapes and Bezier shapes accept it; Nulls do not. Multiple
instances are permitted. Disable retains parameters, mask and animation.

| Parameter | Default | Range |
| --- | --- | --- |
| Static RGB color | Black | Three 8-bit components, no color alpha |
| Opacity | 50% | 0–100% |
| Angle | 45 degrees | -1e9–1e9 degrees |
| Distance | 10 px | 0–16384 px |
| Softness | 10 px | 0–100 px |

Zero degrees points right, 90 degrees down; positive angles turn clockwise.
Full turns remain in bases and keys; only trigonometric evaluation reduces them.
Distance and softness use content pixels before media fitting, following the
existing Gaussian Blur. They then follow the layer's complete affine hierarchy,
including unequal scale, shear, Anchor and binding compensation.

Opacity, angle, distance and softness use layer-local scalar animation. Six
decimals affect presentation only. With no keys a field edits its base; at a key
it edits that key; between keys it is read-only. Key buttons insert the evaluated
value or remove the current key. Timeline/Graph Editor provide independent key
selection, moves/collision rejection and Linear/Cubic Bezier easing. Color is
static; cancelling its RGB picker makes no edit.

Edits finish previous edits and cancel transient Viewer/Pen/timeline gestures
before applying the captured choice. A completed field edit has one history
entry, as do color, activation, add/remove and reorder. No-ops and collisions
preserve Redo. Export/audio jobs block editing. There are no new shortcuts.
Glow, inner shadows, spread, shadow-only output and animated color are outside
this delivery. Transparent video export is documented separately in the
[Video export guide](VIDEO_EXPORT.md).

## Pixels, geometry and lifetime

Each shadow consumes the alpha at its ordered stage, after preceding effects
and the content mask. It blurs alpha with the shared Gaussian kernel, translates
it using bilinear sampling, applies color and shadow opacity, and composites the
original source over that shadow. For straight source RGB/alpha `Cs, As` and
shadow RGB/alpha `Ch, Ah`, the result is:

`Ao = As + Ah*(1-As)` and `Po = Cs*As + Ch*Ah*(1-As)`.

Return straight RGB `Po/Ao` when alpha is nonzero. No color conversion is added.
Layer opacity is applied once to the complete result at Transform; blend modes
and track mattes remain later stages. A source evaluated for a matte includes
its effects and shadow. The ordered stack can cast a shadow of a previous
shadow, or recolor/blur a shadow with later effects.

Motion owns expanded-stage origin, logical mask reference and original media
fit. Padding never changes content size, Position, Anchor, pivot or descendant
world matrices. The complete original Bezier path still defines its source
raster, including when Trim Paths makes the visible stroke empty or partial.
The length index and unchanged text/shape/media sources remain reusable.

Expansion includes translation, bilinear support and all three rounded-radius
box passes of the existing Gaussian kernel. Blur stages following a shadow get
the necessary transparent halo. Source/effect masks retain their original
logical coordinate system as the raster origin changes. A shadow's effect mask
mixes its aligned original and processed images with the existing premultiplied
MaskedBlend contract. Transform masks remain in composition coordinates. View
Mask stays independent of the shadow.

Layers without active nonzero-opacity shadows retain the exact legacy path.
Disabled/zero-opacity shadows preserve input pixels and limits. Intermediate
origin and fitting state are transient, never serialized.

### Shared processing boundary

`libs/effects` owns Qt-free `DropShadowParameters`, `DropShadowBounds`,
`dropShadowBounds` and `applyDropShadow`. Input/output are straight RGBA8.
The bounds report where source pixel zero lands in the expanded image. Input
storage, values, finite geometry and bounds are validated before allocation;
invalid input/resource limits throw, cancellation returns an empty optional.
The synchronous executor/cancellation contract is reused from Gaussian Blur;
no input, executor or callback is retained.

The shared GPU image contract adds Drop Shadow. Bilinear alpha and source-over
use the same operation order and precise byte-to-scalar conversion as CPU,
retaining exact alpha parity without increasing existing RGB tolerances. OpenGL pads with an affine
image operation, reuses Gaussian Blur, then combines original/blurred alpha in
distinct input/output textures. No intermediate readback or framebuffer feedback
is permitted. Vulkan explicitly returns Unsupported for this new operation.
The CPU implementation remains available through existing Motion recovery.
Video/Image do not gain an effect selector or document fields.

Expanded images remain capped at 16384 pixels per dimension and 64 MiB each;
the preparation working set is capped at 256 MiB, accounting for prepared
layers already retained in the same frame before allocating additional buffers. Existing GPU pool/scratch
limits and leases also apply. Resource exhaustion may fall back on CPU, but
must not loop forever on Busy. Cancellation discards partial work. Failures
are logged with operation, layer, effect index, composition frame and backend;
expected cancellation is not logged as an error. Shadow timings/fallbacks have
their own diagnostic category rather than being counted as Gaussian Blur.

## Native format and interchange

Native `.motion` v17 stores an ordered `effects` entry with `type: drop_shadow`,
`enabled`, `color_rgb` (exactly three integer bytes), `opacity_percent`,
`angle_degrees`, `distance_pixels`, `softness_pixels`, optional `mask` and an
optional `keyframes` object. Curve names match the four numeric field names and
use the existing scalar key representation. Color and activation have no curves.

Versions 1–16 contain no Drop Shadow; any such entry declared before v17 is
invalid. Reject invalid types/ranges/curves, unknown curve names, dangling masks,
Null effects and excessive total keys before replacing an open document.
All editing, replacement and persistence paths count shadow keys in the shared
two-million document budget. Recovery retains wrapper version 1 with native v17.

Hub accepts Motion through v18 and rejects v19, preserving complete effect data
while rebasing existing media/link paths. Glow uses native v18; this Drop Shadow
record remains native v17. `.cscomp` stays v3 and refuses any Drop Shadow
instance, including disabled effects and hidden/inactive layers, identifying
the layer name/ID and effect position. Application versions are unchanged.

## Verification and pending acceptance

[Drop Shadow tests](../../../apps/motion-editor/tests/drop_shadow_test.cpp) are
[registered](../../../apps/motion-editor/tests/CMakeLists.txt) as
`creative-suite-motion-editor-drop-shadow` and `...-drop-shadow-hidpi`.
They cover numeric references, alpha/bilinear/source-over, bounded geometry,
animation/editing/history, migration/recovery/rejection, ordered effects,
independent padded-mask references, affine geometry, mattes/blending, Trim Paths,
video/raster cache reuse, forced recovery with a current gesture candidate and encoded
preview/export parity. The native run accepts `--require-gpu` and fails if native
composition cannot be exercised; offscreen results do not qualify native GPU.
Hub's producer/reader backup gate reopens vault and restored v17 payloads.

Manual checks pending:

1. At native 100%/200%, add/reorder/mask multiple shadows, animate all four
   channels, change RGB, disable/enable and Undo/Redo. Check readonly fields
   between keys and no history for cancelled color/no-ops/collisions.
2. Animate a path and Trim Paths under an external Anchor, affine parenting,
   unequal scale/shear and hidden/offline ancestors. Confirm no clipping or
   content/pivot/descendant movement caused only by padding.
3. Compare image/video fitting, text and shapes through source/effect/Transform
   masks, blend modes and matte chains in preview, playback and export. Exercise
   Pen and transform drafts, cancellation, late deliveries and recovery.
4. Save/reopen/autosave/recover, restore a Hub backup after relocating media,
   and verify explicit interchange refusal for disabled/hidden shadows.
5. Record native Windows/macOS/Linux and Viewer GPU presentation separately.
   Retain the known OpenGL presentation failure separately from composition
   parity and fallback. Benchmark small/medium/heavy scenes later.

No installer, commit or publication is part of this work.

## Windows Release execution, 2026-10-20

The canonical Visual Studio Release build completed all 53 explicit affected
application/test targets. The selected CTest gate passed **59/59** in 36.54 s,
including the Drop Shadow 100%/200% tests, transform/Pen/Trim/matte/blend/history,
persistence/recovery, preview/export, translations, both Hub backup gates and
shared composition/effects plus Video/Image consumer regressions.

A separate Windows native `--require-gpu` run passed with
`Drop Shadow native=1 fallback=0`. It checked RGBA with exact alpha and the
existing RGB tolerance, prepared affine/masked layers, texture delivery,
asynchronous readback, cancellation and resource release. Its separately forced
recovery case reported `native=0 cpu=1` and matched the current Position gesture
candidate. That recovery result is not counted as native GPU approval. The
previously recorded OpenGL Viewer presentation failure remains separate and
unresolved; this run establishes composition parity, not presentation acceptance.

Verified executable modification times, local UTC-03:00:

| Application | Canonical executable | Modification time |
| --- | --- | --- |
| Motion | `build/apps/motion-editor/Release/creative-suite-motion-editor.exe` | 2026-10-20 10:41:54 |
| Hub | `build/apps/hub/Release/creative-suite-hub.exe` | 2026-10-20 10:22:44 |
| Video | `build/apps/video-editor/Release/creative-suite-video-editor.exe` | 2026-10-20 10:41:47 |
| Image | `build/apps/image-editor/Release/creative-suite-image-editor.exe` | 2026-10-20 10:41:50 |

The Motion executable used directly is
`C:\Users\Admin\Desktop\ProjetosCode\AdobeShoppee\build\apps\motion-editor\Release\creative-suite-motion-editor.exe`.
The diff, new sources, UTF-8 documentation, local links and translation catalog
manifest were reviewed. Manual visual/platform checks, native Viewer presentation
and benchmarks listed above remain pending. Application versions are unchanged.
