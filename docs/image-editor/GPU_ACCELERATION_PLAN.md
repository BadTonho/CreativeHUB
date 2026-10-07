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
[`performance-baseline-windows-2026-10-06-v2.json`](performance-baseline-windows-2026-10-06-v2.json).
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

GPU rendering is a runtime implementation choice. Keep the current `.cimg`
format (v16), its supported older versions, recovery wrapper v1, and the Video
Editor's `.csp` contract unchanged. Editable operations remain the source of truth; textures
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
Each profile used three warmups and 30 measured iterations in both cold and
warm view-refresh modes. The full schema v2 report is
[`performance-baseline-windows-2026-10-06-v2.json`](performance-baseline-windows-2026-10-06-v2.json);
the prior [schema v1 direct-renderer report](performance-baseline-windows-2026-10-06.json)
remains available as a historical record and is not directly comparable. The
v2 report and the table below are the initial measurements before group
thumbnail caching. The table gives wall time average/p95 for cold and warm
refreshes, then composite average/p95 for warm refreshes:

| CPU profile | Canvas | Cold refresh avg/p95 (ms) | Warm refresh avg/p95 (ms) | Warm composite avg/p95 (ms) |
| --- | ---: | ---: | ---: | ---: |
| Reference | 1920×1080 | 134.85 / 143.83 | 124.80 / 133.63 | 84.76 / 91.68 |
| Mask-heavy | 1920×1080 | 580.84 / 637.08 | 563.09 / 614.86 | 370.85 / 412.29 |
| Stroke-heavy | 1920×1080 | 992.77 / 1,031.43 | 945.04 / 1,005.65 | 637.64 / 686.48 |
| Large image | 3840×2160 | 279.59 / 296.19 | 260.83 / 278.91 | 176.92 / 189.51 |
| Repeated source | 1920×1080 | 65.29 / 70.16 | 58.19 / 64.59 | 40.03 / 44.55 |
| Export | 1920×1080 | 134.13 / 144.31 | 126.43 / 140.38 | 85.60 / 94.26 |

For the export profile, the separate PNG+JPEG export iteration averaged
579.57 ms (p95 614.07 ms, maximum 629.32 ms).

The post-cache Release run is recorded in
[`performance-after-group-thumbnail-cache-windows-2026-10-06.json`](performance-after-group-thumbnail-cache-windows-2026-10-06.json).
It used the same machine, schema v2, three warmups, and 30 measured iterations.
All six profiles reported 30 cold group-thumbnail renders and zero warm
group-thumbnail renders; cold and warm pixels matched. The table compares warm
refresh wall time before and after caching:

| CPU profile | Warm avg/p95 before (ms) | Warm avg/p95 after (ms) |
| --- | ---: | ---: |
| Reference | 124.80 / 133.63 | 86.44 / 91.80 |
| Mask-heavy | 563.09 / 614.86 | 386.35 / 429.06 |
| Stroke-heavy | 945.04 / 1,005.65 | 696.62 / 785.30 |
| Large image | 260.83 / 278.91 | 176.30 / 191.32 |
| Repeated source | 58.19 / 64.59 | 39.56 / 43.43 |
| Export | 126.43 / 140.38 | 82.46 / 90.38 |

These are observations of two runs on one Windows PC, not guaranteed gains or
general hardware requirements. Cold refreshes still render each group's
thumbnail. Mask thumbnails remain uncached. Per-stage values are totals within
an iteration, and different stages include nested work, so do not sum them.
Resource peaks are sampled during the full profile run, including warmups and
cache priming. Native manual panel inspection and macOS/Linux measurements
remain pending.

## Layer raster cache measurement

The session's normal composition path now caches premultiplied rasterized layer
images, including each layer's operations and mask, with a 64 MiB per-session
limit. The cache preserves valid entries when another image would exceed the
limit; that image is rendered without storage. Layer content/resource changes
invalidate the affected entry, while selection, visibility, opacity, and stack
reordering reuse it. Undo/Redo and document/canvas/source replacement clear or
synchronize entries. Transient previews and exports continue through the
uncached renderer. The collector reports `layer_raster_cache_hit`,
`layer_raster_cache_miss`, and `layer_raster_cache_bypass`.

The schema v2 report
[`performance-after-layer-raster-cache-windows-2026-10-06.json`](performance-after-layer-raster-cache-windows-2026-10-06.json)
records a Release run on the same reference PC, with six profiles, three
warmups, and 30 measured iterations per cold and warm mode. Cold sessions
rebuild the cache; the warm session reuses it. Every profile reported matching
cold/warm pixels and 30 cold group-thumbnail renders versus zero warm renders.
Wall-time averages/p95 are cold after this change, warm after the earlier group
thumbnail cache, and warm with both caches active:

| CPU profile | Canvas | Cold avg/p95 after layer cache (ms) | Warm avg/p95 after group cache (ms) | Warm avg/p95 after layer cache (ms) | Cold misses / warm hits / bypasses | Retained layer pixels (MiB) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Reference | 1920×1080 | 140.91 / 159.46 | 86.44 / 91.80 | 6.82 / 7.67 | 120 / 120 / 0 | 31.6 |
| Mask-heavy | 1920×1080 | 610.63 / 656.08 | 386.35 / 429.06 | 24.44 / 30.47 | 120 / 120 / 0 | 31.6 |
| Stroke-heavy | 1920×1080 | 997.56 / 1,134.70 | 696.62 / 785.30 | 7.32 / 7.99 | 120 / 120 / 0 | 31.6 |
| Large image | 3840×2160 | 298.32 / 353.80 | 176.30 / 191.32 | 117.75 / 134.37 | 60 / 60 / 120 | 63.3 |
| Repeated source | 1920×1080 | 68.14 / 79.04 | 39.56 / 43.43 | 7.28 / 9.22 | 120 / 120 / 0 | 31.6 |
| Export | 1920×1080 | 144.70 / 177.29 | 82.46 / 90.38 | 7.93 / 9.62 | 120 / 120 / 0 | 31.6 |

For `large-image`, cold calls include 60 misses and 60 bypasses; warm calls
include 60 hits and 60 bypasses. Two additional layers do not fit the remaining
budget. Retained memory peaked at 63.3 MiB, below the 64 MiB limit. The separate
PNG+JPEG export iteration averaged 593.09 ms (p95 639.06 ms, maximum 742.37 ms);
it remains uncached.
These measurements are observations from one PC and do not set a target or
guarantee a general speedup. The schema v1 and v2 pre-cache reports and the
post-group-cache report remain unchanged for historical comparison.

## Shared Video Editor stage 2 delivery

Video Editor remains the first consumer. Its experimental backend now supports
[direct texture delivery](../video-editor/GPU_TEXTURE_DELIVERY.md), public Qt
global sharing, a three-target/64 MiB reservation budget including retired targets,
producer/consumer fences and asynchronous RGBA recovery. This application remains
on its current CPU renderer; adoption requires its own rendering, alpha, lifecycle,
export and regression contracts. See [native evidence and remaining platform gates](../video-editor/GPU_COMPOSITION_RESULTS.md).
