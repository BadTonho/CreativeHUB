# Video Editor Roadmap

Current application version: **Beta 0.1.5**.

This roadmap tracks the Video Editor through its foundation exit gate. It
separates implemented capabilities, approved foundation scope, release gates,
open technical validation, and later expansion. A completed product-decision
item records an agreed direction; it does not mean its feature is implemented.
Update it when implementation, scope, or a decision changes.

## Current direction

- The Video Editor is an active application track and its stability remains a
  priority. The Image Editor and Motion Studio may be developed in parallel
  on independent tracks.
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
- CPU composition remains the default. An experimental shared OpenGL 3.2 Core
  backend is available for timeline preview through Settings > General, with
  CPU fallback and automatic shared-texture delivery, retaining worker readback
  when required. Offline export independently offers per-job experimental GPU
  composition through 4K, with RGBA readback and CPU fallback. Qt OpenGL presents
  the final preview frame. See the
  [rendering boundary](architecture/RENDERING.md).
- The Image Editor's standalone acceptance gate comes before acceptance of
  its linked-image workflow. Motion Studio development may proceed in
  parallel; cross-application integration remains gated on stable contracts
  and producer/consumer validation. Track the independent milestones in the
  [Image Editor roadmap](../image-editor/ROADMAP.md) and
  [Motion Studio roadmap](../motion-editor/ROADMAP.md).

## GPU acceleration planning

The [GPU acceleration plan](GPU_ACCELERATION_PLAN.md) records the implemented
optional timeline composition, direct delivery and offline export stages, followed
by effects and platform acceptance. Video Editor is the first consumer of the
shared backend; Motion Studio and Image Editor adoption remain planned.

**Status: Stages 1–3 implemented, experimental, disabled by default.** Direct
texture presentation has RGBA and CPU fallbacks. Export has its own per-job choice
in Render > Video, with readback and CPU fallback. See the
[delivery contract](GPU_TEXTURE_DELIVERY.md) and [export contract](GPU_EXPORT.md).
Native parity/benchmark and build/test evidence is recorded in
[GPU_COMPOSITION_RESULTS.md](GPU_COMPOSITION_RESULTS.md) and
[GPU_EXPORT_RESULTS.md](GPU_EXPORT_RESULTS.md). Further OS/driver and
human audio/visual acceptance remain pending; formats and versions are unchanged.

## Status legend

- `[ ]` Not started
- `[-]` In progress
- `[x]` Completed
- `[!]` Blocked or requiring a decision

## 1. Product decisions

These decisions set the boundaries for the Video Editor foundation and the
release work that follows it. These are approved product directions for the
foundation; platform packaging, shipped codec availability, numeric hardware
requirements, and implementation details still require validation below.

- [x] Set the product direction: build an open creative suite that gives people
  locally usable tools and reduces reliance on proprietary creative software.
- [x] Define the initial target audience and main workflow: content creators
  broadly, with YouTube long-form videos and Shorts as the first priority. Keep
  the interface easy to explore and provide shortcuts for frequent tasks;
  foundation scope does not promise parity with advanced post-production tools.
- [x] Confirm the foundation scope and deferred workflows. Target a typical
  project up to 1080p and 15 minutes, with three video tracks, text, essential
  transitions, embedded video audio, and basic independent Audio tracks.
  Recording, track automation, advanced audio mixing, advanced grading,
  compositing, proxies, and plugins remain later work.
- [x] Set the maintainer's current PC as the reference test machine for the
  representative workload above: AMD Ryzen 5 3600, 32 GB RAM, NVIDIA GeForce
  GTX 1660 SUPER with 6 GB VRAM, and Windows 11. Record test conditions and
  results during validation; derive numeric requirements from measurements
  rather than claiming an unvalidated minimum.
- [x] Include 16:9 (1920x1080) and 9:16 (1080x1920) project canvases, with 16:9
  and 30 fps as defaults. Offer 24, 25, 30, 48, 50, and 60 fps. The typical
  1080p/30 fps project is the real-time playback target; measure higher rates
  separately.
- [x] Accept 4K source media in 1080p projects, without promising real-time
  playback for 4K sources.
- [x] Choose a broad media-compatibility policy: do not impose a fixed
  application-level format allow-list; seek the broadest practical support
  from the shipped FFmpeg build and Qt image-format plugins.
- [x] Choose the default clip-deletion behavior: keep following clips at their
  positions and leave a gap; provide a separate Ripple Delete / Close Gap
  command when the user wants following clips to move earlier.
- [x] Include direct operating-system file drops. Dropping on the Media Browser
  imports media; dropping on the Timeline imports and inserts it using the
  existing Timeline media-drop behavior. Use the existing import pipeline and
  its supported media types.
- [x] Include one adjustable YouTube export preset that follows the project
  canvas and frame rate. Its planned 1080p SDR defaults are MP4/H.264, AAC-LC
  stereo at 48 kHz and 192 kbps, Fast Start, BT.709 color, and VBR video at
  8 Mbps for 24–30 fps or 12 Mbps for 48–60 fps. Keep generic export controls
  available. If a shipped build lacks the required codecs, explain that the
  preset is unavailable rather than silently substituting another codec.
- [x] Set Windows 10 and Windows 11 as Windows targets and Qt 6.12 as the
  planned Qt baseline. Apply the dependency update when the stable release and
  compatible package are available.
- [x] Keep Linux and macOS as required targets; defer their exact distribution,
  OS-version, and architecture baselines until those platforms can be built and
  tested.
- [x] Keep the product name temporary until an explicit identity decision is
  made.
- [x] Choose the open-source license: GPL-3.0-or-later.

### Approved foundation scope

Build on the implemented local-media, multitrack-editing, preview/playback,
transform, keyframe, text, essential-transition, embedded and independent
audio, project save/open, Undo/Redo, autosave, and recovery workflows. Complete
the release gates below and add the approved portrait canvas, project-rate choices,
operating-system file drops, and adjustable YouTube preset. Preserve a clear,
discoverable interface with shortcuts for frequent actions.

The current project format still accepts only a 1920x1080 canvas. Future
implementation of portrait canvases and project-rate choices must preserve
opening existing `.csp` projects as 16:9; define and validate any required
format migration before changing persistence.

Waveforms, recording, advanced audio mixing, automatic captions, advanced color
grading, masks, 3D layers, proxies,
advanced compositing, plugins, and full Fusion processing remain outside the
foundation. Continue evaluating these only as later milestones.

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
  linear keyframes, essential Cross Dissolve and Fade to Black transitions,
  and equal-power Audio Crossfades on independent Audio clips.
- [x] Embedded video audio playback with per-clip and per-track gain and mute.
- [x] Audio-only import and dedicated Audio tracks, including a default empty
  Audio 1 lane in new and reset projects, plus synchronized linked
  Audio companions for videos with sound; automatic compatible-track reuse or
  creation, microsecond source timing, paired editing/unlinking, v14 persistence
  and legacy migration, mixed preview/export, black frames through audio tails,
  and v16 Audio Crossfade persistence.
- [x] Qt OpenGL preview presentation with CPU fallback and grayscale preview.
- [x] Versioned `.csp` persistence, transactional New/Open/Save/Save As,
  autosave, and recovery snapshots.
- [x] Local structured logging with bounded retention and actionable error
  context.
- [x] Automated regression coverage for current media, playback, timeline,
  project, logging, and UI boundaries, plus a documented manual validation
  matrix. See [Regression Testing](REGRESSION_TESTING.md).
- [x] Document the application module boundaries and the CPU
  composition/Qt OpenGL presentation split. Current shared-library boundaries
  are maintained in [Repository Structure](architecture/REPOSITORY_STRUCTURE.md).

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
- [x] Add the initial offline export workflow: a session-only ordered queue,
  project/settings snapshots, CPU composition, dynamically discovered FFmpeg
  muxer and encoder choices, progress, cancellation, failure reporting, and
  temporary-file verification before publishing.
- [ ] Add the adjustable YouTube preset for 1080p SDR using the approved
  project canvas and frame rate. Validate its MP4/H.264 and AAC-LC combination,
  Fast Start, default bitrates, encoder availability, license constraints, and
  unavailable-preset message against each shipped FFmpeg build. Keep generic
  export options available when the preset cannot run.
- [ ] Validate practical export compatibility across the intended shipped
  FFmpeg builds and supported platforms. Measure quality and performance,
  verify hardware-encoder combinations, and complete manual export validation
  with representative video, image, text, transition, and audio projects.
- [x] Add the explicit Ripple Delete / Close Gap command. Ordinary deletion
  keeps later clips in place; Ripple Delete moves the selected track's
  sequence, follows linked companions, and stops at collisions without removing
  blockers. See the Timeline contract and manual regression checklist.
- [ ] Add project creation choices for the approved 16:9 and 9:16 canvases and
  24/25/30/48/50/60 fps rates, defaulting to 16:9 at 30 fps. Preserve the
  existing behavior of `.csp` versions 1 through 13 as 16:9 when opening them;
  define and test the versioned persistence change before implementation.
- [ ] Implement direct operating-system file drops as decided in section 1.
  Dropping on the Media Browser imports; dropping on the Timeline uses the
  existing media-drop behavior. Keep the import dialog and Media Browser drag
  workflow available.
- [x] Repeatedly migrate the same long-lived `.csp` project when advancing
  persisted-format versions. The owner reports doing this since the application
  was created and says the migrations have worked.
- [x] Validate save/reopen, invalid-project preservation, and recovery after
  reopening unsaved work. The owner reports that opening through recovery after
  forgetting to save has restored the project every time it was needed.
- [ ] Validate offline media and failure handling with representative small,
  medium, and heavy projects, including portrait projects.
  For the typical 1080p, 15-minute, three-track project, target 30 fps playback
  on the maintainer's reference PC using 1080p sources. Measure
  4K-source and higher-frame-rate playback separately without a real-time
  guarantee. Record fixtures, exact test-machine CPU/RAM/GPU, and results.
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

## 4. Image Editor and Motion Studio coordination

The Video Editor already contains the initial linked-image implementation.
Its broader acceptance is tracked in the Image Editor roadmap and is gated by
the standalone Image Editor acceptance criteria. This gate applies within the
Image Editor track; Motion Studio development may continue in parallel.

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
- [ ] Coordinate the Image Editor first editing release and Motion Studio
  foundation as parallel tracks. Gate their shared services and handoff on
  stable contracts and producer/consumer regression coverage.

The existing handoff prototype and its full contract are documented in the
[cross-application compatibility proposal](../CROSS_APPLICATION_COMPATIBILITY.md).
Unsaved live preview streaming remains outside the current handoff scope.

## 5. Later Video Editor expansion

Prioritize these workflows after the Video Editor foundation using user needs,
performance measurements, and Video Editor stability as the decision criteria.

- [ ] Improve media organization, synchronization, relinking, and asset
  management.
  - [x] Add shared CPU Grayscale, Brightness, Contrast, and Saturation filters
    for video and image clips, available through Effects and Functions with
    editable stacks, individual enable/bypass controls, and version 18 project
    persistence.
  - [ ] Expand clip color correction into broader grading workflows.
- [x] Display derived, cached waveforms on independent and linked Audio clips,
  with a global Mono/Stereo view preference and separate channel peaks.
- [x] Add clip-local volume automation with editable points, fade support,
  Undo/Redo, project persistence, and matching Preview/export mixing.
- [x] Add equal-power Audio Crossfades between adjacent independent clips on
  the same Audio track, with Undo/Redo, version 16 persistence, and shared
  Preview/export mixing.
- [ ] Expand audio editing with recording, audio-only file export, track
  automation, and advanced mixing as user workflows require.
- [ ] Add masks and more advanced compositing.
- [ ] Add proxies, incremental rendering, and broader cache workflows where
  measurements show a need.
- [ ] Profile representative projects and address measured performance and
  memory bottlenecks.
- [ ] Establish startup, memory, and playback/preview baselines on the
  maintainer's reference PC and additional systems; set numeric minimum
  hardware requirements from those measurements.
- [x] Deliver shared composed textures directly, with bounded leases/fences,
  asynchronous recovery and transfer diagnostics; platform acceptance pending.
- [x] Add experimental per-layer GPU composition with CPU fallback and transfer
  measurements. Keep default and platform acceptance tied to recorded evidence.
- [x] Add independent per-job experimental GPU offline export through 4K, with
  readback, CPU fallback, transactional output and separate export metrics.
  Broader platform/driver/codec and human acceptance remain pending; see
  [GPU_EXPORT_RESULTS.md](GPU_EXPORT_RESULTS.md).
- [ ] Revisit plugin architecture, additional export presets, and templates
  after stable public workflows and extension points are known.

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
