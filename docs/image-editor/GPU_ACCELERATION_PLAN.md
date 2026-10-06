# Image Editor GPU Acceleration Plan

Status: **provisional Image Editor integration plan, saved on 2026-10-02;
Image Editor GPU implementation deferred**.
All Image Editor GPU stages below are planned. Video Editor is the first
consumer of the now implemented shared optional compositor.

## Current implementation and shared direction

`ImageDocumentSession` currently replays editable operations into CPU `QImage`
buffers and composes layers/groups with `QPainter`. Layer buffers use
premultiplied alpha. Masks multiply layer pixels before opacity and group
composition. The canvas displays the composed image; standalone exports render
immutable snapshots on a worker, while linked PNG publication remains
synchronous. See [ARCHITECTURE.md](ARCHITECTURE.md).

An opt-in CPU performance collector and synthetic Release benchmark now record
operation replay, mask/layer/group composition, thumbnails, canvas paint, PNG/
JPEG export, process CPU, and sampled memory. The first reference-PC results
are available in
[`performance-baseline-windows-2026-10-06.json`](performance-baseline-windows-2026-10-06.json).
They characterize only the current CPU path; they do not measure GPU parity,
interaction latency, allocation/transfer costs, or optimization gains.

The [Video Editor plan](../video-editor/GPU_ACCELERATION_PLAN.md) establishes
Video Editor as the first consumer of the shared OpenGL compositor. Reuse its resource/backend services
when their contract fits; Image Editor needs transparent output, document pixel
coordinates, affine image transforms, and different sampling behavior. The
current shared video compositor's opaque black output, aspect fit, and nearest
sampling cannot be applied unchanged to image documents. Extend the shared
boundary without changing its existing consumers' defaults.
The Video backend is now available as an optional Qt adapter; Image Editor
integration remains planned and requires its own transparent-output contract.
Video Editor Stage 3 also delivers per-job offline GPU composition with isolated
worker resources and two 16 KiB axis lookup buffers for UHD/portrait 4K. The
[export contract](../video-editor/GPU_EXPORT.md) and
[measurements](../video-editor/GPU_EXPORT_RESULTS.md) provide reuse evidence.
Image Editor export/publication remains CPU; transparent-output and mask semantics
still require its own covered adapter integration.

GPU rendering is a runtime implementation choice. Keep `.cimg` v13, its
supported older versions, recovery wrapper v1, and the Video Editor's `.csp`
contract unchanged. Editable operations remain the source of truth; textures
are disposable caches, not persisted document or Undo state.

## Stage 1 — Rendering contract and experimental adapter

- Extend the current opt-in timing/resource collector to any new GPU stages.
  The six-profile CPU baseline records operation replay, mask application,
  group composition, thumbnails, canvas paint, and export on the reference PC.
  Add gesture-response, allocation, and transfer measurements before introducing
  complex optimizations.
- Define a shared raster input/output contract with explicit alpha convention,
  channel layout, stride, origin, pixel coordinates, sampling, and clear color.
  Preserve transparent output and the existing antialiasing/smoothing behavior.
- Introduce an optional adapter for GPU composition, retaining CPU rasterization
  of paint, eraser, text, shapes, and masks in the first increment.
- Keep document/history/UI ownership in Image Editor. Provide immutable render
  inputs, cancellation, generation checks, and worker-owned contexts; respect
  Qt's GUI-thread requirements for surface creation/destruction.
- Keep CPU rendering as the default during the experiment. Capability limits,
  allocation failures, or context loss must fall back for the complete request
  without modifying the document. Log technical failures with useful context.

**Exit:** a documented transparent composition contract, covered adapter and
fallback boundaries, and baseline measurements. This stage does not promise a
faster brush or move all operation replay to the GPU.

## Stage 2 — Layer, mask, and group composition

- Blend visible layers in the existing stack order and retain opacity semantics.
  Preserve the locked Background and transparent canvases.
- Apply enabled raster masks before layer opacity and group composition. White
  reveals, black hides, and gray controls coverage; a disabled mask has no
  visual effect. Preserve premultiplied channel and alpha multiplication.
- Compose each group's children first, apply its crop/rotation/flips, and apply
  group opacity once. Keep the current one-level group boundary; do not add
  masks to groups or Background.
- Preserve clipping to the canvas and the operation order. Unsupported
  operations must use a defined CPU path instead of silently disappearing.
- Keep GPU resources bounded and reusable; account for source uploads, group
  intermediates, masks, and final output separately.

**Exit:** CPU/GPU comparison covers transparent edges, white/black/gray masks,
disabled/removed masks, overlapping layers, group opacity, crops, rotations,
flips, hidden items, and empty output. Record any allowed pixel tolerance.

## Stage 3 — Interactive editing and canvas presentation

- Use GPU composition for temporary gesture previews and confirmed document
  states. Reject stale frames after edits, Undo/Redo, deletion, or New/Open.
- Preserve imported-image movement, proportional/independent scale, and free
  rotation. Object gestures leave masks and unselected objects fixed; existing
  layer transforms still affect both content and mask.
- Preserve Paint/Eraser thumbnail targeting, grayscale mask painting, eraser
  behavior, antialiased edges, one history action per stroke/gesture, and Esc.
- Profile incremental uploads, dirty regions, and cached operation replay;
  implement them only where measurements justify the complexity. Start GPU
  brush/raster kernels only with separate coverage for existing pixel semantics.
- Present completed textures directly where practical, with explicit shared
  context ownership and synchronization. Retain CPU presentation and ensure
  checkerboard, zoom/pan, text editing, and selection overlays remain correct.
- Generate content/mask thumbnails from the same rendering semantics. Prevent
  cache growth across history, document replacement, relink, and deletion.

**Exit:** interactive preview, confirmed output, thumbnails, cancellation, and
Undo/Redo agree; responsiveness and memory are measured on real drivers.

## Stage 4 — Export and Video Editor publication

- Render full export, Quick Export, and published PNG through the same covered
  composition contract, using the appropriate immutable snapshot and scope.
- Preserve full-canvas selected-layer/group export, Background export, PNG
  alpha, JPEG quality/matte, output dimensions, and encoding behavior.
- Use export-worker resources safely. Read back at the image-writer boundary;
  account for that transfer and preserve cancellation and atomic publication.
- Preserve missing/unreadable/incompatible source diagnostics, export blocking,
  compatible relinking, session-stable decoded sources, and original files.
  Saving and editing other layers must remain possible with unavailable sources.
- Cover publication and consumption together: Video Editor import, linked-image
  refresh, shared links/clip variants, preview, and rendered export must retain
  the masked PNG result and its alpha. Failed output preserves the prior file.

**Exit:** standalone exports and the two-app PNG workflow pass producer/consumer
regressions and documented native checks; document/protocol formats are intact.

## Stage 5 — Acceptance and default selection

- Compare CPU/GPU on the documented 1080p workload with up to five layers and
  one group. Include mask-heavy, stroke-heavy, large-image, and repeated-source
  documents as separate workloads; these are measurements, not new size limits.
- Record build, OS, GPU/driver, workload, interaction latency, composition/export
  time, upload/readback, CPU/RAM, and GPU memory where available. Use repeat runs.
- Compile affected applications in Debug and Release; run focused Image Editor,
  shared-core, and Video Editor consumer tests plus the full Release suite as
  part of the future implementation delivery.
- Validate Windows, macOS, and Linux contexts, packaged dependencies, resource
  exhaustion, and CPU fallback. Headless success is not native driver acceptance.
- Change the default only after correctness and repeatable gains are recorded.
  Retain fallback and choose CPU for unsupported requests or unsuitable workloads.

**Exit:** coverage, native platform evidence, and measured acceptance support
the default choice; no general performance claim is made from one machine.

## Technology and scope

Use the shared experiment described in the Motion plan as the provisional
starting point. OpenGL through public Qt APIs offers reuse of existing suite
dependencies; costs include shader parity, driver differences, texture memory,
and transfers. Compare Qt RHI or native backends if platform support or profiling
justifies their compatibility, dependency, and maintenance costs. Record module
licenses and deployment requirements before adding dependencies.

This plan accelerates existing editing behavior. Retouching, new color/effect
features, embedded imported images, live external refresh, perspective, advanced
typography, nested groups, and new mask types remain outside this delivery.

## Future regression and manual evidence

Extend the existing core, mask, raster-image, UI, and export checks listed in
[ROADMAP.md](ROADMAP.md). Add shared GPU boundary coverage and preserve the
Video Editor consumer fixture. Existing CPU tests do not establish GPU parity.

For each stage, document native comparisons for transparent edges, mask/gesture
behavior, thumbnails, export, source failures, context loss, and repeated
opening/closing. The publication check must run both editors and confirm updated
PNG content in Video Editor preview and export. Follow
[MANUAL_VALIDATION.md](MANUAL_VALIDATION.md) and the repository
[regression policy](../REGRESSION_POLICY.md).

## Resuming and delivery record

Read `AGENTS.md`, this plan, the architecture, format, and regression guides;
inspect the current repository and shared GPU work before starting Stage 1.
After each delivery, record implemented items, build/test results, platform
evidence, unresolved issues, and the next stage here.

- 2026-10-02: documentation saved; implementation, GPU measurements, and GPU
  acceptance remain planned. Coordinate shared work with the
  [Motion Studio](../motion-editor/GPU_ACCELERATION_PLAN.md) and
  [Video Editor](../video-editor/GPU_ACCELERATION_PLAN.md) plans.

## Image Editor CPU measurement baseline

The development benchmark ran all six profiles in Release on the Windows
reference PC: AMD Ryzen 5 3600, 32 GB RAM, NVIDIA GeForce GTX 1660 SUPER with
6 GB VRAM, and Windows 11 Version 26H2. The build used MSVC 1944 and Qt 6.7.2.
Each profile used three warmups and 30 measured iterations. The Release Image
Editor CTest group passed 11/11, including collector behavior, UI persistence
and shutdown, render/export pixel equality, JSONL rotation, and benchmark report
structure. The reproducible full report is
[`performance-baseline-windows-2026-10-06.json`](performance-baseline-windows-2026-10-06.json).

| CPU profile | Canvas | Iteration average / p95 / maximum (ms) | Composite average / p95 (ms) |
| --- | ---: | ---: | ---: |
| Reference | 1920×1080 | 145.06 / 171.03 / 176.68 | 91.87 / 110.47 |
| Mask-heavy | 1920×1080 | 588.43 / 685.74 / 693.67 | 373.15 / 441.85 |
| Stroke-heavy | 1920×1080 | 1,018.63 / 1,080.30 / 1,088.52 | 653.14 / 702.94 |
| Large image | 3840×2160 | 309.76 / 355.37 / 409.22 | 199.27 / 225.31 |
| Repeated source | 1920×1080 | 61.54 / 68.42 / 70.53 | 38.43 / 44.29 |
| Export (PNG + JPEG) | 1920×1080 | 713.36 / 761.79 / 765.31 | 86.91 / 94.48 |

These figures are a first CPU baseline, not GPU comparisons, gains, limits, or
general hardware requirements. Per-process CPU is reported as the latest
one-second sample; memory peaks are sampled and may miss brief higher values.
Native manual panel inspection and macOS/Linux measurements remain pending.

## Shared Video Editor stage 2 delivery

Video Editor remains the first consumer. Its experimental backend now supports
[direct texture delivery](../video-editor/GPU_TEXTURE_DELIVERY.md), public Qt
global sharing, a three-target/64 MiB reservation budget including retired targets,
producer/consumer fences and asynchronous RGBA recovery. This application remains
on its current CPU renderer; adoption requires its own rendering, alpha, lifecycle,
export and regression contracts. See [native evidence and remaining platform gates](../video-editor/GPU_COMPOSITION_RESULTS.md).
