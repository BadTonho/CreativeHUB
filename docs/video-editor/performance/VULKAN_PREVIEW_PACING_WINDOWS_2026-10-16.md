# Vulkan preview scheduling — Windows, 2026-10-16

Status: experimental; native automated evidence on the reference Windows device.
Automatic remains OpenGL. This extends [the Vulkan contract](../gpu/VULKAN_RENDERER.md)
and preserves [the previous asynchronous results](VULKAN_ASYNC_RESULTS_WINDOWS_2026-10-16.md)
as historical evidence.

## Method and comparison boundary

Windows 11 Pro x64 10.0.26300; Ryzen 5 3600; NVIDIA GeForce GTX 1660 SUPER,
driver 32.0.16.1742; Qt 6.7.2; MSVC x64 Release, same reference environment as
the preceding study. No new dependencies, extensions, present modes or queue
architecture are introduced. Presentation stays FIFO; accounted image/transfer
reservations remain capped at 256 MiB. Export still pipelines two RGBA readbacks.

An instrumentation-only baseline was built and measured **before** introducing
the scheduler or empty-acquisition fix. Both versions observe the same per-frame
presentation receipt, original submission fence and production 1 ms completion
timer. The benchmark no longer waits for whole-device `pending == 0`. An opaque
receipt survives context reuse without retaining the device or native window.
The retained baseline executable is a test artifact in ignored `build/`, not the
runnable application.

Preview uses worker composition of the existing three-layer rotated synthetic
fixture into an 800×500 widget. Each trial warms 30 frames, measures 180 frames,
and waits for the measured frame/Viewer receipt before requesting the next.
Three trials run per size/backend. Tables use median trial averages and median
nearest-rank p95 values (rank 171 of 180), not pooled percentiles. No builds or
other test suites ran concurrently with the measurements; desktop activity was
not controlled. Neither queued presentation nor GPU command completion measures
physical display scanout. Decode, audio and real timeline pacing are excluded.

The previous approximately 27 ms p95 used a whole-device completion heuristic
and 12 measured samples. It is **not** a comparable baseline for these changes.
The corrected baseline already changes that measurement before scheduling changes.

## Per-frame GPU completion

| Canvas | Baseline average / p95 ms | Scheduled average / p95 ms | Completion feedback frames/s, before / after |
| --- | ---: | ---: | ---: |
| 480×270 | 13.34 / 16.56 | 13.32 / 14.97 | 74.91 / 74.95 |
| 960×540 | 13.32 / 15.13 | 13.32 / 14.88 | 74.99 / 75.02 |
| 1920×1080 | 13.32 / 14.94 | 13.33 / 15.00 | 75.01 / 74.93 |

Small-fixture p95 improved in all three trials: baseline 17.01/15.68/16.56 ms,
scheduled 14.76/15.00/14.97 ms (median improvement approximately 9.6%). Medium
p95 trial ranges overlap: 15.13/14.77/15.14 versus 15.16/14.79/14.88 ms. Heavy
p95 is effectively unchanged: 14.94/15.02/14.89 versus 15.00/14.88/15.03 ms;
the median increased approximately 0.4%, without a repeatable trial-wide regression.
Average latency and throughput show no general improvement. This does not establish
a universal reduction in preview p95 or superiority over OpenGL.

The synthetic feedback loop already avoids frame bursts, so it does not quantify
the benefit of latest-frame replacement under real playback pressure. Regression
tests separately force redundant updates, overwritten pending frames and unavailable
acquisition. No gain is inferred solely from faster queuing.

| Canvas | Vulkan queued average / p95 ms, before → after | OpenGL Qt-swap average / p95 ms, before → after |
| --- | ---: | ---: |
| 480×270 | 7.80 / 9.09 → 7.63 / 9.08 | 13.33 / 13.61 → 13.33 / 13.66 |
| 960×540 | 7.71 / 8.79 → 7.78 / 9.18 | 13.33 / 13.72 → 13.32 / 13.70 |
| 1920×1080 | 7.90 / 9.48 → 7.81 / 9.08 | 13.33 / 13.62 → 13.33 / 13.48 |

OpenGL's Qt-swap event has its existing semantics and is not equivalent to a
Vulkan GPU receipt. CPU control averages also varied (10.89→11.59, 36.27→38.49,
137.91→136.93 ms); small timing changes should not be treated as guarantees.

## Submissions, resources and transfers

Every Vulkan trial recorded 210 Viewer submissions/completions and 420 total
device submissions, including warm-up. Both versions observed zero deferrals and
zero drops in this paced fixture. The native acquisition test forces ten unavailable
acquisitions and verifies **zero additional submissions**, followed by a successful
acquire/present. Repeated Qt UpdateRequest events likewise produce no extra Viewer
submissions once the visual revision is clean.

| Canvas | Accounted reservations MiB, before / after | Process working set MiB, before / after | Measured upload bytes |
| --- | ---: | ---: | ---: |
| 480×270 | 4.55 / 4.55 | 199.55 / 198.45 | 139,968,000 |
| 960×540 | 18.74 / 18.74 | 208.23 / 207.71 | 559,872,000 |
| 1920×1080 | 70.81 / 79.25 | 259.63 / 255.65 | 2,239,488,000 |

Explicit normal-preview readback stayed zero in all trials. Accounted heavy
reservations increased by approximately 8.44 MiB in this run; they include reusable
pools retained across trials. The process working set includes Qt/driver caches
and is not isolated VRAM. Swapchain/driver allocations are outside the existing
accounted limit. No memory-limit increase or hidden transfer elimination is claimed.

## Export preservation

The existing FFV1/Matroska fixture renders six frames after one warm-up job per
size/backend, three measured repetitions. Totals include preparation, composition,
encoding and atomic finalization. Median elapsed milliseconds:

| Canvas | Vulkan before / after | OpenGL before / after | CPU before / after |
| --- | ---: | ---: | ---: |
| 480×270 | 139.87 / 130.90 | 220.97 / 190.86 | 189.67 / 166.69 |
| 960×540 | 249.71 / 249.23 | 341.07 / 316.67 | 379.49 / 380.67 |
| 1920×1080 | 709.65 / 680.27 | 867.06 / 766.26 | 1189.65 / 1134.23 |

No repeatable export regression appeared; improvements in multiple unchanged
controls prevent attributing these elapsed changes to preview scheduling. All
Vulkan jobs retained two slots, two peak pending frames and six async submissions.
Uploads stayed 94,003,200 bytes/job; readback stayed 3,110,400 / 12,441,600 /
49,766,400 bytes. The cumulative device peak was 267,548,688 bytes (255.15 MiB),
below 268,435,456 bytes. Heavy median readback wait changed from 10.27 to 7.09 ms;
overlapping stage buckets must not be summed as elapsed render time. Codec input
remains RGBA; no direct D3D11VA/NVENC sharing is implemented.

## Native checks and acceptance scope

Native checks use `--require-gpu`, `CREATIVE_SUITE_VULKAN_VALIDATION=1` and
`VK_LAYER_VALIDATE_SYNC=1`. The shared test covers explicit receipts, unchanged
submission counts on unavailable acquisition, next-acquire recovery, context/window
retirement, unrelated pending/recording work and absence of device ownership cycles.
The Viewer test covers scheduler state, native repeated updates, grayscale, resize,
generation retirement, suspend/resume, hidden/exposed resumption, closing with a
tracked presentation, workspace transitions and two Viewers receiving
12 frames each during a real 12-frame two-slot export. The metric test preserves
existing stage IDs, keeps queued Vulkan traces incomplete and credits completion once.

Native composition/preview/timeline/Fusion/export all passed with a required real
GPU and synchronization validation; logs contained no VUID/validation errors.
Software RGBA export and NVIDIA NVENC via RGBA both passed. The available NVENC
check encoded/decoded eight frames with 1,843,200 readback/input bytes and no direct
Vulkan encoder surfaces. Serial Vulkan composition also passed. Native
D3D11/OpenGL/NVENC retained zero decoded downloads, and Motion retained nine PBO
async export frames. The Vulkan-disabled/no-FFmpeg library build passed.

An initial matrix passed 38/39: the new close-in-flight test incorrectly assumed
its native child was already visible while PreviewWidget still showed the CPU
placeholder. Waiting for the first payload to expose/complete and then submitting
a new frame before destruction fixed the fixture; no production behavior was
relaxed. It passed three repeated CTest runs; the next full matrix passed 39/39
without skips in 81.79 s. The final matrix after diagnostic/hidden-timer refinements
also passed **39/39, no skips, 83.43 s** (`vulkan-pacing-regressions-accepted.log`).
`git diff --check` and local documentation-link checks passed.

Broader human editing with paced audio, monitor/DPR changes,
driver loss, packaged runtime and Linux/macOS native acceptance remain pending.
Windows synthetic native tests do not establish those acceptance gates.

## Local evidence

Raw logs stay under ignored `build/`: `vulkan-pacing-preview-baseline.log`,
`vulkan-pacing-preview-candidate.log`, `vulkan-pacing-export-baseline.log`,
`vulkan-pacing-export-candidate.log`, `vulkan-pacing-core-native.log` and
`vulkan-pacing-preview-native.log`. Benchmark commands remain in the renderer
contract. Final build/test logs are retained alongside them:
`vulkan-pacing-accepted-build.log`, `vulkan-pacing-disabled-build.log`,
`vulkan-pacing-regressions-accepted.log`, `vulkan-pacing-preview-repeat-final.log`,
`vulkan-pacing-*-native-final.log`, `vulkan-pacing-export-native.log`,
`vulkan-pacing-nvenc-native.log`, `vulkan-pacing-timeline-native.log`,
`vulkan-pacing-serial-native.log`, `vulkan-pacing-opengl-nvenc-native.log` and
`vulkan-pacing-motion-native.log`. No installers,
versions, project formats, preferences, commits or remote publications change.


The canonical Release application was compiled directly at
`C:\Users\Admin\Desktop\ProjetosCode\AdobeShoppee\build\apps\video-editor\Release\creative-suite-video-editor.exe`.
Verified modification time: **2026-10-16 10:08:00 -03:00**, size **3,389,440 bytes**.
No executable was copied from an auxiliary tree into this path.
