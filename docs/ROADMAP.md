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
- Motion Studio will follow the Main Editor foundation instead of being built
  in parallel from the beginning.

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
- [ ] Choose the open-source license.

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
- [x] Create `apps/main-editor/` as the first real application.
- [ ] Define shared-core responsibilities and create only the modules required
  by the Main Editor.
- [x] Choose Qt 6 Widgets as the provisional UI, window, and input approach.
- [ ] Define the rendering abstraction and CPU/GPU responsibilities.
- [ ] Create the project and document model.
- [ ] Create the media import and management layer.
- [ ] Create the basic file, autosave, recovery, undo, and redo systems.
- [ ] Define the initial project format and document it.
- [ ] Add automated tests for the core and module boundaries.
- [ ] Track dependencies, licenses, codecs, and third-party assets.

## 4. Main Editor MVP

- [x] Create the initial application shell and primary workspace.
- [ ] Add media import, a media browser, and project bins.
- [ ] Create a functional timeline.
- [ ] Support video, image, text, and audio clips.
- [ ] Implement cutting, splitting, moving, and rearranging clips.
- [ ] Add a real-time video preview.
- [ ] Add basic audio editing and volume control.
- [ ] Add basic layers, transformations, and keyframes.
- [ ] Add basic text and captions.
- [ ] Add essential transitions and effects.
- [ ] Add common video export formats.
- [ ] Ensure projects are stable, recoverable, and tested with small, medium,
  and heavy projects.

## 5. Main Editor Expansion

- [ ] Add advanced media organization and synchronization tools.
- [ ] Add color correction and grading workflows.
- [ ] Improve audio editing and mixing.
- [ ] Add masks and more advanced compositing.
- [ ] Add proxies, caching, and incremental rendering.
- [ ] Improve performance through profiling and measurement.

## 6. Motion Studio

- [ ] Define the Motion Studio scope and boundaries with the Main Editor.
- [ ] Create `apps/motion-studio/` after the shared foundation is stable.
- [ ] Add advanced keyframes and property curves.
- [ ] Add nested compositions.
- [ ] Add animated masks.
- [ ] Add advanced text and shape tools.
- [ ] Add chained effects.
- [ ] Define how Motion Studio compositions are linked to Main Editor projects.
- [ ] Evaluate particles, node-based workflows, and 3D features for future
  phases.

## 7. Image Editor

- [ ] Revisit the image editor after the Main Editor and Motion Studio
  foundations are stable.
- [ ] Define whether it will be a separate application or an integrated
  module.
- [ ] Plan layers, masks, selections, text, color adjustments, filters, and
  export.
- [ ] Define how image documents will be shared with the other applications.

## 8. Cross-Platform and Release Work

- [ ] Test file paths, permissions, fonts, color management, audio devices,
  and keyboard shortcuts on Windows, macOS, and Linux.
- [ ] Define installation, update, packaging, signing, and distribution
  workflows.
- [ ] Add crash reporting and useful error handling without requiring online
  services.
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
