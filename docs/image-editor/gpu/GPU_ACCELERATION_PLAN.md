# Image Editor GPU Acceleration Plan

Status: **interactive raster composition and direct texture presentation implemented**.
The first delivery keeps tools, standalone export, Quick Export, and linked PNG
publication on the CPU. Automatic / CPU is available in Settings > Rendering.
Automatic activation requires the correctness and performance gate described below;
reference Windows evidence is recorded in [the delivery results](../performance/INTERACTIVE_RENDERING_RESULTS_WINDOWS_2026-10-12.md).
Native macOS/Linux acceptance remains pending; the existing three-platform CI matrix
builds the implementation and runs the registered portable tests.

## Implemented interactive boundary

`ImageRenderController` captures implicitly shared immutable document/resource
snapshots. One dedicated worker owns the incremental raster cache and OpenGL
compositor. There is one running job and one latest pending request. Monotonic
request identities and cancellation reject superseded results after edits, preview
cancellation, Undo/Redo, tab changes, closing, and document replacement. No worker
accesses the live session or UI widgets. Surface creation/destruction occurs on
the GUI thread; context/resource operations and destruction occur on the worker.
Qt context sharing is configured before QApplication construction.

The CPU raster cache retains content and mask pixels separately within 64 MiB,
including Background and thumbnail generations; space for thumbnails is reserved
before rasterization. Paint/erase changes reconstruct the union of changed stroke
bounds with a two-pixel antialias margin, replaying preceding operations within
the region so overlapping alpha remains correct. Global-context operations such
as blur, transforms, gradients, and changed object geometry invalidate a whole
layer. Transient previews reuse confirmed entries without replacing them or
creating history actions. Compact Grayscale8 masks preserve Qt's exact qGray
coverage. Semantic source revisions survive CPU pixel eviction, avoiding repeated
GPU uploads of intact layers when large documents exceed the CPU cache.

The shared `creative-suite::composition-opengl` adapter adds a distinct raster
contract: canvas-sized top-down premultiplied ARGB32 content, ARGB32 or Grayscale8
masks, native QImage channel layout and stride, document pixel coordinates,
ordered layers, masks before opacity, and grouped children before ordered affine
transforms and group opacity. Empty output is transparent. Video aspect fit,
opaque clearing, and video sampling rules do not apply to this entry point.
Inputs supply resource identities, semantic revisions, preceding revisions, and
dirty rectangles. A dirty upload is accepted only against the resident preceding
revision. Normal GPU delivery uses texture leases and GPU synchronization without
full-frame readback. Released output targets order pending producer/consumer
fences on the GPU before reuse, preventing CPU fallback merely because a released
target has unfinished GPU work. Live leases still protect frames from reuse.
Readback exists for parity tests, not normal editing. The benchmark rejects every
CPU fallback in its GPU runs instead of mixing CPU samples into GPU results.

Known compositor-owned GPU textures are limited to 256 MiB per window, including
source content, coverage masks, group intermediates, final targets and leased
frames. Unused sources are removed in use order; the output pool has at most three
targets. Driver storage and Qt's internal window/widget backing storage are not
reported as compositor texture bytes. Capacity limits and temporary pool occupancy
use CPU for the complete request without an error entry. Technical failures are
logged before recovered frames are presented; context failure disables GPU for the
session. Allocation/exception recovery uses the established complete CPU renderer.

`ImageCanvas` still owns gestures. A replaceable OpenGL child presents the leased
texture and existing selection/cursor overlays, with editable text above it.
Document dimensions exist independently of a CPU frame. Zoom/pan redraw the same
texture. Mask, eraser, gradient, blur, object movement and text previews use the
controller; ordinary brush gestures retain their existing stroke overlay.
Thumbnails are generated on the worker only when their content changes and update
existing panel items. Mask controls remain usable before thumbnail completion. Eyedropper
sampling renders a confirmed CPU snapshot on demand and checks the current request
identity before applying the result; transparent pixels leave the color unchanged.

Image-only configuration sets BUILD_VIDEO_EDITOR, BUILD_MOTION_EDITOR,
BUILD_CREATIVE_HUB and BUILD_VIDEO_MEDIA_LIBRARY to OFF. The raster adapter still
builds without FFmpeg; native video/D3D interop is compiled only with the optional
video media library. Qt OpenGL/OpenGLWidgets come from the existing Qt dependency
and retain its existing license obligations. No new external package is added.

## Correctness and activation gate

- Incremental CPU output must exactly match a complete CPU rebuild normalized to
  premultiplied ARGB32 after edits, masks, clipping, object changes, transforms,
  history restoration and document replacement.
- CPU/GPU geometry and coverage are exact. Final normalized premultiplied channel
  composition allows at most two byte levels per channel because Qt and GL
  source-over rounding differ. Coverage is separately checked at all 256 gray
  levels with no tolerance. Canvas/checkerboard display is inspected separately.
- Bounded queues, cancellation, failure recovery, session disable, resource
  release, cache eviction, pool occupancy and memory limits have regression tests.
  Shared Video Editor and Motion Studio consumers retain their own regressions.
- Release performance uses the same incremental cache in CPU and GPU modes,
  three paired runs per profile, four warmups and thirty measured samples each.
  Timing ends at CPU paint completion or GPU frameSwapped with swap interval zero.
  Record p50/p95, uploaded/readback bytes, CPU cache and known GPU bytes, process
  CPU/RAM, driver, Qt, OS and hardware; counts exclude warmups for transfers.
- Automatic activation requires >=20% lower mean run p95 in at least two heavy
  composition profiles and no measured profile with >10% regression. Correct
  failed paths and repeat the gate before activating. These are machine-specific
  observations, not a general hardware performance guarantee.

## Verification and deferred work

The feature index is in [ROADMAP.md](../ROADMAP.md), with repeatable native steps in
[MANUAL_VALIDATION.md](../validation/MANUAL_VALIDATION.md). No document, history or recovery
version changes. `.cimg` v16 and recovery wrapper v1 remain current. CPU export
and publication remain the result reference, with existing producer/consumer
coverage. GPU brush kernels, GPU export, PNG publication acceleration, new effects,
color-management changes, nested groups and alternate graphics APIs are deferred.

The earlier CPU measurements below are historical renderer/session baselines;
they are not directly comparable to the new request-to-presentation benchmark.

## Image Editor CPU measurement baseline

The development benchmark ran all six profiles in Release on the Windows
reference PC: AMD Ryzen 5 3600, 32 GB RAM, NVIDIA GeForce GTX 1660 SUPER with
6 GB VRAM, and Windows 11 Version 26H2. The build used MSVC 1944 and Qt 6.7.2.
Each profile used three warmups and 30 measured iterations in both cold and
warm view-refresh modes. The full schema v2 report is
[`performance-baseline-windows-2026-10-06-v2.json`](../performance/performance-baseline-windows-2026-10-06-v2.json);
the prior [schema v1 direct-renderer report](../performance/performance-baseline-windows-2026-10-06.json)
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
[`performance-after-group-thumbnail-cache-windows-2026-10-06.json`](../performance/performance-after-group-thumbnail-cache-windows-2026-10-06.json).
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
[`performance-after-layer-raster-cache-windows-2026-10-06.json`](../performance/performance-after-layer-raster-cache-windows-2026-10-06.json)
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
[direct texture delivery](../../video-editor/gpu/GPU_TEXTURE_DELIVERY.md), public Qt
global sharing, a three-target/64 MiB reservation budget including retired targets,
producer/consumer fences and asynchronous RGBA recovery. This application remains
on its current CPU renderer; adoption requires its own rendering, alpha, lifecycle,
export and regression contracts. See [native evidence and remaining platform gates](../../video-editor/gpu/GPU_COMPOSITION_RESULTS.md).
