# Video Editor GPU Acceleration Plan

Status: **Stages 1–3 implemented as opt-in experiments on 2026-10-02;
cross-platform acceptance and later stages pending**.
Video Editor is the first consumer of the shared compositor. Motion Studio and
Image Editor adoption follow their separate plans.

## Current implementation and shared direction

Timeline playback collects ordered decoded layers and evaluated transforms on
the worker, then uses the shared CPU compositor by default. Settings > General
offers **Use GPU for timeline preview (Experimental)**, disabled by default,
stored globally as `performance/gpu_composition_enabled`. The separate public
Qt/OpenGL 3.2 Core adapter composes those layers on the worker and automatically
delivers shared textures when supported, retaining RGBA readback as fallback.
Export defaults to CPU and offers independent per-job GPU composition through
Render > Video. Its RGBA readback path supports 1080p, 1440p and 4K UHD; see
[the export contract](GPU_EXPORT.md) and [results](GPU_EXPORT_RESULTS.md).
See [the rendering boundary](architecture/RENDERING.md).

The [Motion Studio plan](../motion-editor/GPU_ACCELERATION_PLAN.md) will adopt
this shared backend through its existing layer-preparation boundary after its
relevant contracts are covered.
Keep timeline scheduling, source timing, decoding, transitions, audio, history,
and render-queue ownership in Video Editor. Avoid a separate composition engine
or a dependency on the Motion Studio executable.

GPU selection is runtime state. Preserve the current `.csp` format, older
supported migrations, recovery, and image/Motion handoff contracts. This plan
does not create a new persistence version or an editable Motion handoff.

## Stage 1 — Optional GPU timeline composition (implemented; acceptance pending)

- Use current decode/composition/payload/presentation diagnostics to profile
  representative timelines before adding complex GPU optimizations. Separate
  composition cost from seeking, RGBA conversion, audio, and UI delivery.
- Connect prepared raster layers to the optional shared GPU compositor. Preserve
  aspect fit, nearest source sampling, normalized transforms, rotation, opacity,
  layer/track priority, alpha blending, and the opaque black canvas/gaps.
- Keep video decoding and text rasterization on their existing worker paths.
  Preserve immutable shared frame ownership and bounded CPU caches.
- Retain the CPU compositor and existing presentation fallback. A composition
  failure must fall back without mutating project state; if both paths fail,
  preserve the last valid preview and log track/clip/frame/source context.
- Implement a new composition opt-in separately from the existing diagnostic
  `CREATIVE_SUITE_DISABLE_GPU_PREVIEW=1`, which currently controls presentation.
  The variable continues to control presentation only; the native timeline test
  explicitly uses it while requiring actual GPU composition.

The GUI creates the offscreen surface; the worker owns context, shader, reusable
source texture, output framebuffer and the bounded geometry lookup. Toggle
invalidates the final-frame cache only and recomposes a paused position. Request
limits fall back individually; technical failures latch CPU until off/on. Errors
are logged before a nonmodal status warning. Preference/project data are preserved.
Cancellation returns no frame. Aggregate metrics schema 9 distinguishes actual
CPU/GPU composition, uploads, draw submission, readback, bytes and fallback from
the existing presentation metrics. Stage 2 adds automatic direct delivery and
Stage 3 adds independent offline export. GPU effects/decode/encode remain deferred.

Exact rotated nearest sampling additionally requires the optional
`ARB_gpu_shader_fp64` and `ARB_gpu_shader5` extensions. Without them, rotated
requests use CPU; unrotated requests keep the OpenGL 3.2 Core lookup path.
The native suite exercises both automatic and core-only capability policies.
See [precision and limits](architecture/RENDERING.md#timeline-composition).

**Exit:** a covered optional timeline backend preserves CPU composition
semantics; initialization/limits/context-loss cases have a defined fallback.
No playback speedup is accepted from successful context creation alone.

## Stage 2 — Direct preview delivery (implemented; acceptance pending)

The [implemented contract](GPU_TEXTURE_DELIVERY.md) defines the shared global
context, GPU lease payload, three-target/64 MiB pool (including retiring targets),
producer/consumer fences, Busy coalescing and 5 ms retries, on-demand recovery,
delivery epochs and shutdown ordering. Normal delivery performs no output
readback or viewer upload. Aggregate/slow schemas are 9; delivery schema is 3.
The existing preference controls this automatically; no new setting was added.

- Preserve global rational Timeline timing, mixed source rates, trims/in-points,
  keyframes, frame stepping, seeking, loop/end behavior, and isolated media
  preview. GPU work must not move timing decisions into the viewer.
- Preserve moving Cross Dissolve overlaps, opacity progression, held-frame
  behavior where used, and Fade to Black. Keep the embedded-video audio hard
  cut at visual Cross Dissolves, independent Audio Crossfade gains, clip/track
  mixing, gains/mutes, silence in gaps, and audio-clock priority.
- Preserve Full/Half/Quarter preview quality and its cache invalidation rules.
  Preview quality must not change export dimensions or project data.
- Retain generation checks, cancellation, latest-frame delivery, and bounded
  pending work during rapid seeks, edits, clip activation, and project changes.
- Define texture sharing, fences, ownership, and release across worker/viewer
  contexts. Present composed textures without full-frame readback where useful;
  preserve a reliable CPU presentation path and safe shutdown.
- Keep Grayscale Preview as a viewing-only effect with the same coefficients;
  it remains disabled by default, unpersisted, and absent from exports.
- Extend diagnostics with actual composition backend and upload/draw/wait/
  readback costs. Keep existing metrics meanings and distinguish draw submission
  time from GPU execution and final presentation.

**Exit:** playback, seeking, transitions, audio synchronization, quality changes,
and delivery/fallback behave correctly under real-driver lifecycle checks.

## Stage 3 — GPU offline export (implemented; acceptance pending)

Render > Video exposes **Use GPU for export (Experimental)**, default off on
application launch, retained in the panel during the session and captured in each
queue item. It is independent of the global preview setting. The queue creates a
GUI-owned surface; each job owns an isolated worker compositor/context. Shared
RGBA/direct composition now uses two 16 KiB geometry lookup buffers, each with
4096 indices, covering UHD and portrait 4K without changing the preview pool budget.

`OfflineExportOptions` supplies the borrowed surface, warning/summary callbacks
and a test adapter factory. Unsupported requests fall back on the same prepared
layers; technical failure latches CPU for the item, with a fresh backend on retry.
Export diagnostics schema 1 is independent of preview 9/3 and includes every
outcome, transfers, encoding and known allocation peaks. The
[implementation contract](GPU_EXPORT.md) defines ownership, cancellation and
logging; [dated results](GPU_EXPORT_RESULTS.md) record native tests and measurements.

- Use the shared backend in `OfflineExportRenderer`, with independent worker
  resources and the queue's immutable project/settings snapshots.
- Preserve output-size/rate conversion, every-frame rendering, layer transforms,
  text, still images, transitions, blank frames, duration, and audio output.
  Offline export must not use playback frame skipping or preview-only effects.
- Read back at the current shared RGBA encoder boundary. Keep composition,
  decoding, and hardware encoding as separately measured capabilities.
- Retain codec discovery, presets, bitrate/color settings, progress, queue states,
  retry/cancel behavior, temporary-file verification, and atomic publication.
- Preserve prior output on failure/cancellation and log job/source/encoder
  context. Export must not change the open project, history, or dirty state.
- Compare decoded output against the CPU baseline, accounting separately for
  compositor tolerance and lossy encoder differences.

**Exit:** CPU/GPU preview and export follow the same rendering contract, queue
regressions pass, and transfer/encode costs are included in performance results.

## Stage 4 — Covered effect reuse and linked assets

- Reuse shared GPU effects only after the Video Editor's relevant effect feature,
  parameters, order, alpha/color semantics, and persistence are defined.
  Motion-owned Blur/Color Adjustment do not automatically become Video features.
- Preserve current transitions and viewing effects independently of future
  color grading, masks, or Fusion processing. The Fusion placeholder does not
  become a composition engine through this acceleration work.
- Cover transparent PNG consumption from Image Editor, including masked and
  transformed imported-image output, shared links, clip variants, and refresh
  after publication. Invalidate dependent CPU/GPU caches on media replacement
  so preview and export use the updated asset.
- Verify both producer and consumer for changes to shared APIs or published
  media. Use the dedicated [Image Editor plan](../image-editor/GPU_ACCELERATION_PLAN.md)
  for its rendering work; preserve existing `.cimg`/`.csp` boundaries.

**Exit:** added capabilities have explicit product contracts and producer/
consumer coverage. Acceleration alone does not expand the feature scope.

## Stage 5 — Platform acceptance and measured default

- Compare CPU/GPU using the documented 1080p, 15-minute, three-track project
  and 30 fps playback target on the reference PC. Include text, transitions,
  transparent stills, audio, seeks, and repeated media; measure 4K sources and
  higher rates separately without assuming real-time playback.
- Record build, OS, GPU/driver, workload, presented FPS, latency, decode/
  composition/transfer timings, CPU/RAM, and GPU resources where measurable.
  Compare repeat runs; do not transfer Motion Studio measurements to Video.
- Compile affected applications in Debug and Release. Run focused rendering,
  playback/audio, export, shared-library, and linked-image consumer/producer
  checks plus the full Release suite with the future implementation delivery.
- Validate Windows, macOS, and Linux resource ownership, context support,
  driver failures, dependencies, codecs, and packaged fallback behavior.
- Enable a default only after parity and useful gains are recorded. Preserve
  CPU operation on unsupported platforms and unsuitable workloads.
- Evaluate hardware decoding/encoding separately if measurements identify them
  as bottlenecks. Decoder-to-texture/encoder interop needs its own supported
  formats, synchronization, resource, license, and fallback contract.

**Exit:** correctness, native platform evidence, and repeatable performance
justify the chosen default without compromising Video Editor stability.

## Provisional technology and alternatives

The implemented starting point is the shared Qt/OpenGL experiment, reusing Video
Editor's existing OpenGL deployment. Benefits are incremental adoption and
shared frame/transform contracts. Costs include driver variation, shader
rounding, transfers, synchronization, and maintaining fallback paths.

Keep the optimized CPU compositor as the baseline. Consider Qt RHI or native
Vulkan/Metal/Direct3D backends if platform needs or profiling justify their
compatibility, dependency, licensing, and maintenance costs. No final renderer
choice or new package is accepted by this documentation.

## Regression and manual evidence

Extend the existing shared compositor, transform/text, playback/transition/
audio, native OpenGL presentation, metrics, render queue/export, and main-window
linked-image checks listed in [REGRESSION_TESTING.md](REGRESSION_TESTING.md).
`creative-suite-composition-opengl` compares CPU/GPU with exact alpha and geometry
and at most two RGB levels of rounding difference. It covers order, transparency,
aspect fit, transforms, text rasters, edge clipping, padded strides, empty canvas,
cancellation, limits and repeated worker teardown. Worker/controller/settings/
metrics tests cover preference persistence, cache, live toggle, failure latching,
retry, logging and cancellation. `creative-suite-main-editor-gpu-timeline` uses
the real backend for text/keyframes/transitions/quality/playback and a masked PNG
producer/consumer refresh when Image Editor is built. Context-unavailable skips
are explicitly distinct from driver approval.

Manually compare CPU/GPU at clip boundaries and transition frames, rapidly seek,
change quality while playing, activate media, refresh a linked PNG, export/cancel
queued jobs, and close/reopen projects during rendering. Record audio sync,
freshness, appearance, output preservation, and resource cleanup on each OS.
Follow the repository [regression policy](../REGRESSION_POLICY.md).

## Resuming and delivery record

Read `AGENTS.md`, this plan, the rendering architecture and regression guide;
inspect the current shared backend and repository before the next stage.
After each delivery, record implemented items, build/test results, platform
evidence, unresolved issues, and the next stage here.

- 2026-10-02: documentation saved; implementation and GPU composition acceptance
  remain planned. Existing GPU presentation remains in place. Coordinate shared
  work with the [Motion Studio](../motion-editor/GPU_ACCELERATION_PLAN.md) and
  [Image Editor](../image-editor/GPU_ACCELERATION_PLAN.md) plans.

- 2026-10-02: Stage 1 code added with Video Editor as first consumer. Windows
  native parity and timeline/PNG checks passed on NVIDIA GTX 1660 SUPER, OpenGL
  3.2, driver 616.92. See [measurement and delivery evidence](GPU_COMPOSITION_RESULTS.md)
  for repeat measurements, builds, tests and remaining platform/manual gates.

- 2026-10-02: Stage 2 adds automatic direct texture delivery with public Qt sharing,
  bounded leases/fences, asynchronous RGBA recovery and separate diagnostics.
  Video Editor remains the first consumer; at this delivery, Motion/Image adoption and GPU export
  remain later work. Native and build/test results are recorded with Stage 1 evidence.

- 2026-10-02: Stage 3 adds per-job experimental offline GPU composition and shared
  4K lookup buffers, with isolated export contexts, same-frame CPU fallback and
  schema-1 summaries. Debug/Release focused and full Release checks plus three
  sequential CPU/GPU measurements per resolution are recorded in
  [GPU_EXPORT_RESULTS.md](GPU_EXPORT_RESULTS.md). Video Editor remains the first
  consumer. Broader driver/platform and human acceptance, effects/decode/encode
  and Motion/Image integration remain pending.
