# GPU Acceleration Plan

Status: **provisional Motion integration plan, saved on 2026-10-02;
Motion implementation deferred**.
The first consumer of the shared GPU compositor is Video Editor. Each stage has a separate delivery and
acceptance gate; completing one stage does not imply that the whole renderer
has moved to the GPU.

Motion Studio GPU integration and effects remain planned. The shared OpenGL
adapter is implemented for the Video Editor's opt-in timeline preview; the
current Motion Studio renderer remains on CPU. Resume from Stage 1A by adopting
that adapter when Motion integration is requested, without creating another
composition engine. See [Video Editor's delivery](../video-editor/GPU_ACCELERATION_PLAN.md).

## Current evidence and priorities

The shared adapter's native Windows parity/measurements are recorded in
[Video Editor results](../video-editor/GPU_COMPOSITION_RESULTS.md). They do not
approve Motion effects, its viewer/export lifecycle, or macOS/Linux drivers.

The current pipeline rasterizes text/shapes and applies effects on the preview
worker, then calls the shared CPU compositor. Offline export uses the same
Motion-owned frame renderer. Existing diagnostics already separate decoding,
effects, composition, full frame rendering, and request-to-viewer-paint times.

A read-only inspection of existing local diagnostic intervals on 2026-10-02
found 1920 x 1080, two-layer samples with composition averages around
2.8–4.0 ms and effect averages around 58 ms when effects ran. These intervals
were not a controlled benchmark: enabled effects and active layers may vary
within an interval, and each stage has its own sample count. They suggest that
effects are an important follow-up target; composition alone must not be
presented as a solution for all playback delays. No private media or local log
files are included in this plan.

## Stage 1 — Layer composition

### 1A — Experimental backend and preview integration (planned)

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
- Proposed opt-in: `CREATIVE_SUITE_MOTION_GPU_COMPOSITION=1`. This setting does
  not exist yet; introduce it with the backend and its regression coverage.
  Default preview and offline export continue to use the CPU compositor.
  On GPU initialization or rendering failure, log the cause and compose the
  same request on the CPU. Hardware limits must also allow a CPU fallback.
- Reuse bounded GPU allocations. Document source uploads and output readback
  explicitly rather than claiming a path without CPU/GPU transfers.
- Keep `.motion` v4, recovery v1, `.cimg` v11, and `.csp` unchanged.

### 1B — Regression coverage and measured acceptance (pending)

- Compare the GPU and CPU output for opaque/transparent sources, partial
  opacity, order, aspect fit, movement, scale, rotation, canvas edges, padded
  strides, blank frames, invalid input, and hardware limits. Determine and
  record a pixel tolerance for floating-point sampling and blend rounding.
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

## Stage 2 — Effects on the GPU (planned)

- Start with Color Adjustment, followed by Gaussian Blur.
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

OpenGL 3.2 Core through public Qt APIs is the proposed first experiment. The
repository already deploys Qt OpenGL for Video Editor presentation, and the
local Qt 6.7.2 installation supports these APIs. The implementation would add
the Qt OpenGL module to Motion Studio's existing Qt dependencies. Track the
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

- 2026-10-02: staged plan saved. The maintainer requested documentation only;
  implementation is deferred. No GPU implementation, completed GPU build,
  regression result, or performance gain is claimed by this delivery.

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

1. Open the same composition in default mode and with the GPU opt-in. Compare
   layer order, transparent image/video edges, text/shapes, opacity, positions,
   scaled content, and clockwise/counterclockwise rotation.
2. Seek rapidly, play/pause/loop, edit keys, replace the composition, and close
   during rendering. Confirm fresh frames and no freeze or resource errors.
3. Include effects: they still run on the CPU before GPU composition. Compare
   the preview with the default renderer; export remains the CPU baseline.
4. Use an unavailable/unsupported context or a source larger than the driver
   texture limit. Confirm CPU output and an actionable fallback log, without
   repeated errors for every frame.
5. Compare the stage timing records and full-render/request-to-paint metrics
   under identical workloads; include upload and readback costs in the result.

This is a future implementation checklist. Automated boundary coverage and
native driver results must accompany the backend; this checklist does not
establish acceptance by itself.

## Shared Video Editor stage 2 delivery

Video Editor remains the first consumer. Its experimental backend now supports
[direct texture delivery](../video-editor/GPU_TEXTURE_DELIVERY.md), public Qt
global sharing, a three-target/64 MiB reservation budget including retired targets,
producer/consumer fences and asynchronous RGBA recovery. This application remains
on its current CPU renderer; adoption requires its own rendering, alpha, lifecycle,
export and regression contracts. See [native evidence and remaining platform gates](../video-editor/GPU_COMPOSITION_RESULTS.md).
