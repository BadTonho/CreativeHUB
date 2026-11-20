# Video Editor Roadmap

The dedicated [Audio Workspace Roadmap](AUDIO_WORKSPACE_ROADMAP.md) records the
implemented editing/mixing, dialogue processing, ducking and voice isolation stages, and later audio candidates without a
release commitment.

The [Color Workspace Roadmap](COLOR_WORKSPACE_ROADMAP.md) records the current
graph, Gallery, and monitoring implementation, the full capability inventory,
and the staged work planned to expand color grading.

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
- GPU composition and hardware decoding are on by default (2026-11-20
  maintainer decision, provisional; stored opt-outs are kept). The shared
  OpenGL 3.2 Core backend composes the timeline preview through Settings >
  General, with CPU fallback and automatic shared-texture delivery, retaining
  worker readback when required. Offline export independently offers per-job
  GPU composition through 4K, also checked by default in Render > Video. The
  Windows/NVIDIA extension (hardware encoders, native NVENC, Vulkan) remains
  opt-in and retains decode,
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
and the final 101-test Windows regression gate on 2026-10-10. Hardware encoders
and Vulkan remain opt-in; human timing and other native platforms are pending.
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
  - [x] Add the Color correction graph, serial and parallel grading, and RGB waveform. Broader grading tools remain in the Color roadmap.
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
- **Audio:** loudness measurement and normalization; further voice separation,
  dereverberation and restoration; expanded recording, automation, and surround
  workflows. Routing, EQ/compression, basic cleanup/noise/repair, ducking and speech enhancement are
  implemented in the Audio workspace.
- **Compositing and effects:** spline and polygon masks; mask tracking; point
  and planar tracking; chroma/luma keying; rotoscoping; reusable effect
  templates; advanced compositing and 3D layers after a separate performance
  and scope review.
- **Delivery:** batch render queue; saved output profiles; still-frame and
  caption delivery; project interchange and relinking checks for expanded
  workflows.

Capability review (2026-11-20): the following candidate areas were added from a
review of what mature professional video editors offer that this application does
not yet provide. They remain unprioritized candidates; each needs its own scoped
contract, persistence and migration plan, preview/export parity coverage and
cross-platform review before implementation. Existing implemented behavior is not
repeated here.

- **Timeline editing workflow:** named timeline and clip markers (comment, chapter,
  range) with a marker list and export; lift/extract and three- or four-point
  source-to-timeline edits with a separate source viewer and in/out marks; track
  targeting and source patching; linked clips and groups; sync locks and clip
  enable/disable; selection by label or shared media; a rate-stretch tool; numeric
  trim entry and a dual-viewer trim mode; gap detection and automatic gap closing;
  subclips, ratings, keywords and favorite/reject flags; match-frame and
  go-to-edit navigation; a history list; find-in-timeline; configurable snapping
  per target type; timeline comparison and change lists for revised cuts.
- **Speed and timing:** constant speed and duration changes with pitch
  preservation, reverse playback, freeze frames, keyframed time remapping,
  frame blending and motion-compensated interpolation; mixed frame-rate timelines
  with interpolation; fractional and drop-frame rates, interlaced footage,
  anamorphic pixel aspect ratios, variable-frame-rate conforming, and a timeline
  resolution that differs from the project canvas; additional project canvases
  and rates beyond 16:9/9:16 and 24-60 fps.
- **Transitions and titles:** an expanded transition set (wipes, slides, pushes,
  zooms, iris shapes, luma/gradient wipes, dip-to-color and stylized transitions)
  with direction, edge, easing and keyframes; transition alignment, default
  transition and duration settings, and applying a transition to several cuts at
  once; selectable audio crossfade curves; rich text (per-run styling, kerning and
  leading, OpenType and variable fonts, text on a path), text animation presets and
  per-character animation, title templates with exposed controls; generators
  (bars and tone, countdown, timecode burn-in, solid, gradient, noise); rolling and
  crawling credits.
- **Effects catalog:** crop, mirror, corner pin, lens correction, stabilization with
  rolling-shutter repair, sharpen, noise reduction, film grain, vignette, glow and
  light effects, stylize effects; blend modes beyond the current set and
  layer-style effects (shadow, glow, stroke) for any clip; effect presets that can be
  saved and shared; copy/paste of effect stacks; keyframes for every effect
  parameter with Bezier, hold and ease interpolation, motion paths and
  speed/value graph views.
- **Color and monitoring:** project, timeline and output color transforms; LUT
  import, preview and placement in the graph; log decoding; wide-gamut display;
  tone mapping and HDR metadata; zebra, false color, gamut and clipping warnings,
  safe-area overlays and additional scopes. The staged color work (curves, tracking,
  managed color, restoration) stays in the
  [Color Workspace Roadmap](COLOR_WORKSPACE_ROADMAP.md).
- **Audio:** see the [Audio Workspace Roadmap](AUDIO_WORKSPACE_ROADMAP.md) for
  recording, automation, loudness, restoration and surround candidates. Additional
  candidates recorded here: per-clip fade handles with selectable curves, pitch-
  preserving time stretch, expanded EQ and dynamics (limiter, expander, gate,
  de-esser, multiband), reverb/delay/modulation, an audio plug-in host, spectrum and
  phase analysis, stem export, and music/voice separation.
- **Media management:** thumbnails and filmstrip display, hover scrubbing, proxies
  and optimized media, a background render cache, project consolidation and
  transcoding, smart bins driven by metadata rules, content search (visual and
  speech), camera RAW decoding, professional intermediate codecs, image sequences,
  alpha-channel footage and layered image documents.
- **Delivery:** presets for common publishing platforms with optional direct
  upload, simultaneous multi-format export, per-clip export, smart rendering that
  avoids re-encoding when formats match, export of in/out ranges, burn-in overlays,
  embedded metadata and chapters, audio-only and stem export, image-sequence and
  still export, and broadcast delivery formats with validation checks.
- **Interchange and conform:** EDL, AAF, XML and OpenTimelineIO import and export,
  relink and conform workflows, audio round trips with dedicated audio tools, and
  version comparison.
- **Interface and hardware:** floating and multi-monitor windows, preset shortcut
  maps from other editors, control-surface and hardware-panel support, command
  search, external monitor output with reference-monitor features, and adaptive
  playback resolution.
- **Extensibility:** a scripting API, a documented plug-in interface for effects and
  codecs, and automation hooks; define stability, security, licensing and ownership
  rules first.
- **Assisted editing (local-first):** silence and scene detection, automatic
  reframing for other aspect ratios, subject tracking, speech-to-text captions,
  beat detection and automatic color matching. Evaluate model size, licenses,
  privacy and whether processing is local or online before choosing an
  implementation; disclose any online processing explicitly.

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

Shared logical mask-raster maintenance for Motion Bezier paths: existing `creative-suite-composition`, `creative-suite-mask-interchange`, `creative-suite-main-editor-fusion-mask`, preview and render/export suites protect Video default coordinates and pixels. No Video feature/format is added; native acceptance remains separate.
