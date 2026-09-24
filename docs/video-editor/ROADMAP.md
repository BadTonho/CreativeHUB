# Project Roadmap

This is the working roadmap for the creative suite. It is a living document
and must be updated whenever priorities, decisions, or project status change.

## Current Direction

- The first real application to build is the Main Editor.
- The current implementation direction is C++ for the initial application,
  media, GPU, and desktop integration layers. This remains a provisional
  direction, not a permanent language decision.
- The Rust video prototype is archived as a technical reference. Rust may be
  introduced later for isolated modules when there is a clear technical
  benefit.
- Real applications must be created outside `prototypes/`. Shared libraries
  should be added only when more than one application genuinely needs them.
- After the Main Editor foundation, the Image Editor is the next application
  stage, ahead of Motion Studio. Its standalone minimum editor comes first,
  followed by linked-image compatibility and the first editing release.
  Motion Studio follows that release.

## Status Legend

- `[ ]` Not started
- `[-]` In progress
- `[x]` Completed
- `[!]` Blocked or requiring a decision

## 1. Product Definition

- [x] Set the Main Editor as the first application milestone.
- [x] Keep the product name temporary until an explicit identity decision is
  made.
- [ ] Define the initial target audience.
- [ ] Define the first Main Editor MVP and its limits.
- [ ] Define the priority operating systems and minimum hardware.
- [x] Choose the open-source license (GPL-3.0-or-later).

## 2. Prototype Archive and Technical Validation

- [x] Implement a C++ vertical slice that opens and decodes a video.
- [x] Implement basic timeline navigation in the prototype.
- [x] Implement the GPU preview path and a simple grayscale effect.
- [x] Archive the Rust comparison prototype for possible future reference.
- [ ] Validate the C++ prototype on Windows, macOS, and Linux.
- [ ] Measure memory usage, startup time, and performance on supported hosts.
- [!] Reactivate the full Rust and C++ comparison only if Rust becomes a
  primary application-language candidate again.
- [ ] Record a final language decision only when complete evidence requires it.

The historical prototype comparison, known blockers, and benchmark protocol
are documented in `TECHNICAL_PROTOTYPE_COMPARISON.md`.

## 3. Application Foundation

- [x] Define the initial repository structure and boundaries between applications,
  shared libraries, and platform adapters.
- [x] Create `apps/video-editor/` as the first real application.
- [ ] Define shared-core responsibilities and create only the modules required
  by the Main Editor.
- [x] Choose Qt 6 Widgets as the provisional UI, window, and input approach.
- [ ] Define the final rendering abstraction and CPU/GPU responsibilities;
  the provisional Qt OpenGL preview and CPU fallback are implemented.
- [x] Create the project and document model with versioned `.csp` persistence;
  manual validation confirmed.
- [x] Create the initial media import and management layer; metadata probing,
  first-frame preview, playback, seeking, sequential timeline playback, and
  basic Media Browser management are implemented. Advanced organization and
  relinking remain future work.
- [x] Create autosave and recovery snapshots; project-wide history remains
  future work.
- [x] Define and validate the initial versioned `.csp` project format.
- [ ] Add automated tests for the core and module boundaries.
- [ ] Track dependencies, licenses, codecs, and third-party assets.

## 4. Main Editor MVP

- [x] Create the initial application shell and primary workspace.
- [x] Add initial media import and Media Browser support with hierarchical bins,
  project-owned names, duplicate-path protection, and visible offline state.
- [x] Display the first decoded video frame through the provisional Qt OpenGL
  preview with a CPU fallback, including manual UI confirmation.
- [x] Add basic CPU playback with play/pause and previous/next frame controls;
  manual validation confirmed pause and frame advancement; GPU playback and
  timeline editing remain pending.
- [x] Add the visual Timeline foundation with multiple video tracks, absolute
  positions, gaps, track management, and positional drops.
- [x] Add basic single-clip click-and-drag seeking; automated and manual
  validation passed. Multiple tracks and clip editing remain pending.
- [x] Optimize timeline seeking with FFmpeg keyframe navigation, bounded frame
  caching, temporal-metadata fallback, and obsolete-request coalescing.
- [x] Add drag-and-drop from imported Media Browser items to the Timeline;
  manual UI validation passed. Operating-system file drops and drop positioning
  remain pending.
- [x] Add the multi-clip Timeline foundation with absolute positions, gaps,
  multiple tracks, and continuous playback within the active track.
- [x] Add clip movement between video tracks and absolute positions with
  configurable `Alt + drag`; Ctrl + Left and Ctrl + Right now nudge the active
  clip by one frame.
- [x] Add real clip splitting at the playhead and with the persistent Blade
  Tool; source offsets and segment-limited playback were manually validated.
- [x] Add basic clip deletion and edge trimming with absolute placement;
  manual UI validation confirmed.
- [x] Add bounded Undo/Redo for successful Timeline edits, restoring the
  Timeline, selected media, active clip, and playhead; automated and manual
  validation confirmed. Advanced ripple editing and project-wide history
  remain future work.
- [x] Add the first project document and persistence workflow with transactional
  Save, Save As, New, and Open; automated and manual validation confirmed.
- [ ] Create a full functional timeline with editing.
- [x] Support video, static raster-image, text, and embedded-audio clips in the
  current Timeline. PNG, JPEG, BMP, WebP, and TIFF images use a five-second,
  150-frame static default; animated GIF and additional media types remain
  future work.
- [ ] Add advanced ripple editing beyond the current bounded Timeline history.
- [x] Add a provisional real-time GPU video preview with shader grayscale and a
  validated CPU fallback; automated, startup, and interactive visual
  validation passed.
- [x] Add basic project media management with hierarchical bins, project labels,
  offline entries, restoration through import, and version 2 `.csp` persistence.
- [x] Add the multi-track Timeline foundation with absolute positions, gaps,
  track management, cross-track overlap, positional drops, and `.csp` version 2
  migration from version 1; manual validation confirmed multiple tracks,
  visual priority, gaps, clip movement, and timeline resizing.
- [x] Add synchronized playback of embedded video audio with per-clip and
  per-track volume/mute controls, deterministic output fallback, persistence,
  and Timeline history coverage; automated and manual audio-device validation
  passed. Audio-only sources, mixing, waveforms, automation, recording, and
  export remain future work.
- [x] Add basic layers, transformations, and keyframes, including worker-side
  composition, normalized transforms, linear keyframes, Inspector controls,
  project persistence, and Undo/Redo; manual validation confirmed.
- [x] Add basic manual text clips and captions with essential styling,
  worker-side rasterization, and `.csp` version 4 migration; automated model,
  compositor, persistence, and interactive validation passed.
- [x] Add essential Cross Dissolve and Fade to Black transitions with worker
  composition and `.csp` version 8 persistence; automated and manual
  validation passed.
- [ ] Add common video export formats.
- [ ] Ensure projects are stable, recoverable, and tested with small, medium,
  and heavy projects.

## 5. Main Editor Expansion

- [ ] Add advanced media organization and synchronization tools, including
  complete relinking and asset management.
- [ ] Add color correction and grading workflows.
- [ ] Improve audio editing and mixing.
- [ ] Add masks and more advanced compositing.
- [ ] Add proxies, caching, and incremental rendering.
- [ ] Improve performance through profiling and measurement.

### Image Editor handoff readiness

This is Main Editor integration work after the Image Editor's standalone
minimum editor is usable. Image Editor Milestone 2 is the bounded compatibility
prototype, and Milestone 3 is the first editing release. Start Motion Studio
after Milestone 3 passes its exit criteria. See the
[provisional cross-application compatibility proposal](../CROSS_APPLICATION_COMPATIBILITY.md).

- [ ] Define how a Media Pool image maps to one companion Image Editor
  document, including create-on-first-open and reuse on later opens while
  preserving the original source image.
- [ ] Define the `.csp` link data and migration strategy for the companion
  document location, document identity and version, saved revision, source
  relationship, and relinking when files move. Keep the current project format
  unchanged until this contract is validated.
- [ ] Choose and document whether the companion document references its source
  image or embeds image data, including portability, storage, and recovery
  behavior.
- [ ] Define a host-consumable image output contract covering dimensions,
  alpha, pixel format, and color behavior so the Main Editor does not need to
  interpret the Image Editor's full native document format.
- [ ] Decide timeline invocation semantics: reuse the Media Pool item's linked
  edit or create a clip-specific variant. Media Pool edits are asset-level and
  should update every timeline use of that item.
- [ ] Add an explicit open/edit action for Media Pool images and image clips;
  create or reopen the linked document and launch the Image Editor through a
  cross-platform handoff boundary.
- [ ] Detect a successfully saved linked revision and refresh its host output,
  media preview, and affected timeline composition. Invalidate only dependent
  render-cache entries, and discard stale refresh results after a project
  change.
- [ ] Handle missing or moved documents and source images, unsupported document
  versions, read-only locations, and stale concurrent revisions with recovery
  guidance and actionable technical logs.
- [ ] Add automated coverage for link creation and reuse, project persistence
  and migration, source preservation, asset-level updates, stale results,
  missing resources, and unsupported versions.
- [ ] Document manual validation for repeated Media Pool opens, multiple
  timeline uses of one image, the chosen timeline edit behavior, large and
  transparent images, save and reopen, moved files, and the handoff on Windows,
  macOS, and Linux.

**Exit criteria:** a Media Pool image opens the same linked editable document
on repeat use; the source remains intact; saving publishes a compatible image
revision that refreshes every use of that media item without stale preview
frames or render-cache results. Timeline-specific behavior and recovery cases
are documented and validated. Unsaved live preview streaming remains a later
milestone.

## 6. Image Editor

- [x] Reserve the `apps/image-editor/` and `docs/image-editor/` placeholders.
- [-] Build the standalone minimum Image Editor in `apps/image-editor/`; see
  [Image Editor roadmap](../image-editor/ROADMAP.md).
- [ ] Validate the standalone document, recovery, editing, and export workflows
  before starting Main Editor compatibility work.
- [ ] Build the linked-image handoff prototype described in the roadmap after
  the standalone minimum is accepted.
- [ ] Complete the Image Editor's first editing release before starting Motion
  Studio; keep advanced expansion scoped separately.

## 7. Motion Studio

- [x] Reserve the `apps/motion-editor/` and `docs/motion-editor/` placeholders.
- [ ] Define the Motion Studio scope and boundaries with the Main Editor; see
  the [Motion Studio roadmap](../motion-editor/ROADMAP.md).
- [ ] Build the application in `apps/motion-editor/` after the Main Editor
  foundation and the Image Editor roadmap Milestone 3 exit criteria pass.
- [ ] Add advanced keyframes and property curves.
- [ ] Add nested compositions.
- [ ] Add animated masks.
- [ ] Add advanced text and shape tools.
- [ ] Add chained effects.
- [ ] Define how Motion Studio compositions are linked to Main Editor projects.
- [ ] Evaluate particles, node-based workflows, and 3D features for future
  phases.

## 8. Cross-Platform and Release Work

- [ ] Test file paths, permissions, fonts, color management, audio devices,
  and keyboard shortcuts on Windows, macOS, and Linux.
- [ ] Define installation, update, packaging, signing, and distribution
  workflows.
- [x] Add local structured error logging and useful error handling without
  requiring online services.
- [ ] Add native crash dumps and backtrace collection in a future milestone.
- [ ] Create contributor documentation and public technical documentation.
- [ ] Review performance, memory usage, and startup time before major
  releases.

## 9. Community and Ecosystem

- [ ] Define the plugin architecture.
- [ ] Publish documented project and plugin formats where possible.
- [ ] Create presets and template support.
- [ ] Define an asset and license tracking process.
- [ ] Prepare contribution guidelines and issue templates.
- [ ] Add translations after the core workflows are stable.

## Working Rule

Every completed task should be marked with `[x]`, and any important decision
should be recorded in the relevant documentation at the same time.
