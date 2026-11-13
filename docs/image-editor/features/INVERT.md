# Invert adjustment layers

## Editing and scope

Layers > New Adjustment Layer > Invert creates a visible, unclipped, Normal
adjustment at 100% opacity. It has no controls, so the properties panel shows no
editor, and no shortcut is added. Placement, selection-seeded mask capture,
masks, visibility, opacity, blend modes, clipping barriers and stack operations
follow the [Gradient Map scope rules](GRADIENT_MAP.md) and the
[Brightness/Contrast](BRIGHTNESS_CONTRAST.md) conventions. An adjustment cannot
own painted content, outlines, individual transforms or an isolated Quick
Export. Full/group export and linked PNG publication include it.

The adjustment thumbnail shows the inverted grayscale ramp.

## Shared processing

`effects::InvertParameters` is an empty member of `effects::AdjustmentParameters`.
For encoded, straight RGB channel `v` in [0,1], the result is
`round((1 - clamp(v)) * 255)` per channel. Alpha is never changed; mask coverage
times opacity sets intensity exactly as for the other adjustment types, then the
existing blend mode applies.

Invert has no neutral state: `identityAdjustment` is always false, so a visible
Invert is never skipped as a no-op. Applying it twice restores every byte.
OpenGL evaluates `1.0 - color` on the GPU-only backdrop copy as adjustment kind 8
and shares the existing byte quantization and blend path, so CPU and OpenGL agree
within the existing two-level tolerance with exact alpha.

## Format and compatibility

Introduced in v31; the current writer reads versions 1-30. Write
`kind: "adjustment"` with `adjustment: {type: "invert"}` and no other field.
Reject extra fields, the kind before v31, and any other malformed payload.
Invalid Open preserves the current document. Recovery envelope v1 and the linked
PNG protocol are unchanged.

Hub accepts Image v1-31, refuses v32 and retains Invert layers and masks through
backup, relocation and restoration, verified by the actual Image reader. Earlier
Image binaries reject v31. Application versions are unchanged.

## Verification and acceptance

Automated coverage is indexed in the
[Invert regression coverage](../ROADMAP.md#invert-regression-coverage).

Status as of this change:

- **Passed (Windows Release):** shared `creative-suite-effects` (Invert numeric
  contract), `creative-suite-composition` and `hub-backup-manager-test`
  (v31 accepted, v32 rejected).
- **Not yet built or run:** the Image Editor suites, including
  `creative-suite-image-editor-invert`, and `hub-backup-format-test` with the real
  Image reader. The development machine lacked the Qt Image Formats plugins
  (`qwebp`/`qtiff`) that the Image Editor configuration requires, so the Image
  Editor could not be configured. The GLSL change is also unverified at runtime.
- **Pending:** native visual checks and macOS/Linux builds; follow the
  [manual checklist](../validation/MANUAL_VALIDATION.md#invert-adjustments).
