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

Video, still images, Background, Text, ordered static color and Gaussian Blur stages, and one
animated Transform per source are supported. Motion Glow is explicitly refused
by v3 with a layer/effect diagnostic. Merge maps to a back-to-front layer
stack. Full-canvas Motion rectangles without strokes map to Background nodes.
Text content, font, alignment, size, RGBA, layout, transforms and linear/Bézier
curves remain editable. Media are referenced, not copied or embedded.

Motion Null layers, parenting, nonidentity parent compensation, masks on Merge/Output, animated effect parameters, shared image branches, disconnected
nodes, processing after Merge, multiple Transform stages, nested foreground
compositions, linked image documents, other shapes, and clip-level effects or
non-neutral transforms are refused with an item-specific report. No partial
export or flattened replacement is produced. Node positions are regenerated;
graph topology is normalized to source/effects/transform branches and Merge.

Rectangle/Ellipse masks are editable on sources, individual color/Blur stages and
Transform. A definition can be shared between destinations; shared mask keys
use composition time, separately from layer-local transform keys. Motion exposes
these operations in [Masks Inspector](motion-editor/features/MASKS.md).

## Ownership and interchange v1/v2/v3

`libs/composition-exchange` owns the UTF-8 JSON contract and worker-side raster
and source-clock helpers. Application adapters own conversion and native editing
semantics. Shared code has no dependency on app models or UI. App-owned dialogs
schedule file preparation on workers; cancellation does not publish partial
files or replace the active document. Technical failures are logged by the app.

The extension is `.cscomp`; `format` is
`creative-suite.composition-interchange`. Readers accept versions 1, 2 and 3;
writers select v3 when Blur is present (including disabled Blur), otherwise v2
when masks are present or v1 without masks. Future v4, typed effects declared as
v1/v2 and mask data declared as v1 are rejected. Root fields are `name`, `width`, `height`, string-encoded
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
4096 layers, layer effect stacks to 1024 stages and layer curves to 100,000 keys.
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

V3 replaces each layer's legacy `colors`/`color_masks` fields with `effects`:
an ordered array of records with `type` (`color_adjustment` or `gaussian_blur`),
required Boolean `enabled`, required `parameters`, and optional string `mask`
(zero/absent means none). Color parameters are `brightness`, `contrast`,
`saturation`; Blur has `radius_pixels` (finite 0–100, default for creation 10).
Readers reject mixed old/new representations and unknown or animated effects.
V1/v2 color passes are normalized to this same internal ordered representation.
Legacy v2 empty `color_masks` arrays retain their original all-unmasked meaning.
The 1024-effect/layer and 16,384-stage budgets include both effect types.
Passes are never fused across Blur or masked stages. Repeated types, disabled
parameters, fractional radii and shared mask references remain editable.
Radius follows receiver pixels and is not rescaled with FPS; native source
profiles preserve the processing dimensions. See [Gaussian Blur](video-editor/features/GAUSSIAN_BLUR.md).

## Independent Motion scale compatibility

Motion v12 adds [Scale X/Y](motion-editor/features/SCALE_AXES.md), while `.cscomp`
remains v3. Export emits uniform Scale only when both axes have identical bases
and full scalar curves, including frame/value/easing. An absent Y channel follows
X and qualifies. Unequal bases or distinct curves are refused with layer IDs;
affine bindings are also refused, alongside existing incompatible hierarchy,
Anchor and effect animation. No implicit flattening or curve sampling occurs.
`creative-suite-motion-editor-scale` covers acceptance/refusal; backup tests use
the native v12 producer/consumer to preserve all new records after relocation.

## Native persistence and backups

Motion's native [Anchor Point](motion-editor/features/ANCHOR_POINT.md) is not
represented by `.cscomp` v1–v3. Export refuses any layer with a nonzero base
Anchor or nonempty Anchor curve, naming the layer. Default Anchor follows the
existing compatibility rules, including refusal of Nulls, parents and
nonidentity binding compensation; the exchange format remains v3.

CSP v31 and Motion v8 persist optional presentation records. Motion v8 adds a shared
mask collection and source/effect/transform associations; CSP v30 already stores
the equivalent graph masks. CSP v31 additionally persists the static
Gaussian Blur node; versions through 30 reject that node. Older supported
projects retain their existing rendering defaults; readers predating these
versions reject the newer files. Motion v7 introduced composition
name/duration and fractional font-size metadata. Autosave and recovery use the
same native serializers. Hub backups accept CSP 1–31 and Motion 1–14, retain
presentation, Scale Y/link/affine binding, Anchor bases/curves, hierarchy, compensation and effect-curve records, and rebase existing media/Input paths. CSP 32 and Motion 15
are rejected. Application versions and the existing linked-handoff v1 protocol
are unchanged.

## Regression and acceptance

`creative-suite-blur-interchange` adds typed ordered Blur stages, v3/v31,
CPU/GPU comparisons and editable roundtrips. `creative-suite-mask-interchange` adds stage masks, shared identity, mask curves,
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


### Gaussian Blur Windows Debug evidence — 2026-10-15

All 36 selected affected-consumer suites have passing results. The initial
175.08-second run passed 35 and exposed a test-fixture generation error in the
new full Preview/export case: its generation had advanced beyond subsequent
requests. The fixture now keeps the worker's monotonic-generation contract.
After preserving legacy v2 empty mask-binding arrays, the three interchange
suites and full Fusion-composition suite were rebuilt and passed together in
47.69 seconds. No tests were skipped in these selections.

Coverage includes shared effects/animation/composition/OpenGL, all three
interchange suites, Effects/Functions/Fusion UI and cable drops, main-window,
project/autosave/timeline/commands/graph/generated clips, CPU/GPU timeline and
export, Motion document/history/persistence/recovery/preview/GPU/export/startup/
UI, Hub real-reader backup/format and Image Editor GPU raster consumers.

The extracted CPU Blur matched an independent convolution reference byte for
byte, including premultiplied alpha, row padding, border clamping, fractional
rounding, maximum radius, range dispatch and cancellation. New Fusion/Motion
and CPU/GPU pre-encode comparisons observed maximum channel difference zero
(the acceptance tolerance remains two). Cases include ordered Color/Blur/Color,
rotation, translucent text, mismatched source sizes, both presentation profiles,
animated/shared masks, zero-dimension/inverted rectangles, video source windows,
cut/trim continuity and FPS preserving pixel radius. Resource exhaustion was
logged and recovered on CPU; releasing occupied textures let the same GPU
backend resume Blur successfully. Complete Video Preview/export frames matched
before encoding. Native OpenGL used NVIDIA GeForce GTX 1660 SUPER, driver
617.42, OpenGL 3.2 Core.

The direct canonical Debug builds were verified without copying application
executables. Modification times use UTC−03:00:

| Executable under the repository root | Modification time |
| --- | --- |
| `build/apps/video-editor/Debug/creative-suite-video-editor.exe` | 2026-10-15 10:27:33 |
| `build/apps/motion-editor/Debug/creative-suite-motion-editor.exe` | 2026-10-15 10:27:39 |
| `build/apps/hub/Debug/creative-suite-hub.exe` | 2026-10-15 10:14:35 |

The native File-menu, driver/font, small/medium/heavy interaction and macOS/Linux
checks in [Gaussian Blur](video-editor/features/GAUSSIAN_BLUR.md) remain pending.
Application versions and dependencies are unchanged. No installer, commit or
remote submission was generated for this delivery.

## Motion static blend-mode compatibility

Motion v13 persists static layer modes; `.cscomp` remains v3. Export refuses all
non-Normal layers with name/ID, including hidden/inactive layers, without flattening
or silently replacing a mode. Normal imports/exports retain existing rules.
Hub preserves the v13 field while rebasing existing media paths and rejects v15.
Motion blend and Hub backup producer/consumer tests cover these boundaries. Video
and Image gain no mode UI or native schema changes. See
[Blend modes](motion-editor/features/BLEND_MODES.md).

## Motion Track Matte boundary

Native Motion v14 and Hub backups retain optional matte source IDs/modes and eye
states while rebasing existing media fields. `.cscomp` remains v3 and refuses all
matte consumers by layer name/ID, including hidden/inactive layers. Imports have
no matte binding; other current refusal rules remain. Motion's new matte test
and Hub's native producer/consumer backup gate cover persistence and refusal.
Manual native restore/reopen and rendered comparisons remain pending.

## Motion Bezier path boundary

Motion native v15 owns freeform shape/mask geometry and complete-path animation. `.cscomp` remains v3: export refuses every Bezier shape or associated mask, including hidden/inactive layers, with layer names/IDs and mask IDs. Primitive mask records remain unchanged through explicit adapters; imports contain no Bezier paths.

Motion native v16 adds Bezier-only Trim Paths bases and scalar curves. The
existing `.cscomp` v3 Bezier refusal remains in force and additionally names
Trim Paths when configured; no interchange schema or Video/Image UI changes.

## Drop Shadow contract maintenance

Motion native v17 adds ordered Drop Shadow effects. `.cscomp` remains v3 and refuses every instance, including disabled or hidden/inactive usage, by layer name/ID and effect position. No shadow is flattened or added to Video/Image interchange.

## Glow contract maintenance

Motion native v18 adds ordered Glow effects with Outer, Inner and Both modes.
`.cscomp` remains v3 and refuses every Glow instance, including disabled or
hidden/inactive usage, by layer name/ID and effect position. Glow is not
flattened, omitted, or added to Video/Image interchange. See [Motion Glow](motion-editor/features/GLOW.md).

Motion export remains all-or-nothing. Every unsupported Bezier mask is reported,
including masks that are not referenced by a layer; export does not publish a
partial document.
