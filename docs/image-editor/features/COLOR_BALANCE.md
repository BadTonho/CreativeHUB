# Color Balance adjustment layers

Color Balance adjusts lower siblings non-destructively through the existing
adjustment-layer stack, masks, opacity, clipping, groups and blend modes. The
panel selects Shadows, Midtones or Highlights, initially Midtones. Each range
has Cyan/Red, Magenta/Green and Yellow/Blue integer controls from -100 to 100;
positive values favor red, green and blue. Reset affects the selected range.
Preserve Lightness defaults to enabled and is independently undoable.

For straight encoded-sRGB, original HSL lightness `L=(min(RGB)+max(RGB))/2`.
Weights are `S=max(0,1-2L)`, `H=max(0,2L-1)` and `M=1-S-H`.
Each channel receives its weighted controls divided by 200 and is clamped to
[0,1]. If preservation is enabled, the adjusted HSL hue and saturation are
recombined with original `L`. Channels are rounded to bytes; alpha is unchanged.
Neutral controls bypass processing, independent of the preservation flag.

The `.cimg` v30 adjustment stores `type: color_balance`, `ranges`, an array of
three arrays of three integer values in shadow/midtone/highlight and RGB-axis
order, and `preserve_lightness`, a boolean. Versions 1–29 remain readable;
invalid or future documents are rejected. Hub backup accepts v30. Recovery
envelope and linked PNG publication remain unchanged.

Automated coverage and visual/platform acceptance must be recorded in the
[roadmap](../ROADMAP.md) and [manual checklist](../validation/MANUAL_VALIDATION.md).

Windows Release execution on 2026-10-26: model, persistence and recovery,
UI/history/linked publication, translation catalog, offscreen interactive,
Hub backup format and manager, Video linked-image and Motion UI consumer tests
passed. Native Windows `gpuColorBalance` passed CPU/OpenGL comparison with
masked pixels, both preservation modes, unchanged uploads and neutral Multiply.
The Release executable was built at
`build/apps/image-editor/Release/creative-suite-image-editor.exe`
(2,712,576 bytes, 2026-10-26 23:18:39 UTC-03:00). Owner visual and macOS/Linux
validation remain pending; no platform release acceptance is implied.
The prior full OpenGL suite's R8 matte budget failure and the Motion export
suite's linked-image assertion are tracked in the [Glow validation](../../motion-editor/features/GLOW.md#verification-and-pending-acceptance);
those full suites were not rerun for Color Balance and are not claimed as passing.
