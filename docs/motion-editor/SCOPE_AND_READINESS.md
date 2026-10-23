# Motion Studio Scope and Readiness

[Gaussian Blur interchange](../COMPOSITION_INTERCHANGE.md) preserves mixed
ordered Color/Blur stages and shared masks through cscomp v3. Motion native v8
is unchanged; CSP advances to v31. Producer/consumer regression coverage is
registered as `creative-suite-blur-interchange`; native platform acceptance
remains separate.

[Shape-mask interchange](../COMPOSITION_INTERCHANGE.md) adds editable shared
Rectangle/Ellipse masks on sources, effects and Transform. Coverage and scalar
curve slicing are shared; the mask delivery introduced Motion v8; Gaussian Blur now advances CSP to v31.
Automated coverage: `creative-suite-mask-interchange`, shared composition/
OpenGL, native Motion preview/export/history/persistence, and Hub backup gates.
Native UI and platform acceptance remain separately pending.

[Editable composition interchange](../COMPOSITION_INTERCHANGE.md) adds independent `.cscomp` import/export and Motion native schema v8. Automated coverage: `creative-suite-composition-interchange` plus native document, preview/export and Hub backup suites. Manual/platform acceptance is tracked in the interchange guide.

Shared video decoding honors tagged YUV matrices and full/limited range before
RGBA8 composition. Decoder changes require the Motion preview/export consumer
gate and the shared tagged-color tests indexed in
[Video Editor regression coverage](../video-editor/validation/REGRESSION_TESTING.md).
This remains an SDR pipeline; HDR color-management acceptance is pending.
Shared display orientation also applies before composition; imported dimensions
and decoded pixels agree. Quarter-turn/reflection and oriented export regressions
are indexed in the same shared-decoder gate. Nonidentity display matrices use
explicit RGBA presentation while native transform delivery is pending.

Shared RGBA conversion pads swscale destination rows and compacts them in place
before delivery, preserving stride, byte count and pixel order. The shared
`creative-suite-video-encoder` Debug regression exercises unaligned and portrait
storage; `creative-suite-motion-editor-preview` and
`creative-suite-motion-editor-export` protect both Motion consumers.

Status: **approved 2D MVP product boundary; Windows technical acceptance
remains in progress**. A standalone
Motion Studio shell and in-memory composition/layer model have started using
provisional C++ and Qt 6. Its workspace creates in-memory canvases and has an
application-owned Media Pool connected to timeline rows for image and video
layers. The timeline supports clip insertion, movement, reordering, visibility,
removal, and duration edits; the selected layer's base transform and transform
keyframes are editable, with a Graph Editor for bounded cubic Bezier easing.
Native text, rectangle, and ellipse layers have content
inspectors and static content; their transforms and transform keyframes work
through the same timeline and preview path. Motion Studio rasterizes that
content with Qt painting on its preview or export worker before composition
with the shared CPU compositor by default or the experimental OpenGL adapter
when `CREATIVE_SUITE_MOTION_GPU_COMPOSITION=1`; failures fall back to CPU. The GPU
composition shader applies Color Adjustment-only stacks before blending;
stacks containing enabled Gaussian Blur use the shared ordered GPU effect path
when supported, and Glow uses its origin-aware OpenGL operation. Both retain
bounded scratch storage and CPU recovery; Vulkan explicitly falls back to CPU
for Glow. [Controlled Windows preview results](performance/GPU_PREVIEW_RESULTS_WINDOWS_2026-10-10.md)
record the reference workload separately from broader manual/platform acceptance.
Experimental preview now delivers shared GPU textures directly to the
Motion-owned viewer, with a bounded mailbox and RGBA/CPU recovery; see
[GPU_TEXTURE_PREVIEW.md](gpu/GPU_TEXTURE_PREVIEW.md). Offline export
uses the same opt-in path and reads back RGBA for the existing CPU encoder.
Manual Save, Save As, and Open use a versioned `.motion` document that includes
the Media Pool. The writer emits schema v18 and reads versions 1–18; migrations
and compatibility behavior are specified in [FORMAT.md](architecture/FORMAT.md). The
separate recovery wrapper remains at version 1 and contains a nested native
document. Undo/Redo and configurable autosave and recovery cover the
composition and Media Pool. Video export uses the shared FFmpeg encoder and
keeps Opaque as the default; its per-job Transparent (alpha) profile uses only
runtime-discovered software adapters. **File > Export PNG Sequence...** writes
transparent RGBA PNG frames into a new folder at the composition's canvas size
and native frame rate. Hidden visual layers extend the sequence; Null layers do
not. Output is staged and published only after every frame is ready. PNG files
do not carry FPS metadata, so importers must use the composition rate. Sequence
export does not modify the document or participate in linked publication.
Audio remains outside video export.
The composition workspace keeps
the Preview central and presents the Media Pool, Inspector, Timeline, and Graph
Editor as rearrangeable Qt dock panels. Timeline and Graph Editor share a
bottom tab group by default, with Timeline selected; the Graph Editor button
selects its tab, while View and the dock close control can hide or restore it.
Motion Studio stores the layout in application settings, independently of each
composition, migrates the previous uncustomized default arrangement to the
tabbed layout, and offers a command to restore the first-run arrangement.
The timeline UI is split into controls, ruler, and layer-track components;
`TimelineNavigator` coordinates navigation and playback state, while
`MainWindow` owns document edits and history.
This document records the agreed starting scope; it does not finalize a
renderer, programming language, native file extension, codec, or implementation
architecture.

[Layer parenting and Null controls](features/HIERARCHY.md) are an implemented
expansion: the Inspector and timeline [pick-whip](features/PARENT_PICK_WHIP.md)
select a parent, local curves remain intact, and
link/unlink/reparent/delete retain the pose at the current composition frame.
Nulls control transforms without producing pixels. Native v12, recovery and Hub
backups preserve the graph; independent `.cscomp` export refuses unsupported
hierarchies. Manual UI/platform and performance acceptance remain pending.
Pick-whip capture is presentation-only until release, with eligible row targets,
wheel/edge scrolling and context cancellation. Confirmation reuses the same
pose-preserving command; no native/recovery/shared format changes are involved.

[Animated Anchor Point](features/ANCHOR_POINT.md) is implemented for every layer,
including Nulls: local pixel X/Y fields, Transform tracks, Graph Editor curves,
and a dialog that preserves current-frame geometry by compensating Position in
one Undo/Redo. Hierarchy resolution includes Anchor, the Viewer marks the actual
pivot, and native v12/recovery/Hub backups preserve its curves. Versions 1–10
load with zero Anchor. `.cscomp` v3 explicitly refuses nondefault Anchor.
[Pan Behind](features/PAN_BEHIND.md) adds direct pivot dragging with a live
Inspector draft, release-only atomic commit and cancellation without history.
It reuses v12 Anchor/Position and the current affine hierarchy without new image
work. Viewer Position/Rotation/Scale handles are implemented; audio-to-Anchor remains planned;
manual visual/platform acceptance and benchmarks remain pending.

[Viewer Position drag](features/POSITION_DRAG.md) provides a separate movement
handle and live asynchronous content/Inspector preview, with release-only atomic
commit. Static axes update bases and animated axes can insert keys between keys.
Immutable scene snapshots and bounded gesture scheduling preserve document and
autosave isolation while retaining affine inheritance and existing caches.
Scale handles are described below; manual/native GPU/platform acceptance
and benchmarks remain pending.

[Viewer Rotation drag](features/ROTATION_DRAG.md) provides a separate curved-arrow
handle, full positive/negative turns, parent/binding-space angle mapping and live
content/Inspector preview. A single release edits static Rotation or inserts/edits
an animated key, including between keys. The pivot remains fixed; descendants
follow complete affine geometry. Document/history/autosave stay isolated until
commit. It reuses Position's scene/queue with unique session IDs. Native v12,
recovery v1 and interchange rules are unchanged. Manual/platform/native GPU
acceptance and benchmarks remain pending.

[Viewer Scale X/Y drag](features/SCALE_DRAG.md) adds X/Y/XY controls near the pivot,
linked proportions or free diagonal scaling, positive range clamping, live
content/Inspector preview and one release-only history operation. Static bases
and animated keys, including between keys, are applied atomically; independent
edits materialize implicit Y without resampling or exceeding the key budget.
It reuses the gesture scene/queue and existing rendering/cache contracts. Native
v12/recovery v1 and application versions are unchanged. Manual/platform/native
GPU acceptance and benchmarks remain pending.

[Independent animated Scale X/Y](features/SCALE_AXES.md) is implemented with
percentage fields, per-layer proportional linking, independent curves and a
uniform audio bake. Full affine inheritance preserves shear through parents and
Nulls, parenting and compensated Anchor edits. Native v12, recovery and Hub retain
Y/link/affine data; `.cscomp` v3 requires identical scale bases/curves. Shared
CPU/precise OpenGL implement the optional pixel matrix; Vulkan requests fallback.
Uniform legacy rendering remains exact. Manual/GPU/platform acceptance and
benchmarks remain pending.

[Static layer blend modes](features/BLEND_MODES.md) add Normal, Multiply, Screen,
Overlay, Add and Difference in the Inspector. New modes blend against transparent
accumulation before opaque black presentation/export by default; the optional
transparent export profile preserves the resulting alpha. Normal-only projects
keep their previous exact path. Native v13/recovery/Hub preserve the mode;
`.cscomp` v3 refuses non-Normal layers. Mode animation,
manual/platform/native GPU acceptance and benchmarks remain pending.

[Track Mattes](features/TRACK_MATTES.md) add fixed Alpha/Luma and inverted
source bindings with automatic source hiding, shared sources and iterative
cycle-checked chains. Existing geometry/effects/masks apply before alpha coverage
and final blending. Native v14/recovery/Hub preserve the relations; `.cscomp` v3
refuses bindings. Native presentation/platform/visual acceptance and benchmarks
remain pending.

[Pen and animated Bezier paths](features/BEZIER_PATHS.md) add explicit shape/mask
authoring, point/tangent editing, complete-path animation and raster origins
that preserve affine layer geometry beyond original bounds. Native v15 and Hub
retain paths; `.cscomp` v3 refuses them. Manual/platform/native presentation and
performance acceptance remain pending.

## Intended Users and Workflows

Motion Studio serves both motion-graphics creators making standalone animated
pieces and video editors creating animated visual elements for later use in a
video project. The first usable workflow is standalone: create an editable
composition, save and reopen it, preview its animation, and export a rendered
video. Direct linked editing with the Video Editor is a later integration
milestone.

The product targets Windows, macOS, and Linux. Current hands-on acceptance is
focused on Windows, where the maintainer reports running tests and a video
export. Video Editor stability remains a priority, and Motion Studio work can
proceed independently of Image Editor linked-image acceptance.

## First MVP Boundary

The first Motion Studio MVP is a 2D composition workflow with:

- ordered layers for text, vector shapes, still images, and video sources;
- a composition timeline, basic 2D transforms, keyframes, and editable property
  curves;
- ordered per-layer Gaussian Blur, Color Adjustment, Drop Shadow and Glow
  effects with editable parameters, enable/disable, reordering, keyframes where
  supported, and undoable stack operations;
- editable project save and reopen, plus rendered video export;
- document validation, actionable local error logging, undo/redo, autosave, and
  recovery appropriate to the supported workflow.

The original MVP does not include audio editing or mixing, animated masks, advanced
effect graphs, nested compositions, 3D, node-based workflows, or particles.
Video sources are visual layers; the baseline output remains opaque video without
audio. Transparent-background video export is an additive per-job option and is
documented below. Runtime codec and output-profile validation remain provisional.

## Approved Validation Profile

Use a 1920x1080 composition at 30 fps, up to 10 seconds and five layers, as the
reference workload. Exercise a representative mix of image, video, text, and
shape layers, transform keyframes, and the existing layer effects. Use the
maintainer's current PC as the reference test machine: AMD Ryzen 5 3600,
32 GB RAM, NVIDIA GeForce GTX 1660 SUPER with 6 GB VRAM, and Windows 11.
Record test conditions and results with each run. This is a benchmark profile,
not a maximum supported canvas, duration, layer count, or frame rate. Do not
claim numeric minimum hardware requirements or playback throughput until
measurements on this and additional systems have been recorded.

Audio-reactive 2D animation remains outside the approved MVP and is the first
planned expansion. At the maintainer's direction, implementation of its first
optional tool has started before the MVP's performance and Windows acceptance
is complete. This tool analyzes a local audio file and bakes its
frame-level RMS envelope into linear keyframes for one selected transform
property. The source audio is not persisted, and the tool does not add audio
playback, mixing, or audio export. MVP and release acceptance gates remain
open. Rectangle/Ellipse masks with geometry animation are now an additional
implemented expansion, separate from release acceptance. 3D, nested
compositions, node-based workflows, particles, advanced mask combinations/tracking and
advanced effect graphs remain later research.

### MVP acceptance

- A user can create a composition, arrange supported layers, animate supported
  properties, save it, and reopen it with its layer order, media references,
  timing, transforms, and keyframes intact.
- The timeline and preview show the evaluated composition at the selected
  frame, and a rendered video reflects the saved composition and its effects.
- Unsupported future document versions are reported without modifying the
  original file.
- Visual behavior and performance are validated on representative small,
  medium, and heavy compositions before release readiness is claimed.

## Video Editor Boundary

The standalone MVP does not depend on opening a Video Editor project or
maintaining a live connection between the applications. Its rendered video can
be used in a video workflow through the Video Editor's supported media import.

The linked workflow uses a separate editable Motion Studio document referenced
by the Video Editor. The supported layer subset, timing conversion, media
dependencies, saved revisions, refresh behavior, and missing-media recovery
are defined in [composition interchange](../COMPOSITION_INTERCHANGE.md).
Unsaved live previews between applications remain deferred and require a
separate demonstrated use case.

Motion Studio and Video Editor keep distinct native project formats and
application-specific document adapters. The Video Editor's `.csp` project
stores a supported reference to a Motion Studio document; it does not become
the Motion Studio document format. Handoff must preserve source media and
provide a recoverable reference if a linked document or dependency is missing.

## Existing Video Editor Capabilities and Ownership

| Capability | Current Video Editor location and behavior | Motion Studio readiness direction |
| --- | --- | --- |
| Media and decoding | `libs/media/` contains neutral metadata, an in-memory catalog, content-based still-image import through runtime Qt readers, the WebP/TIFF FFmpeg fallback, video probing/decoding, and RGBA frames. Single-frame GIF is accepted when Qt can read it; multi-frame images are rejected. | Motion Studio reuses the shared catalog and import processing in its own Media Pool. Imported items remain references to original files; the pool clears when replacing a composition. Its picker still uses the app-owned extension filters, while All Files can pass additional content to the shared importer. |
| Composition and preview | `apps/video-editor/src/playback/` and `src/rendering/`; worker-side CPU composition, shared OpenGL composition and texture preview paths. | Motion Studio rasterizes native text and shape content to transparent RGBA8 on its preview worker, then uses `libs/composition/` by default or the optional `libs/composition-opengl` adapter. This app-owned Qt renderer and experimental opt-in GPU path are provisional and do not establish a final renderer choice. |
| Transforms and animation | `apps/video-editor/src/timeline/`; normalized 2D position, scale, rotation, opacity, and linear per-clip keyframes. | Motion Studio uses `libs/animation/` to evaluate Linear and bounded cubic Bezier transform segments consistently in preview, playback, and export. Its Graph Editor, presets, property tracks, and inspector remain application-owned. |
| Timeline and history | Timeline model, commands, and bounded undo/redo are application-specific. | Motion Studio owns its composition timeline and editing history; share lower-level behavior only where a second real consumer uses the same contract. |
| Project persistence | `apps/video-editor/src/project/`; versioned `.csp` format currently at version 31, with migrations for supported earlier versions. | Keep a separate versioned Motion Studio native document and adapter. Do not reuse `.csp` as the native composition format. |
| Autosave and recovery | `src/project/autosave_manager.*` and application coordination provide autosave snapshots and recovery. | Motion Studio implements an application-owned version 1 recovery wrapper around validated `.motion` v1-v8 data, including the Media Pool. Shared recovery services remain deferred until both document owners have stable common requirements. |
| Diagnostics | Structured local logging is implemented in `libs/diagnostics/`; each application selects its own log directory. | Reuse the service with a Motion Studio-specific application identifier and context. |

### Candidate shared capabilities

The approved reuse direction and its current implementation are recorded in
[REUSE_PLAN.md](architecture/REUSE_PLAN.md). The transform/keyframe evaluator and raster
frame compositor now have focused, Qt-independent CMake targets under
`libs/`. Motion Studio uses the shared animation types in its document model
and evaluator, and the raster compositor in its preview, with model and
preview regression coverage. The Video Editor uses compatibility headers and retains
timeline-specific keyframe split and trim operations. Shared contracts remain
provisional until both consumers have appropriate regression coverage.

The RGBA frame model, FFmpeg playback session, media catalog/import processor,
video and still-image decoders, and logger are focused shared targets. Each
application compiles and packages the targets it needs; it does not load or
launch another editor. The Video Editor supplies preview-metric recording
through an observer adapter. Motion Studio has its own import dialog, worker
orchestration, Media Pool UI, bins presentation, details inspector, and
composition lifecycle. The `.motion` document persists the complete Media Pool
catalog and keeps media files as external references; thumbnails and decoded
frames are rebuilt on open. Image Editor link references remain in the Video Editor's
application-side project adapter rather than the shared media catalog.

Any composition contract must define coordinate units and transforms, pixel
format and color/alpha assumptions, resource lifetime and thread requirements,
error context, and serialization compatibility when applicable. The current
frame type defines RGBA8 storage and stride but no color space. Keyframe and
curve evaluation remains a separate capability from composition. The shared
animation library validates and evaluates outgoing Linear or bounded cubic
Bezier easing. Motion Studio's Graph Editor owns property-curve display,
segment selection, draggable handles, easing presets, and Undo/Redo grouping;
shared evaluation drives preview, playback, and export.

### Video Editor playback and preview path audit

This audit follows the existing Video Editor implementation and its automated
tests. It records source-level behavior; it does not claim that OpenGL works on
every target GPU or that the path has met performance targets.

| Stage | Existing implementation and evidence | Motion Studio direction |
| --- | --- | --- |
| Build dependencies | `apps/video-editor/CMakeLists.txt` requires Qt 6 Core, Widgets, OpenGL, and OpenGLWidgets plus FFmpeg AVCODEC, AVFORMAT, AVUTIL, SWRESAMPLE, and SWSCALE. Qt Multimedia is optional and used for audio output. | This describes the Video Editor build, not a final Motion Studio stack. Keep renderer and dependency decisions open until platform, license, and performance validation. |
| Video decode and frames | `libs/media/src/video_playback.cpp` uses FFmpeg to decode video and convert frames to owned RGBA8 storage. `VideoPlaybackSession` is independent of Qt and can report optional decode timings through an observer. | `creative-suite::video-media` and `creative-suite::media-frame` are candidates for video layers, subject to Motion Studio consumer regression coverage. |
| Timeline playback and CPU composition | `apps/video-editor/src/playback/playback_worker.cpp` selects and decodes timeline layers, rasterizes text, and calls the CPU `FrameCompositor`. The compositor returns the completed raster frame before preview presentation. | The shared raster compositor and animation evaluator are reusable candidates. Timeline scheduling, project data, and playback controls remain application-specific. |
| Still images and text | `libs/media/src/still_image_decoder.cpp` uses Qt `QImageReader` for static raster images and returns the shared RGBA frame type. `apps/video-editor/src/rendering/text_renderer.cpp` rasterizes Video Editor text through Qt `QPainter`. | Motion Studio uses the shared still-image decoder for import previews and its own Qt rasterizer for multiline text, rectangles, and ellipses. It does not depend on the Video Editor text renderer or timeline types. |
| Preview presentation | `OpenGLPreviewSurface` requests an OpenGL 3.2 Core context, uploads the already-composed RGBA frame as a texture, and draws it with a shader. GPU work presents the final frame; layer composition is CPU-side. `PreviewWidget` can use a CPU `QImage`/`QPixmap` path when GPU preview is disabled or fails. | Treat this Qt/OpenGL widget as Video Editor UI. It does not provide GPU layer composition or establish a Motion Studio renderer. |
| Video encoding and output capabilities | The Video Editor previously owned FFmpeg container/codec discovery and its FFmpeg encoder wrapper alongside timeline assembly and audio rendering. The shared `libs/media/` encoder handles RGBA frame encoding, optional stereo audio, container/codec discovery, alpha-adapter discovery, and atomic publication support. | Motion Studio uses the shared encoder and capability list while its own worker schedules frames from an immutable composition snapshot. Opaque video remains the default; an optional transparent profile uses available software FFV1/Matroska, ProRes 4444/MOV, or VP9/WebM adapters. Audio, project scheduling, settings UI, progress, cancellation, and output verification remain Motion-owned. |

Existing regression tests cover the shared animation and composition
libraries, Video Editor transform and text composition, playback-worker
behavior, preview delivery, and preview metrics. The preview-widget test sets
`CREATIVE_SUITE_DISABLE_GPU_PREVIEW=1` and uses Qt's offscreen platform, so it
exercises the CPU preview path and does not validate OpenGL context creation,
GPU presentation, or driver support. The standalone FFmpeg playback test's
valid-video decode and seek coverage is conditional on a reference-video
argument; that prototype fixture is excluded from this audit run. Thus the
successful decode path is mapped from source here, while GPU runtime behavior
and measured Windows performance remain open validation work.

For Motion Studio, the existing neutral video decoder, shared media catalog,
still-image decoder, RGBA frame model, transform evaluator, and raster
compositor are reused directly. The Qt preview, Motion Studio timeline worker,
text and shape rasterizer, and application-specific project adapters remain
application-owned. Motion Studio has its own composition/layer model, canvas viewer,
timeline rows linked to Media Pool sources, and manual versioned save/open
format. Image, video, text, and shape content render in the preview with
shared Linear or bounded cubic Bezier transform-keyframe evaluation. The
Graph Editor selects a transform property and segment, edits Bezier handles,
and applies Linear, Ease In, Ease Out, or Ease In/Out presets. Graph selection
and panel visibility are not saved. Text and shape content is static;
fonts are resolved by family name on the current system and are not embedded.
**File > Export Video...** renders from frame 0 through the furthest layer end,
including hidden layers for duration, and converts between exact rational frame
rates. Opaque output remains the default and writes blank frames as opaque black.
The optional **Transparent (alpha)** profile composes over alpha zero and keeps
blank frames transparent. The settings dialog discovers containers and
compatible encoders, offering composition, common, or custom dimensions, output
frame rate, and codec-specific quality controls. Supported software alpha
adapters are Matroska/FFV1 lossless, MOV/ProRes 4444 or 4444 XQ with 16-bit alpha,
and WebM/VP9 with CRF 0-63 (default 32), subject to the installed FFmpeg
capabilities. Hardware codecs are excluded. Settings apply to the current job
and are not persisted. Export jobs show progress, support cancellation, verify
the staged file, and publish it only after successful completion; cancellation
and failure preserve an existing destination. Audio is not exported. Linked
Motion-to-Video Editor publication remains opaque. PNG sequence export always
preserves alpha, uses composition size and FPS, and creates a new numbered
folder without a manifest. Gaussian Blur, Color Adjustment,
Drop Shadow and Glow stacks run before each layer's transforms in the shared
Motion frame renderer, so preview, playback, and export evaluate the same effects. Blur
uses a bounded three-pass box approximation to a Gaussian with premultiplied
alpha. Its rolling sums use integer arithmetic with byte-identical rounding,
and the vertical pass traverses 32-pixel-wide tiles. The test suite compares
the optimized blur against the previous implementation for radii 0, 1, 10,
and 100, including padded rows and transparent edges. Neutral Color Adjustment
(`0` brightness, `100%` contrast, `100%` saturation) bypasses the pixel loop
and preserves all RGBA bytes exactly while still appearing in effect timing
records. Non-neutral color adjustment processes stored RGBA8 values without
color-space conversion and preserves alpha.
Preview, playback, and export use one reusable Motion Studio worker pool for
the independent blur stages. It parallelizes premultiplication and final
conversion by rows, horizontal passes by rows, and vertical passes by 32-pixel
tiles, while synchronizing between passes. The pool is capped at eight threads,
reserves one reported logical core when possible, and falls back to one worker
if the core count is unavailable. Automated tests compare single- and
multi-worker output byte-for-byte, exercise simultaneous calls and cancellation,
and check that each application of the effect still produces one timing record.
The blur duration is wall-clock time, including pool scheduling and pass barriers.
Autosave and recovery use a separate versioned wrapper:
dirty compositions and the full Media Pool are snapshotted atomically, untitled
work is isolated by session, and recovery restores through staged media loading.
The native `.motion` writer emits schema v18 and reads versions 1–18, including
layer-local keyframes and Cubic Bezier easing on Gaussian Blur and Color
Adjustment parameters. Migration
details are maintained in [FORMAT.md](architecture/FORMAT.md); the recovery wrapper remains
at version 1 and contains the validated native document. Other interpolation
modes, overshoot-capable curves, and advanced effects remain open. Final
encoder/profile selection and output color handling remain Windows technical
validation work. The one-hour ruler range controls
navigation only and does not define the composition's duration. See
[ROADMAP.md](ROADMAP.md) and [REUSE_PLAN.md](architecture/REUSE_PLAN.md) for current
implementation details and provisional shared API contracts.

## Motion Studio Performance Diagnostics

Motion Studio has a persisted **Settings > General...** option,
**Enable preview performance metrics**, enabled by default. When enabled,
preview activity is aggregated and logged once per second through the existing
Motion Studio diagnostics logger. Empty intervals are silent. Records include a
schema version; best-effort process CPU, working-set/private memory, and system
memory; canvas dimensions and exact frame-rate numerator/denominator; layer and
effect counts; request, rendered-frame, coalesced-request, and stale-result
counters; and count, average, maximum, p95, and p99 durations for video decode,
text/shape rasterization, effects, CPU composition, total frame render, and
request-to-viewer-paint latency. Schema v5 reports actual application counts
and separate timing summaries for Gaussian Blur and Color Adjustment, plus the
effective effect-worker count. Schema v7 introduced opt-in GPU composition,
Color Adjustment, and Gaussian Blur effect counts, fallbacks, failures,
uploaded/readback bytes, and upload/draw/effect/readback timings,
as well as actual timestamp-seek outcomes
and duration, playback forward-decode attempts/completions/fallbacks, and
discarded intermediate frames. Seek and forward-decode durations are
submeasurements of total decode time and should not be added to it. The
configured effect count remains distinct
from per-interval application counts. A bounded sample window is used for
percentile estimates. Current schema v9 adds separate Glow effect timing and
OpenGL effect/fallback/failure counters. Schema v8 distinguishes texture/RGBA
mailbox delivery and successful presentation, recovery, bounded pool peaks and
backpressure, and producer/viewer fence submission timing. Direct preview records
zero readback and viewer-upload bytes; see [its contract](gpu/GPU_TEXTURE_PREVIEW.md).

The cross-platform CPU and memory sampler is owned by the shared
`creative-suite::system-monitor` library and is consumed by both editors. The
Video Editor continues to use its existing preference and log schema through
compatibility headers. Motion Studio keeps its stage aggregation, one-second
activity policy, and export summaries application-owned. Export jobs log one
schema-3 summary on completion, failure, or cancellation with elapsed time,
rendered frames, render and encode/write timings, output dimensions/rate,
achieved frames per second, and realtime factor. The summary also records the
GPU request and used backend, GPU/fallback frame counts, failures,
upload/readback bytes, upload/draw/readback averages, async PBO submissions and
collections, CPU-side fence-wait/copy timings, staging peaks, and encoder queue
metrics. Cancellation is an informational outcome; technical export failures
continue through the existing error log.

Performance records do not include project/media paths, layer names, or text
content. GPU utilization and device memory are not sampled. These diagnostics
help inspect a running session; they are not performance benchmarks.
An initial Windows preview/export measurement is documented in
[PERFORMANCE_RESULTS_WINDOWS_2026-10-07.md](performance/PERFORMANCE_RESULTS_WINDOWS_2026-10-07.md).
It identifies Gaussian Blur as the main preview bottleneck in a 1080p, two-layer
workload. Representative small, medium, and heavy compositions, startup,
seek/paint latency, memory limits, and the approved five-layer benchmark still
require measured validation.
On Windows, toggle the environment variable, seek and play compositions, export
a job, then use **Help > Open Log Folder** to inspect preview samples and
schema-3 export summaries. The previous 54.96 s synchronous-GPU export is the
baseline for the asynchronous-readback change. Use the same saved 3,405-frame
composition and settings for three post-change GPU runs. Compare median
end-to-end FPS, async readback and queue metrics, backend/fallback counts,
transfer bytes, and output parity. The target is a median of 52.21 s or less;
GPU mode remains opt-in until the improvement repeats.
Native macOS and Linux driver validation is pending.
For a controlled Windows comparison, use the same 1920 × 1080, 60 fps
composition with radius-10 Gaussian Blur and neutral Color Adjustment in
automatic mode and with `CREATIVE_SUITE_MOTION_EFFECT_WORKERS` set to `1`, `2`,
`4`, and `8`. Restart the app for each setting, allow playback to warm up, and
collect three 10-second runs. Compare weighted blur and total frame-render
averages, decode time, rendered frames, coalesced requests, and CPU use. The
selected count appears in each active preview sample. The temporary override
is diagnostic-only; invalid values fall back to the automatic policy. Keep the
automatic default until playback-wide results show a repeatable improvement,
and confirm output remains byte-identical. Motion Studio playback uses forward
decoding when the decoder is already positioned two to eight source frames
before its target; one-frame advances use the existing direct next-frame path.
Backward seeks, larger gaps, and interactive scrubbing use timestamp seeking.
An unsuccessful forward decode falls back to the existing seek path unless it
was cancelled. Export is unchanged. Validate with repeated playback runs and
compare seek/decode counters, decode and frame-render durations, displayed
frames, coalesced requests, and CPU use; confirm scrubbing and export output are
unchanged.

## Native Format and Compatibility Policy

- Motion Studio uses a native document format distinct from the Video Editor's
  `.csp` project format and the Image Editor's `.cimg` format.
- The current implementation writes version 18 JSON documents and reads
  versions 1 through 18, identified by
  `creative-suite.motion-studio`. Its provisional extension is `.motion`; the
  field layout, path rules, exact limits, and atomic-save behavior are described
  in [FORMAT.md](architecture/FORMAT.md).
- Older text, shape, keyframe, effect, and linked-image records migrate as
  specified in [FORMAT.md](architecture/FORMAT.md); saving an older document writes version
  18.
  An unsupported future schema version is
  rejected without replacing the current composition or overwriting or
  normalizing the original file.
- Cross-application references identify a Motion Studio document and its saved
  revision through a separately documented compatibility contract. Native
  document versioning and the Video Editor reference contract are distinct.
- The supported cross-application reference and editable interchange behavior
  are implemented for the documented subset. See
  [composition interchange](../COMPOSITION_INTERCHANGE.md) for its contract,
  compatibility behavior, and pending acceptance.

## Deferred Technical Decisions

The source-level Qt 6, FFmpeg, CPU-composition, and OpenGL-presentation audit is
recorded above. Remaining Milestone 1 work is to revalidate applicable paths on
Windows; measure startup, memory, timeline/seek response, preview latency, and
rendering for representative small, medium, and heavy compositions; and record
dependency licenses, output-profile and codec findings, measurable resource
and responsiveness targets, and alternatives. No final language or renderer
choice is made by this document.
Manual Windows validation of autosave and recovery remains pending: verify an
untitled recovery after restart, saved-project recovery and Ignore behavior,
missing media, and snapshot management in Settings. See the [roadmap](ROADMAP.md)
for the complete manual checklist.

[Animated Trim Paths](features/TRIM_PATHS.md) adds stroke-only Start/End/Offset
channels to Bezier shapes, circular ranges on open/closed paths and local-length
cubic cuts with stable raster origins. Native v16/recovery v1 and Hub preserve
the operator; interchange refuses it. Manual visual/platform/native presentation
acceptance and benchmarks remain pending.

[Animated Drop Shadow](features/DROP_SHADOW.md) adds ordered RGB shadows with four
local scalar curves, expanded origins and stable fitting/mask references. Native
v17 introduced the effect; the current v18/recovery v1 and Hub preserve it.
Manual visual/platform/native Viewer presentation acceptance and benchmarks
remain pending.

[Glow](features/GLOW.md) adds ordered Outer, Inner and Both modes with static RGB,
keyframed opacity/radius, source-space bounds, masks, CPU/OpenGL processing and
Vulkan CPU fallback. Native v18 and Hub preserve Glow; `.cscomp` v3 refuses it
with a diagnostic. Regression sources are registered; execution and manual
visual/platform/native presentation acceptance remain pending.
