# Integrated Vulkan / D3D11 / NVENC: Windows, 2026-10-16

Status: **qualified experimental integration on the reference Windows device**.
The actual Video Editor offline renderer, Fusion evaluator, FIFO and existing
FFmpeg encoder now use the bridge. Automatic remains OpenGL. The
[contract](../gpu/VULKAN_NATIVE_EXPORT.md) defines selection and recovery;
[prototype evidence](VULKAN_NVENC_PROTOTYPE_WINDOWS_2026-10-16.md) is historical
and uses a different measurement boundary.

## Environment and acceptance

- Windows 11 Pro 10.0.26300, Ryzen 5 3600, NVIDIA GeForce GTX 1660 SUPER,
  driver 32.0.16.1742; real Vulkan/NVENC hardware.
- Release, C++20, MSVC 14.44.35207, Qt 6.7.2, FFmpeg 7.1.2,
  Vulkan SDK 1.4.357.0. No additional encoding SDK or dependency.
- All **60 qualified tuples** passed: five resolutions (1280x720, 1920x1080,
  1080x1920, 2560x1440, 3840x2160), 30/60 fps, three current preset bitrates,
  H.264/HEVC NVENC. Each encoded four native frames with zero output readback
  and explicit encoder upload. Decode checks verify dimensions, frame count,
  color markers and packet timestamps. See the
  [complete qualification CSV](VULKAN_NATIVE_QUALIFICATION_WINDOWS_2026-10-16.csv).
  Effective custom values matching these tuples are eligible too.
- **29/29 scoped CTest entries passed, no skips**, in 73.48 s. Shared composition,
  OpenGL, Vulkan, media, interchange, Video queue/playback/Fusion/export,
  Image rendering and Motion composition/texture/export consumers were exercised.
  The [regression index](../validation/REGRESSION_TESTING.md) maps behaviors to tests.
- Eleven required native executions passed: qualification/fault suite, isolated
  retirement test, two Viewers, shared Vulkan, shared OpenGL, standalone bridge,
  Vulkan timeline, software export, RGBA NVENC, OpenGL/D3D video pipeline and
  Motion export. Vulkan synchronization validation reported no VUID or Sync hazards.
- Native tests cover whole-frame CPU recovery after submit/collect faults,
  ordered callbacks without duplication, bounded pool recovery, 180 MiB pressure
  reducing flights to one, 250 MiB pressure selecting RGBA/CPU without a technical
  error, cancellation and encoder failure preserving an existing destination.
  Fusion includes masks, rotation, alpha and ordered effects: existing RGB
  tolerance 2 and exact alpha before encoding, mean RGB tolerance 12 after lossy
  encoding. Embedded trimmed audio and gain, timestamps and pending-worker
  shutdown pass. These oracle downloads are outside performance runs.
- A separate process injects unconfirmed retirement: resources/reservation stay
  retained, reinitialization fails, the job aborts before CPU reuse, the previous
  destination survives and the app log contains operation and error code.
  This tests conservative quarantine, not an actual driver hang.
- Shared composition also builds with Vulkan and media disabled. This is a
  portability build check, not native acceptance on Linux/macOS.

## Controlled performance method

Three interleaved RGBA/native repetitions per resolution, 30 warm-up frames and
180 measured frames; files contain all 210. Validation layers and concurrent
builds/tests were absent. Both modes use Vulkan composition, H.264 NVENC,
30 fps, Matroska and current Standard bitrates: 10, 17.8 and 40 Mbps.
Application FIFO limits match at 2, 2 and 1 for 1080p, 1440p and 4K. Both modes
use delay=0 and rc-lookahead=0 through the diagnostic
`CREATIVE_SUITE_NVENC_NATIVE_LATENCY=1`; ordinary RGBA defaults remain unchanged
when it is absent. CPU YUV conversion/RGBA delivery versus native BGRA input
remains part of the comparison, so the result does not isolate a single copy.

The export-only workload generates a 512x288 RGB background and two 256x144
alpha overlays: rotation 23 degrees, brightness +7 followed by saturation 80,
and a second overlay at opacity 0.6. Sources, composition and parameters match.
No per-frame verification callback downloads output in measured native mode.

Export total starts at preparation of output frame 30 and ends after renderer
completion, including encoder flush, existing output probing and atomic
publication. The warmed pipeline is not drained at that boundary; its preceding
tail can contribute. This is a steady-state window, not cold-start duration.
Delivery runs from preparing a frame until its encoder input is ready, including
FIFO waits. It is not isolated GPU execution. Mean and p95 use the 180 samples
per repetition; tables show medians of three per-trial statistics, not pooled
percentiles. Stage times and transfer counters in the CSV cover **all 210 frames**,
so they must not be added to reconstruct the 180-frame wall window. Submit time
measures the host call; completion/copy wait time is reported separately.

## Export-only results

All trials and counters are in the
[export CSV](VULKAN_NATIVE_EXPORT_WINDOWS_2026-10-16.csv).

| Resolution | Path | Flights | Total ms | Frames/s | Delivery mean ms | Delivery p95 ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1920x1080 | RGBA | 2 | 1977.88 | 91.01 | 12.067 | 12.872 |
| 1920x1080 | Native | 2 | 944.02 | 190.67 | 6.563 | 7.535 |
| 2560x1440 | RGBA | 2 | 3330.16 | 54.05 | 20.491 | 21.279 |
| 2560x1440 | Native | 2 | 1496.64 | 120.27 | 10.543 | 11.275 |
| 3840x2160 | RGBA | 1 | 10552.90 | 17.06 | 24.858 | 25.919 |
| 3840x2160 | Native | 1 | 4843.65 | 37.16 | 17.151 | 17.817 |

Median total falls by 52.27%, 55.06% and 54.10%, respectively, on this workload
and device. These are complete delivery/encoding results under matched diagnostic
latency settings, not a promise for arbitrary projects or the default RGBA encoder.

Every native trial encoded all 210 frames with zero full output CPU readback and
zero explicit encoder upload. Each mode uploaded 185,794,560 source bytes.
RGBA output readbacks were 1,741,824,000 / 3,096,576,000 / 6,967,296,000 bytes.
Native bridge blits and D3D11 copies **each** moved the corresponding payload.
The native path eliminates the full output CPU transfer, not GPU copies.
RGBA's `encoding_upload_bytes=0` does not mean no driver upload: opaque FFmpeg
and driver transfers are outside that explicit counter.

The maximum known session reservation was 268,272,640 bytes, below
268,435,456 (256 MiB). Peaks include cache history across trials, not per-job
live allocations or total measured VRAM. Shared backing is counted once;
pool/recovery staging estimates share the cap with Vulkan allocations. Opaque
NVENC/driver/swapchain storage is unmeasured and decoder pools retain their
separate existing accounting. Qualification peaked at 268,288,064 bytes.

## Two Viewers during export

This separate workload exports one generated 320x180 image scaled to each output
resolution while both native Viewers present a retained 64x36 GPU texture per
output callback. Each callback waits for both frame receipts before advancing,
so it is a controlled concurrency/pacing check, not unpaced user playback.
It is deliberately distinct from the multi-layer export-only workload.
Each Viewer completes 210 frames, with 180 measured receipt samples and no
GPU fallback. Timing starts when the GUI accepts the payload and ends when both
GPU command receipts complete. It excludes worker-to-GUI queue delay and is not
physical scanout. Total includes export completion/publication, excludes window
teardown. See all [Viewer trials](VULKAN_NATIVE_VIEWERS_WINDOWS_2026-10-16.csv).

| Output | Path | Total ms | Viewer completion mean ms | Viewer completion p95 ms |
| --- | --- | ---: | ---: | ---: |
| 1920x1080 | RGBA | 3569.14 | 7.848 | 9.657 |
| 1920x1080 | Native | 2560.84 | 7.656 | 9.623 |
| 2560x1440 | RGBA | 4923.20 | 7.704 | 9.634 |
| 2560x1440 | Native | 2919.50 | 7.736 | 9.606 |
| 3840x2160 | RGBA | 11905.40 | 7.987 | 9.654 |
| 3840x2160 | Native | 5680.83 | 7.897 | 10.062 |

The native export is faster here too, but there is **no uniform Viewer-tail
improvement**. At 4K, native p95 is 0.408 ms higher by the median statistic.
Paired p95 values are RGBA 9.642/10.066/9.654 ms versus native
9.682/10.062/11.532 ms; the final trial increases by 1.878 ms. This tail change
is not consistent in all three pairs and remains a measured limitation requiring
broader interactive observation, rather than a claim of unchanged preview latency.

## 320x180 at 10 Mbps investigation

The standalone high-frequency H.264 case reproduces NVENC bitstream-lock
`invalid param (8)` in native BGRA mode. RGBA succeeds with its ordinary latency
and with delay=0/lookahead=0 matched to native. Native fails in both runs;
matching latency alone does not remove the failure. Pixel/input format and
internal encoder behavior differ, and the exact cause remains unproven.
No conclusion that bitrate, dimensions, synchronization or the bridge alone is
the cause is justified. Integrated 320x180 at 10 Mbps is explicitly unqualified;
both H.264 and HEVC pass through RGBA without codec/parameter substitution.

## Reproduction and artifacts

From the repository root, build the canonical application and relevant tests:

```powershell
cmake --build build --config Release --target creative-suite-main-editor creative-suite-main-editor-render-export-tests creative-suite-main-editor-vulkan-preview-tests --parallel 4
$env:CREATIVE_SUITE_VULKAN_VALIDATION='1'
$env:VK_LAYER_VALIDATE_SYNC='1'
& build/apps/video-editor/tests/ui/Release/creative-suite-main-editor-render-export-tests.exe --native-gpu --vulkan --require-gpu --qualify-native
& build/apps/video-editor/tests/ui/Release/creative-suite-main-editor-render-export-tests.exe --native-gpu --vulkan --require-gpu --native-retirement-only
& build/apps/video-editor/tests/rendering/Release/creative-suite-main-editor-vulkan-preview-tests.exe --require-gpu --native-bridge
```

Run benchmarks separately, without validation layers or concurrent work:

```powershell
Remove-Item Env:CREATIVE_SUITE_VULKAN_VALIDATION -ErrorAction SilentlyContinue
Remove-Item Env:VK_LAYER_VALIDATE_SYNC -ErrorAction SilentlyContinue
& build/apps/video-editor/tests/ui/Release/creative-suite-main-editor-render-export-tests.exe --native-gpu --vulkan --require-gpu --native-bridge-benchmark
& build/apps/video-editor/tests/rendering/Release/creative-suite-main-editor-vulkan-preview-tests.exe --require-gpu --native-bridge-benchmark
```

The benchmark modes alternate forced RGBA/native themselves and restore their
diagnostic variables. For the small-case investigation, build the optional
prototype and run `--mode=rgba` and `--mode=native` with `--codec=h264_nvenc
--small-high-bitrate`, repeating each with and without
`CREATIVE_SUITE_NVENC_NATIVE_LATENCY=1`. Keep outputs in separate directories.

Local ignored evidence is under `build/vulkan-native-integration-*.log`,
`build/vulkan-native-integration-benchmark-final.log`,
`build/vulkan-native-integration-viewer-benchmark-final.log` and
`build/vulkan-native-small-reproduction/`. CSVs above retain all measurements
without generated videos. The Vulkan/media-disabled build log is
`build/vulkan-native-integration-disabled-final.log`.

Canonical Release executable verified at
`C:\Users\Admin\Desktop\ProjetosCode\AdobeShoppee\build\apps\video-editor\Release\creative-suite-video-editor.exe`,
3,444,224 bytes, modified **2026-10-16 23:28:44 -03:00**. It is built directly
at that path, not copied from a test build. Versions, preferences and project
formats remain unchanged; no installer, commit or publication was produced.

## Provisional conclusion and remaining acceptance

Retain the integration for qualified experimental Windows/NVIDIA output: pixels,
real NVENC files, ordered recovery and resource reuse pass on this device, with
zero full output readback in measured native runs. RGBA remains the recovery and
unqualified path. Keep the selection matrix constrained and the 320x180 failure
open; do not infer qualification of other configurations from the timing gains.
Human editing with representative projects, actual device removal/watchdog
expiry, other adapters/drivers and Linux/macOS native interoperability remain
pending. Native D3D11VA input import is a separate future scope.
