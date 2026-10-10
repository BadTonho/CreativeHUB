# Motion Studio Direct GPU Preview — Windows Results, 2026-10-10

## Environment and workload

Windows 11 Pro 10.0.26300; AMD Ryzen 5 3600; 32 GB RAM; NVIDIA GeForce GTX
1660 SUPER, Windows driver version 32.0.16.1742; Qt 6.7.2; MSVC Release.
The application version remains Beta 0.1.0. These results establish this
controlled reference workload, not general hardware requirements or platform
release acceptance.

The [measurement tool and fixture](GPU_TEXTURE_PREVIEW.md) use a 1920 x 1080,
10-second, five-layer composition: a full-resolution synthetic raster background
with radius-10 Gaussian Blur followed by Color Adjustment (brightness 3,
contrast 105, saturation 95); 640 x 360, 30 fps generated FFV1 video at scale
0.55; a translucent 480 x 320 image at scale 0.3; static text; and a moving
180 x 180 translucent ellipse. Media is generated locally in temporary storage.
The preview window is 1008 x 606. Video sources, text and shapes retain CPU
preparation; this is not hardware decoding or a completely GPU-resident input
pipeline.

Each run has two seconds of warm-up followed by ten seconds of measured
monotonic playback. Three trials interleave CPU, GPU with RGBA readback, and
GPU texture delivery at 30 fps, followed by the same three trials at 60 fps.
No build or other test workload ran concurrently. CPU figures use process CPU
time over wall time: 100% represents one occupied core. Memory is the end-of-run
working set, not peak memory or driver-measured VRAM.

## Median results

Medians are calculated from the three run averages/p95 values; a median p95
is not the percentile of pooled frames. Presentation counts come from successful
viewer draws, not physical display scanout. The warm-up/measurement boundary can
include one in-flight frame, explaining the 60.1 fps count in the RGBA path.

| Target | Backend | Presented fps | Request-to-paint avg / p95 (ms) | Render avg (ms) | CPU % | Working set (MiB) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 30 | cpu | 7.2 | 154.13 / 174.61 | 131.35 | 319.6 | 207.8 |
| 30 | gpu_rgba | 30.0 | 17.75 / 22.95 | 14.59 | 60.2 | 217.0 |
| 30 | gpu_texture | 30.0 | 12.44 / 14.99 | 9.24 | 49.1 | 214.9 |
| 60 | cpu | 7.9 | 138.21 / 153.46 | 126.00 | 329.1 | 220.0 |
| 60 | gpu_rgba | 60.1 | 13.81 / 18.01 | 10.51 | 91.7 | 221.7 |
| 60 | gpu_texture | 60.0 | 10.64 / 13.69 | 7.10 | 76.6 | 220.5 |

Compared with GPU RGBA, direct delivery reduces median average request-to-paint
latency by **29.9% at 30 fps** and **22.9% at 60 fps**. Each individual trial
also has lower average latency with direct delivery. All direct runs sustain the
requested rate without coalescing, stale results, busy drops or GPU failures.
CPU preview delivers 6.6–8.2 fps in this deliberately full-frame blur workload.
These measurements accept direct delivery as an improvement on this reference
system and composition. GPU remains experimental and default off.

At 30 fps, GPU RGBA reads back 2,488,320,000 bytes per measured interval;
direct delivery reads back **zero** and performs **zero viewer texture uploads**.
Input upload remains 3,090,000,000 bytes. At 60 fps, direct upload is
6,180,000,000 bytes with zero readback. Output pool reservations peak at
16,588,800–24,883,200 bytes (15.82–23.73 MiB), below 64 MiB; occupied targets
peak at two, below the three-target limit.

## Individual runs

All runs have zero recorded GPU failures. Bytes are decimal GB in this table;
output pool size uses binary MiB. These are known reservations, not measured VRAM.

| Target | Backend | Trial | Presented fps | Latency avg / p95 (ms) | Render avg (ms) | Coalesced / stale | CPU % | Working set MiB | Upload / readback GB | Output pool peak MiB / occupancy |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 30 | cpu | 1 | 7.2 | 154.13 / 174.61 | 131.35 | 211 / 1 | 319.6 | 118.6 | 0.000 / 0.000 | 0.00 / 0 |
| 30 | gpu_rgba | 1 | 30.0 | 17.93 / 22.95 | 14.59 | 0 / 0 | 61.4 | 219.1 | 3.090 / 2.488 | 0.00 / 0 |
| 30 | gpu_texture | 1 | 30.0 | 14.33 / 21.36 | 10.36 | 0 / 0 | 53.7 | 229.5 | 3.090 / 0.000 | 15.82 / 2 |
| 30 | cpu | 2 | 8.0 | 144.85 / 163.87 | 124.58 | 220 / 0 | 345.6 | 207.8 | 0.000 / 0.000 | 0.00 / 0 |
| 30 | gpu_rgba | 2 | 30.0 | 17.75 / 24.01 | 14.67 | 0 / 0 | 60.2 | 214.7 | 3.090 / 2.488 | 0.00 / 0 |
| 30 | gpu_texture | 2 | 30.0 | 12.44 / 14.99 | 9.24 | 0 / 0 | 46.2 | 214.0 | 3.090 / 0.000 | 15.82 / 2 |
| 30 | cpu | 3 | 6.6 | 173.49 / 201.76 | 152.90 | 235 / 0 | 287.6 | 219.2 | 0.000 / 0.000 | 0.00 / 0 |
| 30 | gpu_rgba | 3 | 30.0 | 16.92 / 21.36 | 13.67 | 0 / 0 | 55.8 | 217.0 | 3.090 / 2.488 | 0.00 / 0 |
| 30 | gpu_texture | 3 | 30.0 | 11.84 / 13.82 | 8.66 | 0 / 0 | 49.1 | 214.9 | 3.090 / 0.000 | 15.82 / 2 |
| 60 | cpu | 1 | 8.2 | 134.88 / 148.46 | 123.00 | 519 / 0 | 324.5 | 220.0 | 0.000 / 0.000 | 0.00 / 0 |
| 60 | gpu_rgba | 1 | 60.1 | 13.81 / 17.91 | 10.51 | 0 / 0 | 88.4 | 217.4 | 6.180 / 4.977 | 0.00 / 0 |
| 60 | gpu_texture | 1 | 60.0 | 10.64 / 13.69 | 7.10 | 0 / 0 | 80.0 | 216.0 | 6.180 / 0.000 | 15.82 / 2 |
| 60 | cpu | 2 | 7.5 | 144.09 / 161.34 | 132.25 | 525 / 0 | 329.1 | 215.6 | 0.000 / 0.000 | 0.00 / 0 |
| 60 | gpu_rgba | 2 | 60.1 | 13.76 / 18.01 | 10.44 | 0 / 0 | 91.9 | 221.7 | 6.180 / 4.977 | 0.00 / 0 |
| 60 | gpu_texture | 2 | 60.0 | 11.24 / 15.72 | 7.48 | 0 / 0 | 76.6 | 220.8 | 6.180 / 0.000 | 23.73 / 2 |
| 60 | cpu | 3 | 7.9 | 138.21 / 153.46 | 126.00 | 520 / 0 | 354.7 | 226.3 | 0.000 / 0.000 | 0.00 / 0 |
| 60 | gpu_rgba | 3 | 60.0 | 14.36 / 19.85 | 10.82 | 2 / 0 | 91.7 | 221.9 | 6.170 / 4.968 | 0.00 / 0 |
| 60 | gpu_texture | 3 | 60.0 | 9.57 / 13.21 | 6.49 | 0 / 0 | 63.6 | 220.5 | 6.180 / 0.000 | 15.82 / 2 |

## Verification and remaining acceptance

The native `gpu-texture-tests --require-gpu` run requires a sharing OpenGL
viewer and real direct texture presentation. It covers CPU/GPU displayed-pixel
parity, orientation, mixed effects, resize, rapid seeks, composition replacement,
presentation recovery, saturated-pool playback/paused behavior, GPU RGBA
delivery without sharing, selection guide/context-menu preservation, and actual
MainWindow Play/Pause/Loop and destruction during active playback. Portable
checks cover mailbox coalescing and invalidation, queued shutdown and CPU fallback.
Windows Debug and Release each passed **16/16** selected CTest targets: all
12 Motion tests plus animation, effects, CPU composition and native OpenGL
composition. The shared OpenGL test reported the NVIDIA driver, performed parity,
lease retirement/capacity, readback and lifecycle checks, and returned success
without a graphical skip. Native texture tests require GPU and pass in both
configurations; skipped graphics tests are not counted as native approval.
Native Motion composition also reported one GPU frame and zero CPU fallbacks in
both configurations. Native `export-tests --require-gpu` passed Debug and Release,
reporting `native_gpu_export_frames=9 readback_mode=pbo_async`. Lossless encoded
parity, synchronous staging fallback, injected collection recovery, cancellation,
atomic destination preservation and encoder failure handling all pass.
Summary assertions compare each completed job's collected/encoded counts against
its own rendered count; the MainWindow fixture has 120 frames, not nine.
The final changed export/texture CTest targets were rerun in both configurations
and passed after these additional native assertions.

Broader manual editing, dock layouts, repeated full-app closure and driver-loss
scenarios beyond the injected context-destruction signal remain pending. The
native UI checks approve their deterministic actions, not every interactive
workflow.
Native macOS/Linux driver and performance acceptance remain pending. This run
does not validate small/medium/heavy project acceptance beyond the documented
fixture and deterministic boundary cases. The historical 3,405-frame export
composition was not available in this validation; its post-PBO export benchmark
and 52.21 s acceptance threshold remain pending. Existing export behavior and
regressions are preserved. A native ProRes run observed a five-level decoded
channel difference against CPU, exceeding the existing encoded tolerance of four.
The native parity fixture therefore explicitly selects lossless Matroska/FFV1;
it keeps the tolerance unchanged and requires real asynchronous GPU readback.
ProRes encoded parity within four levels remains pending; the lossless run does
not establish that codec's acceptance. No encoder or production export behavior
was changed.

Reproduce with the native Qt platform by explicitly building and running
`creative-suite-motion-editor-gpu-preview-benchmark`. It emits aggregate CSV;
performance is intentionally not a timing-sensitive CTest. Generated media and
logs must not be published. The measured benchmark executable SHA-256 was:

`34b4f8cae084d80aae1706bd942d97d540efaf18080a223de3037ddf76dde2c8`

## Development build delivery

The canonical runnable Release output is
`build/apps/motion-editor/Release/creative-suite-motion-editor.exe`, verified at
`C:/Users/Admin/Desktop/ProjetosCode/AdobeShoppee/build/apps/motion-editor/Release/creative-suite-motion-editor.exe`.
Its modification time is **2026-10-10 16:04:53 -03:00**; size 1,616,896 bytes.
This is a development build, not an installer or published release.

The final canonical build and windeployqt completed successfully using the
configured Qt 6.7.2 qtpaths executable. Deployed runtime includes Qt6Core,
Qt6Gui, Qt6Widgets, Qt6OpenGL, Qt6OpenGLWidgets and the Windows platform plugin.
The deployed Qt6OpenGLWidgets.dll matches the installation's SHA-256:
`B458A2B7332E362CCC9EAF49AC3E9466DD50FA5DA3AB828165C5801230A6A5D2`.
No shared-library API, persisted format, integration contract, application
version or shortcut changed; unrelated application rebuilds were not required.
