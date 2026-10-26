# Motion Studio Roadmap

[Gaussian Blur interchange](../COMPOSITION_INTERCHANGE.md) preserves mixed
ordered Color/Blur stages and shared masks through cscomp v3. Motion native v8
is unchanged; CSP advances to v31. Producer/consumer regression coverage is
registered as `creative-suite-blur-interchange`; native platform acceptance
remains separate.

This roadmap tracks open work, release gates, and feature-to-verification
coverage. Completed implementation checklists are archived in
[legacy documentation](../legacy/motion-editor/IMPLEMENTED_ROADMAP_ITEMS.md).

See the [documentation index](README.md) for organized references by topic.

## Documentation map

| Question | Canonical document |
| --- | --- |
| What is in the approved MVP and what is its readiness? | [Scope and readiness](SCOPE_AND_READINESS.md) |
| How are native documents stored and migrated? | [Native format](architecture/FORMAT.md) |
| How do masks and editable handoff work? | [Masks](features/MASKS.md) and [composition interchange](../COMPOSITION_INTERCHANGE.md) |
| Which tests and manual checks cover each feature? | [Regression coverage index](#regression-coverage-index) in this roadmap |
| How is shared code reuse planned? | [Reuse plan](architecture/REUSE_PLAN.md) |

Use the linked scope, format, and contract documents for current behavior; use
this roadmap for remaining work and its regression index for evidence and
pending acceptance.

Current application version: **Beta 0.1.0**.

Status: **2D MVP product scope approved; implementation is substantially
complete, with Windows acceptance in progress.** See
[SCOPE_AND_READINESS.md](SCOPE_AND_READINESS.md) for current implementation
and open readiness items, [REUSE_PLAN.md](architecture/REUSE_PLAN.md) for shared-library
boundaries, and [FORMAT.md](architecture/FORMAT.md) for the native document contract.

Motion Studio may be developed in parallel with the Video Editor and Image
Editor on an independent track. Cross-application integrations still depend on
stable contracts and validated producer/consumer behavior. Motion Studio
focuses on advanced motion design and compositing, while the applications share
media, rendering, animation, and project services when those boundaries are
technically clear.

## Principles

- Keep Video Editor stability as a priority while developing Motion Studio in
  parallel with the other application tracks.
- Scope early Motion Studio work so it can proceed independently of unfinished
  Image Editor milestones; gate shared services and handoffs on stable APIs.
- Reuse shared media, rendering, animation, caching, and recovery services
  instead of building duplicate engines.
- Keep each application's Media Pool UI and document lifecycle independent,
  while sharing neutral media metadata, decoding, and catalog behavior.
- Keep video frames and GPU resources shared or referenced efficiently across
  module boundaries.
- Validate performance, memory use, startup time, licenses, and all three
  target operating systems before recording technical decisions as final.
- Keep this roadmap provisional while technical choices and later release
  gates remain unvalidated.

## GPU acceleration planning

The [GPU acceleration plan](gpu/GPU_ACCELERATION_PLAN.md) records four deliveries:
layer composition, GPU effects, preview/export integration, and adoption by the
other applications. Video Editor is the first consumer of the shared OpenGL
compositor; Motion now has opt-in experimental preview and export integration
with CPU fallback.

**Status: experimental preview composition, Color Adjustment, Gaussian Blur, Glow,
offline export integration, and direct texture preview implemented; Windows
reference preview measurements verified, broader acceptance pending.** Color Adjustment-only stacks run in the composition
shader, while stacks containing Gaussian Blur or Glow use their shared ordered
GPU image paths when supported. Glow falls back to CPU when OpenGL or Vulkan
cannot process it. `CREATIVE_SUITE_MOTION_GPU_COMPOSITION=1` opts both preview
and export into the backend; both remain CPU-only by default. Export reads the
composed result back to RGBA for the existing CPU encoder.
[Controlled Windows reference measurements](performance/GPU_PREVIEW_RESULTS_WINDOWS_2026-10-10.md)
record direct-preview improvements; cross-platform driver acceptance remains
pending. Direct preview now uses shared
texture delivery with bounded resources and CPU recovery; see
[GPU_TEXTURE_PREVIEW.md](gpu/GPU_TEXTURE_PREVIEW.md) for contracts and native checks.

## Cross-application updates

[Shape-mask interchange](../COMPOSITION_INTERCHANGE.md) adds editable shared
Rectangle/Ellipse masks on sources, effects and Transform. Coverage and scalar
curve slicing are shared; the mask delivery introduced Motion v8; Gaussian Blur now advances CSP to v31.
Automated coverage: `creative-suite-mask-interchange`, shared composition/
OpenGL, native Motion preview/export/history/persistence, and Hub backup gates.
Native UI and platform acceptance remain separately pending.

The first [editable composition interchange](../COMPOSITION_INTERCHANGE.md)
transfers compatible layer stacks to Fusion and back. Masks on Merge/Output
and group processing require later work. Gaussian Blur interchange uses v3. Native menu/platform
acceptance remains separate from automated coverage.

The shared engine adds straight-RGBA ApplyMask/MaskedBlend for Video Editor
Fusion masks. Graphs without masks retain their previous rendering behavior.
The Windows Debug consumer gate includes Image interactive/GPU raster and Motion
preview/GPU composition/export, plus shared composition CPU/OpenGL tests.
Native macOS/Linux acceptance remains separate and pending.

## Milestones

### 1. Technical validation and gap audit

The completed scope/readiness checklist is archived. The approved product
boundary and current readiness are summarized in
[SCOPE_AND_READINESS.md](SCOPE_AND_READINESS.md).

- [ ] Revalidate the applicable existing paths on Windows; record GPU runtime
  support separately from GPU presentation of CPU-composed frames.
- [ ] Measure startup, memory, timeline/seek response, preview latency, and
  render performance for representative small, medium, and heavy compositions.
  Include the approved 1080p/30 fps, 10-second, five-layer reference workload;
  use the maintainer's Windows PC as the reference system, and document
  measured targets and unmet limits. Validate additional Windows systems
  before generalizing hardware requirements.
- [ ] Record dependency and asset licenses, output profile/codec findings, and
  alternatives needed to resolve the identified gaps before choosing a
  renderer or other technology.

**Windows-phase exit criteria:** existing evidence, Motion-specific gaps,
Windows reference results, performance targets, and remaining alternatives are
documented. Implementation choices remain provisional until the required
cross-platform prototype and release-readiness validation are complete.

### 2. Composition foundation

The completed implementation checklist is archived. Current behavior and
contracts are summarized in [Scope and readiness](SCOPE_AND_READINESS.md) and
[Native Format](architecture/FORMAT.md); the remaining technical validation items stay here.

- [-] Compare effect worker limits on Windows using the same 1920 × 1080,
  60 fps composition with radius-10 Gaussian Blur and neutral Color Adjustment.
  Measure automatic mode and limits 1, 2, 4, and 8 in three 10-second runs each;
  compare blur and total frame-render averages, decode time, rendered/coalesced
  requests, and CPU use. Keep the automatic default unless playback-wide results
  show a repeatable improvement. Confirm pixel equality.
- [-] Extend actionable local error logging and automated coverage for the
  remaining document, rendering, and application boundaries. Save/open and
  export failures are logged before concise user feedback.

Detailed automated and manual verification is maintained in the
[regression coverage index](#regression-coverage-index). The implementation
checklist and its older test summary are archived in
[legacy documentation](../legacy/motion-editor/IMPLEMENTED_ROADMAP_ITEMS.md).

## Regression coverage index

The automated targets are registered in
[`apps/motion-editor/tests/CMakeLists.txt`](../../apps/motion-editor/tests/CMakeLists.txt).
This map ties current MVP behaviors to their test sources; detailed manual
readiness checks remain in this roadmap and
[`SCOPE_AND_READINESS.md`](SCOPE_AND_READINESS.md).

| Behavior group | Automated evidence | Manual evidence and status |
| --- | --- | --- |
| Per-app language selection loads the downloaded/bundled pt-BR catalog before the editor window and falls back to English | `motion_editor_translation_test.cpp` (`creative-suite-motion-editor-translation-catalog`) checks the General Settings selector, saved preference resolution, bundled loading, regional Portuguese selection, visible menu text, placeholders, missing-text fallback, and unsupported-locale fallback; shared `creative-suite-translations` checks preference isolation and manifest integrity | Startup-before-window code path and automated preference/catalog checks are present. Native Windows/macOS/Linux locale and clipping checks remain pending in [translation acceptance](../TRANSLATIONS_AND_LANGUAGE_PACKS.md#regression-and-acceptance). |
| Hub snapshot references and restoration across versions 1–14 | `apps/hub/tests/test_backup_formats.cpp` (`hub-backup-format-test`), registered in `apps/hub/tests/CMakeLists.txt`; actual Motion persistence producer and consumer | Automated coverage present for Media Pool, layer sources, linked-image sidecars, and opening vault/relocated snapshots. Native UI and macOS/Linux checks remain pending in [Hub backups](../hub/BACKUPS.md). |
| Layer Track Mattes | `track_matte_test.cpp` (`creative-suite-motion-editor-track-matte`): model/UI/history, numeric pixels, chains/timing/offline sources, affine/masks/gesture overrides, preview/export/cache, v14/migration/recovery/invalid Open/interchange. Shared CPU/OpenGL/Vulkan and Hub/Video/Image gates protect boundaries. | [Matte guide and pending checks](features/TRACK_MATTES.md); visual/platform/native presentation acceptance and benchmarks pending. |
| Transparent video export | `motion_video_export_test.cpp` covers the profile selector, alpha adapter choice, output extension and decoded transparent lead-in; `libs/tests/composition_test.cpp` covers empty, partial-alpha and blend-mode CPU pixels; `libs/tests/opengl_composition_test.cpp` covers CPU parity and synchronous/asynchronous alpha readback; `libs/media/tests/video_encoder_test.cpp` covers runtime adapter filtering, unsupported-pair rejection and alpha round-trip for every available adapter. | [Video export guide](features/VIDEO_EXPORT.md); regression sources are added but were not executed in this implementation stage. Native UI, codec/platform matrix, visual and performance acceptance remain pending. |
| Transparent PNG sequence export | `motion_video_export_test.cpp` covers destination naming/collision, exact frame count, hidden-layer duration, Null exclusion, dimensions, transparent and partial alpha, CPU fallback, cancellation cleanup, write failure cleanup, dialog state and File menu export. | [PNG sequence export guide](features/IMAGE_SEQUENCE_EXPORT.md); regression sources are added but were not executed in this implementation stage. Native UI, platform and representative large-sequence acceptance remain pending. |
| Static layer blend modes | `blend_mode_test.cpp` (`creative-suite-motion-editor-blend`): model/UI/history, independent preview/export values, cache/mask/affine behavior, v13/migration/recovery/invalid Open and interchange. Shared CPU/OpenGL/Vulkan and Hub backup tests cover the contracts; Video/Image consumer gates protect Normal behavior. | [Blend guide and pending checks](features/BLEND_MODES.md); visual/native GPU/platform acceptance and benchmarks pending. |
| Independent Scale X/Y and affine inheritance | `layer_scale_test.cpp` (`creative-suite-motion-editor-scale`): proportional/independent editing, curves/history/UI, known matrices, parenting/Anchor, v12/recovery/migration/interchange and independent preview/export references; shared composition/OpenGL/Vulkan, Motion GPU/audio and Hub backup suites protect the boundaries. | [Scale guide and pending checks](features/SCALE_AXES.md); visual/native GPU/platform acceptance and benchmarks pending. |
| Animated pixel Anchor Point | `layer_anchor_test.cpp` (`creative-suite-motion-editor-anchor`): geometry, compensation/history, parenting, Inspector/timeline/Graph Editor/dialog/marker, v11/recovery/v1–v10 migration, preview/export parity and interchange refusal; GPU and Hub backup tests cover their affected boundaries. | [Feature guide and pending manual checklist](features/ANCHOR_POINT.md); native visual/platform/GPU acceptance and benchmarks remain pending. |
| Viewer Pan Behind pivot dragging | `pan_behind_test.cpp` (`creative-suite-motion-editor-pan-behind` and `...-pan-behind-hidpi`): hit/threshold/logical mapping, grab offset, live Inspector/document isolation, cancellation/history, Nulls, affine/offline ancestors, keys, pixels/cache reuse, v12 save/autosave/recovery; translation and GPU texture regressions cover presentation boundaries. | [Pan Behind guide and pending checks](features/PAN_BEHIND.md); manual visual/platform/native GPU acceptance and benchmarks pending. |
| Viewer Position drag | `position_drag_test.cpp` (`creative-suite-motion-editor-position-drag` and `...-position-drag-hidpi`): logical hit/threshold/fractional/final coordinates, affine geometry, static/animated atomic editing, key budget, live content/Inspector isolation, history/Redo, lifecycle/context cancellation, autosave/v12/recovery, source/mask/cache parity and bounded session scheduling/failure/recovery. Existing UI, preview/export and GPU suites protect adjacent contracts. | [Position guide and pending checks](features/POSITION_DRAG.md); visual/platform/native GPU acceptance and benchmarks pending. Known OpenGL presentation failure remains separate. |
| Viewer Rotation drag | `rotation_drag_test.cpp` (`creative-suite-motion-editor-rotation-drag` and `...-rotation-drag-hidpi`): logical hit/threshold/final coordinates, neutral zone, full turns, affine references and fixed pivot, static/animated editing/key budget, live content/Inspector/document/autosave isolation, history/Redo, cancellation, v12/recovery, source/mask/cache/export parity and bounded Position/Rotation sessions/fallback/failure. | [Rotation guide and pending checks](features/ROTATION_DRAG.md); visual/platform/native GPU acceptance and benchmarks pending. Known OpenGL presentation failure remains separate. |
| Viewer Scale X/Y drag | `scale_drag_test.cpp` (`creative-suite-motion-editor-scale-drag` and `...-scale-drag-hidpi`): control mapping, linked/free projection, positive clamps, affine/pivot geometry, static/animated edits, implicit-Y materialization/key budget, transient Inspector/document/autosave isolation, history/Redo/cancellation, v12/recovery/interchange, mask/cache/export parity and bounded shared sessions. | [Scale drag guide and pending checks](features/SCALE_DRAG.md); visual/platform/native GPU acceptance and benchmarks pending. Known OpenGL presentation failure remains separate. |
| Timeline Parent & Link pick-whip | `parent_pick_whip_test.cpp` (`creative-suite-motion-editor-parent-pick-whip` and `...-parent-pick-whip-hidpi`): shared eligibility, icon/threshold/feedback/final coordinates, expanded rows, wheel/deterministic autoscroll, cancellation/Redo, one history entry, affine geometry/pixels, Inspector parity, v12/autosave/recovery and technical logs; UI, Pan Behind and translation suites protect adjacent behavior. | [Pick-whip guide and pending checklist](features/PARENT_PICK_WHIP.md); visual/platform acceptance and benchmarks pending. Known OpenGL presentation failure remains separately recorded. |
| Layer parenting and Null controls | `layer_transform_hierarchy_test.cpp` (`creative-suite-motion-editor-hierarchy`): graph validation, pixel-space evaluation, current-pose preservation, Undo/Redo, native parenting/recovery/migration, Inspector, CPU preview/export, and `.cscomp` rejection; `gpu_composition_test.cpp` covers the inherited-transform GPU/CPU boundary; `hub-backup-format-test` reopens parented documents with the current v12 reader. | [Manual hierarchy checklist](features/HIERARCHY.md#pending-manual-checks); native platforms and benchmarks remain pending. |
| Composition model, validation, layer data, and timing | `composition_document_test.cpp` (`creative-suite-motion-editor-document`) | Pending manual composition and supported-layer checks remain part of MVP acceptance. |
| Undo/Redo and edit history | `composition_history_test.cpp` (`creative-suite-motion-editor-history`) | Pending manual validation: confirm interactive edit sequences during the broader Windows UI checklist. |
| `.motion` save/open, migrations, and invalid-file preservation | `motion_document_store_test.cpp` (`creative-suite-motion-editor-persistence`), `motion_editor_ui_test.cpp` (`creative-suite-motion-editor-ui`) | Automated round trips and migration coverage exist. The owner reports repeatedly migrating the same long-lived project across persisted-format versions and says the migrations have worked. Windows invalid-file and path results remain part of Windows acceptance. |
| Autosave and restart recovery | `motion_recovery_store_test.cpp` (`creative-suite-motion-editor-recovery`), `motion_editor_ui_test.cpp` | Automated snapshots and UI recovery paths exist. Basic recovery was reported working on the Windows 11 reference PC on 2026-10-01; detailed restart/recovery scenarios remain pending (**P0 validation**). |
| Preview, transforms, curves, and layer effects | `preview_renderer_test.cpp` (`creative-suite-motion-editor-preview`), `motion_editor_ui_test.cpp`; Glow CPU/mask/history/Inspector/Graph Editor in `drop_shadow_test.cpp`, decoded export and GPU fallback/parity in `motion_video_export_test.cpp`, and separate metrics in `performance_metrics_test.cpp`; shared evaluator in `libs/tests/animation_test.cpp` and `libs/tests/composition_test.cpp`; shared pixels/OpenGL/Vulkan in `libs/tests/effects_test.cpp`, `opengl_composition_test.cpp` and `vulkan_composition_test.cpp` | Glow regression sources were added but not executed in this work stage. Real-hardware visual output and graphics-driver acceptance remain pending (**P2 validation**). See the [Glow regression boundary](#glow-regression-boundary). |
| Experimental GPU preview and offline export | `gpu_composition_test.cpp` (`creative-suite-motion-editor-gpu-composition`), `motion_video_export_test.cpp` (`creative-suite-motion-editor-export`), `performance_metrics_test.cpp` (`creative-suite-motion-editor-performance`), and shared `libs/tests/opengl_composition_test.cpp` (`creative-suite-composition-opengl`) | Automated coverage checks CPU fallback parity, GPU/CPU frame parity, worker-owned context lifecycle, two-PBO FIFO/reuse/backpressure, cancellation, budget fallback, mid-pipeline CPU recovery, and schema-3 metrics. The current Windows selection passes 16/16 in Debug and Release, including export; native lossless GPU export is checked separately with `--require-gpu`. See [current results and codec limits](performance/GPU_PREVIEW_RESULTS_WINDOWS_2026-10-10.md). Three post-change Windows GPU exports and native macOS/Linux driver checks remain pending (**P2 validation**). |
| Direct GPU texture preview | `gpu_texture_preview_test.cpp` (`creative-suite-motion-editor-gpu-texture`); native `--require-gpu`; `performance_metrics_test.cpp`; explicitly invoked `gpu_preview_benchmark.cpp` | Portable mailbox/fallback checks and native Windows texture parity, orientation, seek/replacement, resize, presentation recovery, pool backpressure, guide/context menu, GPU RGBA fallback without sharing, and actual MainWindow Play/Pause/Loop/shutdown pass in Debug and Release. Three 1080p five-layer trials per route at 30/60 fps confirm zero direct readback and reduced latency; see [Windows results](performance/GPU_PREVIEW_RESULTS_WINDOWS_2026-10-10.md). Broader manual and native macOS/Linux checks remain pending. |
| Performance diagnostics | `performance_metrics_test.cpp` (`creative-suite-motion-editor-performance`), `motion_video_export_test.cpp` (`creative-suite-motion-editor-export`) | Earlier Windows Release evidence passed all 11 Motion CTest targets before asynchronous readback. Existing 33.79 fps CPU and 54.96 s GPU exports remain historical; their export did not record its layer/effect setup. The current schema-3 summary records backend, async mode and transfer counts, CPU-side submission/wait/copy timings, staging and encoder queue peaks, fallbacks, and failures. Three post-change GPU runs use 54.96 s as baseline and 52.21 s as the 5% target; see [`PERFORMANCE_RESULTS_WINDOWS_2026-10-07.md`](performance/PERFORMANCE_RESULTS_WINDOWS_2026-10-07.md). |
| Video export controls and opaque output | `motion_video_export_test.cpp` (`creative-suite-motion-editor-export`), shared `libs/media/tests/video_encoder_test.cpp` | Automated coverage includes output parity and order, cancellation with submitted GPU work, prior-destination preservation, injected encoder-thread failure, missing-surface CPU fallback, staging-budget synchronous fallback, and mid-pipeline CPU recovery. The current Windows selection passes 16/16 in Debug and Release, including export; native lossless GPU export is checked separately with `--require-gpu`. See [current results and codec limits](performance/GPU_PREVIEW_RESULTS_WINDOWS_2026-10-10.md). GPU export remains opt-in pending the post-change performance result (**P1/P2 validation**). Transparent export coverage is tracked in its own row; its new regression sources were not executed. |
| Optional audio-to-transform keyframe generation | `audio_keyframe_generation_test.cpp` (`creative-suite-motion-editor-audio-keyframes`) | Automated coverage includes per-frame RMS, whole-file peak normalization past the layer boundary, empty and silent sources, invalid audio, atomic track replacement, Undo/Redo, `.motion` round-trip, worker success and progress callbacks on the receiver thread, pre-start cancellation without an error log, and failure logging with operation, path, and layer ID. Manual generation and cancellation during an active UI analysis remain pending. |
| Startup, application UI, and closing during linked-media preparation | `motion_editor_startup_test.cpp` (`creative-suite-motion-editor-startup`), `motion_editor_ui_test.cpp` (`creative-suite-motion-editor-ui`) | Startup and Media Pool bin filtering have offscreen coverage. Manual regression: start opening a composition with linked media, close Motion Studio while the preparation dialog is active, confirm close, then verify no dialog reappears and no document is installed after close; cancel the close prompt once and verify the open continues normally. This lifecycle check remains pending. The current UI has no separate media-search control. Broader Windows visual and graphics-driver checks remain pending (**P2 validation**). |
| Motion Studio to Video Editor editable handoff | Motion `composition_document_test.cpp` (`creative-suite-motion-editor-document`) covers source-in mapping across frame rates; `preview_renderer_test.cpp` and `motion_video_export_test.cpp` (`creative-suite-motion-editor-preview` / `creative-suite-motion-editor-export`) cover the preview and export paths; `motion_document_store_test.cpp` (`creative-suite-motion-editor-persistence`) covers `.motion` v5 source-in round-trip, v1-v4 migration, handoff request validation, and Unicode paths. Video Editor `project_file_test.cpp` (`creative-suite-main-editor-project`) covers `.csp` v24 links and v23 compatibility; `timeline_model_test.cpp` covers clip state preservation; `application_media_services_test.cpp` and `media_library_test.cpp` cover the linked Media Pool item and refreshed video presentation. | Contract, format, rendering, timeline, and Media Pool service coverage is present. End-to-end launch, multiple save/refresh cycles, reopen, stale-callback rejection, concurrent-writer recovery, and output-profile validation remain pending manual checks in both applications. |
| Motion Studio to Image Editor layer link | `composition_document_test.cpp` (`creative-suite-motion-editor-document`) covers image-only link assignment and per-layer path isolation; `motion_document_store_test.cpp` (`creative-suite-motion-editor-persistence`) covers v6 round-trip, v1-v5 migration, and rejection of shared sidecars; `motion_editor_ui_test.cpp` covers the Layer action, timeline and composition-canvas context menus, multiple asynchronous PNG refreshes, Unicode paths, corrupt-publication fallback, and independent Save As copies; `motion_video_export_test.cpp` (`creative-suite-motion-editor-export`) resolves same-source still frames by layer ID. Image Editor linked publication remains covered by `image_editor_ui_test.cpp` (`creative-suite-image-editor-ui`). | Automated consumer coverage is present; the Image Editor producer UI suite was blocked by its unrelated Magic Wand geometry failure on this Windows run. Manual save/refresh, missing-file recovery, and cross-app reopen checks are pending (**P1 validation**); acceptance remains gated on the Image Editor standalone minimum. |

The Motion UI linked-image fixture also publishes a PNG from an Image Editor
document with a non-neutral Hue/Saturation color-range adjustment before
verifying the Motion preview and a subsequent refresh.

### Windows updater coverage

The shared updater tests in `libs/updater/tests/` cover catalog validation,
version comparison, download resume/cancellation, network failure, and hash
rejection (`updater-catalog-test`, `updater-service-test`). The additional
`updater-runtime-test` covers schema-1 pending persistence, independent editor
waits, self-update shutdown, and per-app launch failures. Motion Studio must
update only its own installation. Packaged Windows validation is pending for
install-path reuse, preserving compositions and preferences, installer
rollback, and Hub restoration. See [`WINDOWS_UPDATES.md`](../WINDOWS_UPDATES.md).

### Current coverage gaps and pending validation

- **P0 — critical paths mapped:** composition validation,
  persistence/migration, startup, and automated recovery each have registered
  test sources. This inventory did not identify a specific P0 behavior with
  neither automated nor documented manual coverage. The owner reports
  repeatedly migrating the same long-lived project across persisted-format
  versions and says the requested basic persistence/recovery checks worked on
  the Windows 11 reference PC, with migrations treated as passed. The
  maintainer reports running tests and a video export on Windows; that basic
  run is complete. Detailed
  restart, invalid-file, and recovery scenarios still need individual results
  for full acceptance.
- **P1 — export acceptance:** the maintainer reports a Windows test/export run.
  The remaining export work in this phase is performance profiling and
  recording any issue found in the tested workflow.
- **P2 — performance acceptance:** an initial Windows run is recorded in
  [`PERFORMANCE_RESULTS_WINDOWS_2026-10-07.md`](performance/PERFORMANCE_RESULTS_WINDOWS_2026-10-07.md).
  Profile Gaussian Blur first, then record the approved 1080p/30 fps,
  10-second, five-layer workload on the Windows reference PC.
- **In progress:** the first optional audio-reactive tool analyzes a local
  audio file and bakes its RMS envelope into a selected transform track.
  Regression sources have been added; execution and manual acceptance remain
  pending. Audio playback, mixing, and audio export remain outside this tool.
- **In progress:** linked composition handoff now has an initial implementation
  in both applications. Complete the documented manual producer/consumer
  validation and remaining runtime refresh/failure regressions before accepting
  this integration.

Frame rates are stored as exact rational values from the supported common-rate
list. Creating a composition does not ask for or set its duration. The ruler
starts with a one-hour navigation range, calculated from the selected exact
frame rate; fractional rates round the frame count up. This range is not a
composition end. Seeking and frame stepping stay within the current range.
At 100%, one hour fits in the timeline. Zoom uses the Video Editor's discrete
levels from 25% to 51,200%, preserves the playhead's screen position, and
scrolls horizontally when the current range no longer fits. The ruler and layer
rows share the same viewport mapping; ruler tick spacing adapts to the visible
frame range and the width of labels in the selected display mode. The timeline
defaults to elapsed `HH:MM:SS.mmm` time calculated from the exact rational frame
rate; **Frames** switches the ruler and playhead readout to frame numbers. This
display selection does not affect frame-based navigation or editing and resets
to **Time** for a new composition. Zoom, scroll, and display mode are UI state
and are not persisted. Dragging past the actual range end extends it
by one hour once per gesture and moves the playhead to the new end; scroll to
the end first if it is offscreen. A drag at the visible viewport edge does not
extend a range whose end is still offscreen. The range saturates at the signed
64-bit frame limit, starting at zero.

Image and video layers reference canonical Media Pool paths, and repeated uses
of one source receive distinct layer IDs. Still images last
`ceil(5 * composition FPS)` frames. Video insertion requires a positive source
frame rate plus either a positive frame count or positive duration; its full
source length is converted to composition frames. Rows display front-to-back
while the document stores layers back-to-front. Dropping on a row inserts above
it; dropping in empty space inserts at the top. The eight-pixel snap tolerance
considers frame zero and other layer starts and ends. Stills may be extended;
videos may be shortened and restored up to their source duration. Selection,
insertion, reordering, visibility, and seeking do not alter layer transforms or
keyframes.

Each image or video layer can be expanded to show its **Transform** group; that
group expands to show Position X, Position Y, Scale X/Y, Rotation, Opacity and Anchor X/Y key
tracks. The layer name is omitted from the left header and remains on the clip.
Keys use layer-local frame numbers;
their timeline markers appear at `layer start + local key frame`. Clicking a
marker seeks to it, while dragging moves it to an integer local frame within
the layer duration. A move onto another key for the same property is rejected
without changing either key. Shortening a layer hides keys outside its current
duration but retains them in the document; extending the layer makes them
visible again. New and opened compositions start with layers and Transform
groups collapsed. Inserting a key from the inspector expands its layer and the
Transform group.

The transform inspector edits the base value when a property has no keys. For
an animated property it displays the shared evaluator's interpolated
value and is read-only between keys. Adding a key copies the currently
evaluated value; at an existing key, the field edits that key. Removing the
last key restores the base value. Key editing is available only when the
playhead is within the selected layer. Transform keys are evaluated for both
manual preview and playback, and the selected-layer guide follows the evaluated
position. Seeking and animation do not change layer timing.

The preview displays cached still-image frames or decodes video frames using
the shared `VideoPlaybackSession`, then composites visible layers back-to-front
with `FrameCompositor`. Decode and composition work runs off the UI thread.
Play/Pause follows the exact composition frame rate; Loop is off by default and
restarts from frame 0 when enabled. Playback stops on the last frame of the
furthest layer, including hidden layers, and pressing Play at the end restarts
from frame 0. Manual seeking pauses playback. The monotonic clock remains on
schedule when rendering falls behind; intermediate preview frames may be
skipped. Transform keyframes use the shared curve evaluator in the preview
worker. A completed frame from the current uninterrupted playback may still be
presented; manual seeking or replacing the composition invalidates it. Playback
extends the navigation range by one hour as needed and scrolls to keep the
playhead visible. This does not set a composition duration. Preview scheduling
remains Motion Studio-owned; composition uses CPU by default or the experimental
shared OpenGL backend when opted in. Text, rectangle, and ellipse layers are
rasterized to transparent RGBA8 with Qt painting on the preview worker; the
shared compositor handles their layer order, transforms, opacity, and alpha.
Text glyphs and shape fills/strokes are static; only their existing transform
properties are animated. Source in-points and audio remain unsupported.

During playback, the preview worker decodes forward sequentially for source
frame gaps of two through eight frames when the decoder is already positioned
before the target. One-frame advances keep the decoder's existing fast path;
backward seeks, larger gaps, and interactive scrubbing use timestamp seeking.
An unsuccessful forward decode falls back to timestamp seeking unless it was
cancelled. Export retains its existing decode path. Preview performance schema
v8 reports GPU composition and Color Adjustment outcomes and upload,
draw-submission, effect, and readback metrics, as well as actual timestamp-seek outcomes and time, forward-decode attempts,
completions and fallbacks, and discarded intermediate frames. These counters
are diagnostics; they do not change the automatic effect-worker policy.

The timeline UI component refactor passed a manual Windows check: playback
controls, ruler seeking, zoom and scrolling, layer editing, and keyframe
interactions worked. The broader manual Windows validation remains pending:
confirm the Motion Studio icon on
the application window and executable; open **Help > System** and confirm
it displays **Beta 0.1.0** and the executable path; open **Help > Open Log
Folder** and confirm the Motion Studio log directory opens, then verify the
About text; verify the centered empty-state
button opens composition creation and disappears after creation; confirm a
composition opens maximized and preserves a restored window size; confirm the
new **File > Save As**, **File > Save**, and **File > Open Composition** actions
use the Windows-native file picker; write and reopen a `.motion` document with
layers, keyframes, and Media Pool bins intact; confirm relative media paths
still resolve after moving the project folder with its media; move a referenced
source away and confirm it reopens offline; verify malformed and future-version
files leave the current document and the source file unchanged; test Save,
Discard, and Cancel before replacing
or closing a dirty composition; and confirm the title's dirty marker clears on
save. Move, resize, tabify, float, and hide the Media Pool and Inspector. Switch
between the Timeline and Graph Editor tabs using the native dock tabs and the
Graph Editor button; use **View > Graph Editor** and its close control to hide
and restore that tab. Restart the app to verify layout restoration, then use
**View > Reset Panel Layout** and confirm Timeline is selected with both tabs
available. Confirm the previous uncustomized default layout migrates to the
tabbed layout while a customized layout remains intact. Then confirm the
one-hour navigation range, stepping, and separate drag extensions; confirm the
timeline controls, ruler, and layer tracks still work after their component
refactor; import
images and videos, organize bins, switch pool views, and restore offline items;
drag media to empty space and existing rows; verify snapping and front-to-back
order; select, move, hide, resize, and remove layers; edit transforms; seek
through image and video previews; exercise zoom buttons, slider, and Ctrl+wheel;
confirm the playhead stays anchored, scroll horizontally to inspect aligned
ruler and layer positions, move and resize clips at multiple zoom levels, and
extend the range only at its actual end, scrolling there if it is offscreen;
resize the viewer, Media Pool, inspector, and timeline; replace the composition
and confirm the pool resets; verify the timeline starts in Time mode, switch to
Frames and back, and confirm the playhead and layer positions do not change;
play and pause a video and confirm a seek pauses it; confirm the left layer
header has no name while the clip retains it; expand a layer to show only
Transform, then expand Transform to show the five properties; collapse and
reopen each level; add Position and Opacity keys, inspect interpolated values,
edit at a key, remove a key, drag a marker, and confirm a colliding move is
rejected; scrub and play through the animation; open the Graph Editor, select a
property and segment, drag each Bezier handle, apply every easing preset, and
verify Undo/Redo, preview, playback, and export use the curve; save and reopen a
v4 project and confirm v1-v3 projects retain empty effect stacks and older
versions retain linear interpolation; create
multiline Unicode text, change its font, size, alignment, box dimensions, and
alpha color; create
rectangles and ellipses, edit their dimensions, fill, optional stroke, and alpha;
use the Effects inspector tab to add Gaussian Blur, Color Adjustment, Drop
Shadow and Glow to layers, adjust parameters, toggle, drag effects above and below other
rows, and remove effects, then verify preview, playback, export,
Undo/Redo, and save/reopen preserve the stack and order; confirm Color
Adjustment looks unchanged after the shared-processing integration and that
preview, playback, and exported frames agree;
animate a text or shape transform, save and reopen it, and confirm content and
keys persist; use **Edit > Undo** and **Edit > Redo** on layer insertion,
content edits, visibility, clip timing, transforms, and keyframes; confirm
inspector edits group into one history step, undoing to
the saved composition clears its dirty marker, Media Pool contents are
unaffected, and new/opened compositions start with empty history; enable Loop and
confirm it restarts at frame 0, then disable Loop and confirm playback stops
on the final layer frame and Play restarts from frame 0; verify playback scrolls and
extends the navigation range for a long clip; open **Settings > Keyboard
Shortcuts**, change Undo and Redo assignments, confirm duplicate assignments are rejected,
check that Cancel discards edits and OK persists them, clear an assignment,
restore defaults with **Reset All**, and confirm timeline commands are disabled
when unavailable; configure autosave interval and retention; create an untitled
composition, allow a snapshot, close the app unexpectedly, then restore it on
startup; open a saved project with a newer recovery snapshot and test Restore
and Ignore; inspect, refresh, delete, restore, and open the folder for snapshots
in **Settings > Autosave & Recovery**; verify missing sources restore offline
and autosave does not overwrite the `.motion` file. Use **Settings > General...**
to disable and re-enable preview metrics; seek and play, then inspect **Help >
Open Log Folder** for active-only samples with CPU/memory, stage timings,
percentiles, and request counters. Confirm idle intervals stay silent and
records contain no paths, layer names, or text content. Export a composition and
inspect its completion summary for schema 3, elapsed time, frame count,
render/write timings, GPU requested/used backend, GPU/fallback frames,
failures, transfer bytes, upload/draw/readback times, PBO counts, fence waits,
staging and encoder queue metrics, output size/rate, and achieved speed; also
confirm canceled and failed jobs have summaries. Run the same 3,405-frame
project and settings three times with the GPU environment variable set to `1`;
compare the median against the 54.96 s synchronous-GPU baseline and the 52.21 s
acceptance threshold. Open **File > Export
Video...**, confirm composition resolution and frame-rate defaults, try a custom
resolution and quality profile, export overlapping image, video, text, and
shape layers with a blank lead-in, and play the result in a media player. Cancel
another export and verify an existing destination remains unchanged; choose an
unavailable encoder if one is offered and confirm a detailed log entry appears.
For effect performance, use the same 1920 × 1080, 60 fps composition with
Gaussian Blur at radius 10 and neutral Color Adjustment. Compare automatic mode
with `CREATIVE_SUITE_MOTION_EFFECT_WORKERS` set to `1`, `2`, `4`, and `8`,
restarting the app for each setting. After warm-up, collect three 10-second runs
per setting. Compare logged blur and full-frame render averages, decode timing,
rendered/coalesced counts, and CPU use; confirm the worker count in each sample
and that preview pixels remain identical. The override is temporary and does
not change the automatic default. For decoder-path validation, use a video layer
with playback metrics enabled. After warming up, compare repeated playback runs
before and after this change; inspect timestamp-seek attempts/successes/failures,
forward-decode completions and fallbacks, decode and full-frame timing, rendered
frames, coalesced requests, and CPU use. Confirm interactive scrubbing remains
responsive and export output is unchanged. In **File > Export Video...**, verify
that Opaque is the default; select Transparent (alpha), confirm the discovered
codec/profile and container extension, export a composition with a blank lead-in
and partial-alpha content, then inspect the alpha channel in a decoder that
supports the selected format. Repeat with OpenGL composition enabled and with
the CPU fallback. Confirm linked Motion-to-Video Editor publication stays opaque.
Audio remains out of scope. Then close the application. Overshoot-capable curves, additional
interpolation modes, advanced effects, Windows platform validation, and
performance measurement remain open.

**Exit criteria:** a user can create, save, reopen, and preview a simple
composition without losing its layer or frame-rate data.

### 3. Motion design MVP

The completed MVP implementation checks are archived. Performance acceptance
and the later mask-authoring initiative remain open below.

- [ ] Profile representative compositions and validate export throughput,
  memory use, and codec behavior on Windows.
- [ ] Address measured bottlenecks before expanding the MVP scope.
- [x] Implement editable Pen-created cubic Bezier shapes and invertible stage
  masks with complete-path keyframes, history, native v15 migration and shared
  preview/export preparation. Reuse existing mask operations and softness units.
  Development is authorized while performance, native presentation, visual and
  platform acceptance remain pending; mask combinations and tracking stay planned.

**Exit criteria:** the agreed MVP workflows pass regression coverage and
manual visual checks, including save/reopen, recovery, and heavy-composition
handling.

### 4. Video Editor integration and release readiness

The handoff implementation is complete and archived; manual, compatibility,
platform, and workload acceptance gates remain open below.

- [ ] Complete manual end-to-end checks in both applications, including several
  saved revisions, project reopen, missing document/render recovery, and
  publication failure/cancellation.
- [ ] Complete automated producer-consumer coverage for asynchronous refresh,
  playback cache invalidation, and stale callbacks after a project replacement.
- [ ] Validate installation, project paths, fonts, graphics drivers, and
  packaging on Windows, macOS, and Linux.
- [ ] Complete small, medium, and heavy project validation before release.

**Exit criteria:** both applications can exchange supported composition
references reliably and each release build passes its platform regression
gate.

### 5. Future research

- [ ] Implement the first optional audio-reactive 2D tool before MVP acceptance
  (in progress),
  per maintainer direction: analyze a local audio file and bake its RMS envelope
  into one selected transform-property track. Keep audio playback, mixing, and
  audio export out of this delivery. The `.motion` schema remains unchanged;
  automated worker lifecycle coverage exists, while manual UI interaction and
  cancellation during an active analysis remain pending.
- [ ] Revisit advanced mask operations, effect graphs, nested compositions,
  particles, 3D features, and node-based workflows only after the core 2D
  motion workflows meet their performance targets and a clear use case
  justifies their added complexity.

#### Candidate feature backlog — unprioritized

The items below refine the future research areas and record motion-design and
compositing capabilities that remain outside the current Motion Studio baseline
(which already includes 2D image/video/text/rectangle/ellipse layers, a Media
Pool with bins, basic 2D Transform keyframes with Linear and bounded cubic
Bezier easing in the Graph Editor, ordered Gaussian Blur and Color
Adjustment effects with layer-local parameter keyframes and Cubic Bezier easing, stage-level animated Rectangle/Ellipse masks, RMS audio-to-keyframe
baking, layer parenting, Null controls, animated Anchor Point, Pan Behind pivot
dragging, Viewer Position drag, timeline parent pick-whip and independent Scale X/Y, `.motion` v18 persistence, `.cscomp` v3 interchange, and opaque-default video export with optional alpha profiles). They are candidates, not approved release commitments. Keep
implementation modular, local-first, and gated by measured rendering and memory
costs.

- **Hierarchy, structure, and layer compositing:**
  - Layer parenting through the Inspector and timeline [pick-whip](features/PARENT_PICK_WHIP.md) is implemented.
  - Null / control layers are implemented; see [Layer hierarchy](features/HIERARCHY.md).
  - Pre-composing (nested compositions treated as reusable single layers).
  - Adjustment layers that apply their effects and masks to all underlying
    layers in the stack.
  - Dedicated solid layers with composition-sized creation presets.
  - [Normal, Multiply, Screen, Overlay, Add and Difference](features/BLEND_MODES.md) are implemented. Additional modes such as Soft Light remain candidates.
  - Track mattes (*Alpha Matte*, *Alpha Inverted*, *Luma Matte*, *Luma Inverted*)
    using arbitrary renderable layers as transparency or luma mattes are [implemented](features/TRACK_MATTES.md).
- **Transforms, viewer gizmos, and animation curves:**
  - Broader pivot/transform tools beyond the implemented [Pan Behind](features/PAN_BEHIND.md)
    marker drag, [Position handle](features/POSITION_DRAG.md) and compensated Inspector [Anchor Point](features/ANCHOR_POINT.md) action.
  - Zero scale and mirroring remain candidates; Viewer Scale handles and positive independent
    [Scale X/Y](features/SCALE_AXES.md) are implemented in native v12.
  - Additional Viewer handles for mask/shape control points;
    the [Position movement handle](features/POSITION_DRAG.md) is implemented.
  - Additional keyframe interpolation modes: *Toggle Hold*, *Roving* keyframes,
    spatial Bezier motion paths editable directly in the Viewer, and Graph
    Editor curves with overshoot (values beyond 0%–100%) and separate *Value
    Graph* vs. *Speed Graph* views.
  - Property links and lightweight expressions/drivers (e.g., `wiggle`, `loopOut`,
    time-driven formulas, and cross-property bindings).
- **Vectors, shape layers, and advanced masks:**
  - Editable cubic Bezier vector paths (*Pen tool*) for freeform shapes and 2D
    alpha masks are implemented in native v15; acceptance remains pending.
  - Keyframe animation of shape and mask content properties: path vertices and
    tangents, rectangle/ellipse dimensions, corner roundness, stroke width, fill
    and stroke RGBA, gradients (linear/radial), dashed strokes, and animated
    mask feather (*Soft Edge*) and expansion.
  - *Trim Paths* (`Start`, `End`, `Offset`) is implemented for Bezier strokes in
    native v16; acceptance remains pending. Other procedural operators remain
    planned: *Repeater*, *Merge Paths*, *Offset Paths*, *Round Corners*,
    *Wiggle Paths* and *Pucker & Bloat*.
  - Multiple masks per stage with boolean combine modes (*Add*, *Subtract*,
    *Intersect*, *Lighten*, *Darken*, *Difference*).
- **Text animation:**
  - Animatable text properties (*Source Text* keyframes, tracking/letter
    spacing, line spacing, fill/stroke colors, and stroke width).
  - Per-character, per-word, and per-line *Text Animators* with *Range
    Selectors* (`Start`, `End`, `Offset`, shape/smoothness) driving position,
    scale, rotation, opacity, or blur without splitting glyphs into separate
    layers.
  - Text on a Bezier path.
- **Effects and rendering:**
  - `.cscomp` interchange support for animated effect parameters (native `.motion`
    v9, timeline, Graph Editor, preview, and export support are implemented for
    Gaussian Blur radius and Color Adjustment brightness/contrast/saturation).
  - Native per-layer and composition-wide *Motion Blur* based on transform and
    path velocity.
  - Expanded effect families:
    - *Color and light:* Curves, Levels, Hue/Saturation, Tint/Tritone, LUTs,
      Vignette.
    - *Layer styles and shading:* Inner Shadow, Bevel, Stroke.
    - *Distortion and stylization:* Turbulent Displace, Wave Warp, Corner Pin,
      Displacement Map, Mirror, Chromatic Aberration, Posterize Time.
    - *Generators:* Fractal Noise, 4-Color Gradient, Grid, Checkerboard, Audio
      Spectrum / Audio Waveform.
    - *Keying and compositing:* Chroma Key, Luma Key, Spill Suppressor, edge
      refinement.
    - *Particles:* 2D particle emitter systems.
- **Timing, audio, and timeline workflow:**
  - Interactive *Source In / Out* slip/slide trimming in the UI and *Split
    Layer at Playhead*.
  - *Time Stretch*, reverse playback, *Freeze Frame*, and keyframeable *Time
    Remapping*.
  - Audio playback/scrubbing, timeline waveform display, frequency-band
    audio-to-property mappings, and mixed audio export (expanding beyond the
    current offline RMS envelope baker).
  - Timeline and layer markers for beat and cue synchronization.
  - Viewer guides, rulers, safe margins (*Title/Action Safe*), grids, and canvas
    snapping.
- **3D layers, camera, and tracking:**
  - 2.5D/3D layer transforms (`Position Z`, 3D orientation/rotation), cameras
    (with depth of field), lights (*Point*, *Spot*, *Ambient*, *Parallel*),
    shadows, and 3D text/shape extrusion.
  - Point tracking, planar tracking, warp stabilization, and tracked/rotoscoped
    masks.
- **Export and pipeline:**
  - Image-sequence export (e.g., PNG sequence).
  - *Render Queue* for batch jobs, reusable output templates/presets, and
    per-composition RAM/disk frame-cache indicators in the timeline ruler.

##### Suggested practical implementation sequence (highest workflow impact first)

When scheduling new feature development from the candidate backlog, prioritize
the following sequence for maximum motion-design utility relative to
architectural complexity:

1. **Animated effect parameters (implemented in native `.motion` v9; `.cscomp`
   interchange deferred):** keyframe support for Gaussian Blur radius and Color
   Adjustment properties across the document model, timeline, Graph Editor,
   preview, and export.
2. **Layer hierarchy and transform ergonomics:** Inspector parenting and Null layers are implemented in native v10. Animated Anchor Point and compensated Inspector repositioning are implemented in native v11. Independent Scale X/Y with affine inheritance is implemented in native v12. Pan Behind adds pivot dragging with transient Inspector values and release-only commit without a format change. The timeline pick-whip adds direct parent selection, candidate feedback and wheel/edge scrolling using the same parent command. The Viewer Position handle adds live asynchronous movement preview and atomic Position commit. The Viewer Rotation handle adds local affine-aware rotation with full turns, live preview and release-only commit. Viewer Scale X/Y/XY handles add linked/free live scaling with positive clamping and atomic release-only edits. Manual acceptance remains pending.
3. **Blending modes, Track Mattes and layer effects:** six static per-layer modes are implemented in native v13 with transparent accumulation, opaque output and CPU/OpenGL composition; Vulkan requests fallback. Alpha/Luma and inverted track mattes with shared sources/chains are implemented in native v14. Pen-created animated shape/mask paths are implemented in native v15; Animated Trim Paths is implemented in native v16; Animated Drop Shadow is implemented in native v17; Glow is implemented in native v18. Native visual/platform acceptance remains pending.
4. **Pen tool (implemented in native v15) and Trim Paths:** freeform shapes and masks support animated vertices/tangents, local Viewer editing and complete-path easing. Trim Paths is implemented in native v16 with Start/End/Offset scalar animation; manual/platform/performance acceptance is pending.
5. **Drop Shadow and Glow:** ordered local RGB shadows are implemented in v17
   with four scalar animation channels. Glow is implemented in v18 with static
   RGB and mode plus animated opacity/radius. Transparent-background video
   export has per-job FFV1, ProRes 4444 and VP9 alpha adapters; manual acceptance
   is pending.

## Status legend

- `[ ]` Not started
- `[-]` In progress
- `[!]` Blocked or requiring a decision

Do not add target dates until capacity, platform support, and scope are
validated. Update this roadmap when a milestone, dependency, or decision
changes.

Native shared-compositor regression can require actual hardware by running
`creative-suite-motion-editor-gpu-composition-tests --require-gpu` with the native
Qt platform plugin. It rejects an absent context or CPU recovery instead of
qualifying that run. The default portable test retains its CPU recovery coverage.

## Pen path regression boundary

| Feature | Automated coverage | Remaining acceptance |
| --- | --- | --- |
| Pen creation/editing, animated shapes and shared masks, origin-aware rendering, v15/recovery | `bezier_path_test.cpp`: `creative-suite-motion-editor-bezier` and `...-bezier-hidpi`; existing Motion preview/export/history/gesture suites; `hub-backup-format-test`; shared composition/exchange consumers | [Manual checklist](features/BEZIER_PATHS.md#verification-and-pending-checks), native presentation/platform checks and benchmarks pending. |

Development of Pen paths is authorized independently of the remaining MVP
performance/visual acceptance gates. Their pending status is not changed by
automated implementation coverage.

## Trim Paths regression boundary

| Feature | Automated coverage | Remaining acceptance |
| --- | --- | --- |
| Bezier stroke trimming, open/closed seams, length/cubic cuts, three scalar channels, history, v16/recovery | [Trim tests](../../apps/motion-editor/tests/trim_paths_test.cpp), [CTest registration](../../apps/motion-editor/tests/CMakeLists.txt): `creative-suite-motion-editor-trim-paths` and `...-trim-paths-hidpi`; existing path/transform/preview/export/UI suites; Motion GPU composition; `hub-backup-format-test` | [Manual checklist](features/TRIM_PATHS.md#verification-and-pending-acceptance); native presentation/platform checks and benchmarks pending. |

## Drop Shadow regression boundary

| Feature | Automated coverage | Remaining acceptance |
| --- | --- | --- |
| Ordered shadows, expanded origins/fitting, masks/mattes, animation/history, v17/recovery and GPU image contract | [Drop Shadow tests](../../apps/motion-editor/tests/drop_shadow_test.cpp), [CTest registration](../../apps/motion-editor/tests/CMakeLists.txt): `creative-suite-motion-editor-drop-shadow` and `...-drop-shadow-hidpi`; `hub-backup-format-test`; existing shared/consumer regressions | [Manual checklist](features/DROP_SHADOW.md#verification-and-pending-acceptance); visual/platform/native Viewer presentation and benchmarks pending. |

## Glow regression boundary

| Feature | Automated coverage | Remaining acceptance |
| --- | --- | --- |
| Outer/Inner/Both pixels and alpha, bounded origins, cancellation, masks, ordered CPU rendering, animated parameters, Inspector/history, preview/export pixels, GPU metrics/fallback, native v18/recovery, `.cscomp` refusal and Hub v18 backup | `libs/tests/effects_test.cpp` (`creative-suite-effects`), `libs/tests/opengl_composition_test.cpp` (`creative-suite-composition-opengl`), `libs/tests/vulkan_composition_test.cpp` (`creative-suite-composition-vulkan`), `apps/motion-editor/tests/drop_shadow_test.cpp` (Glow model/render/UI and history coverage), `apps/motion-editor/tests/motion_video_export_test.cpp` (decoded export halo and backend parity/fallback), `apps/motion-editor/tests/performance_metrics_test.cpp` (separate Glow diagnostics), `apps/hub/tests/test_backup_formats.cpp` (`hub-backup-format-test`) | [Glow guide](features/GLOW.md#verification-and-pending-acceptance); the regression sources were added but are not executed in this work stage. Native OpenGL presentation, visual/platform acceptance and representative performance measurements remain pending. |


## Image transparent blend composition maintenance

Image v19 reuses shared channel math and extends transparent OpenGL raster
records with default-Normal blend fields. Motion's own formats, opaque output,
preview and export remain unchanged. Shared composition/OpenGL/Vulkan and Motion
blend, preview, GPU composition and export gates protect this additive boundary.
See [Image contract and execution evidence](../image-editor/features/BLEND_MODES.md).
Native visual and other-platform acceptance remain pending.


## Shared Gradient Map maintenance

Shared effects/composition/OpenGL/Vulkan and Motion blend, preview, GPU composition and export gates protect default raster records and opaque output while Image adds optional Gradient Map adjustments. Motion UI, effects registries and formats remain unchanged. See [Image contract](../image-editor/features/GRADIENT_MAP.md).

## Brightness/Contrast shared maintenance

The shared RGB evaluator and typed Image raster adjustment add no Motion effect
registry entries. Preserve effects and native composition semantics; run shared
effects/composition and Motion gates. [Contract](../image-editor/features/BRIGHTNESS_CONTRAST.md).

## Shared Image exposure adjustment maintenance

The shared raster compositor/effects API adds an Exposure / Offset / Gamma
alternative for Image Editor; Motion formats, UI and published PNG consumption
retain their existing contracts. Preview/GPU-composition, persistence, export
and UI regressions protect the consumer boundary. Export queue metrics use
an explicit `std::uint64_t` frame count to avoid mixed unsigned-long/unsigned-
long-long `std::max` deduction on LP64 Linux; queue limits/accounting are unchanged. Source-range duration comparison likewise
uses explicit `std::int64_t` to accommodate Qt qint64 on LP64.
Execution evidence is tracked in the [Image contract](../image-editor/features/EXPOSURE_OFFSET_GAMMA.md).

## Shared Image Hue/Saturation maintenance

The fourth shared raster adjustment adds global HSL for Image; Motion adds no
effect selector or persisted field. Existing PNG consumption and Video-style
Rec.709 saturation retain their contracts. Preview/GPU-composition, persistence,
export and UI gates protect this additive API. Known local baseline failures
remain separately recorded in the [Image contract](../image-editor/features/HUE_SATURATION.md).
