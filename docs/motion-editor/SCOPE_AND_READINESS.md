# Motion Studio Scope and Readiness

Status: **provisional product scope; Milestone 0 complete**. A standalone
Motion Studio shell and in-memory composition/layer model have started using
provisional C++ and Qt 6. This document records the agreed starting scope; it
does not finalize a renderer, programming language, native file extension,
codec, or implementation architecture.

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
- simple effects, with the initial effect set selected during technical
  validation;
- editable project save and reopen, plus rendered video export;
- document validation, actionable local error logging, undo/redo, autosave, and
  recovery appropriate to the supported workflow.

The MVP does not include audio editing or mixing, animated masks, chained
effects, nested compositions, 3D, node-based workflows, or particles. Video
sources are visual layers; the MVP does not promise audio playback or audio in
the export. Transparent export is not a requirement established by this
scope. Codecs, output profiles, and any later alpha-channel support remain for
technical validation.

### MVP acceptance

- A user can create a composition, arrange supported layers, animate supported
  properties, save it, and reopen it with its layer order, media references,
  timing, transforms, and keyframes intact.
- The timeline and preview show the evaluated composition at the selected
  frame, and a rendered video reflects the saved composition.
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
| Media and decoding | `apps/video-editor/src/media/`; FFmpeg video playback is implemented in `libs/media/` behind a neutral observer. Raster-image decoding remains a Video Editor service. | Reuse FFmpeg video decoding and the RGBA frame model. Motion Studio still needs its own raster-image decoding path or a separately validated shared one; keep import UI, media organization, and composition timing local to each application. |
| Composition and preview | `apps/video-editor/src/playback/` and `src/rendering/`; worker-side CPU composition, frame compositor, and a provisional OpenGL preview surface. | The CPU raster compositor is in `libs/composition/`; document coordinates, pixel format, alpha, lifetime, thread, and error behavior before treating its API as stable. GPU per-layer composition is not an existing capability. |
| Transforms and animation | `apps/video-editor/src/timeline/`; normalized 2D position, scale, rotation, opacity, and linear per-clip keyframes. | The transform evaluator is in `libs/animation/`; Motion Studio owns curve editing and property controls. |
| Timeline and history | Timeline model, commands, and bounded undo/redo are application-specific. | Motion Studio owns its composition timeline and editing history; share lower-level behavior only where a second real consumer uses the same contract. |
| Project persistence | `apps/video-editor/src/project/`; versioned `.csp` format currently at version 12, with migrations for supported earlier versions. | Keep a separate versioned Motion Studio native document and adapter. Do not reuse `.csp` as the native composition format. |
| Autosave and recovery | `src/project/autosave_manager.*` and application coordination provide autosave snapshots and recovery. | Motion Studio must provide equivalent workflow-specific recovery; extract lower-level services only after ownership and boundary tests are clear. |
| Diagnostics | Structured local logging is implemented in `libs/diagnostics/`; each application selects its own log directory. | Reuse the service with a Motion Studio-specific application identifier and context. |

### Candidate shared capabilities

The approved reuse direction and its current implementation are recorded in
[REUSE_PLAN.md](REUSE_PLAN.md). The transform/keyframe evaluator and raster
frame compositor now have focused, Qt-independent CMake targets under
`libs/`. The initial Motion Studio document model consumes the shared animation
types and has model-level regression coverage; the raster compositor is not
yet consumed. The Video Editor uses compatibility headers and retains
timeline-specific keyframe split and trim operations. Shared contracts remain
provisional until both consumers have appropriate regression coverage.

The RGBA frame model, FFmpeg playback session, and logger are now focused
shared targets. Each application links the targets it needs into its own build
and package; it does not load or launch another editor. The Video Editor
supplies preview-metric recording through an observer adapter; Motion Studio
can use the decoder without that observer. UI, timeline workflows, project
adapters, import/export controllers, and application history remain local to
each application. Motion Studio currently links the animation library only;
other shared capabilities will be added when its implemented workflows use
them.

Any composition contract must define coordinate units and transforms, pixel
format and color/alpha assumptions, resource lifetime and thread requirements,
error context, and serialization compatibility when applicable. The current
frame type defines RGBA8 storage and stride but no color space. Keyframe and
curve evaluation remains a separate capability from composition.

### Video Editor playback and preview path audit

This audit follows the existing Video Editor implementation and its automated
tests. It records source-level behavior; it does not claim that OpenGL works on
every target GPU or that the path has met performance targets.

| Stage | Existing implementation and evidence | Motion Studio direction |
| --- | --- | --- |
| Build dependencies | `apps/video-editor/CMakeLists.txt` requires Qt 6 Core, Widgets, OpenGL, and OpenGLWidgets plus FFmpeg AVCODEC, AVFORMAT, AVUTIL, SWRESAMPLE, and SWSCALE. Qt Multimedia is optional and used for audio output. | This describes the Video Editor build, not a final Motion Studio stack. Keep renderer and dependency decisions open until platform, license, and performance validation. |
| Video decode and frames | `libs/media/src/video_playback.cpp` uses FFmpeg to decode video and convert frames to owned RGBA8 storage. `VideoPlaybackSession` is independent of Qt and can report optional decode timings through an observer. | `creative-suite::video-media` and `creative-suite::media-frame` are candidates for video layers, subject to Motion Studio consumer regression coverage. |
| Timeline playback and CPU composition | `apps/video-editor/src/playback/playback_worker.cpp` selects and decodes timeline layers, rasterizes text, and calls the CPU `FrameCompositor`. The compositor returns the completed raster frame before preview presentation. | The shared raster compositor and animation evaluator are reusable candidates. Timeline scheduling, project data, and playback controls remain application-specific. |
| Still images and text | `apps/video-editor/src/media/still_image_decoder.cpp` uses Qt `QImageReader` with an FFmpeg compatibility fallback. `apps/video-editor/src/rendering/text_renderer.cpp` rasterizes text through Qt `QPainter`. Both are Video Editor application services, not neutral shared APIs. | Motion Studio needs still-image and text rasterization in its own workflow or separately reviewed shared services. Vector-shape rasterization is not established by this path. |
| Preview presentation | `OpenGLPreviewSurface` requests an OpenGL 3.2 Core context, uploads the already-composed RGBA frame as a texture, and draws it with a shader. GPU work presents the final frame; layer composition is CPU-side. `PreviewWidget` can use a CPU `QImage`/`QPixmap` path when GPU preview is disabled or fails. | Treat this Qt/OpenGL widget as Video Editor UI. It does not provide GPU layer composition or establish a Motion Studio renderer. |

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

For Motion Studio, the existing neutral video decoder, RGBA frame model,
transform evaluator, and raster compositor are reuse candidates. The Qt
preview, Video Editor timeline worker, still-image import path, and text
rasterizer remain application-specific until an independent shared contract
is justified. Motion Studio now has its own in-memory document and layer model;
it still needs a viewer, timeline, image and text/shape workflows, effects, and
standalone persistence/export.
See [REUSE_PLAN.md](REUSE_PLAN.md) for the provisional shared API contracts.

## Native Format and Compatibility Policy

- Motion Studio uses a native document format distinct from the Video Editor's
  `.csp` project format and the Image Editor's `.cimg` format.
- The Motion Studio format has its own schema version and migration rules;
  versions in the explicitly supported older range migrate forward when
  opened.
- A document with an unsupported future schema version is rejected without
  overwriting or normalizing the original file.
- Cross-application references identify a Motion Studio document and its saved
  revision through a separately documented compatibility contract. Native
  document versioning and the Video Editor reference contract are distinct.
- The file extension, serialized field layout, path policy, migration window,
  and exact link representation remain undecided until their implementation
  contracts are validated.

## Deferred Technical Decisions

The source-level Qt 6, FFmpeg, CPU-composition, and OpenGL-presentation audit is
recorded above. Remaining Milestone 1 work is to revalidate applicable paths
across Windows, macOS, and Linux; measure startup, memory, timeline/seek
response, preview latency, and rendering for representative small, medium,
and heavy compositions; and record dependency licenses, output-profile and
codec findings, measurable resource and responsiveness targets, and
alternatives. No final language or renderer choice is made by this document.
