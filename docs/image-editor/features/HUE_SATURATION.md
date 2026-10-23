# Global Hue/Saturation adjustment layers

## Editing contract

Layers > New Adjustment Layer > Hue/Saturation creates the fourth adjustment
kind, with global Hue, Saturation and Lightness. The Portuguese label is
Matiz/Saturação. Colorize and per-color-range controls remain planned separately.
A single selected layer, Background or group is required. Insert above the
selected sibling or its clipping chain, retain group ownership and capture an
area selection as the independent initial mask; otherwise initialize white.

| Control | Range | Default | Step |
| --- | --- | --- | --- |
| Hue | -180° to +180° | 0° | 1° |
| Saturation | -100% to +100% | 0% | 1% |
| Lightness | -100% to +100% | 0% | 1% |

Integer fields and sliders stay synchronized. A drag, continuous field edit,
keyboard or wheel gesture is one Undo. Reset restores all three atomically;
no-op edits preserve Redo. Finish edits on focus/layer/tab changes and existing
document commands. Mask selection keeps owner controls visible. Opacity,
visibility, six blend modes, clipping, stack movement and groups use existing
adjustment semantics. Adjustments cannot contain painted content, outlines or
individual transforms, and have no isolated Quick Export. Document/group export
and linked PNG include the result. The adjustment thumbnail processes a chromatic
hue strip, while groups show the actual composite. Existing adjustment thumbnails
keep their old sampling. UI labels and load errors ship in English and pt-BR.
No shortcut is added.

## Shared calculation and ownership

`effects::HueSaturationParameters` holds integer `hue`, `saturation`, `lightness`,
all defaulting to zero. It is the fourth `effects::AdjustmentParameters` variant.
The new Qt-free `hue_saturation.h` validates ranges and evaluates borrowed finite,
normalized straight encoded-sRGB channels, returning rounded RGB bytes without
allocation, alpha processing or retained state. The Image session owns typed
creation/setters, stack scope, masks, history and document errors. A setter cannot
change an existing layer's adjustment kind. Video/Motion registries, effect
parameters and native formats are not extended.

Decode HSL from encoded RGB (no linear-light conversion): let min/max be channel
extrema, delta their difference, and L=(max+min)/2. For delta>0, calculate the
standard six-sector hue and S=delta/(1-abs(2L-1)); achromatic inputs have S=0.
For an original chromatic input, rotate H by hue/360 with circular wrapping,
positive red → yellow → green, and adjust S using the rule below. Original
achromatic pixels keep S=0, including when saturation is positive.

For normalized S or L and signed percent a:

- Negative: `adjust(t,a) = t * (1+a/100)`.
- Nonnegative: `adjust(t,a) = t + (1-t) * a/100`.

Adjust L independently, convert HSL back to RGB, clamp and round to bytes before
the existing masked blend. Saturation -100 produces HSL gray, not Rec.709 luma;
Lightness -100/+100 produces black/white with unchanged alpha. ±180 hue rotations
render identically while preserving the user's signed parameter. The neutral
0/0/0 Normal adjustment bypasses conversion and preserves the old byte path.
Other blend modes still apply their existing blend rule.

The Video saturation evaluator keeps its Rec.709 behavior. This feature does not
introduce ICC management, HDR or a promise of exact Photoshop equivalence.
CPU preview, sampling, export, publication and thumbnails use the same adapter;
OpenGL performs matching math against its retained backdrop using uniforms,
without effect readback. Parameter edits retain raw content/mask caches and
existing 64 MiB CPU / 256 MiB GPU budgets. Cancellation publishes no partial
frame; capacity recovery and actionable technical-error logging retain their
existing contracts.

## Format and compatibility

Introduced in v23, this kind is read by the current v26 writer alongside
supported v1-25 documents. Store:

```json
{"kind":"adjustment","adjustment":{"type":"hue_saturation","hue":75,"saturation":-25,"lightness":10}}
```

All three integers are required, in the documented ranges. Reject fractional,
non-numeric, missing and out-of-range controls, this kind before v23, mismatched
adjustment fields, incompatible Background/content/outline fields and future
schemas. Invalid Open retains the active document. Earlier binaries cannot open
v23; there is no downgrade writer. Recovery envelope v1, application versions
and linked PNG protocol stay unchanged. The current Hub accepts Image v1-26 and rejects v27,
retaining all six kinds and masks while rebasing existing media paths. Actual
Image readers verify vault and relocated restores.

## Regression coverage

| Contract | Automated gate |
| --- | --- |
| Independent HSL reference, RGB grid, gray/near-gray, ±180, bounds, alpha/coverage/opacity/six modes and exact identity | `creative-suite-image-editor-hue-saturation`, shared `creative-suite-effects` |
| Four-kind stack/order, history, clipping/groups/masks, cache reuse, thumbnails, v23/recovery/migration/rejected loads and PNG/JPEG/group export | `creative-suite-image-editor-hue-saturation`, existing core/raster/mask/export suites |
| Actual menu, selection seed, integer controls, keyboard/wheel/sliders, Reset, restrictions, tabs and linked PNG | `creative-suite-image-editor-hue-saturation-ui`, full UI and translation catalog |
| Native OpenGL parity, unchanged alpha, zero-upload edits, mixed types, cancellation/budgets and 1080p | Interactive `gpuHueSaturation` / `gpuHueSaturationColorGrid`, native shared OpenGL and existing Image interactive gates |
| Real v23 PNG producer, rotated RGB, mask alpha and isolated clip variant | `creative-suite-main-editor-linked-image`, also reused by the full Video main-window suite |
| v1-23 backup/rebase/restore and future refusal | `hub-backup-format-test`, `hub-backup-manager-test` with the actual Image reader |
| Existing shared consumers | Scoped Video adjustment/transform/project/media and Motion preview/GPU/persistence/export/UI suites |

Execution evidence is recorded below. Keep native context
results distinct from offscreen skips. The six pre-existing local failures
recorded in [Exposure evidence](EXPOSURE_OFFSET_GAMMA.md#linux-execution-evidence--2026-10-21)
are baseline comparisons, not accepted release gates. Native visual, Windows/macOS,
packaged runtime and controlled reference-PC acceptance remain pending in the
[manual checklist](../validation/MANUAL_VALIDATION.md#global-huesaturation-adjustments).

## Raster cache invalidation fix

Broader validation exposed an intermittent `scenesCachesAndExports` crash in the
existing Gradient Map gate. It also reproduced in a clean pre-HSL `git archive
HEAD` build. A debugger located invalid image destruction in
`ImageLayerRasterCache::synchronize`/`removeEntry`: pre-incrementing a QHash iterator
before erase can miss entries relocated while compacting a collided bucket.
Synchronization now continues with the iterator returned by erase.

`layerRasterCacheInvalidatesCollidingEntriesSafely` in
`image_editor_performance_test.cpp` derives colliding QString IDs from the current
Qt hash seed, verifies retained hits/removed misses after partial invalidation,
and then removes every entry. Before the fix it retained 2,048 bytes instead of
zero; after the fix it passed. The Gradient Map gate subsequently passed 30
consecutive complete runs. This additional pre-existing cache defect is fixed
here; the original six baseline failures remain tracked separately. No gate was
disabled, and native/API/format semantics are otherwise unchanged by this fix.

The full UI gate also exposed a timer race in its existing small-image import
cancellation fixture: decoding could finish before the timer clicked Cancel.
The fixture now clicks before delivering the worker completion to the modal
dialog and asserts that cancellation was requested. This changes test scheduling,
with no production import behavior change.

## Linux execution evidence — 2026-10-21

Scoped Release targets compiled on Fedora 44, Qt 6.11.2, GCC 16.2.1 and FFmpeg
8.1.3: Image model/UI/rendering/benchmark, shared effects/composition/OpenGL,
Hub backup format/manager and selected Video/Motion consumers. This used the
same temporary build dependencies and FFmpeg 8 profile-name aliases as the
previous Exposure run. No application installer, application version change or
release artifact was produced.

The final scoped CTest run selected **50 gates: 43 passed, six failed, one
skipped**. Passing gates include new HSL model/UI, translation, full Image UI,
all older adjustment suites, core/masks/raster/performance, Hub real-reader
restore and focused Video linked-image publication. Selected Video adjustment/
transform/media and Motion GPU-composition/persistence/export/UI passed.
The HSL model reported **8/8 QtTest cases**. Both full and focused HSL UI passed
after the import fixture scheduling correction described above.

The six failures match the pre-HSL local run and remain unresolved:

| Gate | Observed failure |
| --- | --- |
| `creative-suite-main-editor-project` | Invalid timeline zoom returns the wrong error category. |
| `creative-suite-main-editor-main-window` | Segmentation fault after export/codec activity; focused actual v23 linked-image publication passes. |
| `creative-suite-image-editor-outline` | Selected-group PNG/full-image comparison in `renderCachesMasksExports`. |
| `creative-suite-image-editor-window-close` | 120-second process timeout. |
| `creative-suite-image-editor-export-ui` | PNG file-dialog automation timeout. |
| `creative-suite-motion-editor-preview` | Text rasterization exceeds the selected alpha ceiling. |

These gates were rechecked before HSL edits with the existing binaries. The
[earlier Exposure record](EXPOSURE_OFFSET_GAMMA.md#linux-execution-evidence--2026-10-21)
also retains their pre-Exposure source comparison. No failing gate was disabled
or treated as passing. These failures still prevent an all-green release gate.
The additional pre-existing cache defect found during this work was fixed and
covered by the failing-before/passing-after collision regression above.

Offscreen shared OpenGL CTest skipped for lack of a native context. Within the
passing offscreen Image interactive gate, **9 QtTest cases passed and 12 native
GPU cases skipped**; these skips do not establish GPU correctness. Separately,
the full native Wayland Image suite passed **21/21 cases**, including HSL scenes
and the gray/near-gray/alpha grid, with exact alpha and at most two premultiplied
RGB levels of difference. Native shared OpenGL parity and worker lifecycle also
passed. The context reported Mesa zink/NVK, GTX 1660 SUPER, OpenGL 4.6 core,
Mesa 26.2.4. These are native automated checks, separate from owner visual review.

The [Linux 1080p profile](../performance/hue-saturation-linux-2026-10-21.json)
records five raster layers, one isolated group and one HSL adjustment on Ryzen
5 3600/GTX 1660 SUPER. Parameter edits preserve reconstruction work and upload
zero bytes. Warm retention is 49,766,400 CPU cache bytes and 82,944,000 GPU texture
bytes. A forced 64 MiB GPU budget refuses the full workload; CPU recovery matches
exactly. The profile records individual CPU/GPU timings, including verification
readback and session load, with no speed guarantee or reference-PC acceptance.
Windows/macOS, packaged runtime, owner visual checks and controlled Windows
reference-PC performance remain pending.

Reproduce the focused behavior with CTest filters for
`creative-suite-image-editor-hue-saturation`, its `-ui` counterpart, shared
effects/composition, Hub backup format/manager and
`creative-suite-main-editor-linked-image`. Run the native Image interactive
executable with `QT_QPA_PLATFORM=wayland` (or the platform's native Qt backend),
including `gpuHueSaturation` and `gpuHueSaturationColorGrid`; keep native and
offscreen results separate.

Coverage sources: [model/reference/export/format](../../../apps/image-editor/tests/image_editor_hue_saturation_test.cpp),
[actual UI](../../../apps/image-editor/tests/image_editor_hue_saturation_ui_test.cpp),
[Image CTest registrations](../../../apps/image-editor/tests/CMakeLists.txt),
[native raster checks](../../../apps/image-editor/tests/image_editor_interactive_test.cpp),
[cache collision regression](../../../apps/image-editor/tests/image_editor_performance_test.cpp),
[Hub actual-reader restore](../../../apps/hub/tests/test_backup_formats.cpp) and
[Video handoff](../../../apps/video-editor/tests/application/main_window_integration_test.cpp).
