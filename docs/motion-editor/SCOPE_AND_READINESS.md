# Motion Studio Scope and Readiness

Status: **provisional product scope; Milestone 0 complete**. A standalone
Motion Studio shell and in-memory composition/layer model have started using
provisional C++ and Qt 6. Its workspace creates in-memory canvases and has an
application-owned Media Pool connected to timeline rows for image and video
layers. The timeline supports clip insertion, movement, reordering, visibility,
removal, and duration edits; the selected layer's base transform and transform
keyframes are editable, with a Graph Editor for bounded cubic Bezier easing.
Native text, rectangle, and ellipse layers have content
inspectors and static content; their transforms and transform keyframes work
through the same timeline and preview path. Motion Studio rasterizes that
content with Qt painting on its preview worker before using the shared CPU
compositor. Per-layer Gaussian Blur and Color Adjustment are applied on that
same CPU worker before transforms, and the same renderer is used for playback
and video export. Manual Save, Save As, and Open use a versioned `.motion`
document that includes the Media Pool. The writer emits v4, reads v1-v3 with
empty effect stacks, reads v1 and v2 with old keyframes migrated as Linear,
and reads v1 text or shape records with default content. The recovery wrapper
remains at v1 and accepts nested documents through v4. Undo/Redo and configurable autosave and recovery cover the
composition and Media Pool. A first-pass video export uses a shared FFmpeg
encoder, with opaque video-only output and per-job settings; audio and alpha
remain outside the implemented export path. The composition workspace keeps
the Preview central and presents the Media Pool, Inspector, Timeline, and Graph
Editor as rearrangeable Qt dock panels. Timeline and Graph Editor share a
bottom tab group by default, with Timeline selected; the Graph Editor button
selects its tab, while View and the dock close control can hide or restore it.
Motion Studio stores the layout in application settings, independently of each
composition, migrates the previous uncustomized default arrangement to the
tabbed layout, and offers a command to restore the first-run arrangement.
This document records the agreed starting scope; it does not finalize a
renderer, programming language, native file extension, codec, or implementation
architecture.

## Intended Users and Workflows

Motion Studio serves both motion-graphics creators making standalone animated
pieces and video editors creating animated visual elements for later use in a
video project. The first usable workflow is standalone: create an editable
composition, save and reopen it, preview its animation, and export a rendered
video. Direct linked editing with the Video Editor is a later integration
milestone.

The application targets Windows, macOS, and Linux. Video Editor stability
remains a priority, and Motion Studio work can proceed independently of Image
Editor linked-image acceptance.

## First MVP Boundary

The first Motion Studio MVP is a 2D composition workflow with:

- ordered layers for text, vector shapes, still images, and video sources;
- a composition timeline, basic 2D transforms, keyframes, and editable property
  curves;
- ordered per-layer Gaussian Blur and Color Adjustment effects with editable
  parameters, enable/disable, reordering, and undoable stack operations;
- editable project save and reopen, plus rendered video export;
- document validation, actionable local error logging, undo/redo, autosave, and
  recovery appropriate to the supported workflow.

The MVP does not include audio editing or mixing, animated masks, advanced
effect graphs, nested compositions, 3D, node-based workflows, or particles. Video
sources are visual layers; the MVP does not promise audio playback or audio in
the export. Transparent export is not a requirement established by this
scope. Codecs, output profiles, and any later alpha-channel support remain for
technical validation.

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

The later linked workflow is a separate editable Motion Studio document
referenced by the Video Editor. It should support opening or creating a
composition from a supported timeline clip and creating one from a Media Pool
item. Before insertion, the contract must define the composition canvas, frame
rate, duration, and media dependency behavior. Saving a supported composition
publishes a new saved revision; the Video Editor refreshes the corresponding
output and invalidates dependent render-cache entries. Unsaved live previews
between applications are deferred and require a separate demonstrated use
case.

Motion Studio and Video Editor keep distinct native project formats and
application-specific document adapters. The Video Editor's `.csp` project
stores a supported reference to a Motion Studio document; it does not become
the Motion Studio document format. Handoff must preserve source media and
provide a recoverable reference if a linked document or dependency is missing.

## Existing Video Editor Capabilities and Ownership

| Capability | Current Video Editor location and behavior | Motion Studio readiness direction |
| --- | --- | --- |
| Media and decoding | `libs/media/` contains neutral metadata, an in-memory catalog, import processing, FFmpeg video probing/decoding, Qt-backed still-image decoding, and RGBA frames. Each editor owns its pool UI, worker lifecycle, and project/document integration. | Motion Studio already reuses the shared catalog and import processing in its own Media Pool. Imported items are in-memory and remain references to original files; the pool clears when replacing a composition. GIF import is unsupported. |
| Composition and preview | `apps/video-editor/src/playback/` and `src/rendering/`; worker-side CPU composition, frame compositor, and a provisional OpenGL preview surface. | Motion Studio rasterizes native text and shape content to transparent RGBA8 on its own preview worker, then uses `libs/composition/` for CPU composition. This app-owned Qt renderer is provisional and does not establish a final renderer choice or GPU layer composition. |
| Transforms and animation | `apps/video-editor/src/timeline/`; normalized 2D position, scale, rotation, opacity, and linear per-clip keyframes. | Motion Studio uses `libs/animation/` to evaluate Linear and bounded cubic Bezier transform segments consistently in preview, playback, and export. Its Graph Editor, presets, property tracks, and inspector remain application-owned. |
| Timeline and history | Timeline model, commands, and bounded undo/redo are application-specific. | Motion Studio owns its composition timeline and editing history; share lower-level behavior only where a second real consumer uses the same contract. |
| Project persistence | `apps/video-editor/src/project/`; versioned `.csp` format currently at version 12, with migrations for supported earlier versions. | Keep a separate versioned Motion Studio native document and adapter. Do not reuse `.csp` as the native composition format. |
| Autosave and recovery | `src/project/autosave_manager.*` and application coordination provide autosave snapshots and recovery. | Motion Studio implements an application-owned version 1 recovery wrapper around validated `.motion` v1-v4 data, including effect stacks. Shared recovery services remain deferred until both document owners have stable common requirements. |
| Diagnostics | Structured local logging is implemented in `libs/diagnostics/`; each application selects its own log directory. | Reuse the service with a Motion Studio-specific application identifier and context. |

### Candidate shared capabilities

The approved reuse direction and its current implementation are recorded in
[REUSE_PLAN.md](REUSE_PLAN.md). The transform/keyframe evaluator and raster
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
| Video encoding and output capabilities | The Video Editor previously owned FFmpeg container/codec discovery and its FFmpeg encoder wrapper alongside timeline assembly and audio rendering. The shared `libs/media/` encoder now handles RGBA frame encoding, optional stereo audio, container/codec discovery, and atomic publication support. | Motion Studio uses the shared encoder and capability list while its own worker schedules frames from an immutable composition snapshot. It currently exports opaque video without audio; project scheduling, settings UI, progress, cancellation, and output verification remain Motion-owned. |

Existing regression tests cover the shared animation and composition
libraries, Video Editor transform and text composition, playback-worker
behavior, preview delivery, and preview metrics. The preview-widget test sets
`CREATIVE_SUITE_DISABLE_GPU_PREVIEW=1` and uses Qt's offscreen platform, so it
exercises the CPU preview path and does not validate OpenGL context creation,
GPU presentation, or driver support. The standalone FFmpeg playback test's
valid-video decode and seek coverage is conditional on a reference-video
argument; that prototype fixture is excluded from this audit run. Thus the
successful decode path is mapped from source here, while GPU runtime behavior,
cross-platform support, and measured performance remain open validation work.

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
including hidden layers for duration, converts between exact rational frame
rates, and writes blank frames as opaque black. The settings dialog discovers
available containers and compatible video encoders and offers composition,
common, or custom dimensions, output frame rate, and quality/bitrate controls.
Settings apply to the current job and are not persisted. Export jobs show
progress, support cancellation, verify the staged file, and publish it only
after successful completion; cancellation and failure preserve an existing
destination. Audio and alpha are not exported. Static Gaussian Blur and Color
Adjustment stacks run before each layer's transforms in the shared Motion frame
renderer, so the preview, playback, and export evaluate the same effects. Blur
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
The native `.motion` writer emits v4, reads v1-v3 with empty effect stacks,
reads v1/v2 keyframes as Linear, and migrates v1 text and shape records using
documented defaults; the recovery wrapper remains at version 1 and accepts
nested documents through v4. Other interpolation modes, overshoot-capable
curves, and advanced effects remain open. Final
encoder/profile selection, output color handling, and cross-platform export
behavior remain technical validation work. The one-hour ruler range controls
navigation only and does not define the composition's duration. See
[ROADMAP.md](ROADMAP.md) and [REUSE_PLAN.md](REUSE_PLAN.md) for current
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
request-to-viewer-paint latency. Schema v4 reports actual application counts
and separate timing summaries for Gaussian Blur and Color Adjustment, plus the
effective effect-worker count. It also reports actual timestamp-seek outcomes
and duration, playback forward-decode attempts/completions/fallbacks, and
discarded intermediate frames. Seek and forward-decode durations are
submeasurements of total decode time and should not be added to it. The
configured effect count remains distinct
from per-interval application counts. A bounded sample window is used for
percentile estimates.

The cross-platform CPU and memory sampler is owned by the shared
`creative-suite::system-monitor` library and is consumed by both editors. The
Video Editor continues to use its existing preference and log schema through
compatibility headers. Motion Studio keeps its stage aggregation, one-second
activity policy, and export summaries application-owned. Export jobs log one
summary on completion, failure, or cancellation with elapsed time, rendered
frames, render and encode/write timings, output dimensions/rate, achieved
frames per second, and realtime factor. Cancellation is an informational
outcome; technical export failures continue through the existing error log.

Performance records do not include project/media paths, layer names, or text
content. GPU metrics are not collected. These diagnostics help inspect a running
session; they are not performance benchmarks. Representative small, medium,
and heavy compositions, cross-platform resource behavior, startup, seek/paint
latency, memory limits, and export throughput still require measured validation.
On Windows, toggle the option, seek and play compositions, export a job, then
use **Help > Open Log Folder** to inspect samples and job summaries.
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
- The current implementation writes version 4 JSON documents and reads
  versions 1, 2, 3, and 4, identified by
  `creative-suite.motion-studio`. Its provisional extension is `.motion`; the
  field layout, path rules, exact limits, and atomic-save behavior are described
  in [FORMAT.md](FORMAT.md).
- Version 1 text and shape records migrate to documented defaults when opened;
  v1-v3 load with empty effect stacks, v1/v2 keyframes load as Linear, and
  saving older documents writes version 4.
  An unsupported future schema version is
  rejected without replacing the current composition or overwriting or
  normalizing the original file.
- Cross-application references identify a Motion Studio document and its saved
  revision through a separately documented compatibility contract. Native
  document versioning and the Video Editor reference contract are distinct.
- Cross-application reference representation remains undecided until the
  handoff contract is implemented and validated.

## Deferred Technical Decisions

The source-level Qt 6, FFmpeg, CPU-composition, and OpenGL-presentation audit is
recorded above. Remaining Milestone 1 work is to revalidate applicable paths
across Windows, macOS, and Linux; measure startup, memory, timeline/seek
response, preview latency, and rendering for representative small, medium,
and heavy compositions; and record dependency licenses, output-profile and
codec findings, measurable resource and responsiveness targets, and
alternatives. No final language or renderer choice is made by this document.
Manual Windows validation of autosave and recovery remains pending: verify an
untitled recovery after restart, saved-project recovery and Ignore behavior,
missing media, and snapshot management in Settings. See the [roadmap](ROADMAP.md)
for the complete manual checklist.
