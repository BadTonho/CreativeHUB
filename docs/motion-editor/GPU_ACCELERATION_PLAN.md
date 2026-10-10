# GPU Acceleration Plan

Status: **experimental preview composition, effects, and opt-in offline export
implemented on 2026-10-07; native driver and performance acceptance pending**.
Direct texture preview is implemented experimentally on 2026-10-10; see
[GPU_TEXTURE_PREVIEW.md](GPU_TEXTURE_PREVIEW.md) for delivery, recovery and tests.
The first consumer of the shared GPU compositor is Video Editor. Each stage has a separate delivery and
acceptance gate; completing one stage does not imply that the whole renderer
has moved to the GPU.

The shared OpenGL adapter is implemented for the Video Editor's opt-in timeline
preview and per-job offline export through 4K; Motion Studio now has an opt-in
experimental preview and offline export integration. Preview and export stacks
containing Color Adjustment and Gaussian Blur can use the GPU. Motion's export
reads the composed frame back to RGBA for the existing CPU encoder. Motion
reuses the shared adapter and does not create another composition engine. See
[Video Editor's delivery](../video-editor/GPU_ACCELERATION_PLAN.md).

## Current evidence and priorities

The shared adapter's native Windows parity/measurements are recorded in
[Video Editor results](../video-editor/GPU_COMPOSITION_RESULTS.md). They do not
approve Motion effects, its viewer/export lifecycle, or macOS/Linux drivers.
Video Editor's [export contract](../video-editor/GPU_EXPORT.md) now covers isolated
per-job worker resources, CPU fallback and separate schema-1 metrics. Shared RGBA
and direct paths use two 16 KiB axis lookup buffers for UHD/portrait 4K. Its
[export measurements](../video-editor/GPU_EXPORT_RESULTS.md) are additional reuse
evidence; Motion Studio GPU effects and export remain experimental, with GPU
effects and export limited to opt-in operation.

The preview worker rasterizes text/shapes and processes effects. By default,
effects and composition use the CPU. With
`CREATIVE_SUITE_MOTION_GPU_COMPOSITION=1`, Color Adjustment-only stacks are
applied in the shared OpenGL composition shader. Stacks with Gaussian Blur use
an ordered shared effect sequence before layer blending; one reusable RGBA8
scratch texture is bounded to 64 MiB per worker. Blur matches Motion's three
horizontal/vertical box-filter pairs, premultiplied-alpha processing, clamped
edges, and per-pass RGBA8 rounding. The GPU compositor can deliver a shared texture directly to the Motion viewer;
RGBA recovery and export still read back the completed frame.
Unsupported contexts and GPU failures fall back to CPU; after the first GPU
failure, the worker keeps using CPU for effects and composition for the rest of
its lifetime to avoid repeated errors. The same opt-in controls preview and
offline export; both remain CPU-only by default. Each export creates a separate
GUI-owned offscreen surface and keeps it alive until its worker has destroyed
the worker-owned context and compositor.
Diagnostics schema 8 reports GPU composition, Color Adjustment, and Gaussian
Blur counts, fallbacks, failures, upload/readback bytes, and stage timings.
Export summary schema 3 records the requested/used backend, rendered GPU and
CPU-fallback frame counts, failures, uploaded/readback bytes, and average
upload, draw-submission, and readback times. Export uses up to two PBOs and
fences with a FIFO frame queue, while a dedicated consumer thread owns the
FFmpeg encoder. The PBO pool and one RGBA frame in transfer or encoding are
bounded to 128 MiB per export; slot count falls with resolution. If one PBO
cannot fit, the exporter keeps synchronous GPU readback. If an asynchronous
transfer fails, pending and remaining frames are rerendered on CPU in order for
the rest of the job. Submission, fence wait, and copy metrics are CPU wall
times; they do not measure GPU execution time.

A read-only inspection of existing local diagnostic intervals on 2026-10-02
found 1920 x 1080, two-layer samples with composition averages around
2.8–4.0 ms and effect averages around 58 ms when effects ran. These intervals
were not a controlled benchmark: enabled effects and active layers may vary
within an interval, and each stage has its own sample count. They suggest that
effects are an important follow-up target; composition alone must not be
presented as a solution for all playback delays. No private media or local log
files are included in this plan.

The maintainer's Windows run on 2026-10-07 confirms the same bottleneck in
current local diagnostics: at 1920 × 1080 and 60 fps, Gaussian Blur averaged
54.7 ms while composition averaged 4.66 ms. With blur active, the preview
delivered about 11 fps; with no effects applied, it delivered about 59 fps.
The completed 60 fps export ran at 33.79 fps (0.563× realtime). The full
measurements and limits are in
[PERFORMANCE_RESULTS_WINDOWS_2026-10-07.md](PERFORMANCE_RESULTS_WINDOWS_2026-10-07.md).
The preview log uses schema 4 while the current source emits schema 8, so it
does not contain GPU counters and cannot establish GPU-path performance.

## Stage 1 — Layer composition

### 1A — Experimental backend and preview integration (implemented)

- Adopt the optional OpenGL 3.2 Core compositor under `libs/composition/`, using
  the existing shared frame, transform, and ordered-layer contracts established
  by the Video Editor's first delivery.
- Compose position, uniform scale, rotation, opacity, and straight source alpha
  over the existing opaque black canvas. Preserve aspect fit and nearest
  sampling. Keep animation evaluation and content rasterization in their
  existing modules.
- Create the offscreen surface on the GUI thread; create, use, and release the
  context and its resources on the preview worker. Keep rendering off the UI
  thread and retain cancellation and generation checks.
- Opt-in: `CREATIVE_SUITE_MOTION_GPU_COMPOSITION=1`. The setting is read once
  when the application starts; all other values leave preview and offline
  export on the CPU compositor.
  On GPU initialization or rendering failure, log the cause and compose the
  same request on the CPU. Hardware limits must also allow a CPU fallback.
- Reuse bounded GPU allocations. Document source uploads and output readback
  explicitly rather than claiming a path without CPU/GPU transfers. Preview
  can consume shared textures directly, with RGBA recovery; export can use two
  PBO slots within a 128 MiB staging budget.
- GPU frame/resource timing and byte totals are included in Motion preview
  diagnostics schema 8. Export-specific totals are collected per job in
  `export_summary` schema 3; asynchronous submission, fence wait, copy,
  encoder queue, and staging metrics do not share the preview interval collector.
- Keep `.motion` v4, recovery v1, `.cimg` v11, and `.csp` unchanged.

### 1B — Regression coverage and measured acceptance (in progress)

- Compare the GPU and CPU output for opaque/transparent sources, partial
  opacity, order, aspect fit, movement, scale, rotation, canvas edges, padded
  strides, blank frames, invalid input, and hardware limits. Determine and
  record a pixel tolerance for floating-point sampling and blend rounding.
- Automated Motion preview regression checks exact CPU output after an
  unavailable-context fallback and compares a layered image/shape request
  with rasterized text against CPU output within two channel values when an
  offscreen OpenGL surface is available. Shared compositor tests cover
  transforms, alpha, ordering,
  padded strides, cancellation, and hardware limits. These checks do not replace
  native-driver acceptance.
- Exercise rapid seeks, playback, cancellation, composition replacement,
  repeated opening/closing, context failure, and resource cleanup.
- Use existing composition, full-render, and request-to-paint metrics, plus
  backend upload/draw/readback measurements. Export now overlaps asynchronous
  PBO transfers with FFmpeg encoding; measure end-to-end performance before
  changing the default backend or adding caches.
- Record three runs per backend for the approved 1080p/30 fps, 10-second,
  five-layer workload, including the reference Windows PC, driver, CPU/memory,
  stage times, and delivered/coalesced frames. Include small and heavy projects.
- Compile Debug and Release; run focused/shared regressions and the complete
  Release suite as part of the implementation delivery. Record native Windows,
  macOS, and Linux results separately from offscreen/headless checks.

**Stage 1 exit:** deterministic coverage, native driver checks, and repeatable
measurements establish correctness and the workloads that benefit. The backend
must stay experimental until these results are recorded.

## Stage 2 — Effects on the GPU

### 2A — Color Adjustment in preview (implemented experimentally)

- Reuse the shared `ColorAdjustmentParameters` contract and process the ordered
  stack in the OpenGL layer shader before source-over composition. Quantize RGB
  after each pass to match the CPU effect's RGBA8 behavior; preserve source alpha.
- The shared composition layer accepts up to the Motion document limit of 256
  adjustments in a stack. Stage 2B extends this contract with ordered mixed
  Color Adjustment and Gaussian Blur stacks. The existing opt-in setting
  controls the GPU path; CPU remains the default and export can opt into GPU.
- Automated parity covers varied and repeated adjustments, transparent source
  pixels, unchanged alpha, cancellation, mixed-stack CPU fallback, and GPU
  initialization fallback. RGB tolerance is one channel level; alpha is exact.
- Diagnostics schema 6 records applied/fallback/failure counts and CPU-side
  draw-submission time for GPU adjustments. This time does not represent GPU
  execution time.

### 2B — Gaussian Blur in preview (implemented experimentally)

- Extend the shared OpenGL layer contract with an ordered Color Adjustment and
  Gaussian Blur sequence; preserve the CPU renderer as the parity reference and
  fallback.
- Preserve repeated and disabled effects, parameters, alpha semantics,
  cancellation, and layer order. The CPU export path is unchanged.
- Reuse one RGBA8 scratch texture per worker, capped at 64 MiB; unsupported or
  failed effect requests fall back to CPU processing and composition.
- Define a shared effect boundary only when its responsibilities are clear;
  retain Motion-owned UI, document parameters, and history.
- Compare CPU/GPU output within one RGB level with exact alpha. Profile each
  effect and complete frame; the current shader's CPU submission metric does
  not measure GPU execution.

**Stage 2 exit:** covered effect parity and measured gains without regressions
in preview, playback, or document behavior.

## Stage 3 — Preview presentation and export (async export readback implemented)

- [x] Share or reference completed textures across worker and viewer contexts;
  define ownership, synchronization, cancellation, and release rules.
- [x] Replace full-frame readback for preview when the GPU viewer can consume the
  texture directly; retain CPU presentation when unavailable.
- [x] Integrate the GPU backend into offline export with the same effects and
  geometry contract. Read back at the current CPU encoder boundary; hardware
  encoding remains separate.
- [x] Preserve atomic publication, cancellation, prior output, and export
  errors, with per-job backend/fallback metrics.
- [x] Add bounded two-PBO FIFO readback, fence collection, and a dedicated
  FFmpeg consumer. Keep the PBO pool, one in-flight RGBA frame, and queued
  frames within 128 MiB; reduce slots by resolution and use synchronous GPU
  readback when a PBO does not fit.
- [x] Recover from an asynchronous transfer failure by discarding pending GPU
  tickets and rerendering unencoded frames on CPU in output order.
- [x] Add export regression coverage for output parity, FIFO/reuse/backpressure,
  staging fallback, mid-pipeline CPU recovery, cancellation with outstanding
  readbacks, encoder-thread errors, destination preservation, worker lifecycle,
  and schema-3 summaries.
- [ ] Measure three post-change GPU exports of the same 3,405-frame Windows
  project. Use the previous 54.96 s synchronous-GPU result as baseline; the
  median must be 52.21 s or lower (5% improvement) without visual or
  cancellation regressions. Native macOS/Linux driver and performance checks
  remain pending.
- [x] Implement bounded shared-texture preview using the existing compositor,
  single-result mailbox, generation validation and worker-side RGBA/CPU recovery.
- [x] Record three paired preview trials per route on the Windows reference
  fixture, including 30 fps and 60 fps stress; see [results](GPU_PREVIEW_RESULTS_WINDOWS_2026-10-10.md).
- [ ] Complete broader manual/native platform acceptance before enabling GPU
  by default. See [direct preview](GPU_TEXTURE_PREVIEW.md).

**Stage 3 exit:** export uses the covered renderer contract. The stage remains
open until the original-project export performance results and native platform
checks are recorded;
direct preview implementation is separate from performance and platform acceptance.

## Stage 4 — Other applications and backend choice (planned)

- Integrate the shared compositor into Video Editor after its own regression
  and performance checks, following its
  [dedicated stages](../video-editor/GPU_ACCELERATION_PLAN.md). Keep Video Editor
  stability as a priority.
- Audit Image Editor composition, masks, transforms, and editable operations
  before adapting this backend to that application's different output contract;
  follow its [dedicated stages](../image-editor/GPU_ACCELERATION_PLAN.md).
- Compare OpenGL with Qt RHI/native Vulkan, Metal, or Direct3D alternatives if
  portability or measurements justify the additional dependency/maintenance
  cost. Do not select a final suite renderer from this experiment alone.

## Provisional technology decision

OpenGL 3.2 Core through public Qt APIs is the first experimental backend. The
repository already deploys Qt OpenGL for Video Editor presentation. Motion
Studio now links the Qt OpenGL module and shared compositor target; track the
deployed module and its existing Qt open-source license obligations.

Benefits include reuse of the current C++ contracts and an isolated worker
backend. Costs include shader maintenance, driver variation, upload/readback,
and macOS OpenGL's limited future. Qt RHI offers more backends but adds a
private API compatibility burden; a native multibackend renderer has a larger
implementation and distribution cost. CPU rendering remains the fallback and
the comparison baseline. No performance gain or platform support is accepted
without measurements.

Qt requires offscreen-surface creation and destruction on the GUI thread,
while worker contexts can render to framebuffer objects. See the
[offscreen surface contract](https://doc.qt.io/qt-6/qoffscreensurface.html) and
[OpenGL context thread rules](https://doc.qt.io/qt-6/qopenglcontext.html).

## Delivery record

- 2026-10-02: documented the staged GPU plan before implementation.
- 2026-10-06: implemented the opt-in Motion preview composition path and GPU
  Color Adjustment in the shared OpenGL compositor. CPU remains the default,
  fallback, and export path. Native-driver acceptance and performance gains
  remain unclaimed pending recorded measurements.
- 2026-10-06: added ordered GPU Gaussian Blur for preview stacks, with a bounded
  scratch target, CPU fallback, and schema-7 metrics. Focused Motion regressions
  pass; the shared OpenGL shader parity test is skipped when this environment
  cannot create a worker context. No performance gain is claimed.
- 2026-10-07: integrated the same opt-in compositor into offline export with a
  dedicated offscreen surface per export worker, CPU fallback, and per-job
  `export_summary` schema-2 GPU metrics.
- 2026-10-07: added a two-PBO, FIFO/fence readback pipeline and a dedicated
  FFmpeg consumer thread. Staging is capped at 128 MiB, initialization falls
  back to synchronous GPU readback, and mid-pipeline errors recover outstanding
  and remaining frames on CPU before atomic publication. Summary schema 3
  records submission, fence wait, copy, queue, and staging metrics. The focused
  compositor, Motion Studio, and Video Editor OpenGL regressions pass (13/13
  affected CTest cases, including injected encoder failure) on Windows. The
  canonical Release executable links and remains running when launched; the
  generated post-build `windeployqt` step
  still exits with a `qtpaths` query error. `Qt6OpenGL.dll` is present and
  matches the Qt 6.7.2 runtime file. Three-run performance acceptance remains
  pending; the prior 54.96 s GPU run is the baseline, with a 52.21 s median
  target.

- 2026-10-10: implemented direct Motion preview texture delivery, bounded mailbox,
  generation checks, output pool backpressure, native viewer parity and context
  recovery. Preview schema 8 distinguishes delivery and presentation. Native
  Windows tests pass; [reference measurements](GPU_PREVIEW_RESULTS_WINDOWS_2026-10-10.md)
  sustain 30 and 60 fps with zero readback and lower median latency than GPU RGBA.
  Broader interactive and macOS/Linux acceptance remain pending; GPU stays opt-in.

## Resuming implementation

1. Read `AGENTS.md`, this plan, `REUSE_PLAN.md`, `SCOPE_AND_READINESS.md`, and
   the current renderer/compositor contracts; inspect the working tree.
2. Complete the remaining Stage 3 interactive/platform checks and historical
   export benchmark. Preserve texture ownership, worker resources and CPU
   fallback when extending the implemented direct preview boundary.
3. Add deterministic boundary tests and native manual checks with each stage.
   Record acceptance before enabling the backend by default or claiming
   performance improvements.
4. Update this file after each delivery with implemented items, build/test
   evidence, unresolved issues, and the next stage. Leave later stages marked
   planned until their code and acceptance work exist.

## GPU preview manual checklist

Record build, OS, GPU, driver, setting, actions, and outcome for each item.

1. Open the same composition once with the environment variable unset and once
   with `CREATIVE_SUITE_MOTION_GPU_COMPOSITION=1`. Compare layer order,
   transparent image/video edges, text/shapes, opacity, positions, scaled
   content, and clockwise/counterclockwise rotation.
2. Seek rapidly, play/pause/loop, edit keys, replace the composition, and close
   during rendering. Confirm fresh frames and no freeze or resource errors.
3. Include Color Adjustment-only stacks and ordered mixed stacks with repeated
   Gaussian Blur. Confirm blur edges and alpha match the CPU output, then verify
   GPU processing when available.
4. Use an unavailable/unsupported context or a source/effect target beyond the
   device or 64 MiB temporary limit. Confirm CPU output and an actionable
   fallback log, without repeated errors for every frame.
5. Compare the stage timing records and full-render/request-to-paint metrics
   under identical workloads; include upload and readback costs in the result.
6. For export, use the same saved 3,405-frame project, dimensions, encoder, and
   quality as the prior 54.96 s synchronous-GPU measurement. Run it three times
   with `CREATIVE_SUITE_MOTION_GPU_COMPOSITION=1`. Record schema-3 elapsed time,
   achieved FPS, async slots/submitted/collected frames, fence waits, staging
   peaks, encoder queue, fallbacks, transfer bytes, and output parity. Accept
   async readback only if the median is 52.21 s or lower with no visual or
   cancellation regression; otherwise retain synchronous GPU readback as the
   selected route and record the outcome. Keep GPU mode opt-in.

This checklist records pending native-driver acceptance; automated boundary
coverage does not establish platform acceptance by itself.

## Shared Video Editor stage 2 delivery

Video Editor remains the first consumer. Its experimental backend now supports
[direct texture delivery](../video-editor/GPU_TEXTURE_DELIVERY.md), public Qt
global sharing, a three-target/64 MiB reservation budget including retired targets,
producer/consumer fences and asynchronous RGBA recovery. Motion adoption is now implemented experimentally through its own viewer and
worker contracts; native platform and performance acceptance remain separate. See [native evidence and remaining platform gates](../video-editor/GPU_COMPOSITION_RESULTS.md).
