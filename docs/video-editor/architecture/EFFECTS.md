# Shared Visual Effects Contract

Status: provisional.

`libs/effects` provides the first shared CPU visual-filter API for the
Video Editor. The `creative-suite::effects` target depends on the shared RGBA
frame type and standard C++ only; it does not depend on Qt Widgets or own
application UI. Other applications may consume this library after their
document and rendering contracts are integrated.

## Built-in filters

| Stable ID | Parameter | Range | Default |
| --- | --- | --- | --- |
| `video.grayscale` | Amount | 0–100% | 100% |
| `video.brightness` | Brightness | -100–100 | 0 |
| `video.contrast` | Contrast | 0–200% | 100% |
| `video.saturation` | Saturation | 0–200% | 100% |

Definitions are discoverable through `builtInEffects()` and `findDefinition()`.
Instances hold an ID and explicit parameter IDs/values. A stack may contain
repeated instances; evaluation follows vector order. Validation rejects
unknown IDs, missing or duplicate parameters, non-finite values, and values
outside the definition's range.

## Processing

`applyStack()` mutates a valid RGBA frame in place. It processes only the
visible RGB bytes for each pixel, respects row stride, preserves alpha and row
padding, and returns false for malformed frames or invalid stacks. An empty
stack succeeds without changing pixels. Brightness offsets encoded channel
values; contrast scales them around 127.5; grayscale and saturation use
Rec. 709 luma coefficients. Each stage clamps to the 8-bit range before the
next filter runs.

The Video Editor evaluates each clip's stack before compositing its layer.
Preview and offline export call the same processor and preserve the clip's
stack order. Filter parameters are Timeline project data; caches and temporary
preview state are not added by this library. The Video Editor persists stacks
under the `.csp` version 17 clip schema described in [PROJECT.md](PROJECT.md).

## Verification

`libs/tests/effects_test.cpp` covers neutral values, parameter bounds,
grayscale, alpha and stride preservation, stack order, repeated instances,
invalid IDs, and malformed RGBA frames. Video Editor boundary coverage lives
in the project, Timeline command, Preview, offline export, Effects dock,
Functions, and Timeline widget tests. The manual acceptance checklist is in
[`../REGRESSION_TESTING.md`](../REGRESSION_TESTING.md#manual-ui-validation).
