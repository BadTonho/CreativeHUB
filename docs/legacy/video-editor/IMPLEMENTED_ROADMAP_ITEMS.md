# Archived completed roadmap items — video-editor

This file preserves completed checklist entries moved from [the active roadmap](../../video-editor/ROADMAP.md).
The active roadmap now focuses on open work, release gates, and required feature-to-verification coverage.
This archive is historical context; current behavior and compatibility contracts remain in the application documentation.

## Video Editor Roadmap / 1. Product decisions

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
  imports to the selected or target bin; ordered Timeline batches use the
  asynchronous import pipeline and insert atomically, including automatic
  Audio tracks and linked video-audio companions.

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

## Video Editor Roadmap / 2. Implemented foundation

- [x] C++20 Qt Widgets application with FFmpeg media probing and decoding.

- [x] Media Browser with bins, project-owned labels, duplicate-path handling,
  offline state, and restoration by reimport.

- [x] Direct operating-system file drops into the Media Browser and Timeline
  are implemented and have automated viewport-level coverage. Browser drops
  honor the selected or target bin and fall back to Unsorted when no concrete
  bin is selected; ordered Timeline batches import
  asynchronously and place as one Undo/Redo edit. Windows Explorer validation
  passed on 2026-10-07; see [Regression Testing](../../video-editor/REGRESSION_TESTING.md).

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

- [x] New Project canvas and frame-rate choices: 16:9 (1920×1080) or 9:16
  (1080×1920), 24/25/30/48/50/60 fps, defaulting to 16:9 at 30 fps; version 19
  persists portrait canvas settings while versions 1–18 remain 16:9.

- [x] Project Settings for an open project: change between supported canvases
  and frame rates, preserve elapsed Timeline time and linked audio alignment,
  and apply the update atomically with Undo/Redo using the existing v19 fields.

- [x] Local structured logging with bounded retention and actionable error
  context.

- [x] Automated regression coverage for current media, playback, timeline,
  project, logging, and UI boundaries, plus a documented manual validation
  matrix. See [Regression Testing](../../video-editor/REGRESSION_TESTING.md).

- [x] Document the application module boundaries and the CPU
  composition/Qt OpenGL presentation split. Current shared-library boundaries
  are maintained in [Repository Structure](../../video-editor/architecture/REPOSITORY_STRUCTURE.md).

- [x] Global menu and toolbar actions follow the active workspace scopes:
  application actions remain available, shared commands appear in Edit/Fusion,
  and Edit-only commands are hidden elsewhere. Empty menus and separators are
  pruned without changing action enablement. Automated workspace-switch
  coverage is in place; manual appearance and shortcut checks remain pending.
  Edit-only Timeline commands now belong to `EditWorkspaceActions`, including
  their handlers, shortcut registrations, and availability rules. `MainWindow`
  mounts those actions and forwards project-loading and playback-activation
  state. Automated coverage checks command ownership, existing assignments,
  focus routing, persisted Timeline preferences, and workspace visibility;
  manual command and shortcut checks in the Release build remain pending.
  `FusionWorkspace` now owns the temporary node-preview lifecycle and sends
  playback requests through the existing shared controller; graph edits still
  use Edit's project and history services. Tests cover entering/leaving Fusion,
  clip activation at its start, preview-target changes, and fallback to Output.
  Manual preview restoration checks in the Release build remain pending.
  `RenderWorkspace` now owns queue commands and lifecycle; prepared jobs keep
  running across workspace changes, and accepted application shutdown requests
  cancellation through Render. Automated integration covers background queue
  completion after switching to Edit and Fusion, and shutdown cancellation.
  Manual checks for switching, returning to Cancel, and closing during a queue
  remain pending in the Release build. See
  [Workspace Modularization](../../video-editor/architecture/WORKSPACE_MODULARIZATION_PLAN.md).

- [x] Consolidate Edit, Fusion, and Render under `src/workspaces/`: Edit has
  `commands/`, `controllers/`, and `ui/`; Fusion has `ui/` and
  `nodes/{model,evaluation,ui}/`; Render has `ui/` and `queue/`. Shared export
  job and output-capability contracts live in
  `src/rendering/`. `WorkspaceHost` now forwards Fusion activation and
  deactivation, removing the duplicate lifecycle dispatch from `MainWindow`.
  Focused workspace, queue, export, and window integration tests pass. This
  structural change adds no new manual interaction check; the existing Render
  queue checks above remain pending.

- [x] Close the workspace modularization documentation against the current
  source tree and CMake: Edit, Fusion (including `workspaces/fusion/nodes/`),
  Render, shared rendering contracts, shortcut scopes, and regression indexes
  are aligned. The focused Release test/build evidence is recorded in
  [Regression Testing](../../video-editor/REGRESSION_TESTING.md). Manual menu/shortcut and Edit
  command checks, Fusion preview checks, and Render queue checks remain pending.

## Video Editor Roadmap / 3. Video Editor foundation completion gates

- [x] Inventory the current Windows development Release runtime: FFmpeg
  demuxers, video/audio decoders, muxers/encoders, Qt image-reader formats, and
  license constraints are recorded in
  [Windows Release Media Capability Inventory](../../video-editor/MEDIA_CAPABILITIES_WINDOWS_RELEASE.md).
  The inventory is not an application-level allow-list; revalidate each final
  distribution package and platform before making release capability claims.

- [x] Detect static images from deployed decoder capabilities and file content,
  including the WebP/TIFF FFmpeg fallback; accept single-frame GIF and reject
  multi-frame images until per-frame Timeline timing is designed. Automated
  regression coverage is implemented, and the maintainer confirmed static-image
  import, Preview, and save/reopen against the rebuilt Windows Release
  executable on 2026-10-07.

- [x] Add the initial offline export workflow: a session-only ordered queue,
  project/settings snapshots, CPU composition, dynamically discovered FFmpeg
  muxer and encoder choices, progress, cancellation, failure reporting, and
  temporary-file verification before publishing.

- [x] Add the explicit Ripple Delete / Close Gap command. Ordinary deletion
  keeps later clips in place; Ripple Delete moves the selected track's
  sequence, follows linked companions, and stops at collisions without removing
  blockers. See the Timeline contract and manual regression checklist.

- [x] Complete Windows Explorer validation for direct operating-system file
  drops. Automated coverage exercises the Media Browser and Timeline viewports
  used by the application. The maintainer confirmed real video, audio, image,
  bin, and ordered Timeline drops, including Undo/Redo, on 2026-10-07. Keep the
  import dialog and internal Media Browser drag workflow available.

- [x] Repeatedly migrate the same long-lived `.csp` project when advancing
  persisted-format versions. The owner reports doing this since the application
  was created and says the migrations have worked.

- [x] Validate save/reopen, invalid-project preservation, and recovery after
  reopening unsaved work. The owner reports that opening through recovery after
  forgetting to save has restored the project every time it was needed.

## Video Editor Roadmap / 4. Image Editor and Motion Studio coordination

- [x] Implement shared Media Pool links and isolated timeline image variants,
  persisted in `.csp` v10 with compatibility for versions 1 through 9.

- [x] Preserve original sources, publish saved PNG output atomically, and
  refresh affected previews asynchronously.

- [x] Add linked-document launch, stale-save protection, and automated coverage
  for key failure and stale-generation cases.

## Video Editor Roadmap / 5. Later Video Editor expansion

  - [x] Add shared CPU Grayscale, Brightness, Contrast, and Saturation filters
    for video and image clips, available through Effects and Functions with
    editable stacks, individual enable/bypass controls, and version 18 project
    persistence. Both panels can also drag the four visual filters into the
    Fusion graph; Functions remains open when a drop is canceled or rejected.

- [x] Display derived, cached waveforms on independent and linked Audio clips,
  with a global Mono/Stereo view preference and separate channel peaks.

- [x] Add clip-local volume automation with editable points, fade support,
  Undo/Redo, project persistence, and matching Preview/export mixing.

- [x] Add equal-power Audio Crossfades between adjacent independent clips on
  the same Audio track, with Undo/Redo, version 16 persistence, and shared
  Preview/export mixing.

- [x] Deliver shared composed textures directly, with bounded leases/fences,
  asynchronous recovery and transfer diagnostics; platform acceptance pending.

- [x] Add experimental per-layer GPU composition with CPU fallback and transfer
  measurements. Keep default and platform acceptance tied to recorded evidence.

- [x] Add independent per-job experimental GPU offline export through 4K, with
  readback, CPU fallback, transactional output and separate export metrics.
  Broader platform/driver/codec and human acceptance remain pending; see
  [GPU_EXPORT_RESULTS.md](../../video-editor/GPU_EXPORT_RESULTS.md).


## Historical implementation and verification note

### Approved foundation scope

Build on the implemented local-media, multitrack-editing, preview/playback,
transform, keyframe, text, essential-transition, embedded and independent
audio, project save/open, Undo/Redo, autosave, and recovery workflows. Complete
the release gates below and add operating-system file drops and the adjustable
YouTube preset. Preserve a clear,
discoverable interface with shortcuts for frequent actions.

Version 19 supports both approved canvas sizes. Projects from versions 1
through 18 continue opening as 16:9, and their stored or migrated Timeline FPS
behavior remains unchanged.

Waveforms, recording, advanced audio mixing, automatic captions, advanced color
grading, advanced masks, 3D layers, proxies,
advanced compositing, plugins, and full Fusion processing remain outside the
foundation. Continue evaluating these only as later milestones.
