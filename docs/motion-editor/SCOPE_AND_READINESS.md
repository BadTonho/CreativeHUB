# Motion Studio Scope and Readiness

Status: **provisional product scope; Milestone 0 complete**. A standalone
Motion Studio shell and in-memory composition/layer model have started using
provisional C++ and Qt 6. Its workspace creates in-memory canvases and has an
application-owned Media Pool connected to timeline rows for image and video
layers. The timeline supports clip insertion, movement, reordering, visibility,
removal, and duration edits; the selected layer's base transform and transform
keyframes are editable. Native text, rectangle, and ellipse layers have content
inspectors and static content; their transforms and transform keyframes work
through the same timeline and preview path. Motion Studio rasterizes that
content with Qt painting on its preview worker before using the shared CPU
compositor. Manual Save, Save As, and Open use a versioned `.motion` document
that includes the Media Pool. The writer emits v2, reads v1 with default text
or rectangle content for older native layers, and keeps the recovery wrapper
at v1. Undo/Redo and configurable autosave and recovery cover the composition
and Media Pool; export remains open.
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
| Media and decoding | `libs/media/` contains neutral metadata, an in-memory catalog, import processing, FFmpeg video probing/decoding, Qt-backed still-image decoding, and RGBA frames. Each editor owns its pool UI, worker lifecycle, and project/document integration. | Motion Studio already reuses the shared catalog and import processing in its own Media Pool. Imported items are in-memory and remain references to original files; the pool clears when replacing a composition. GIF import is unsupported. |
| Composition and preview | `apps/video-editor/src/playback/` and `src/rendering/`; worker-side CPU composition, frame compositor, and a provisional OpenGL preview surface. | Motion Studio rasterizes native text and shape content to transparent RGBA8 on its own preview worker, then uses `libs/composition/` for CPU composition. This app-owned Qt renderer is provisional and does not establish a final renderer choice or GPU layer composition. |
| Transforms and animation | `apps/video-editor/src/timeline/`; normalized 2D position, scale, rotation, opacity, and linear per-clip keyframes. | Motion Studio uses `libs/animation/` to evaluate transform keyframes and owns basic key editing, property tracks, and inspector controls. Rich curves and easing remain open. |
| Timeline and history | Timeline model, commands, and bounded undo/redo are application-specific. | Motion Studio owns its composition timeline and editing history; share lower-level behavior only where a second real consumer uses the same contract. |
| Project persistence | `apps/video-editor/src/project/`; versioned `.csp` format currently at version 12, with migrations for supported earlier versions. | Keep a separate versioned Motion Studio native document and adapter. Do not reuse `.csp` as the native composition format. |
| Autosave and recovery | `src/project/autosave_manager.*` and application coordination provide autosave snapshots and recovery. | Motion Studio implements an application-owned version 1 recovery wrapper around validated `.motion` v1 or v2 data. Shared recovery services remain deferred until both document owners have stable common requirements. |
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
| Still images and text | `libs/media/src/still_image_decoder.cpp` uses Qt `QImageReader` for static raster images and returns the shared RGBA frame type. `apps/video-editor/src/rendering/text_renderer.cpp` rasterizes Video Editor text through Qt `QPainter`. | Motion Studio uses the shared still-image decoder for import previews and its own Qt rasterizer for multiline text, rectangles, and ellipses. It does not depend on the Video Editor text renderer or timeline types. |
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

For Motion Studio, the existing neutral video decoder, shared media catalog,
still-image decoder, RGBA frame model, transform evaluator, and raster
compositor are reused directly. The Qt preview, Motion Studio timeline worker,
text and shape rasterizer, and application-specific project adapters remain
application-owned. Motion Studio has its own composition/layer model, canvas viewer,
timeline rows linked to Media Pool sources, and manual versioned save/open
format. Image, video, text, and shape content render in the preview with
linearly evaluated transform keyframes. Text and shape content is static;
fonts are resolved by family name on the current system and are not embedded.
Autosave and recovery use a separate versioned wrapper:
dirty compositions and the full Media Pool are snapshotted atomically, untitled
work is isolated by session, and recovery restores through staged media loading.
The native `.motion` writer emits v2 and reads v1 using default content for old
text and shape records; the recovery wrapper remains at version 1. Richer
curves and interpolation, effects, and export remain open. The one-hour ruler
range controls navigation only and does not define the composition's duration. See
[ROADMAP.md](ROADMAP.md) and [REUSE_PLAN.md](REUSE_PLAN.md) for current
implementation details and provisional shared API contracts.

## Native Format and Compatibility Policy

- Motion Studio uses a native document format distinct from the Video Editor's
  `.csp` project format and the Image Editor's `.cimg` format.
- The current implementation writes version 2 JSON documents and reads
  versions 1 and 2, identified by
  `creative-suite.motion-studio`. Its provisional extension is `.motion`; the
  field layout, path rules, exact limits, and atomic-save behavior are described
  in [FORMAT.md](FORMAT.md).
- Version 1 text and shape records migrate to documented defaults when opened;
  saving them writes version 2. An unsupported future schema version is
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
