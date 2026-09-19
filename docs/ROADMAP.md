# Project Roadmap

This is the working roadmap for the creative suite. It is a living document and must be updated whenever priorities, decisions, or project status change.

## Status Legend

- `[ ]` Not started
- `[-]` In progress
- `[x]` Completed
- `[!]` Blocked or requiring a decision

## 1. Product Definition

- [ ] Define the initial target audience.
- [ ] Define the first MVP and its limits.
- [ ] Choose the first application to develop: the Main Editor.
- [ ] Define the priority operating systems and minimum hardware.
- [ ] Choose the open-source license.
- [ ] Keep the product name temporary until an explicit identity decision is made.

## 2. Technical Validation

- [ ] Create a minimal prototype for opening and decoding video.
- [ ] Implement basic timeline navigation in the prototype.
- [ ] Display a GPU-accelerated preview.
- [ ] Apply one simple video effect.
- [ ] Measure memory usage, startup time, and performance.
- [ ] Build and test the prototype on Windows, macOS, and Linux.
- [ ] Compare Rust and C++ using the same practical criteria.
- [ ] Record the language decision only after the prototype comparison.

## 3. Project Foundation

- [ ] Define the repository structure and module boundaries.
- [ ] Define the shared core API.
- [ ] Create the project and document model.
- [ ] Create the media import and management layer.
- [ ] Create the basic file, autosave, recovery, undo, and redo systems.
- [ ] Define the rendering abstraction and CPU/GPU responsibilities.
- [ ] Define the initial project format and document it.
- [ ] Add automated tests for the core and module boundaries.
- [ ] Track dependencies, licenses, codecs, and third-party assets.

## 4. Main Editor MVP

- [ ] Create a functional timeline.
- [ ] Support video, image, text, and audio clips.
- [ ] Implement cutting, splitting, moving, and rearranging clips.
- [ ] Add basic audio editing and volume control.
- [ ] Add a real-time preview.
- [ ] Add basic layers, transformations, and keyframes.
- [ ] Add basic text and captions.
- [ ] Add essential transitions and effects.
- [ ] Add common video export formats.
- [ ] Ensure projects are stable, recoverable, and tested with small, medium, and heavy projects.

## 5. Main Editor Expansion

- [ ] Add media organization and project bins.
- [ ] Add synchronization tools.
- [ ] Add color correction and grading workflows.
- [ ] Improve audio editing and mixing.
- [ ] Add masks and more advanced compositing.
- [ ] Add proxies, caching, and incremental rendering.
- [ ] Improve performance through profiling and measurement.

## 6. Motion Studio

- [ ] Define the Motion Studio scope and boundaries with the Main Editor.
- [ ] Add advanced keyframes and property curves.
- [ ] Add nested compositions.
- [ ] Add animated masks.
- [ ] Add advanced text and shape tools.
- [ ] Add chained effects.
- [ ] Define how Motion Studio compositions are linked to Main Editor projects.
- [ ] Evaluate particles, node-based workflows, and 3D features for future phases.

## 7. Image Editor

- [ ] Revisit the image editor after the Main Editor and Motion Studio foundations are stable.
- [ ] Define whether it will be a separate application or an integrated module.
- [ ] Plan layers, masks, selections, text, color adjustments, filters, and export.
- [ ] Define how image documents will be shared with the other applications.

## 8. Cross-Platform and Release Work

- [ ] Test file paths, permissions, fonts, color management, audio devices, and keyboard shortcuts on all target platforms.
- [ ] Define installation, update, packaging, signing, and distribution workflows.
- [ ] Add crash reporting and useful error handling without requiring online services.
- [ ] Create contributor documentation and public technical documentation.
- [ ] Review performance, memory usage, and startup time before major releases.

## 9. Community and Ecosystem

- [ ] Define the plugin architecture.
- [ ] Publish documented project and plugin formats where possible.
- [ ] Create presets and template support.
- [ ] Define an asset and license tracking process.
- [ ] Prepare contribution guidelines and issue templates.
- [ ] Add translations after the core workflows are stable.

## Working Rule

Every completed task should be marked with `[x]`, and any important decision should be recorded in the relevant documentation at the same time.
