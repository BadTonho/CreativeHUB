# Exposure / Offset / Gamma adjustment layers

## Editing contract

Layers > New Adjustment Layer > Exposure / Offset / Gamma creates the third
editable adjustment type. A single selected layer, Background or group is
required. Insert above the selected sibling or its entire clipping chain;
retain group ownership. Capture the current area selection as an independent
mask, or use white. Defaults are Exposure 0, Offset 0, Gamma 1, Normal mode,
100% opacity, visible and unclipped. No shortcut is added.

The properties panel has synchronized sliders and decimal fields:

| Control | Range | Precision / single step |
| --- | --- | --- |
| Exposure | -20 to +20 stops | 2 decimals / .01 |
| Offset | -.5 to +.5 | 4 decimals / .0001 |
| Gamma | .01 to 9.99 | 2 decimals / .01 |

A continuous edit or slider gesture is one Undo. Reset restores all three
parameters atomically. No-op edits preserve Redo. Finish edits on focus,
layer/tab changes and existing document commands. Selecting the mask keeps
owner properties visible. Masks, opacity, visibility, blend modes, clipping,
stack movement and groups retain their existing adjustment semantics.
Adjustments have no painted content, outlines, individual transforms or
isolated Quick Export. Full/group exports and linked PNG include the result.
The thumbnail shows the adjusted grayscale ramp; group thumbnails show the
composite. Labels and load errors are available in English and pt-BR.

## Processing and ownership

`effects::ExposureOffsetGammaParameters` contains three doubles, defaults
`{0,0,1}`, and is the third alternative of `effects::AdjustmentParameters`.
Parameters must be finite and within the UI ranges; persisted values need not
be rounded to UI precision. The shared evaluator borrows straight normalized
RGB and returns quantized RGB bytes without allocations or alpha processing.
Image Editor owns stack scope, masks, history, resource lifetime and errors.
Typed session creation/set operations cannot change an existing layer's type.
No effect registry entry or document model is added to Video or Motion.

Treat current RGB as sRGB, using piecewise transfer functions, independently
of any embedded profile. For encoded channel v in [0,1]:

1. Decode `L = v / 12.92` for `v <= .04045`, otherwise
   `L = ((v + .055) / 1.055)^2.4`.
2. Calculate `G = clamp(max(0, L * 2^exposure + offset)^(1/gamma), 0, 1)`.
   Clamping the base to one before exponentiation is equivalent and prevents
   overflow at the permitted extremes.
3. Encode `12.92 * G` for `G <= .0031308`, otherwise
   `1.055 * G^(1/2.4) - .055`, then round RGB to bytes.
4. Use existing blend-mode processing and intensity (mask coverage times
   opacity) on encoded RGB, preserving every original alpha byte.

Neutral Normal bypasses conversion and preserves the legacy byte path exactly.
CPU preview, export, publication, thumbnails, sampling and interactive rendering
share the evaluator/adapter. OpenGL performs the same computation against its
budgeted backdrop texture, with uniforms and no effect readback. Parameter edits
reuse raw content and masks; CPU and GPU limits remain 64 MiB and 256 MiB.
Cancellation publishes no partial frame. Existing capacity recovery and logged
technical errors remain distinct. This is an 8-bit adjustment, not an HDR,
wide-gamut or ICC color-management implementation, and does not promise exact
Photoshop equivalence.

## Persistence and compatibility

Introduced in v22; the current v24 writer reads supported v1-23 and writes:

```json
{"kind":"adjustment","adjustment":{"type":"exposure_offset_gamma","exposure":1.25,"offset":-0.0123,"gamma":1.4}}
```

All three numbers are required, finite and in range. Reject the new type before
v22, unknown types, mismatched adjustment fields, Background ownership and
incompatible raster content/outlines. Invalid Open retains the current session.
Earlier binaries cannot open v22; no downgrade writer is introduced. Existing
v20 Gradient Map and v21 Brightness/Contrast fixtures remain readable.
Recovery envelope v1, application versions and published PNG protocol are unchanged.
Hub accepts Image v1-24, rejects v25 and preserves all five adjustment types,
masks and rebased media through vault/relocated restore with the actual reader.

## Regression coverage and acceptance

- `creative-suite-image-editor-exposure-offset-gamma` covers an independent
  long-double numeric reference, transfer thresholds, all 256 byte values,
  extreme controls (with adjacent-byte tolerance only at numeric half-byte ties),
  alpha/coverage/opacity/six modes, exact identity,
  cancellation, history/no-ops, masks, mixed stacks/clipping/groups,
  cache reuse, PNG/JPEG/group exports, thumbnails, recovery, invalid loads,
  v20/v21 migration and the five-layer/one-group 1080p CPU workload.
- `creative-suite-image-editor-exposure-offset-gamma-ui` and full UI cover the
  actual menu, selection seeding, control ranges/precision, atomic Reset,
  gesture history, restrictions, mask ownership, tabs and linked PNG publication.
- `gpuExposureOffsetGamma` in the interactive suite covers native OpenGL
  equivalence within two premultiplied RGB levels and exact alpha, mixed
  groups/chains, modes, zero-upload parameter edits, cancellation, validation,
  the 1080p workload and constrained-capacity recovery. Offscreen runs skip
  native GPU assertions explicitly.
- Shared effects/composition/OpenGL, existing Image suites, Hub backup
  format/manager and affected Video/Motion consumers protect the boundaries.

Execution evidence is recorded separately below. Native visual, controlled Windows reference-PC
measurements, packaged runtime and untested platform acceptance remain pending;
use the [manual checklist](../validation/MANUAL_VALIDATION.md#exposure--offset--gamma-adjustments).

## Linux execution evidence — 2026-10-21

Release targets for Image model/UI/rendering, shared effects/composition/OpenGL,
Hub backup format/manager, and the selected Video/Motion consumers compiled on
Fedora 44, Qt 6.11.2, GCC 16.2.1 and FFmpeg 8.1.3. This was a scoped test build;
no application installer, release artifact or version change was produced.
The native Wayland context reported Mesa zink/NVK on GTX 1660 SUPER. The Image
native interactive suite passed **19/19 QtTest cases**, including the new typed
adjustment, exact alpha and maximum two-level premultiplied RGB difference;
the native shared OpenGL parity/worker gate also passed. The offscreen shared
OpenGL CTest entry skipped for lack of a native context and is not counted as a pass.

The new model, UI and translation gates passed; full Image UI, Hub backup/restore,
existing Gradient Map/Brightness/Contrast, core/masks/raster/performance and the
selected Motion GPU-composition/persistence/export/UI gates passed. A focused
`creative-suite-main-editor-linked-image` gate also passed using an actual Image
v22 producer, expected +1-stop RGB, mask alpha and clip-variant isolation. This
scenario is also reused by the full Video main-window test. The final new UI
recheck includes keyboard edits and exposure/offset/gamma slider precision.

The broader scoped CTest run selected 47 gates: **40 passed, six failed, one
skipped**. The additional focused linked-image gate passed independently.
Known failing gates are recorded below; this is not an all-green release gate.

| Gate | Observed failure |
| --- | --- |
| `creative-suite-main-editor-project` | Invalid timeline zoom returned the wrong error category. |
| `creative-suite-main-editor-main-window` | Segmentation fault after native export/codec activity; focused linked-image scenario passes separately. |
| `creative-suite-image-editor-outline` | Selected-group PNG/full-image comparison at `renderCachesMasksExports`. |
| `creative-suite-image-editor-window-close` | Process timeout after 120 seconds. |
| `creative-suite-image-editor-export-ui` | PNG dialog automation timeout; isolated recheck reproduces it. |
| `creative-suite-motion-editor-preview` | Existing text rasterization exceeds the selected alpha ceiling. |

Baseline comparison uses a separate `git archive HEAD` source tree, with only
the same two LP64 compile fixes for Motion and local FFmpeg name aliases; it
contains no Exposure / Offset / Gamma implementation. All six gates failed again
in that baseline, reproducing the same failure messages/timeout. They are
pre-existing in this local environment; no failing gate was disabled or marked
passing. These unresolved gates still prevent an all-green release acceptance.

The [Linux profile](../performance/exposure-offset-gamma-linux-2026-10-21.json)
records the 1080p workload on Ryzen 5 3600/GTX 1660 SUPER. CPU cold/warm measured
173/137 ms and exposure/offset/gamma edits 141/144/145 ms. Native GPU parameter
edits used zero uploads and retained source reconstruction work; warm resources
were 82,944,000 GPU bytes and 49,766,400 CPU cache bytes. A forced 64 MiB GPU
budget refused the full workload and recovered through CPU with exact pixels.
These are individual local observations, including verification readback and
session load, not general speed guarantees or Windows acceptance.

Reproduce the scoped new behavior with CTest filters for
`creative-suite-image-editor-exposure-offset-gamma`, its `-ui` counterpart,
`creative-suite-main-editor-linked-image`, shared effects/composition, and Hub
backup format/manager. For a native Linux context, run the interactive executable
with `QT_QPA_PLATFORM=wayland` and QtTest function `gpuExposureOffsetGamma`; keep
native and offscreen outcomes separate. See the profile for temporary dependency
and FFmpeg 8 compatibility setup used here.
