# GPU Acceleration Plan

Status: **experimental preview composition and Color Adjustment implemented
on 2026-10-06; native driver acceptance pending**.
The first consumer of the shared GPU compositor is Video Editor. Each stage has a separate delivery and
acceptance gate; completing one stage does not imply that the whole renderer
has moved to the GPU.

The shared OpenGL adapter is implemented for the Video Editor's opt-in timeline
preview and per-job offline export through 4K; Motion Studio now has an opt-in
experimental preview integration. Motion's Gaussian Blur and offline export
remain on CPU; Color Adjustment-only preview stacks can use the GPU. Motion
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
evidence; Motion Studio Gaussian Blur and export remain CPU while preview
composition and Color Adjustment are experimental and opt-in.

The preview worker rasterizes text/shapes and processes effects. By default,
effects and composition use the CPU. With
`CREATIVE_SUITE_MOTION_GPU_COMPOSITION=1`, Color Adjustment-only stacks are
applied in the shared OpenGL composition shader, with the source uploaded once
and no per-effect intermediate texture or readback. A stack containing enabled
Gaussian Blur remains entirely on the CPU to preserve effect order. The GPU
compositor reads the completed frame back to RGBA for the existing viewer.
Unsupported contexts and GPU failures fall back to CPU; after the first GPU
failure, the worker keeps using CPU for effects and composition for the rest of
its lifetime to avoid repeated errors. Offline export remains CPU-only.
Diagnostics schema 6 reports GPU composition and Color Adjustment counts,
fallbacks, failures, upload/readback bytes, and effect/composition timings.

A read-only inspection of existing local diagnostic intervals on 2026-10-02
found 1920 x 1080, two-layer samples with composition averages around
2.8–4.0 ms and effect averages around 58 ms when effects ran. These intervals
were not a controlled benchmark: enabled effects and active layers may vary
within an interval, and each stage has its own sample count. They suggest that
effects are an important follow-up target; composition alone must not be
presented as a solution for all playback delays. No private media or local log
files are included in this plan.

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
  when the application starts; all other values leave the CPU preview enabled.
  Default preview and offline export continue to use the CPU compositor.
  On GPU initialization or rendering failure, log the cause and compose the
  same request on the CPU. Hardware limits must also allow a CPU fallback.
- Reuse bounded GPU allocations. Document source uploads and output readback
  explicitly rather than claiming a path without CPU/GPU transfers.
- GPU frame/resource timing and byte totals are included in Motion preview
  diagnostics schema 5. Export's frame renderer does not receive the opt-in
  backend and remains CPU-only.
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
  backend upload/draw/readback measurements. Profile before adding caches,
  asynchronous transfers, or changing the default backend.
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
  adjustments in a stack. A stack with enabled Gaussian Blur uses the CPU for
  every effect in that stack. The existing opt-in setting controls the GPU path;
  CPU remains the default and export backend.
- Automated parity covers varied and repeated adjustments, transparent source
  pixels, unchanged alpha, cancellation, mixed-stack CPU fallback, and GPU
  initialization fallback. RGB tolerance is one channel level; alpha is exact.
- Diagnostics schema 6 records applied/fallback/failure counts and CPU-side
  draw-submission time for GPU adjustments. This time does not represent GPU
  execution time.

### 2B — Gaussian Blur (planned)

- Add Gaussian Blur after Color Adjustment while preserving the CPU renderer as
  the parity and fallback implementation.
- Preserve ordered/repeated/disabled effects, parameters, alpha semantics,
  cancellation, and the existing CPU fallback.
- Keep effect intermediate textures on the GPU, with bounded temporary storage.
- Define a shared effect boundary only when its responsibilities are clear;
  retain Motion-owned UI, document parameters, and history.
- Compare CPU/GPU output and profile each effect and the complete frame.

**Stage 2 exit:** covered effect parity and measured gains without regressions
in preview, playback, or document behavior.

## Stage 3 — Preview presentation and export (planned)

- Share or reference completed textures across worker and viewer contexts;
  define ownership, synchronization, cancellation, and release rules.
- Replace full-frame readback for preview when the GPU viewer can consume the
  texture directly; retain CPU presentation when unavailable.
- Integrate the GPU backend into offline export with the same effects and
  geometry contract. Read back only at the current CPU encoder boundary;
  investigate hardware encoding separately.
- Preserve atomic publication, cancellation, prior output, and export errors.
- Cover preview/export parity and lifecycle on all target platforms.

**Stage 3 exit:** preview and export share a covered renderer contract and
unnecessary transfers have been removed where measurements justify it.

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

## Resuming implementation

1. Read `AGENTS.md`, this plan, `REUSE_PLAN.md`, `SCOPE_AND_READINESS.md`, and
   the current renderer/compositor contracts; inspect the working tree.
2. Recheck current Qt/platform capabilities and diagnostic evidence. Preserve
   CPU output and fallback behavior while introducing Stage 1A in a small change.
3. Add the deterministic boundary tests and native manual checks in the same
   implementation change. Use Stage 1B to record acceptance results before
   enabling the backend by default or claiming performance improvements.
4. Update this file after each delivery with implemented items, build/test
   evidence, unresolved issues, and the next stage. Leave later stages marked
   planned until their code and acceptance work exist.

## Stage 1 manual checklist

Record build, OS, GPU, driver, setting, actions, and outcome for each item.

1. Open the same composition once with the environment variable unset and once
   with `CREATIVE_SUITE_MOTION_GPU_COMPOSITION=1`. Compare layer order,
   transparent image/video edges, text/shapes, opacity, positions, scaled
   content, and clockwise/counterclockwise rotation.
2. Seek rapidly, play/pause/loop, edit keys, replace the composition, and close
   during rendering. Confirm fresh frames and no freeze or resource errors.
3. Include Color Adjustment-only stacks and stacks that also contain Gaussian
   Blur. Confirm the former run through the GPU path and the latter use the
   ordered CPU effect stack before GPU composition. Export remains the CPU
   baseline.
4. Use an unavailable/unsupported context or a source larger than the driver
   texture limit. Confirm CPU output and an actionable fallback log, without
   repeated errors for every frame.
5. Compare the stage timing records and full-render/request-to-paint metrics
   under identical workloads; include upload and readback costs in the result.

This checklist records pending native-driver acceptance; automated boundary
coverage does not establish platform acceptance by itself.

## Shared Video Editor stage 2 delivery

Video Editor remains the first consumer. Its experimental backend now supports
[direct texture delivery](../video-editor/GPU_TEXTURE_DELIVERY.md), public Qt
global sharing, a three-target/64 MiB reservation budget including retired targets,
producer/consumer fences and asynchronous RGBA recovery. This application remains
on its current CPU renderer; adoption requires its own rendering, alpha, lifecycle,
export and regression contracts. See [native evidence and remaining platform gates](../video-editor/GPU_COMPOSITION_RESULTS.md).
