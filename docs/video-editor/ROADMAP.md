# Main Editor Roadmap

This roadmap tracks the Main Editor through its foundation exit gate. It
separates implemented capabilities, release gates, open product decisions, and
later expansion. Update it when implementation, scope, or a decision changes.

## Current direction

- The Main Editor remains the first application and its stability remains a
  priority.
- Continue Main Editor development in C++20 with Qt 6 and FFmpeg. This is the
  current working direction and remains provisional; no final project-wide
  language decision is recorded.
- Keep modules inside `apps/video-editor/` while the Main Editor is their only
  consumer. Extract a focused shared library only when another application has
  a real use for the same behavior and a stable API can serve both. See the
  [cross-application compatibility proposal](../CROSS_APPLICATION_COMPATIBILITY.md).
- The current compositor runs on the CPU and Qt OpenGL presents the final
  frame. Keep this implementation until profiling or a concrete
  second backend justifies a broader renderer abstraction. See the
  [rendering boundary](architecture/RENDERING.md).
- The Image Editor follows the Main Editor foundation. Its standalone
  acceptance gate comes before acceptance of the linked-image workflow; its
  first editing release comes before Motion Studio. Track those milestones in
  the [Image Editor roadmap](../image-editor/ROADMAP.md) and
  [Motion Studio roadmap](../motion-editor/ROADMAP.md).

## Status legend

- `[ ]` Not started
- `[-]` In progress
- `[x]` Completed
- `[!]` Blocked or requiring a decision

## 1. Product decisions

These decisions set the boundaries for the Main Editor foundation and the
release work that follows it.
The scope below is a working proposal based on the existing application, not a
final product commitment.

- [ ] Define the initial target audience and the main editing workflows.
- [ ] Confirm the Main Editor foundation scope and explicitly list deferred
  workflows.
- [ ] Define minimum hardware and per-platform release baselines for Windows,
  macOS, and Linux, plus representative project sizes.
- [ ] Define supported import and export containers, codecs, frame sizes, and
  frame rates for the Main Editor foundation.
- [ ] Decide whether ripple editing, automatic gap management, and direct
  operating-system file drops are required for the foundation.
- [x] Keep the product name temporary until an explicit identity decision is
  made.
- [x] Choose the open-source license: GPL-3.0-or-later.

### Working foundation proposal

Use the current Main Editor foundation as the starting point: local media
import and organization; multi-track clip assembly, movement, splitting,
trimming, and deletion; preview and playback; basic transforms, keyframes,
text, essential transitions, embedded video audio controls; project save/open,
Undo/Redo, autosave, and recovery. Add a defined export workflow and complete
the release gates below. Audience, supported media limits, output formats,
platform packaging, and any additional timeline requirements remain open
decisions.

Advanced color grading, independent audio tracks and mixing, masks, proxy
workflows, advanced compositing, and plugin support are candidates for later
milestones unless product validation promotes a specific workflow into the
foundation scope.

## 2. Implemented foundation

The following capabilities are already present in the Main Editor. This list
records the current baseline; it is not a sequence of new implementation
tasks.

- [x] C++20 Qt Widgets application with FFmpeg media probing and decoding.
- [x] Media Browser with bins, project-owned labels, duplicate-path handling,
  offline state, and restoration by reimport.
- [x] Multi-track timeline with absolute positions, gaps, cross-track overlap,
  clip selection and movement, split, trim, delete, and bounded Undo/Redo.
- [x] Video and static raster-image clips, manual text clips, transforms,
  linear keyframes, essential Cross Dissolve and Fade to Black transitions.
- [x] Embedded video audio playback with per-clip and per-track gain and mute.
- [x] Qt OpenGL preview presentation with CPU fallback and grayscale preview.
- [x] Versioned `.csp` persistence, transactional New/Open/Save/Save As,
  autosave, and recovery snapshots.
- [x] Local structured logging with bounded retention and actionable error
  context.
- [x] Automated regression coverage for current media, playback, timeline,
  project, logging, and UI boundaries, plus a documented manual validation
  matrix. See [Regression Testing](REGRESSION_TESTING.md).
- [x] Document the current application-local module boundaries and the CPU
  composition/Qt OpenGL presentation split.

The detailed implemented scope and deferred behaviors are recorded in
[Current Scope and Non-goals](architecture/SCOPE.md). The active `.csp` schema
and migrations are recorded in [Project Document and Persistence](architecture/PROJECT.md).

## 3. Main Editor foundation completion gates

Complete these gates after the foundation decisions in section 1 are
recorded. A feature counts as complete when its automated regression coverage
and required manual validation pass.

- [ ] Implement export for the agreed foundation formats. Define the
  output settings, audio handling, progress and cancellation behavior, error
  reporting, and a way to verify the produced file.
- [ ] Implement the timeline additions selected by the foundation decision.
  Candidate items currently documented as future work are ripple editing,
  automatic gap management, and direct operating-system file drops. Keep
  unselected items in the later-work section instead of treating them as
  release blockers.
- [ ] Validate project save/reopen, autosave, recovery, offline media, and
  failure handling with representative small, medium, and heavy projects.
  Record the project fixtures and acceptance criteria when hardware and media
  limits are defined.
- [ ] Measure startup time, memory use, and playback/preview behavior on the
  agreed representative projects. Use the measurements to set release limits
  and prioritize optimizations.
- [ ] Build and run automated and manual release workflows on Windows, macOS,
  and Linux. Validate paths, permissions, fonts, color behavior, audio devices,
  and documented keyboard shortcuts on each supported platform.
- [ ] Define installation and packaging for each supported platform, including
  required Qt plugins and FFmpeg runtime components.
- [ ] Record the exact dependency, codec, asset, and license configuration for
  distributed builds and the associated source/relinking obligations.

**Main Editor foundation exit criteria:** the agreed foundation workflows,
including export, pass automated regression coverage and manual validation;
projects save and recover under the defined workload; supported platform
builds and packages pass their release checks; dependency and licensing
records are complete.

## 4. Image Editor integration and application sequence

The Main Editor already contains the initial linked-image implementation.
Its broader acceptance is tracked in the Image Editor roadmap and is gated by
the standalone Image Editor acceptance criteria.

- [x] Implement shared Media Pool links and isolated timeline image variants,
  persisted in `.csp` v10 with compatibility for versions 1 through 9.
- [x] Preserve original sources, publish saved PNG output atomically, and
  refresh affected previews asynchronously.
- [x] Add linked-document launch, stale-save protection, and automated coverage
  for key failure and stale-generation cases.
- [ ] Pass the Image Editor standalone release, manual, recovery, and platform
  packaging gate before accepting linked-image compatibility.
- [ ] After the standalone gate passes, complete linked-image validation for
  repeated opens, multiple uses, variants, transparency, large images,
  save/reopen, conflicts, and Windows, macOS, and Linux.
- [ ] Complete the Image Editor's first editing release before starting Motion
  Studio. See its Milestone 3 exit criteria.

The existing handoff prototype and its full contract are documented in the
[cross-application compatibility proposal](../CROSS_APPLICATION_COMPATIBILITY.md).
Unsaved live preview streaming remains outside the current handoff scope.

## 5. Later Main Editor expansion

Prioritize these workflows after the Main Editor foundation using user needs,
performance measurements, and Main Editor stability as the decision criteria.

- [ ] Improve media organization, synchronization, relinking, and asset
  management.
- [ ] Add color correction and grading workflows.
- [ ] Expand audio editing to audio-only sources, independent tracks,
  waveforms, automation, recording, and mixing as validated workflows require.
- [ ] Add masks and more advanced compositing.
- [ ] Add proxies, incremental rendering, and broader cache workflows where
  measurements show a need.
- [ ] Profile representative projects and address measured performance and
  memory bottlenecks.
- [ ] Consider per-layer GPU composition or another rendering backend only
  when measured needs justify the added implementation and deployment cost.
- [ ] Revisit plugin architecture, presets, and templates after stable public
  workflows and extension points are known.

## 6. Deferred technical and ecosystem work

- Keep the Rust prototype archived. Reopen a Rust comparison only if a concrete
  technical need makes Rust a primary candidate again. A final language
  decision would require the cross-platform prototype evidence described in
  [Technical Prototype Comparison](TECHNICAL_PROTOTYPE_COMPARISON.md).
- Native crash dumps, translations, and broader contributor materials should
  be prioritized against release needs and maintainer capacity.
- Do not set delivery dates until product scope, platform packaging, and
  project capacity are known.

## Working rule

Update this roadmap and the relevant architecture or validation document in
the same change when implementation, scope, or a technical decision changes.
Do not mark a gate complete without its required automated and manual evidence.
