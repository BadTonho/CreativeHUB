# Video Editor Roadmap

This roadmap tracks the Video Editor through its foundation exit gate. It
separates implemented capabilities, release gates, open product decisions, and
later expansion. Update it when implementation, scope, or a decision changes.

## Current direction

- The Video Editor remains the first application and its stability remains a
  priority.
- Continue Video Editor development in C++20 with Qt 6 and FFmpeg. This is the
  current working direction and remains provisional; no final project-wide
  language decision is recorded.
- Target Qt 6.12 for the Windows 10 and 11 builds. Treat it as a planned
  baseline until the stable release and compatible package are available; the
  active build configuration still needs to be updated and validated.
- Windows 10 and 11 are the current Windows targets. Linux and macOS remain
  required product targets; their release validation is pending access to
  suitable build and test environments.
- Keep modules inside `apps/video-editor/` while the Video Editor is their only
  consumer. Extract a focused shared library only when another application has
  a real use for the same behavior and a stable API can serve both. See the
  [cross-application compatibility proposal](../CROSS_APPLICATION_COMPATIBILITY.md).
- The current compositor runs on the CPU and Qt OpenGL presents the final
  frame. Keep this implementation until profiling or a concrete
  second backend justifies a broader renderer abstraction. See the
  [rendering boundary](architecture/RENDERING.md).
- The Image Editor follows the Video Editor foundation. Its standalone
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

These decisions set the boundaries for the Video Editor foundation and the
release work that follows it.
The scope below is a working proposal based on the existing application, not a
final product commitment.

- [x] Set the product direction: build an open creative suite that gives people
  locally usable tools and reduces reliance on proprietary creative software.
- [ ] Define the initial target audience and the main editing workflows.
- [ ] Confirm the Video Editor foundation scope and explicitly list deferred
  workflows.
- [ ] Define minimum hardware and representative project sizes.
- [x] Choose a broad media-compatibility policy: do not impose a fixed
  application-level format allow-list; seek the broadest practical support
  from the shipped FFmpeg build and Qt image-format plugins.
- [x] Choose the default clip-deletion behavior: keep following clips at their
  positions and leave a gap; provide a separate Ripple Delete / Close Gap
  command when the user wants following clips to move earlier.
- [ ] Decide whether direct operating-system file drops are required for the
  foundation.
- [x] Set Windows 10 and Windows 11 as Windows targets and Qt 6.12 as the
  planned Qt baseline. Apply the dependency update when the stable release and
  compatible package are available.
- [x] Keep Linux and macOS as required targets; defer their exact distribution,
  OS-version, and architecture baselines until those platforms can be built and
  tested.
- [x] Keep the product name temporary until an explicit identity decision is
  made.
- [x] Choose the open-source license: GPL-3.0-or-later.

### Working foundation proposal

Use the current Video Editor foundation as the starting point: local media
import and organization; multi-track clip assembly, movement, splitting,
trimming, and deletion; preview and playback; basic transforms, keyframes,
text, essential transitions, embedded video audio controls; project save/open,
Undo/Redo, autosave, and recovery. Add a defined export workflow and complete
the release gates below. Audience, supported media limits, export controls,
platform packaging, and direct operating-system file drops remain open.

Advanced color grading, independent audio tracks and mixing, masks, proxy
workflows, advanced compositing, and plugin support are candidates for later
milestones unless product validation promotes a specific workflow into the
foundation scope.

## 2. Implemented foundation

The following capabilities are already present in the Video Editor. This list
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

## 3. Video Editor foundation completion gates

Complete these gates after the foundation decisions in section 1 are
recorded. A feature counts as complete when its automated regression coverage
and required manual validation pass.

- [ ] Inventory the actual media capabilities of the intended shipped build:
  FFmpeg demuxers, video and audio decoders, muxers and encoders, plus available
  Qt image-format plugins. Record capability and license constraints without
  turning the inventory into a fixed application-level allow-list.
- [ ] Replace hardcoded still-image extension checks with capability-based
  detection from the deployed Qt image plugins. Define how multi-frame images
  retain timing before importing them as animated Timeline clips.
- [ ] Maximize practical video export compatibility using valid muxer/encoder
  combinations available in the shipped FFmpeg build. Define output controls,
  audio handling, progress and cancellation behavior, error reporting, and
  verification of the produced file.
- [ ] Add the explicit Ripple Delete / Close Gap command. Ordinary deletion
  must keep subsequent clips in place and leave the gap.
- [ ] Decide and, if included, implement direct operating-system file drops.
  The existing import dialog and drag from the Media Browser remain available.
- [ ] Validate project save/reopen, autosave, recovery, offline media, and
  failure handling with representative small, medium, and heavy projects.
  Record the project fixtures and acceptance criteria when hardware and media
  limits are defined.
- [ ] Build and run automated and manual release workflows on Windows, macOS,
  and Linux. Validate paths, permissions, fonts, color behavior, audio devices,
  and documented keyboard shortcuts on each supported platform. Windows 10 and
  11 can be validated now; Linux and macOS acceptance remains pending access to
  suitable build and test environments.
- [ ] Define installation and packaging for each supported platform, including
  required Qt plugins and FFmpeg runtime components.
- [ ] Record the exact dependency, codec, asset, and license configuration for
  distributed builds and the associated source/relinking obligations.

**Video Editor foundation exit criteria:** the agreed foundation workflows,
including export, pass automated regression coverage and manual validation;
projects save and recover under the defined workload; supported platform
builds and packages pass their release checks; dependency and licensing
records are complete.

## 4. Image Editor integration and application sequence

The Video Editor already contains the initial linked-image implementation.
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

## 5. Later Video Editor expansion

Prioritize these workflows after the Video Editor foundation using user needs,
performance measurements, and Video Editor stability as the decision criteria.

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
- [ ] Establish startup, memory, and playback/preview baselines after minimum
  hardware and representative project sizes are defined.
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
