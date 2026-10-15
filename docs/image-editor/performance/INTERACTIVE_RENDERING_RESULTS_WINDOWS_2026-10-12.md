# Interactive rendering delivery — Windows, 2026-10-12

Status: implementation and local Windows validation. Native macOS/Linux acceptance
and the complete owner-operated manual checklist remain pending. This is a
development executable update, not a packaged release.

## Delivered behavior

Immutable snapshots run on a dedicated render worker with one running request and
one latest pending request. Document/revision/gesture identities discard stale
results. Content and mask caches reconstruct local paint/erase damage; global
operations rebuild affected layers. Transient previews leave confirmed history
and pixels unchanged. Raster composition preserves document geometry, mask
coverage, premultiplied alpha, layer/group order and opacity. The canvas presents
shared textures directly, reusing them for zoom/pan without full-frame readback.
Settings > Rendering provides Automatic and CPU, with logged CPU recovery and
session GPU disable after technical/context failure. Tools, export, Quick Export
and linked PNG publication remain CPU operations. Formats remain unchanged.

The retained CPU raster/thumbnail budget is 64 MiB. Known compositor textures,
including source images, masks, intermediates and leased output frames, share a
256 MiB limit per window. Driver allocations and Qt backing stores are outside
the known texture counter; process working set is reported separately. Requests
that cannot fit, and genuinely occupied pools, use complete CPU composition.

## Correctness and regressions

Incremental pixels match a complete CPU rebuild exactly after clipping, paint,
erase, masks, object/resource changes, transforms, Undo/Redo and replacement.
Native GPU tests compare normalized premultiplied channels with a maximum
two-level difference, independently verifying exact geometry and all 256 mask
coverage levels. Tests also cover empty/hidden content, bounded queues, gesture
identity, cancellation, allocation/context failure, session disable, eviction,
pool occupancy and resource release.

Native window tests exercise Automatic/CPU presentation and logging before
recovery. Thumbnail completion preserves panel items held by editors/menus, and
mask controls work before thumbnail completion. Deleting during a drag cancels
the gesture, so mouse release cannot transform a deleted object. CPU export and
linked publication regressions retain their existing result checks.

Both canonical Debug and Release application/test builds succeeded. Local
verification results are:

| Verification | Result |
| --- | --- |
| Final Release: all 23 Image Editor tests and seven shared/backend Video Editor/Motion Studio regressions | 30/30 passed, 43.18 s |
| Debug: complete Image Editor set plus shared OpenGL backend | 24/24 passed, 117.79 s |
| Final Debug rerun after synchronization/test-fixture changes: shared OpenGL, UI, export UI, interactive, native GPU raster and native rendering UI | 6/6 passed, 40.98 s |
| Final isolated image/raster Release configuration, without FFmpeg: shared OpenGL, interactive CPU and native GPU raster | 3/3 passed, 5.03 s |
| Final native Windows window capture, CPU/GPU switching and injected context-loss recovery | Passed and visually inspected |

The existing Windows/macOS/Linux CI build-and-test matrix is retained;
macOS/Linux were not executed from this Windows task. The final Release suite
includes producer/publication and consumer regressions. No test was skipped in
the native GPU raster run.

## Performance method and correction

Reference machine: AMD Ryzen 5 3600 (12 logical CPUs), 32 GB RAM, NVIDIA GeForce
GTX 1660 SUPER, NVIDIA OpenGL driver 617.42, Windows 11 Version 26H2, Qt 6.7.2,
MSVC 1944, x86_64 Release.

Each profile runs CPU and GPU with the same incremental cache, in three paired
runs, four warmups per mode and 30 measured samples per run. Four full-canvas
translucent shape layers are composed; the mask profiles add masks and the stroke
profiles retain 80 strokes per shape layer. Measured requests alternate layer
opacity, exercising warm composition rather than brush rasterization. The large
profile uses 3840 × 2160; others use 1920 × 1080. Presentation is a 960 × 540
widget. Timing ends at CPU paint completion or QOpenGLWidget frameSwapped, with
swap interval zero; this is the application's presentation boundary, not monitor
scanout timing. Table p95 is the mean of the three run p95 values. Raw reports
retain each run's p50/p95, worker latency, transfers, cache/resource bytes and RAM.

An earlier measurement failed the 4K gate. Investigation found that released
output targets were still treated as occupied while GPU fences were pending,
causing CPU fallback to be counted inside the old GPU benchmark. A strict
[diagnostic report](interactive-rendering-fallback-diagnostic-windows-2026-10-12.json)
reproduced fallback on the reference profile's fifth request. Raster composition
now orders released targets' producer/consumer fences on the GPU before reuse,
using the existing synchronization path. Live leases still prevent reuse; video
composition semantics are unchanged. A 60-frame native regression exercises two
output targets and bounded storage. The benchmark now rejects every CPU fallback
in GPU mode. Earlier mixed-mode reports are not accepted GPU performance evidence.

Process CPU uses the shared sampler: 100% means one logical CPU, and values can
exceed 100%. Short-interval Windows CPU accounting is quantized; raw instantaneous
peaks can exceed the machine's sustained capacity. Use the elapsed-time-weighted
mean for interpretation. Working-set peaks describe the complete process, which
also includes Qt/driver allocations and may retain allocator storage across runs.
Measured upload/readback counters exclude warmups. Editing changed pixels still
uploads damage regions; native tests verify those transfers independently.

## Final performance result

The [raw final report](interactive-rendering-performance-windows-2026-10-12.json)
was produced at 2026-10-12 13:30:43 UTC, with no concurrent builds or regression
runs. All GPU samples used GPU textures. The gate passed: at least two heavy
profiles improve p95 by 20% or more, and no measured profile regresses by more
than 10%. Automatic rendering is enabled by default; CPU remains selectable.

| Profile | CPU mean run p95 (ms) | GPU mean run p95 (ms) | Reduction |
| --- | ---: | ---: | ---: |
| Reference, 1920 × 1080 | 16.704 | 2.462 | 85.26% |
| Mask-heavy, 1920 × 1080 | 80.036 | 6.435 | 91.96% |
| Stroke-heavy, 1920 × 1080 | 13.518 | 3.216 | 76.21% |
| Large image, 3840 × 2160 | 89.877 | 49.580 | 44.84% |
| Mask/stroke-heavy, 1920 × 1080 | 83.063 | 4.902 | 94.10% |

| Profile | CPU/GPU mean process CPU (%) | CPU/GPU peak working set (MiB) | GPU-mode peak CPU cache (MiB) | Peak known GPU textures (MiB) |
| --- | ---: | ---: | ---: | ---: |
| Reference | 383.54 / 140.69 | 211.45 / 207.81 | 47.52 | 63.28 |
| Mask-heavy | 159.93 / 141.54 | 246.95 / 244.72 | 55.47 | 71.19 |
| Stroke-heavy | 434.54 / 180.21 | 241.00 / 238.36 | 47.52 | 63.28 |
| Large image | 322.84 / 151.13 | 315.25 / 329.40 | 63.34 | 253.13 |
| Mask/stroke-heavy | 142.92 / 109.83 | 305.17 / 302.42 | 55.47 | 71.19 |

CPU means average the three per-run elapsed-time-weighted means. Resource figures
are maxima across the three runs. Every profile measured zero uploaded bytes and
zero full-frame readback bytes after warmup: opacity changes reused resident
source textures. The 4K known GPU peak was 265,420,800 bytes, below 268,435,456
bytes (256 MiB); the retained CPU cache peak was 66,417,408 bytes, below
67,108,864 bytes (64 MiB). These are warm-composition results on this machine,
not a measured speedup for all tools, cold uploads or every graphics driver.

## Native visual and platform acceptance

The native Windows full-window capture was visually inspected: the translucent
ellipse, checkerboard, document border, layer thumbnails and layout were correct.
The native window test compares CPU/GPU center coverage, switches rendering modes
and verifies fault recovery. Generate the local capture with the Release UI test
executable's `--rendering-only <absolute-png-path>` arguments. The local artifact
is `build/interactive-window-validation.png`; no screenshot is required in the
repository.

Windows automation and this visual inspection do not establish every manual
editing workflow or every driver. The complete checklist is in
[MANUAL_VALIDATION.md](../validation/MANUAL_VALIDATION.md#interactive-cpu--gpu-rendering).
Owner-operated native gesture/long-session acceptance and native macOS/Linux
driver/UI acceptance remain separately pending. No macOS/Linux native acceptance
or installer/distribution acceptance is claimed.

## Executable delivery

Canonical direct-launch executable:
`build/apps/image-editor/Release/creative-suite-image-editor.exe`.
Debug is built in the corresponding `Debug` directory. Both outputs are generated
by the canonical build; neither is copied from an auxiliary tree. The isolated
image/raster configuration builds and links without FFmpeg. Unified builds deploy
the existing optional video runtime, including imports retained by Debug linking.
Verified executable modification times (local UTC−03:00): Release,
2026-10-12 10:12:38 (2,075,136 bytes); Debug, 2026-10-12 10:12:55
(6,592,000 bytes). Subsequent rebuilds changed only verification executables.
No installer, commit or push is produced.
