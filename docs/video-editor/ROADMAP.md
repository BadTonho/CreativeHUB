# Video Editor Roadmap

[Gaussian Blur](features/GAUSSIAN_BLUR.md) is a static, maskable Fusion-only
catalog node and an editable cscomp v3 stage. It shares CPU/GPU processing with
Motion, persists in CSP v31 and is covered by `creative-suite-blur-interchange`.

Current application version: **Beta 0.1.0**. This roadmap tracks open work,
release gates, and pending validation. Completed implementation checklists are
archived in [legacy documentation](../legacy/video-editor/IMPLEMENTED_ROADMAP_ITEMS.md).

See the [documentation index](README.md) for the organized references by topic.

## Documentation map

| Question | Canonical document |
| --- | --- |
| What works in the application today? | [Current scope](architecture/SCOPE.md) |
| How do the native project and effects contracts work? | [Project format](architecture/PROJECT.md), [effects](architecture/EFFECTS.md), and the feature guides below |
| What is implemented, planned, or gated? | This roadmap |
| Which automated and manual checks cover the behavior? | [Regression Testing](validation/REGRESSION_TESTING.md) and its linked manual/platform evidence |
| How do supported cross-application workflows behave? | [Composition interchange](../COMPOSITION_INTERCHANGE.md) and [cross-application compatibility](../CROSS_APPLICATION_COMPATIBILITY.md) |

Editable Fusion/Motion composition interchange uses independent `.cscomp` files
for the documented compatible subset. Full graph conversion remains future
work; see [contract and acceptance](../COMPOSITION_INTERCHANGE.md).

The implemented 0.1.1 features and their completed checks are archived in the
linked history. Current behavior is described in [Effects](architecture/EFFECTS.md),
[Background Generator](features/BACKGROUND_GENERATOR.md), [Fusion Masks](features/FUSION_MASKS.md),
[Fusion Composition](features/FUSION_COMPOSITION.md), and the
[Regression Testing guide](validation/REGRESSION_TESTING.md). Release acceptance and
platform validation remain tracked below and in those guides.

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
  composition through 4K. The opt-in Windows/NVIDIA extension retains decode,
  effects/Fusion, composition, and NVENC input on the GPU, with RGBA/CPU recovery. Qt OpenGL presents
  the final preview frame. See the
  [rendering boundary](architecture/RENDERING.md).
- The Image Editor's standalone acceptance gate comes before acceptance of
  its linked-image workflow. Motion Studio development may proceed in
  parallel; cross-application integration remains gated on stable contracts
  and producer/consumer validation. Track the independent milestones in the
  [Image Editor roadmap](../image-editor/ROADMAP.md) and
  [Motion Studio roadmap](../motion-editor/ROADMAP.md).

## GPU acceleration planning

The [GPU acceleration plan](gpu/GPU_ACCELERATION_PLAN.md) records the implemented
optional timeline composition, direct delivery and offline export stages, followed
by the [native visual pipeline](gpu/GPU_PIPELINE.md) and platform acceptance.
Video Editor and Motion Studio consume the shared compositor; Image Editor
uses shared deterministic composition and image handoff contracts.

**Status: Stages 1–3 implemented, experimental, disabled by default.** Direct
texture presentation has RGBA and CPU fallbacks. Export has its own per-job choice
in Render > Video, with explicit recovery when native operations are unavailable.
Experimental D3D11 decoding, built-in effects/Fusion, native NVENC, and additional
hardware encoder discovery extend these stages; their device acceptance is
recorded separately in [GPU_PIPELINE.md](gpu/GPU_PIPELINE.md). See the
[delivery contract](gpu/GPU_TEXTURE_DELIVERY.md) and [export contract](gpu/GPU_EXPORT.md).
The Windows/NVIDIA implementation passed explicit H.264/HEVC native integration
and the final 101-test Windows regression gate on 2026-10-10. Hardware options
remain disabled by default; human timing and other native platforms are pending.
The 15-minute offscreen run completed 27,000 native GPU frames without fallback
or readback. Its recorded latency spikes keep smooth physical 30 fps playback
acceptance pending; see the pipeline's prolonged-run statistics.
Native parity/benchmark and build/test evidence is recorded in
[GPU_COMPOSITION_RESULTS.md](gpu/GPU_COMPOSITION_RESULTS.md) and
[GPU_EXPORT_RESULTS.md](gpu/GPU_EXPORT_RESULTS.md). Further OS/driver and
human audio/visual acceptance remain pending; formats and versions are unchanged.

## Status legend

- `[ ]` Not started
- `[-]` In progress
- `[!]` Blocked or requiring a decision

## Remaining foundation implementation

- [-] Functional Fusion node graphs for selected video and image clips:
  Input, Transform, Color, Merge, Output, and the existing Grayscale,
  Brightness, Contrast, and Saturation effects as draggable graph nodes;
  Media Pool video/image inputs; cable insertion; shared Preview/export
  evaluation; animated Transform and Brightness parameters; v22 graph
  persistence; and Timeline Undo/Redo. Automated coverage
  now includes per-node evaluation, disconnected-node output, isolated worker
  preview, temporary Viewer targeting, and workspace entry/exit behavior. Keep
  animation of Transform position/scale/rotation/opacity and Brightness amount,
  and v22 persistence. Focused automated tests passed on 2026-10-08; visual
  checks of node indicators, animated playback, save/reopen, Undo/Redo, and
  Timeline restoration remain pending until performed in the Release build. See
  [Current Scope](architecture/SCOPE.md),
  [Project Persistence](architecture/PROJECT.md), and [Regression Testing](validation/REGRESSION_TESTING.md).
See [Current Scope and Non-goals](architecture/SCOPE.md) for implemented
behavior and deferred scope, and [Project Document and Persistence](architecture/PROJECT.md)
for the active `.csp` schema and migrations.

## 1. Video Editor foundation completion gates

Complete these gates against the approved foundation scope. A feature counts
as complete when its automated regression coverage and required manual
validation pass.

- [ ] Add the adjustable YouTube preset for 1080p SDR using the approved
  project canvas and frame rate. Validate its MP4/H.264 and AAC-LC combination,
  Fast Start, default bitrates, encoder availability, license constraints, and
  unavailable-preset message against each shipped FFmpeg build. Keep generic
  export options available when the preset cannot run.
- [ ] Validate practical export compatibility across the intended shipped
  FFmpeg builds and supported platforms. Measure quality and performance,
  verify hardware-encoder combinations, and complete manual export validation
  with representative video, image, text, transition, and audio projects.
- [-] Fusion graph and node-animation code, tests, and documentation are in
  place. The focused animation, graph, Timeline, project, Fusion workspace, and
  Preview/Render tests passed on 2026-10-08. The canonical Windows Release app
  build and Qt runtime deployment completed successfully on 2026-10-08. The
  executable is `build/apps/video-editor/Release/creative-suite-video-editor.exe`
  (last modified 2026-10-08 00:24:26) and launched successfully. Manual checks
  of animated playback, save/reopen, Undo/Redo, and Timeline restoration remain
  pending.
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

## 2. Image Editor and Motion Studio coordination

The Video Editor already contains the initial linked-image implementation.
Its broader acceptance is tracked in the Image Editor roadmap and is gated by
the standalone Image Editor acceptance criteria. This gate applies within the
Image Editor track; Motion Studio development may continue in parallel.

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

## 3. Later Video Editor expansion

Prioritize these workflows after the Video Editor foundation using user needs,
performance measurements, and Video Editor stability as the decision criteria.

- [ ] Improve media organization, synchronization, relinking, and asset
  management.
  - [ ] Expand clip color correction into broader grading workflows.
- [ ] Expand audio editing with recording, audio-only file export, track
  automation, and advanced mixing as user workflows require.
- [-] Rectangle/Ellipse masks implemented; advanced mask authoring and compositing remain pending.
- [ ] Add proxies, incremental rendering, and broader cache workflows where
  measurements show a need.
- [ ] Profile representative projects and address measured performance and
  memory bottlenecks.
- [ ] Establish startup, memory, and playback/preview baselines on the
  maintainer's reference PC and additional systems; set numeric minimum
  hardware requirements from those measurements.
- [ ] Revisit plugin architecture, additional export presets, and templates
  after stable public workflows and extension points are known.

### Candidate feature backlog — unprioritized

These capabilities are candidates for future scope, not approved release
commitments. Prioritize them against user workflows, local-first operation,
performance, licensing, and maintenance cost.

- **Editing and media:** synchronized multicamera editing; nested sequences;
  dedicated ripple, roll, slip, and slide trim operations; scene-change
  detection; speed remapping with ramps and high-quality frame interpolation;
  transcript search and text-based rough cuts; caption creation, styling, and
  export.
- **Color:** RGB and hue/saturation curves; secondary color selection; tracked
  masks and power windows; comparison and waveform/vectorscope displays; shot
  matching; managed wide-gamut and HDR workflows with explicit input/output
  transforms and LUT handling.
- **Audio:** track routing and submixes; equalization and dynamics processing;
  loudness measurement and normalization; dialogue cleanup, noise reduction,
  ducking, and repair; expanded recording, automation, and surround workflows.
- **Compositing and effects:** spline and polygon masks; mask tracking; point
  and planar tracking; chroma/luma keying; rotoscoping; reusable effect
  templates; advanced compositing and 3D layers after a separate performance
  and scope review.
- **Delivery:** batch render queue; saved output profiles; still-frame and
  caption delivery; project interchange and relinking checks for expanded
  workflows.

## 4. Deferred technical and ecosystem work

- Keep the Rust prototype archived. Reopen a Rust comparison only if a concrete
  technical need makes Rust a primary candidate again. A final language
  decision would require the cross-platform prototype evidence described in
  [Technical Prototype Comparison](decisions/TECHNICAL_PROTOTYPE_COMPARISON.md).
- Native crash dumps, translations, and broader contributor materials should
  be prioritized against release needs and maintainer capacity.
- Do not set delivery dates until product scope, platform packaging, and
  project capacity are known.

## Working rule

Update this roadmap and the relevant architecture or validation document in
the same change when implementation, scope, or a technical decision changes.
Do not mark a gate complete without its required automated and manual evidence.

## Selectable graphics renderer

An experimental Vulkan backend is implemented alongside OpenGL/CPU for Video
Editor preview, Fusion and RGBA export. Startup selection is application-local;
GPU composition and codecs retain separate controls. See
[Vulkan implementation and acceptance](gpu/VULKAN_RENDERER.md).
Native Linux/macOS and broader manual acceptance remain pending.
