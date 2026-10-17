# Vulkan / D3D11 / NVENC prototype: Windows, 2026-10-16

Historical prototype record. The subsequent
[integrated editor evidence](VULKAN_NATIVE_EXPORT_WINDOWS_2026-10-16.md) records
the qualified export integration; the status below describes this earlier stage.

Status: **native proof achieved for the tested configuration; provisional and
external to the Video Editor export flow**. Linux/macOS and other Windows
adapters/drivers remain pending. The existing editor Vulkan export still uses
RGBA; Automatic stays on OpenGL.

## Environment and method

- Windows 11 Pro 10.0.26300, Ryzen 5 3600, NVIDIA GeForce GTX 1660 SUPER,
  driver 32.0.16.1742. Hardware Vulkan/NVENC, not a software device.
- MSVC 14.44.35207, C++20 Release, Qt 6.7.2, FFmpeg 7.1.2,
  Vulkan SDK headers/tools 1.4.357.0. Existing FFmpeg encoder/native pool,
  no additional vendor encoding SDK.
- The existing two-frame BGRA pool is created first; its LUID selects Vulkan.
  Only experimental creation enables external-memory/keyed-mutex extensions.
- Composition uses the same generated 512x288 RGB background and two 256x144
  alpha overlays, rotation 23 degrees, opacity 0.6, ordered brightness (+7) then
  saturation (80), plus a per-frame RGB marker. Nearest sampling, RGBA8 arithmetic
  and precision are shared with the existing compositor.
- Both measured modes use Vulkan composition, H.264 NVENC, 20 Mbps, Matroska,
  30 fps timestamps, the existing encoder settings and identical flight limits.
  This compares the whole delivery/encoder-input paths, including CPU conversion
  for RGBA versus native BGRA NVENC input; it does not isolate just one copy.
- Per resolution, interleave RGBA/native for three repetitions. Encode 30 warm-up
  frames, drain the application FIFO, then measure 180 frames through encoder
  finalization. Files contain all 210 frames. No concurrent builds/tests or
  validation layers during benchmarks. Verification downloads/decoding are
  excluded; native benchmark oracle/output-readback counters are zero.
- Total excludes setup, warm-up and final atomic publication; includes final
  encoder flush. Delivery starts before preparing/submitting a frame and ends
  when its corresponding CPU/native encoder input is ready. At two flights it
  includes time waiting behind the preceding encoder call. It is neither isolated
  GPU execution nor display latency. Mean and p95 are calculated per repetition;
  tables below give medians of the three values, not a pooled percentile.

[Reproduction commands and ownership contract](../../../prototypes/vulkan-nvenc/README.md).
The [complete CSV](VULKAN_NVENC_PROTOTYPE_WINDOWS_2026-10-16.csv) retains all 18
measurements, including counters, waits, occupancy and reservation peaks.

## Completed delivery and total export results

| Size | Mode | Flights | Total ms | Frames/s | Delivery mean ms | Delivery p95 ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1080p | rgba | 2 | 1573.66 | 114.38 | 10.60 | 12.80 |
| 1080p | native | 2 | 987.61 | 182.26 | 8.22 | 9.39 |
| 1440p | rgba | 2 | 2642.14 | 68.13 | 17.98 | 21.12 |
| 1440p | native | 2 | 1677.68 | 107.29 | 14.39 | 15.63 |
| 4K | rgba | 1 | 9701.34 | 18.55 | 28.93 | 31.01 |
| 4K | native | 1 | 5483.11 | 32.83 | 22.21 | 24.03 |

Native median total wall time was lower by 37.2% / 36.5% / 43.5% at
1080p / 1440p / 4K in this workload. This is a repeatable synthetic-path result,
not a promise of that gain or frame rate in a complete editor project.

| Size | Native bridge submission calls ms | Native bridge waits ms | Native encoder calls ms | RGBA encoder calls ms |
| --- | ---: | ---: | ---: | ---: |
| 1080p | 9.32 | 444.74 | 482.79 | 1151.70 |
| 1440p | 9.04 | 866.68 | 748.81 | 1908.32 |
| 4K | 11.21 | 3932.61 | 1490.87 | 4218.38 |

These stage totals are worker wall-clock observations across 180 measured frames.
Bridge submission measures vkQueueSubmit call time, not completed GPU execution;
waits cover Vulkan completion, mutex acquisition and D3D copy-event polling.
Queued submission time alone is not presented as a throughput improvement.
RGBA has no bridge counters; zero there does not mean no Vulkan submissions.

## Transfers, copies and memory

Each native repetition collected exactly 180 of 180 measured submissions in FIFO
order. Peak pending equals two at 1080p/1440p and one at 4K. Both modes use that
same conservative parallelism. No native run silently recovered through RGBA.

| Size | Source CPU upload bytes, each mode | RGBA output readback / CPU encoder input bytes | Native output readback / CPU encoder input bytes | Native Vulkan blit bytes | Native D3D copy bytes | Largest combined known MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1080p | 160911360 | 1492992000 | 0 | 1492992000 | 1492992000 | 103.38 |
| 1440p | 160911360 | 2654208000 | 0 | 2654208000 | 2654208000 | 180.49 |
| 4K | 160911360 | 5971968000 | 0 | 5971968000 | 5971968000 | 224.54 |

The native path eliminates full output pixel transfer through the CPU, **not GPU
copies**. Existing integer-to-normalized compute processing precedes the counted
RGBA-to-BGRA blit and D3D CopyResource. The two GPU byte columns count logical
frame payloads, not measured memory-bus reads plus writes. Source fixture uploads
remain in both modes. Verification downloads are explicit and absent in benchmarks.

`encoder_explicit_upload_bytes=0` in both modes measures only the existing explicit
av_hwframe_transfer_data fallback counter. Software RGBA supplied to NVENC can
cause opaque FFmpeg/driver uploads that this counter cannot measure. Their absence
must not be inferred from zero. Native writeVideo retains/clones the D3D11 AVFrame
and submits it directly; it does not call the RGBA conversion or download path.

Known peaks include Vulkan allocations (live/idle/retiring images and buffers),
imported shared backing counted once, and output-pool estimates. They include
cache high-water marks across repetitions on the same device; consequently a later
RGBA trial can report the previous native peak. The first RGBA peaks were smaller.
D3D pool estimates are 16,588,800 / 29,491,200 / 66,355,200 bytes. All combined peaks
remain below 268,435,456 bytes. This is not driver-measured VRAM and does not bound
opaque encoder/driver allocations or host fixture storage.

## Correctness and lifecycle evidence

The required-hardware self-test passed with Vulkan synchronization validation:

- Distinct RGB/alpha synthetic data survives integer normalization, BGRA sharing
  and D3D copy with existing RGB tolerance 2 and exact alpha. CPU composition
  references additionally cover rotation, position, opacity and ordered effects.
- H.264 and HEVC NVENC encode/decode 12 frames at 320x180, 1 Mbps, with dimensions,
  frame markers/order, packet PTS in 1/30 units and existing mean RGB tolerance 12.
- Two pending tickets, capacity Busy, FIFO refusal, invalid/stale tickets, reuse,
  explicit one-flight mode and memory-driven reduction pass. A controlled rejected
  format, unmatched LUID, ordinary non-opt-in device and zero/excess/exhausted
  budget are rejected. Hardware really lacking extensions was not substituted;
  its rejection follows queried capabilities and the unavailable-build path.
- Cancelled submission and pending collection retain safe ownership. Retrying
  collection, discard and destruction with pending GPU consumers pass. Injected
  cancellation/encoder failure leaves existing destination bytes unchanged.
- All 18 benchmark outputs additionally passed packet-level inspection: H.264,
  expected dimensions, 210 packets and ordered 30 fps PTS. Pixel/decode verification
  runs separately from the measured benchmark; there is no claim of lossless NVENC.

No VUID, SYNC-HAZARD or validation-error messages were found in the eight required
native test logs. Actual driver-loss/watchdog expiry and physical device removal
were not induced; they remain failure-qualification limits, not successful tests.

## Regression gate and artifacts

Commands were run after explicitly rebuilding affected test targets. Required
hardware commands all returned 0; none was accepted as skipped:

```powershell
$env:CREATIVE_SUITE_VULKAN_VALIDATION = '1'
$env:VK_LAYER_VALIDATE_SYNC = '1'
& ./build/prototypes/vulkan-nvenc/Release/creative-suite-vulkan-nvenc-prototype.exe --self-test --require-native
& ./build/libs/tests/Release/creative-suite-composition-vulkan-tests.exe --require-gpu
& ./build/apps/video-editor/tests/rendering/Release/creative-suite-main-editor-vulkan-preview-tests.exe --require-gpu
& ./build/apps/video-editor/tests/rendering/Release/creative-suite-main-editor-gpu-timeline-tests.exe --vulkan --require-gpu
& ./build/apps/video-editor/tests/ui/Release/creative-suite-main-editor-render-export-tests.exe --native-gpu --vulkan --require-gpu
& ./build/apps/video-editor/tests/ui/Release/creative-suite-main-editor-render-export-tests.exe --native-gpu --vulkan --require-gpu --nvenc-only
& ./build/apps/video-editor/tests/rendering/Release/creative-suite-main-editor-gpu-video-pipeline-tests.exe --require-gpu
& ./build/apps/motion-editor/tests/Release/creative-suite-motion-editor-export-tests.exe --require-gpu
Remove-Item Env:CREATIVE_SUITE_VULKAN_VALIDATION
Remove-Item Env:VK_LAYER_VALIDATE_SYNC
```

The existing Video NVENC regression above still intentionally uses RGBA; its
readbacks are unrelated to the new native prototype proof. OpenGL/D3D11 pipeline
and Motion regressions protect existing consumers. The scoped CTest gate passed **27/27**, with no skipped entries (63.55 s). It includes
composition/OpenGL/Vulkan, media encoder, interchange, Video preview/timeline/
export/queue/playback, Image canvas and Motion texture/composition/export.
The Vulkan-disabled, media-disabled shared composition build also succeeded:

```powershell
cmake --build build/vulkan-disabled-check --config Release --target creative-suite-composition-opengl --parallel 4
```

Its cache uses `ENABLE_VULKAN_RENDERER=OFF`, `BUILD_VIDEO_MEDIA_LIBRARY=OFF`,
with application targets disabled. This verifies portable stubs, not native interop
on another operating system. Default prototype option is OFF.

Canonical Release artifacts were built directly, with no executable copying:

- `C:\Users\Admin\Desktop\ProjetosCode\AdobeShoppee\build\apps\video-editor\Release\creative-suite-video-editor.exe`:
  2026-10-16 14:55:15 -03:00, 3,392,512 bytes.
- `C:\Users\Admin\Desktop\ProjetosCode\AdobeShoppee\build\prototypes\vulkan-nvenc\Release\creative-suite-vulkan-nvenc-prototype.exe`:
  2026-10-16 14:55:12 -03:00, 437,248 bytes.

Local logs stay in ignored `build/`: `vulkan-nvenc-build-final.log`,
`vulkan-nvenc-regression-build.log`, `vulkan-nvenc-build-delivery.log`,
`vulkan-nvenc-disabled-build.log`, `vulkan-nvenc-disabled-build-delivery.log`,
`vulkan-nvenc-native-*.log`, `vulkan-nvenc-benchmark-final.log`,
`vulkan-nvenc-output-inspection.log` and `vulkan-nvenc-ctest.log`.
The final rebuild added ticket/error-code context to exceptional retirement logs;
the normal measured path was unchanged. The final executable repeated its native
self-test with synchronization validation successfully. The CSV above retains
measurements without binaries or private media.
CLI cancellation, unavailable/required-native exit codes and an invalid selected
encoder were also checked (`vulkan-nvenc-cli-check.log`): cancellation returned 0,
unavailability returned 77 or 1 as requested, encoder failure returned 1, all
existing destinations survived and temporary outputs were removed. Cancellation
produced no error entry.

## Limitations and recommendation

During bring-up, the 320x180 high-frequency native H.264 fixture at 10 Mbps caused
NVENC bitstream-lock error 8 on frame zero. Raw shared-image parity was correct;
the 1 Mbps small correctness fixture and all 20 Mbps large benchmark runs passed.
This is observed parameter sensitivity, **not a proven diagnosis or a general
encoder fix**. Native mode did not switch codec or use RGBA after that failure.
Broader size/bitrate/preset/driver qualification remains necessary for integration.

Recommend a **separate, controlled experimental editor-integration step** for
qualified NVENC jobs, retaining the existing RGBA path and full-frame recovery.
Evidence supports eliminating output CPU transfer with correct pixels, valid
NVENC output and safe resource reuse on this machine; the synthetic speed gain
is encouraging but does not decide editor acceptance. Preserve per-job backend
capture, cancellation, audio, queue and atomic publication when integrating.
Do not enable this automatically, change pool flags, or import D3D11VA inputs as
part of this prototype. Keep RGBA for unqualified configurations. Other platforms,
real-project workloads, device-loss handling and codec-parameter coverage require
their own acceptance rather than expanding this delivery implicitly.
