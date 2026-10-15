# Editable composition interchange

The Video Editor and Motion Studio exchange independent editable compositions
through **File > Export Composition Interchange...** and **File > Import
Composition Interchange...**. These commands have no default keyboard shortcuts.
This workflow is separate from linked Motion documents and published video.

## Workflow and supported content

Export a selected Fusion Composition in the Video Editor or the entire document
in Motion. Importing in Motion creates an unsaved document, subject to the usual
pending-change confirmation. Importing in Video inserts a generated clip at the
playhead on the selected video track through the timeline command service, as one
undoable edit. The destination project must have the same canvas and exact FPS.
Normal overlap and audio-track rejection rules apply.

Video, still images, Background, Text, ordered static color passes, and one
animated Transform per source are supported. Merge maps to a back-to-front layer
stack. Full-canvas Motion rectangles without strokes map to Background nodes.
Text content, font, alignment, size, RGBA, layout, transforms and linear/Bézier
curves remain editable. Media are referenced, not copied or embedded.

Masks on Merge/Output, blur, animated effect parameters, shared image branches, disconnected
nodes, processing after Merge, multiple Transform stages, nested foreground
compositions, linked image documents, other shapes, and clip-level effects or
non-neutral transforms are refused with an item-specific report. No partial
export or flattened replacement is produced. Node positions are regenerated;
graph topology is normalized to source/color/transform branches and Merge.

Rectangle/Ellipse masks are editable on sources, individual color stages and
Transform. A definition can be shared between destinations; shared mask keys
use composition time, separately from layer-local transform keys. Motion exposes
these operations in [Masks Inspector](motion-editor/features/MASKS.md).

## Ownership and interchange v1/v2

`libs/composition-exchange` owns the UTF-8 JSON contract and worker-side raster
and source-clock helpers. Application adapters own conversion and native editing
semantics. Shared code has no dependency on app models or UI. App-owned dialogs
schedule file preparation on workers; cancellation does not publish partial
files or replace the active document. Technical failures are logged by the app.

The extension is `.cscomp`; `format` is
`creative-suite.composition-interchange`. Readers accept versions 1 and 2;
writers select v2 when masks are present and retain v1 for unmasked exports.
Future v3 and mask data declared as v1 are rejected. Root fields are `name`, `width`, `height`, string-encoded
`fps_numerator`, `fps_denominator`, `duration`, and an ordered `layers` array.
Layers contain string-encoded IDs, kind (`video`, `image`, `background`, `text`),
media source/rate/count, RGBA, text data, a five-number transform, five ordered
curves, ordered static color passes (brightness, contrast, saturation, enabled)
and a `presentation` record.

CSP v30 also preserves the Color node enable state and exposes it in Fusion
Inspector. Disabled color effects retain their editable values through
interchange.

Presentation records preserve `profile` (`fusion_canvas` or `motion_layer`),
name, visibility, start/duration, signed composition-frame source offset,
source-frame start, and text box dimensions. Curves store local frame, value,
linear/Bézier selection and four easing coordinates. Fusion sources are fitted
to transparent canvas before processing; Motion sources retain intrinsic size
until Transform. Text retains the corresponding measured/margined or explicit
box layout. Fusion video ends become transparent; Motion preserves its rounded
sampling and last-frame hold. Source windows use the original composition clock
through generated-clip cuts and trims.

Paths inside the interchange directory are relative; external paths are
absolute. Imports validate referenced files before applying. Files are limited
to 64 MiB, canvases/intermediate RGBA buffers to 16,777,216 pixels, documents to
4096 layers, layer color stacks to 1024 passes and layer curves to 100,000 keys.
Interchange graphs are limited to 16,384 stages. Prepared source rasters are
bounded to 256 MiB in both consumers, in addition to the existing
decoder/compositor/GPU limits. Text sizes are limited to 1–512 pixels by the
common subset; Motion retains its currently supported exact FPS choices. Stored
font sizes retain decimals and the Motion Inspector accepts fractional values.
Missing font families retain their names and use Qt fallback.

V2 adds a `masks` array of definitions (`id`, `kind`: `rectangle`/`ellipse`,
five-value `geometry`, `soft_edge`, `invert`, and five scalar `curves` arrays).
IDs/references are decimal strings. Layers optionally contain `source_mask`,
`transform_mask`, and `color_masks`, parallel to the ordered color passes;
zero means no association. Mask IDs are independent of layer IDs, unique within
the collection, and must resolve. Unreferenced definitions, invalid parameters
and mismatched association arrays are refused. The collection is capped at
4096 masks; their graph nodes count toward the existing 16,384-stage limit.
Image branches remain unshareable; only mask branches may fan out.

## Native persistence and backups

CSP v30 and Motion v8 persist optional presentation records. Motion v8 adds a shared
mask collection and source/effect/transform associations; CSP v30 already stores
the equivalent graph masks and does not advance for this delivery. Older supported
projects retain their existing rendering defaults; readers predating these
versions reject the newer files. Motion v7 introduced composition
name/duration and fractional font-size metadata. Autosave and recovery use the
same native serializers. Hub backups accept CSP 1–30 and Motion 1–8, retain
presentation records and rebase existing media/Input paths. CSP 31 and Motion 9
are rejected. Application versions and the existing linked-handoff v1 protocol
are unchanged.

## Regression and acceptance

`creative-suite-mask-interchange` adds stage masks, shared identity, mask curves,
Inspector controls, schema v2/v8, and mask raster/CPU/GPU comparisons.
`creative-suite-composition-interchange` exercises both app adapters, persistence,
normal model edits after import, editable curves, generated sources,
pre-encode pixel comparisons, source clocks,
refusal, cancellation, insertion collisions, committed-edit notifications and
global/local playhead mapping. `creative-suite-media-assets` checks transactional
prepared-media append and pixel ownership without cached-frame copies.
Native project/recovery,
timeline, graph, GPU composition, Motion preview/export and Hub backup suites
protect their respective consumers. Test registration is in the Video Editor
application-test CMake group and requires both editors enabled.

### Pre-mask Windows Debug baseline — 2026-10-14

The selected 26-suite gate passed, covering interchange, media ownership,
shared CPU/OpenGL composition, Video timeline/graph/UI/persistence/recovery,
Motion document/history/UI/preview/export, and Hub backup/format consumers.
The additional Image Editor GPU raster consumer passed separately (27 suites
total). No test was skipped in these selections.

The OpenGL run used an NVIDIA GeForce GTX 1660 SUPER, driver 617.42, OpenGL 3.2
Core. The interchange test confirmed GPU execution and complete CPU recovery
with an intentionally insufficient texture budget. It compared unencoded RGBA
output for measured Fusion text, boxed Motion text, fractional font sizes,
semi-transparent backgrounds, rotated/scaled images and encoded synthetic
video (320 × 180, 30 fps, four seconds, FFV1/PCM). The maximum observed channel
difference was 2 on the 0–255 scale, matching the documented tolerance; native
properties and curves are checked independently of this pixel tolerance.

The canonical executables were built directly, without copying executables
from an auxiliary tree. Modification times below use UTC−03:00:

| Executable under the repository root | Modification time |
| --- | --- |
| `build/apps/video-editor/Debug/creative-suite-video-editor.exe` | 2026-10-14 22:00:39 |
| `build/apps/motion-editor/Debug/creative-suite-motion-editor.exe` | 2026-10-14 22:00:50 |
| `build/apps/hub/Debug/creative-suite-hub.exe` | 2026-10-14 22:00:56 |

Manual checks pending native UI acceptance: export/import from each File menu;
cancel each file/progress dialog; refuse unsupported nodes without rewriting an
existing destination; import onto occupied/audio tracks; edit text/color/curves,
undo/redo, cut/trim, save/reopen/recover, then transfer back. Check Unicode paths,
missing fonts and missing media. Compare pre-encode CPU/GPU frames with alpha,
rotated/scaled mismatched-size media and animated text on additional drivers and
font installations. Windows Debug automation does not establish macOS/Linux UI
or driver acceptance.


### Editable-mask Windows Debug evidence — 2026-10-14

The 29-suite affected-consumer gate passed in 149.23 seconds without skips:
shared animation/composition/OpenGL, both interchange suites, Video Editor
main-window/project/autosave/timeline/commands/graph/generated clips/masks/GPU,
Motion document/history/persistence/recovery/preview/GPU/export/startup/UI,
Hub backup/format, and the Image Editor GPU raster consumer. After the final
Inspector contrast adjustment, the mask-interchange and Motion UI suites were
rebuilt and passed again (22.08 seconds combined).

Synthetic pre-encoding Fusion/Motion comparisons observed a maximum difference
of zero per channel for Rectangle/Ellipse source/effect/transform masks,
rotated soft edges, animated Bezier geometry, inverted zero dimensions,
multiline translucent text and mismatched-size intrinsic sources. GPU graph,
Motion source-mask and masked Gaussian Blur comparisons passed the maximum
2-per-channel tolerance. The native OpenGL run used NVIDIA GeForce GTX 1660
SUPER, driver 617.42, OpenGL 3.2 Core. Four retained masked layers intentionally
exhausted the bounded image pool, recovered completely on CPU, and a following
single-layer frame returned to GPU. The expected recovery was logged.

The mask suite additionally exported and decoded a lossless six-frame video
with a delayed layer to check composition-time mask animation; checked v1
zero-only associations, v2/native-v8 persistence/recovery, shared references,
independent copies, Inspector/history edits, effect reorder/removal, source
windows, Video split/Bezier/FPS continuity, invalid geometry/references, the
4096-mask/16,384-stage limits and cancellation. Hub fixtures reopened backed-up
and restored v8 masks through the real Motion reader. A test-generated Inspector
capture was inspected for layout and active-button contrast. Full native menu
interaction and additional drivers/macOS/Linux remain pending as described
above and in [Motion masks](motion-editor/features/MASKS.md).

All three canonical Debug executables were compiled directly. Modification
times use UTC−03:00; no application versions, installers, commits or remote
submissions were produced by this delivery.

| Executable under the repository root | Modification time |
| --- | --- |
| `build/apps/video-editor/Debug/creative-suite-video-editor.exe` | 2026-10-14 22:38:31 |
| `build/apps/motion-editor/Debug/creative-suite-motion-editor.exe` | 2026-10-14 22:55:39 |
| `build/apps/hub/Debug/creative-suite-hub.exe` | 2026-10-14 22:40:54 |
