# Editable clipping masks

Introduced in v18 on 2026-10-20. Application versions are unchanged. `.cimg` now writes
v21 and reads v1-20; recovery retains envelope v1. Native visual acceptance,
macOS and Linux remain pending.

## Editing and stack contract

Create/Release Clipping Mask is available in the Layers context menu for one
editable layer. Ctrl+Alt+G (Command+Option+G on macOS) is customizable in Settings.
Saved assignments take precedence; a colliding introduced default is unassigned.
Background, groups and multiple selections cannot be clipped. Selecting a mask
thumbnail still targets its owner; area selection does not constrain clipping.
An arrow marks clipped rows and their tooltip names the base. One toggle is one
Undo entry; unchanged requests do nothing. Layer/tab switching retains document
state and independent history.

A consecutive run of clipped sibling layers shares the nearest ordinary layer
below. Hidden layers still determine binding. Root group items break chains;
groups cannot be bases, while Background can be a root base. No nested groups
are introduced. Reordering follows the new stack. Group/move/ungroup edits
release an entire orphan run atomically. Deleting a base releases its surviving
dependents instead of rebinding them; Undo restores all flags in the same edit.
New layers start unclipped and may become the base of a run above them.

## Rendering and ownership

The application-owned resolver in `image_layer_clipping.h` is shared by session,
stack editing, format validation, reference rendering and the worker cache.
The base matte is its content after operations/transforms and its enabled raster
mask, before outline and opacity. Child pixels include their own operations,
mask, outline and opacity. SourceAtop composition preserves base alpha and its
holes/partial coverage; it does not repeatedly multiply coverage for a chain.
The base outline is applied after the chain, retaining its own inside/outside
color, then base opacity once. Group transforms and opacity follow composition.
Hiding the base or setting its opacity to zero hides the whole chain; hiding a
child only omits that child. Empty base alpha produces no clipped content.

The CPU renderer supplies completed chains to both preview backends, export and
published PNG. GPU inputs have an empty mask and only the base opacity, avoiding
double application. The worker cache retains a completed chain within its
existing 64 MiB budget, keyed by ordered layer properties/operations/masks/
outlines, resource identities, canvas/base geometry and Background operations.
Cancelled or failed renders publish no partial image/cache entry. Existing
worker/export failure boundaries log technical errors and recover on CPU.
Object drags involving a chain use worker-composed previews. Paint, fill,
selection and mask targets remain editable original pixels; composite
Eyedropper sees the finished image.

Full/group export composes chains. Quick Export of a clipped layer isolates its
styled pixels using the base matte, visibility and opacity, without base/sibling
colors. Quick Export of a base excludes dependent layers. Layer thumbnails show
isolated clipped content; group thumbnails include the chain. Changes to the
base invalidate dependent thumbnails.

## Persistence and integration

The optional layer boolean `clip_to_base` defaults false and is written when
true. v1-17 load without this field; older-schema fields, non-booleans,
Background/group clipping and chains lacking a sibling base are rejected.
Invalid loads preserve the current document. Hub accepts v1-21 and preserves
clipping metadata during media relocation and restoration; future v22 is
rejected. Linked PNG publication retains the existing atomic save/refresh
protocol and does not change the Video Editor document format.

## Verification

- `image_editor_clipping_test.cpp` / `creative-suite-image-editor-clipping`:
  partial alpha, holes, base/child opacity, inside/outside outlines, hidden and
  empty bases, isolated export, cache reuse/resource changes/budget/cancellation,
  stack/history/group edits, v18/legacy/invalid loading, recovery, masked text,
  thumbnails and full/selected PNG export; representative 1080p timing.
- `image_editor_clipping_ui_test.cpp` / `creative-suite-image-editor-clipping-ui`
  and full UI: command restrictions, mask ownership, Undo/Redo, tabs, base
  tooltip, shortcut migration, real save/published PNG and source preservation.
- `image_editor_interactive_test.cpp` / native GPU raster: clipped styled layers,
  opacity, group transformations and CPU/OpenGL parity (channel tolerance <=2).
- Hub `hub-backup-format-test`: actual producer/reader round trips v1-18,
  including a masked/outlined clipping chain. `hub-backup-manager-test`: v19
  rejection. Existing Video consumer gates protect PNG refresh compatibility.

Windows Release evidence on 2026-10-20: all 24 scoped CTest gates passed,
including Image core/raster/masks/drawing/styles/UI/translation/export/formats/
performance/interactive/native GPU and presentation, three Video consumer gates,
and both Hub backup gates. The native clipping binary passed all seven QtTest
cases; the native interactive/GPU binary passed all twelve. Windows offscreen
checks used `QT_QPA_FONTDIR=C:/Windows/Fonts`. Native recovery presentation now
uses an isolated error-log directory rather than the user profile. The Hub
fixture materializes its Motion linked paths for portable canonical-path checks.

A separate native Windows/Qt 6.7.2 Release run measured the 1920x1080 workload
with five painted layers, four clipped children, one group and Background:
30 ms cold prepare/composition, 7 ms unchanged prepare/composition, and 24 ms
for a base opacity update. The unchanged frame rasterized zero extra pixels.
Retained rasters occupied 16,588,800 bytes; sampled peak working set across the
entire clipping test process was 83,939,328 bytes. These single-run timings
characterize this synthetic workload; whole-application latency/memory and
manual responsiveness acceptance remain separate. See the
[measurement artifact](../performance/clipping-masks-windows-2026-10-20.json) and
[manual checklist](../validation/MANUAL_VALIDATION.md#clipping-masks).

## Development outputs

Canonical Image Editor Release and Debug and Hub Debug builds succeeded without
installers; application version metadata remains unchanged. `lugarExecutaveis.md`
selects Image Release for direct launch and Hub Debug. The Hub launcher selects
its matching Image Debug sibling; no Image installation registry override or
Image executable in the Hub output directory was present during verification.

Verified modification times on 2026-10-20 (America/Sao_Paulo, UTC-03:00):

| Output relative to repository root | Modification time |
| --- | --- |
| `build/apps/image-editor/Release/creative-suite-image-editor.exe` | 17:42:59 |
| `build/apps/image-editor/Debug/creative-suite-image-editor.exe` | 17:46:15 |
| `build/apps/hub/Debug/creative-suite-hub.exe` | 17:46:22 |

Clipped children now support six blend modes inside their chain; see
[blend contract](BLEND_MODES.md). The base mode applies to the finished chain.

Gradient Map layers in v20 can process the lower stack or clipped-chain colors;
see [adjustment scope and alpha contract](GRADIENT_MAP.md).

Current writer: v22. [Brightness/Contrast](BRIGHTNESS_CONTRAST.md) shares the
adjustment stack contract; historical execution evidence above is unchanged.
