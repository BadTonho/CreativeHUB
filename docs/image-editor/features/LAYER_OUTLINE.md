# Editable layer outline

Introduced in v17 on 2026-10-20. Application versions are unchanged; `.cimg` now writes
v21 and reads v1-20. Native visual acceptance, macOS and Linux remain pending.

## Editing contract

The Layers panel offers Outline, Thickness (1-1024 px), RGBA Color,
Inside/Outside, and Remove Outline below the stack. Defaults are 2 px, opaque
black, Outside. Only one editable layer can be targeted; group children are
supported, Background/groups/multiple selections are disabled. Selecting a
mask thumbnail still targets its owning layer. Disabling retains parameters;
removing omits the style and resets defaults. No new default shortcut is added.

Each continuous thickness edit is one history change. Color changes preview
live; cancelling restores the previous optional style without a history entry.
Switching layers/tabs and saving finish pending edit history. Unchanged values
do not create edits. Content and text remain editable and source files stay
unchanged. The active area selection does not clip the layer property.

## Pixel and resource contract

Render ordered content and transforms, apply the enabled raster mask, apply
outline, then layer opacity and group composition/transforms. Outline follows
all alpha boundaries, including holes, and is clipped to canvas bounds. Object
resizing does not scale its document-pixel thickness. The shape-stroke setting
remains independent. Inside preserves alpha and overlays color in the eroded
edge band; Outside composites its dilated edge band behind the content.

The CPU helper uses grayscale-alpha disk dilation (complement dilation for
erosion) with a one-pixel fractional circular fringe. It processes the occupied
bounds (expanded for Outside), using horizontal sliding maxima per disk row,
two byte planes and one row deque. Work is O(radius * region pixels), not disk
area per pixel. Cancellation is checked per scanline; cancelled/failed work
returns no image. Existing worker/export paths log technical failures.

Reference composition, CPU preview, export and outlined thumbnails share this
helper. The interactive cache retains processed results by content/mask
revision and style within its existing 64 MiB budget. GPU composition receives
already-masked/outlined pixels and no separate mask. Editing kernels stay CPU.
Outlined text/shape transforms request worker-composed previews rather than
raw object overlays so the outline follows the drag. Raw fill/selection/paint targets exclude the effect; composite Eyedropper
sampling and PNG/JPEG/full/Quick Export include it.

## Persistence and integration

The optional layer `outline` stores boolean `enabled`, integer `thickness`,
RGBA `color` and `position` (`inside`/`outside`). See [format](../architecture/FORMAT.md).
Older supported documents open without the property; older editor binaries
reject v17. Recovery envelope v1 carries v17. Hub backup/restore accepts v17,
rebases media paths and preserves the property; future Image v30 is rejected.
Linked PNG publication uses the existing atomic save/refresh protocol.

## Regression coverage and evidence

- `image_editor_outline_test.cpp` / `creative-suite-image-editor-outline`:
  independent circular morphology reference, grayscale alpha, holes, no-ops,
  cancellation, history, v17/legacy loading and invalid data, recovery,
  masked text, transforms/groups, cache/preview/export parity and 1080p timing.
- `image_editor_outline_ui_test.cpp` / `creative-suite-image-editor-outline-ui`
  (also in the complete UI suite): panel defaults, coalesced width undo,
  color accept/cancel, disabled-style retention/removal, selection restrictions,
  save/reopen and actual linked PNG publication/source preservation.
- `image_editor_interactive_test.cpp`: outlined CPU incremental parity and
  native GPU premultiplied-channel parity (existing tolerance <=2).
- Hub `hub-backup-format-test`: actual Image reader round trips v1-17 including
  an outlined group child. `hub-backup-manager-test`: future Image v18 rejection.

Windows Release evidence on 2026-10-20: the 17-test Image/Video workspace
selection passed, alongside four Video consumer/Hub backup gates. After the
cache-retention correction, the seven directly affected outline/core/UI/export/
interactive/native-GPU gates passed again; after the final catalog update,
full UI, outline UI, translation, export UI and native rendering UI passed.
The outline test also passed all eight QtTest cases natively on Windows.
The existing performance/cache suite passed after correcting its deleted-layer
byte-count expectation: only the surviving editable layer occupies the reference
cache, not Background or the removed layer. Overall, 22 distinct CTest gates
passed across the scoped selections. Following the outlined-object drag-preview
integration, full UI and focused outline UI passed again.

The native CPU measurement used Windows 11, Qt 6.7.2, Ryzen 5 3600 and GTX 1660
SUPER. Its synthetic 1920x1080 workload has five outlined shape layers in one
group plus Background. One cold and one unchanged warm frame, including CPU
composition, measured 133 ms and 8 ms respectively; warm preparation measured
0 ms at millisecond resolution. The test asserts zero additional rasterized
pixels for the unchanged frame. Retained cache: 49,766,400 bytes (47.5 MiB).
Sampled peak working set across the entire native outline test process:
99,545,088 bytes (94.9 MiB). These single-run measurements describe the test
process, not whole-application peak memory, UI latency, or a frame-rate promise.
The repeatable visual checklist is in [manual validation](../validation/MANUAL_VALIDATION.md#layer-outline).

Clipping in v18 composes children before applying the base outline, preserving
its own color. See [clipping semantics](CLIPPING_MASKS.md).

Gradient Map layers in v20 can process the lower stack or clipped-chain colors;
see [adjustment scope and alpha contract](GRADIENT_MAP.md).

Current writer: v29. [Brightness/Contrast](BRIGHTNESS_CONTRAST.md) shares the
adjustment stack contract; historical execution evidence above is unchanged.
