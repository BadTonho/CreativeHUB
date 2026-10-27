# Glow

Glow is an ordered per-layer effect for Image, Video, Text and Shape layers.
Null control layers do not accept effects. Repeated Glow instances are allowed
and keep independent parameters in the layer's effect order.

## Controls

Each effect instance exposes one static mode, one static RGB color, opacity and
radius:

| Control | Range | Default | Animation |
| --- | --- | --- | --- |
| Mode | Outer, Inner, Both | Outer | Static |
| Color | RGB bytes, 0–255 per channel | White | Static |
| Opacity | 0–100% | 50% | Keyframes |
| Radius | 0–100 px | 10 px | Keyframes |

Opacity and radius use the existing layer-local keyframes and Graph Editor
curves. The Timeline exposes separate Glow Opacity and Glow Radius tracks.
Mode and color edits are ordinary Undo/Redo operations. Both uses the same RGB,
opacity and radius for its interior and exterior parts.

## Processing

The shared effects library processes straight RGBA8 pixels before the layer's
transform. It blurs source alpha with the shared Gaussian processor, using
bounded scratch storage and cancellation checks. Outer colors the blurred alpha
behind the source and expands the raster by three blur radii plus one pixel on
each side. Inner tints pixels near the inside alpha boundary and preserves the
source raster bounds and alpha coverage. Both combines the two results and
expands like Outer. Transparent source pixels do not create a halo by themselves.
Outer uses source-over composition: an opaque source covers the halo, whereas a
partially transparent source allows it to contribute to both color and alpha.

Glow remains at its position in the ordered effect stack. Its existing effect
mask limits the processed result while retaining the unprocessed pixels outside
the mask. Source masks run before the stack; Transform masks, mattes and layer
transforms retain their existing pipeline order. Repeated outer glows expand
the current raster in sequence. Each output and blur scratch image is limited to
64 MiB and 16,384 pixels per dimension, in addition to the Motion frame
preparation budget. Invalid bounds and processing failures are logged; cancelled
work is discarded.

The CPU renderer implements all modes. The experimental OpenGL image path uses
the same blur and compositing contract and records Glow timing, fallback and
failure metrics separately. If OpenGL cannot prepare or process Glow, Motion
recovers through CPU. Vulkan reports Glow as unsupported so the caller selects
CPU recovery.

## Native files and interchange

The `.motion` writer emits schema v18 and reads v1–v18. Version 18 adds an
ordered `glow` effect record with `mode`, `color_rgb`, `opacity_percent`,
`radius_pixels`, optional `mask`, and optional opacity/radius keyframes. Older
documents migrate with no Glow. The recovery wrapper remains version 1, and Hub
backs up v18 while rejecting future v19. See the [native format](../architecture/FORMAT.md#glow-v18).

`.cscomp` v3 cannot represent Glow. Export refuses the layer and effect with an
explicit diagnostic and does not flatten or omit it. Glow is outside the
interchange contract.

## Verification and pending acceptance

Regression sources cover shared CPU pixels, alpha, all modes, bounds,
cancellation and resource limits; Motion ordered rendering, masks, animation,
Inspector/history, preview/export pixels, persistence and interchange refusal;
OpenGL parity and Vulkan refusal; and Hub v18 backup/restore and v19 rejection.
On 2026-10-26, Windows Release `creative-suite-effects`,
`creative-suite-composition-opengl-glow`, `creative-suite-motion-editor-drop-shadow`
and `creative-suite-motion-editor-preview` passed. The focused OpenGL test used
the NVIDIA GTX 1660 SUPER and checks automatic precision parity and core-only
precision fallback. The complete `creative-suite-composition-opengl` suite failed
earlier in its separate R8 matte budget check, before reaching Glow. The Motion
export test compiled after correcting its mutable fixture, but failed a linked
image pixel check before reaching its Glow export assertion. Neither full gate
is recorded as passing. Manual visual checks, native Viewer presentation, other
platform/driver acceptance and representative performance measurements remain
pending.
