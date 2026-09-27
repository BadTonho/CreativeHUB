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
| Media and decoding | `apps/video-editor/src/media/`; FFmpeg video decoding and raster-image decoding are application services. | Reuse compatible low-level decoding only after source/resource semantics and ownership are documented; keep import UI and workflow local. |
| Composition and preview | `apps/video-editor/src/playback/` and `src/rendering/`; worker-side CPU composition, frame compositor, and a provisional OpenGL preview surface. | Candidate composition boundary; document coordinate, pixel, alpha, lifetime, thread, and error contracts before extraction. GPU per-layer composition is not an existing capability. |
| Transforms and animation | `apps/video-editor/src/timeline/`; normalized 2D position, scale, rotation, opacity, and linear per-clip keyframes. | Candidate shared animation evaluation; Motion Studio curves and property editing remain application-specific until a shared contract is proven. |
| Timeline and history | Timeline model, commands, and bounded undo/redo are application-specific. | Motion Studio owns its composition timeline and editing history; share lower-level behavior only where a second real consumer uses the same contract. |
| Project persistence | `apps/video-editor/src/project/`; versioned `.csp` format currently at version 12, with migrations for supported earlier versions. | Keep a separate versioned Motion Studio native document and adapter. Do not reuse `.csp` as the native composition format. |
| Autosave and recovery | `src/project/autosave_manager.*` and application coordination provide autosave snapshots and recovery. | Motion Studio must provide equivalent workflow-specific recovery; extract lower-level services only after ownership and boundary tests are clear. |
| Diagnostics | `apps/video-editor/src/logging/`; local structured diagnostic logging. | Motion Studio must log actionable failures locally without secrets or unnecessary personal data. A shared logging library is not required by this scope. |

### Candidate shared capabilities

Composition operations and keyframe/curve evaluation are the smallest current
candidates for shared libraries because both the Video Editor and Motion
Studio have a documented use for them. Keep current implementations in their
application boundaries while contracts are validated. Extract focused
libraries under `libs/` only after one implementation serves both applications
through the same documented API and regression coverage exercises the shared
behavior and each application boundary. Keep UI, timeline workflows, project
adapters, and application history outside those libraries.

Any composition contract must define coordinate units and transforms, pixel
format and color/alpha assumptions, resource lifetime and thread requirements,
error context, and serialization compatibility when applicable. Keyframe and
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

Milestone 1 starts by reusing evidence already in the repository. The C++ SDL3
vertical slice covers one-video decoding, playback and seeking, a basic
timeline, GPU texture presentation, and a simple grayscale effect. Its Windows
Release build passed, but SDL3 GPU runtime initialization is blocked in the
recorded host; macOS and Linux remain unvalidated. It is a technical reference,
not a selected Motion Studio renderer.

The Video Editor provides a second, distinct reference: Qt 6 and FFmpeg media
handling, worker-side CPU layer composition, and Qt OpenGL presentation of the
final composed frame. This already supports video, text, raster images, basic
transforms, and linear keyframes. Its OpenGL presentation does not mean that
layer composition runs on the GPU.

First compare these existing capabilities with the MVP and record what evidence
transfers and what gaps remain. Revalidate existing paths across Windows,
macOS, and Linux, and measure startup, memory, timeline/seek response, preview
latency, and rendering for small, medium, and heavy compositions. Add a narrow,
isolated spike only for a Motion-specific requirement the existing code cannot
validate; do not create another generic prototype or restart the full
Rust/C++ comparison by default. Record dependency and asset licenses, output
profile/codec findings, measurable resource and responsiveness targets, and
remaining alternatives. No final language or renderer choice is made by this
document.
