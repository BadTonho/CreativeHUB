# Selectable renderer evidence — Windows, 2026-10-15

Status: **automated Windows/NVIDIA evidence; experimental implementation**.
Broader manual editing, physical scanout, driver-loss and packaged runtime
acceptance remain pending. Native macOS/Linux acceptance remains pending.
See [the implementation and manual checklist](../gpu/VULKAN_RENDERER.md).

## Environment and method

Windows x64, OS version 10.0.26300.0; AMD Ryzen 5 3600; approximately 32 GiB RAM;
NVIDIA GeForce GTX 1660 SUPER, Windows driver **32.0.16.1742**; Qt 6.7.2,
MSVC 14.44.35207, Release. The Vulkan device advertises shaderFloat64.
The recorded Vulkan instance/device/pipeline initialization sample is **93.51 ms**;
this is one process sample, not complete application startup time.

Each table reports medians of three interleaved trials. A median p95 is the median
of the trials' p95 values, not a percentile of pooled samples. Benchmarks ran
sequentially without agent builds or other agent tests running concurrently.
Background desktop/driver activity was not controlled. These are generated
fixtures, not personal media or a representative release acceptance project.

The RGBA composition fixture uses an opaque background and two half-size
transparent overlays, one rotated 23 degrees, with padded input rows. Each
backend warms for three frames and measures twelve. GPU totals include upload,
composition, synchronization and RGBA readback. They are CPU wall times, not GPU
execution timestamps. Vulkan now prefers HOST_CACHED coherent memory for readback
when available; other devices retain the coherent-memory fallback.

## Composition to RGBA

| Canvas | CPU average / p95 ms | OpenGL average / p95 ms | Vulkan average / p95 ms |
| --- | ---: | ---: | ---: |
| 480×270 | 7.81 / 8.66 | 0.71 / 0.86 | 2.75 / 3.00 |
| 960×540 | 32.49 / 34.79 | 2.66 / 3.35 | 4.74 / 5.33 |
| 1920×1080 | 122.31 / 127.08 | 14.15 / 17.35 | 11.81 / 13.05 |

The larger RGBA fixture favors Vulkan over OpenGL on this run; the smaller ones
favor OpenGL. This is not evidence that one backend is universally faster.

## Native preview

The preview fixture uses tightly packed rows, the same three-layer arrangement,
and an 800×500 viewer. Composition runs on a worker; the production PreviewWidget
presents CPU pixels or shared GPU images. Each trial warms three frames and
measures twelve, with one request in flight and no target-rate pacing. Latency
starts before worker composition and ends at the observed presentation event.
Throughput includes the intervals between those requests. These events are Qt
paint/swap completion or Vulkan presentation completion, **not physical display
scanout FPS**. Decode, audio and timeline scheduling are covered separately by
regression tests and are not part of this performance fixture.

| Canvas | Backend | Average / p95 ms | Presentation events/s | Process CPU % | End working set MiB |
| --- | --- | ---: | ---: | ---: | ---: |
| 480×270 | cpu | 11.49 / 13.28 | 86.92 | 81.7 | 232.6 |
| 480×270 | opengl | 16.68 / 17.12 | 59.93 | 115.4 | 255.0 |
| 480×270 | vulkan | 12.65 / 15.05 | 78.97 | 17.2 | 239.5 |
| 960×540 | cpu | 38.63 / 46.32 | 25.88 | 99.0 | 243.9 |
| 960×540 | opengl | 16.66 / 16.97 | 59.98 | 106.4 | 267.8 |
| 960×540 | vulkan | 13.78 / 15.75 | 72.51 | 43.1 | 268.9 |
| 1920×1080 | cpu | 137.22 / 153.73 | 7.29 | 100.4 | 306.2 |
| 1920×1080 | opengl | 12.77 / 13.61 | 78.25 | 122.4 | 317.3 |
| 1920×1080 | vulkan | 18.79 / 20.41 | 53.17 | 32.8 | 325.4 |

Both GPU preview modes report **zero RGBA readback bytes**. Vulkan's measured
process CPU load is lower, while the larger preview has higher latency than
OpenGL. CPU percentages use 100% for one occupied core. CPU sampling includes
warm-up; working sets are end samples, not peaks. All modes run in the same
process with driver/device caches retained, so these samples do not isolate the
memory cost of selecting one API at application startup.

## Complete export

Each job exports six frames through FFV1/Matroska, including source preparation,
composition, pixel conversion, encoding and finalization. Sources remain a
1920×1080 background and two 1280×720 transparent overlays; output sizes vary.
Each backend has an unreported warm-up job before three measured trials per size.
The native export regression suite runs before this benchmark.

| Output | CPU total ms | OpenGL total ms | Vulkan total ms |
| --- | ---: | ---: | ---: |
| 480x270 | 158.35 | 192.99 | 173.92 |
| 960x540 | 386.68 | 326.52 | 316.42 |
| 1920x1080 | 1187.92 | 776.72 | 806.54 |

OpenGL has the shorter 1080p export total here; Vulkan has a slightly shorter
960×540 total. CPU wins the smallest export. Keep Automatic on the established
policy rather than promoting Vulkan from these fixtures.

At 1080p each GPU job reads back **49,766,400 bytes** for six encoder inputs.
Median readback wall time is 42.65 ms for OpenGL and 26.17 ms for Vulkan. Source
uploads total 94,147,200 bytes for OpenGL (including geometry lookup data) and
94,003,200 bytes for Vulkan. The Vulkan device peak reported by export is
**216.91 MiB**, including caches retained from earlier validation jobs. The OpenGL
per-job peak is **15.86 MiB**. Their accounting scopes differ: these are not a
per-job VRAM comparison or the minimum memory required by this fixture. Vulkan's
shared image/transfer reservations remain bounded to 256 MiB; driver and Qt
swapchain storage are outside that counter.

A separate explicit NVENC check exports and decodes eight 320×180 H.264 frames
through Vulkan composition and the existing RGBA encoder boundary. Its observed
RGBA encoder input and readback totals are each **1,843,200 bytes**; native encoded
surface count is zero as intended for this implementation. NVENC's internal
upload is not measured by the existing explicit-transfer counter; zero there
must not be interpreted as an end-to-end zero-copy encoder path.

## Verification and reproduction

The selected Release regression matrix passed **38/38 tests with no skips**
(81.76 seconds). It includes renderer selection/settings and translation,
composition/effects/animation, Vulkan preview/timeline/Fusion/export, existing
OpenGL preview, interchange, and affected Image/Motion consumers. Native Windows
Vulkan runs additionally require `--require-gpu`; unavailable graphics cannot
silently satisfy these checks. The separate D3D11/OpenGL/NVENC pipeline regression
also passed after the bounded released-target retry adjustment.
After matching the Vulkan viewer surround to OpenGL, explicit native composition,
preview, timeline/Fusion and NVENC checks passed again, together with the startup
renderer-selection test. The NVENC check retained eight decoded frames and the
RGBA transfer totals above.

The optional Vulkan-disabled, image-only shared-library configuration also built
without FFmpeg. Dependency inspection of the canonical executable found no
mandatory Vulkan-loader DLL import. No Linux/macOS native run, packaged runtime
acceptance, physical scanout measurement or driver-loss exercise is claimed.

Reproduce from the canonical build tree, sequentially after builds finish:

```powershell
build/libs/tests/Release/creative-suite-composition-vulkan-tests.exe --require-gpu --benchmark
build/apps/video-editor/tests/rendering/Release/creative-suite-main-editor-vulkan-preview-tests.exe --require-gpu --benchmark
build/apps/video-editor/tests/ui/Release/creative-suite-main-editor-render-export-tests.exe --benchmark --vulkan --require-gpu --compare-renderers
build/apps/video-editor/tests/ui/Release/creative-suite-main-editor-render-export-tests.exe --native-gpu --vulkan --require-gpu --nvenc-only
```

Local raw logs remain under ignored `build/`: `vulkan-comparison-benchmark-final.log`,
`vulkan-preview-comparison-benchmark.log`, and
`vulkan-export-comparison-benchmark-final.log`. The tracked tables preserve their
scope and medians without committing build logs or generated media.
The selected regression run is retained locally as
`vulkan-accepted-regressions.log`; the native pipeline result is
`native-gpu-video-pipeline-final.log`.
