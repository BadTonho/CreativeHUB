# Shared Visual Effects Contract

Status: provisional.

`libs/effects` provides a shared CPU visual-processing API used by the
Video Editor and Motion Studio. The `creative-suite::effects` target depends
on the shared RGBA frame type and standard C++ only; it does not depend on Qt
Widgets or own application UI. Each application keeps its own effect model,
document format, and user interface.

## Built-in filters

| Stable ID | Parameter | Range | Default |
| --- | --- | --- | --- |
| `video.grayscale` | Amount | 0–100% | 100% |
| `video.brightness` | Brightness | -100–100 | 0 |
| `video.contrast` | Contrast | 0–200% | 100% |
| `video.saturation` | Saturation | 0–200% | 100% |

Definitions are discoverable through `builtInEffects()` and `findDefinition()`.
Instances hold an ID, explicit parameter IDs/values, and an `enabled` flag that
defaults to true. A stack may contain repeated instances; evaluation follows
vector order while skipping disabled instances without changing their stored
position or parameters. Validation rejects unknown IDs, missing or duplicate
parameters, non-finite values, and values outside the definition's range.

## Processing

`applyStack()` mutates a valid RGBA frame in place. It processes only the
visible RGB bytes for each pixel, respects row stride, preserves alpha and row
padding, and returns false for malformed frames or invalid stacks. An empty
stack succeeds without changing pixels. Brightness offsets encoded channel
values; contrast scales them around 127.5; grayscale and saturation use
Rec. 709 luma coefficients. Each stage clamps to the 8-bit range before the
next filter runs.

The Video Editor submits each clip's original stack to the shared compositor.
Preview and offline export use `colorAdjustmentPasses()` to prepare the same
ordered GPU operations when experimental composition is selected. That helper
validates every instance, including disabled instances, and preserves rounding
after each enabled pass. CPU composition applies the original stack exactly once,
including GPU recovery. Disabled instances do not modify the frame;
active instances retain their relative order. Filter parameters and enabled
states are Timeline project data; caches and temporary preview state are not
added by this library. The Video Editor persists stacks under the `.csp`
version 18 clip schema described in [PROJECT.md](PROJECT.md). Version 17
stacks migrate with every instance enabled.

`applyColorAdjustment()` is a fused processing entry point for applications
whose Color Adjustment combines brightness (-100 to 100), contrast (0–200%),
and saturation (0–200%). It applies brightness, contrast around 0.5, and
Rec. 709 saturation in one pixel pass and rounds RGB only at the end. This
preserves Motion Studio's existing pixel result; it is separate from the
Video Editor's independently ordered brightness, contrast, and saturation
filters in `applyStack()`, which quantize after each filter. The fused call
preserves alpha and row padding, validates its frame and parameters before
processing, and checks cancellation before each row. It returns Completed,
Cancelled, or InvalidInput. Cancellation may leave earlier rows changed, so
callers must discard a cancelled frame. Exceptions raised by the cancellation
callback propagate to the caller.

## Verification

`libs/tests/effects_test.cpp` covers neutral values, parameter bounds,
grayscale, alpha and stride preservation, stack order, enabled-state defaults,
disabled-instance skipping, repeated instances, invalid IDs, malformed RGBA
frames, fused Color Adjustment compatibility, and cancellation. Motion Studio consumer coverage lives in
`apps/motion-editor/tests/preview_renderer_test.cpp` and exercises delegation,
pixel parity, cancellation, and timing. Video Editor boundary coverage lives
in the project, Timeline command, Preview, offline export, Effects dock,
Functions, and Timeline widget tests. The manual acceptance checklist is in
[`../REGRESSION_TESTING.md`](../REGRESSION_TESTING.md#manual-ui-validation).
