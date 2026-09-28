# Motion Studio Scope and Readiness

Status: **provisional product scope; Milestone 0 complete**. This document
records the agreed starting scope for Motion Studio. It does not select a
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
`libs/`. The Video Editor uses compatibility headers and retains
timeline-specific keyframe split and trim operations. These APIs remain
provisional until Motion Studio consumes them and has regression coverage at
its own application boundary.

The RGBA frame model, FFmpeg playback session, and logger are now focused
shared targets. Each application links the targets it needs into its own build
and package; it does not load or launch another editor. The Video Editor
supplies preview-metric recording through an observer adapter; Motion Studio
can use the decoder without that observer. UI, timeline workflows, project
adapters, import/export controllers, and application history remain local to
each application.

Any composition contract must define coordinate units and transforms, pixel
format and color/alpha assumptions, resource lifetime and thread requirements,
error context, and serialization compatibility when applicable. The current
frame type defines RGBA8 storage and stride but no color space. Keyframe and
curve evaluation remains a separate capability from composition.

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

Milestone 1 starts by validating the Video Editor's existing Qt 6 and FFmpeg
media path, worker-side CPU composition, and Qt OpenGL presentation of the
composed frame. It supports video, text, raster images, basic transforms, and
linear keyframes. OpenGL presents the completed frame; layer composition itself
runs on the CPU.

Compare those capabilities with the Motion Studio MVP and record which
contracts transfer and which gaps remain. In particular, validate the gaps in
still-image decoding, vector-shape rasterization, editable curves, effects, and
the standalone project workflow. Revalidate the applicable Video Editor paths
across Windows, macOS, and Linux, and measure startup, memory, timeline/seek
response, preview latency, and rendering for representative small, medium, and
heavy compositions. Record dependency licenses, output-profile and codec
findings, measurable resource and responsiveness targets, and alternatives.
No final language or renderer choice is made by this document.
