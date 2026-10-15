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

**Status: experimental preview composition, Color Adjustment, Gaussian Blur,
offline export integration, and direct texture preview implemented; Windows
reference preview measurements verified, broader acceptance pending.** Color Adjustment-only stacks run in the composition
shader, while stacks containing Gaussian Blur use the shared ordered GPU effect
path when supported. `CREATIVE_SUITE_MOTION_GPU_COMPOSITION=1` opts both preview
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
| Startup loads the system-selected downloaded/bundled Qt catalog before the editor window | Shared `creative-suite-translations` covers locale selection, load precedence, and English source fallback; app integration is in `apps/motion-editor/src/main.cpp` | Startup-before-window code path is present. Native Windows/macOS/Linux startup and locale checks remain pending in [translation acceptance](../TRANSLATIONS_AND_LANGUAGE_PACKS.md#regression-and-acceptance). |
| Hub snapshot references and restoration across versions 1–8 | `apps/hub/tests/test_backup_formats.cpp` (`hub-backup-format-test`), registered in `apps/hub/tests/CMakeLists.txt`; actual Motion persistence producer and consumer | Automated coverage present for Media Pool, layer sources, linked-image sidecars, and opening vault/relocated snapshots. Native UI and macOS/Linux checks remain pending in [Hub backups](../hub/BACKUPS.md). |
| Composition model, validation, layer data, and timing | `composition_document_test.cpp` (`creative-suite-motion-editor-document`) | Pending manual composition and supported-layer checks remain part of MVP acceptance. |
| Undo/Redo and edit history | `composition_history_test.cpp` (`creative-suite-motion-editor-history`) | Pending manual validation: confirm interactive edit sequences during the broader Windows UI checklist. |
| `.motion` save/open, migrations, and invalid-file preservation | `motion_document_store_test.cpp` (`creative-suite-motion-editor-persistence`), `motion_editor_ui_test.cpp` (`creative-suite-motion-editor-ui`) | Automated round trips and migration coverage exist. The owner reports repeatedly migrating the same long-lived project across persisted-format versions and says the migrations have worked. Windows invalid-file and path results remain part of Windows acceptance. |
| Autosave and restart recovery | `motion_recovery_store_test.cpp` (`creative-suite-motion-editor-recovery`), `motion_editor_ui_test.cpp` | Automated snapshots and UI recovery paths exist. Basic recovery was reported working on the Windows 11 reference PC on 2026-10-01; detailed restart/recovery scenarios remain pending (**P0 validation**). |
| Preview, transforms, curves, and layer effects | `preview_renderer_test.cpp` (`creative-suite-motion-editor-preview`), `motion_editor_ui_test.cpp`; exported Color Adjustment pixels in `motion_video_export_test.cpp` (`creative-suite-motion-editor-export`); shared evaluator coverage in `libs/tests/animation_test.cpp` and `libs/tests/composition_test.cpp`; shared color processing in `libs/tests/effects_test.cpp` (`creative-suite-effects`) | Offscreen tests cover preview, shared Color Adjustment delegation, export output, cancellation, timing, and interaction behavior. Real-hardware visual output and graphics-driver validation remain pending (**P2 validation**). |
| Experimental GPU preview and offline export | `gpu_composition_test.cpp` (`creative-suite-motion-editor-gpu-composition`), `motion_video_export_test.cpp` (`creative-suite-motion-editor-export`), `performance_metrics_test.cpp` (`creative-suite-motion-editor-performance`), and shared `libs/tests/opengl_composition_test.cpp` (`creative-suite-composition-opengl`) | Automated coverage checks CPU fallback parity, GPU/CPU frame parity, worker-owned context lifecycle, two-PBO FIFO/reuse/backpressure, cancellation, budget fallback, mid-pipeline CPU recovery, and schema-3 metrics. The current Windows selection passes 16/16 in Debug and Release, including export; native lossless GPU export is checked separately with `--require-gpu`. See [current results and codec limits](performance/GPU_PREVIEW_RESULTS_WINDOWS_2026-10-10.md). Three post-change Windows GPU exports and native macOS/Linux driver checks remain pending (**P2 validation**). |
| Direct GPU texture preview | `gpu_texture_preview_test.cpp` (`creative-suite-motion-editor-gpu-texture`); native `--require-gpu`; `performance_metrics_test.cpp`; explicitly invoked `gpu_preview_benchmark.cpp` | Portable mailbox/fallback checks and native Windows texture parity, orientation, seek/replacement, resize, presentation recovery, pool backpressure, guide/context menu, GPU RGBA fallback without sharing, and actual MainWindow Play/Pause/Loop/shutdown pass in Debug and Release. Three 1080p five-layer trials per route at 30/60 fps confirm zero direct readback and reduced latency; see [Windows results](performance/GPU_PREVIEW_RESULTS_WINDOWS_2026-10-10.md). Broader manual and native macOS/Linux checks remain pending. |
| Performance diagnostics | `performance_metrics_test.cpp` (`creative-suite-motion-editor-performance`), `motion_video_export_test.cpp` (`creative-suite-motion-editor-export`) | Earlier Windows Release evidence passed all 11 Motion CTest targets before asynchronous readback. Existing 33.79 fps CPU and 54.96 s GPU exports remain historical; their export did not record its layer/effect setup. The current schema-3 summary records backend, async mode and transfer counts, CPU-side submission/wait/copy timings, staging and encoder queue peaks, fallbacks, and failures. Three post-change GPU runs use 54.96 s as baseline and 52.21 s as the 5% target; see [`PERFORMANCE_RESULTS_WINDOWS_2026-10-07.md`](performance/PERFORMANCE_RESULTS_WINDOWS_2026-10-07.md). |
| Opaque video export and export controls | `motion_video_export_test.cpp` (`creative-suite-motion-editor-export`), shared `libs/media/tests/video_encoder_test.cpp` | Automated coverage includes output parity and order, cancellation with submitted GPU work, prior-destination preservation, injected encoder-thread failure, missing-surface CPU fallback, staging-budget synchronous fallback, and mid-pipeline CPU recovery. The current Windows selection passes 16/16 in Debug and Release, including export; native lossless GPU export is checked separately with `--require-gpu`. See [current results and codec limits](performance/GPU_PREVIEW_RESULTS_WINDOWS_2026-10-10.md). GPU export remains opt-in pending the post-change performance result (**P1/P2 validation**). |
| Optional audio-to-transform keyframe generation | `audio_keyframe_generation_test.cpp` (`creative-suite-motion-editor-audio-keyframes`) | Automated coverage includes per-frame RMS, whole-file peak normalization past the layer boundary, empty and silent sources, invalid audio, atomic track replacement, Undo/Redo, `.motion` round-trip, worker success and progress callbacks on the receiver thread, pre-start cancellation without an error log, and failure logging with operation, path, and layer ID. Manual generation and cancellation during an active UI analysis remain pending. |
| Startup and application UI | `motion_editor_startup_test.cpp` (`creative-suite-motion-editor-startup`), `motion_editor_ui_test.cpp` (`creative-suite-motion-editor-ui`) | Startup and Media Pool bin filtering have offscreen coverage; the current UI has no separate media-search control. After tightening the seek test to wait for the final still frame, the full UI suite passed 10 repeated Debug runs on 2026-10-01. Broader Windows visual and graphics-driver checks remain pending (**P2 validation**). |
| Motion Studio to Video Editor editable handoff | Motion `composition_document_test.cpp` (`creative-suite-motion-editor-document`) covers source-in mapping across frame rates; `preview_renderer_test.cpp` and `motion_video_export_test.cpp` (`creative-suite-motion-editor-preview` / `creative-suite-motion-editor-export`) cover the preview and export paths; `motion_document_store_test.cpp` (`creative-suite-motion-editor-persistence`) covers `.motion` v5 source-in round-trip, v1-v4 migration, handoff request validation, and Unicode paths. Video Editor `project_file_test.cpp` (`creative-suite-main-editor-project`) covers `.csp` v24 links and v23 compatibility; `timeline_model_test.cpp` covers clip state preservation; `application_media_services_test.cpp` and `media_library_test.cpp` cover the linked Media Pool item and refreshed video presentation. | Contract, format, rendering, timeline, and Media Pool service coverage is present. End-to-end launch, multiple save/refresh cycles, reopen, stale-callback rejection, concurrent-writer recovery, and output-profile validation remain pending manual checks in both applications. |
| Motion Studio to Image Editor layer link | `composition_document_test.cpp` (`creative-suite-motion-editor-document`) covers image-only link assignment and per-layer path isolation; `motion_document_store_test.cpp` (`creative-suite-motion-editor-persistence`) covers v6 round-trip, v1-v5 migration, and rejection of shared sidecars; `motion_editor_ui_test.cpp` covers the Layer action, timeline and composition-canvas context menus, multiple asynchronous PNG refreshes, Unicode paths, corrupt-publication fallback, and independent Save As copies; `motion_video_export_test.cpp` (`creative-suite-motion-editor-export`) resolves same-source still frames by layer ID. Image Editor linked publication remains covered by `image_editor_ui_test.cpp` (`creative-suite-image-editor-ui`). | Automated consumer coverage is present; the Image Editor producer UI suite was blocked by its unrelated Magic Wand geometry failure on this Windows run. Manual save/refresh, missing-file recovery, and cross-app reopen checks are pending (**P1 validation**); acceptance remains gated on the Image Editor standalone minimum. |

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
group expands to show Position X, Position Y, Scale, Rotation, and Opacity key
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
use the Effects inspector tab to add Gaussian Blur and Color Adjustment to
different layers, adjust parameters, toggle, drag effects above and below other
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
responsive and export output is unchanged.
Motion Studio export is opaque and video-only; audio and alpha export remain
open. Then close the application. Overshoot-capable curves, additional
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
- [ ] (Provisional later initiative, after the performance gate) Build editable
  cubic Bezier vector paths and 2D alpha masks. Start with Pen-created paths
  and one invertible mask per image, video, text, or shape layer; then add
  animated path keyframes, Undo/Redo, `.motion` version migration, and preview
  and export parity. Reuse the shape-mask stage operations and softness already
  implemented; defer mask combinations and tracking to later work.

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

The items below refine the future research areas; they are candidates, not
approved release commitments. Keep implementation modular, local-first, and
gated by measured rendering and memory costs.

- **2D animation:** editable Bezier shape paths and path operations; trim and
  repeat controls; shape/text animation presets; per-character and per-word
  text animation; parenting, null/control layers, reusable rigs, and
  expression-driven links between animatable properties.
- **Tracking and compositing:** point, planar, and camera tracking; stabilization;
  rotoscoping and tracked masks; mattes; chroma/luma keying; edge refinement.
- **3D:** spatial layer transforms; cameras and lights; shadows and materials;
  3D text and shape depth; compositing 2D and 3D layers in one composition.
- **Effects and rendering:** motion blur; glow and distortion families; particle
  systems; reusable effect graphs and presets; render queue, output templates,
  and per-composition caching.
- **Audio-driven animation:** expand the in-progress envelope tool only after
  its manual acceptance; consider frequency-band analysis and reusable
  audio-to-property mappings while keeping audio playback and mixing a
  separately reviewed scope.

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
