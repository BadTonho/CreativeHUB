# D3D11VA / Vulkan / NVENC prototype: Windows, 2026-10-17

Status: **native end-to-end proof at 1080p and 1440p on the reference device;
4K composed native workload unavailable within the existing budget**. This is
standalone research, not editor input integration. Automatic, preferences,
versions and project formats remain unchanged. See the
[input contract](../gpu/VULKAN_DECODE_PROTOTYPE.md) and
[reproduction guide](../../../prototypes/vulkan-nvenc/README.md).

## Environment and correctness evidence

- Windows 11 Pro 10.0.26300, Ryzen 5 3600, GTX 1660 SUPER,
  driver 32.0.16.1742. Real D3D11VA, Vulkan and NVENC hardware.
- Release, MSVC 14.44.35207, Qt 6.7.2, FFmpeg 7.1.2,
  Vulkan SDK 1.4.357.0. Decoder/output/Vulkan adapter LUID bytes match:
  `f7b0000000000000`. Input NV12, shared BGRA8, internal RGBA8_UINT.
- **31/31 scoped CTest entries passed without skips**, 80.86 s. Coverage includes
  media, shared Vulkan/OpenGL, composition/interchange, Video playback/Fusion/
  queue/export/preview, Motion export/composition/texture and Image rendering.
- **14 required native executions passed** with synchronization validation:
  decoded self-test, existing output prototype, OpenGL video pipeline, shared
  Vulkan, shared OpenGL, Vulkan timeline, two Viewers during native export,
  existing 60-tuple native output matrix, Motion export, hardware media and four
  decoded native pixel/output checks (1080p/1440p, H.264/HEVC NVENC output).
  No VUID, Sync hazard or validation error was reported.
- The decoded self-test covers all twelve input-codec/matrix/range combinations
  (H.264/HEVC; BT.601/709/2020 NCL; full/limited). Existing RGB tolerance 2 and
  exact alpha are preserved; independent tagged-patch equations retain their
  existing tolerance 5. A 322x182 source exercises padded storage/visible extent.
- Tests submit two imports, reject a third and invalid/stale/out-of-order tickets,
  cancel collection, destroy the decoder before collecting, re-prepare after a
  device-changing seek and retain an old GPU image across slot reuse. A separate
  1.5 MiB session proves Busy and one-flight reduction. Ordinary Vulkan creation
  and incompatible formats are refused; the existing output self-test retains
  wrong-LUID creation coverage.
- Injected input failures after D3D conversion while holding the mutex, after
  external Vulkan command recording and after the D3D handoff all retire safely.
  Collection failure is covered too. D3D query/fence completion, not submission,
  authorizes release. Actual driver removal/watchdog expiry was not induced.
- All four native/RGBA input/output combinations pass with both output codecs;
  HEVC input also passes end to end. Pre-encoder composition includes ordered
  brightness/saturation, rotation and an alpha overlay. Lossy decode retains
  mean RGB tolerance 12, dimensions/count and ordered timestamps. Cancellation
  and encoder failure preserve existing destinations.
- A separate required 4K native run returns explicit unavailability, preserves
  sentinel destination bytes and leaves no partial output. This expected refusal
  is **not** counted as successful 4K native encoding.
- The shared library builds with Vulkan and media disabled. This is a portable
  build check, not Linux/macOS native acceptance. The
  [regression index](../validation/REGRESSION_TESTING.md) maps the checks to sources.

## Performance method

The final validated executable generated one 210-frame H.264/NVENC YUV420P source
per resolution, 4 Mbps, 30 fps, BT.709 limited range, with rotating temporal color
patches. Both measured modes consume that same file through hardware decoding,
use the same brightness (+7), saturation (80), a 64x36 alpha overlay rotated
23 degrees at opacity 0.6, and H.264 NVENC output at 20 Mbps/30 fps in Matroska.
This compares native BGRA encoder input against RGBA/CPU conversion, not a single
isolated copy. No audio or application timeline engine is added.

Interleave RGBA/native three times per resolution. Drain 30 warm-up frames, then
time 180 through encoder finalization and atomic publication. Source generation,
decoder/device/pool/shader/encoder setup and warm-up are excluded. Both modes use
**one application frame in flight**, with the same output-pool allowance (two
maximum frames at 1080p/1440p, one at 4K), and delay=0/lookahead=0 through the
existing diagnostic. Ordinary editor RGBA encoder defaults are unchanged.
There were no concurrent builds/tests or validation layers during measurements.

Counters/stage times cover all **210 frames**; total/fps and sample statistics
cover the 180-frame window. Do not sum stage counters to reconstruct total.
Input preparation includes decode and, at one flight, waiting for native import
completion. Observed input completion is reported separately and nearly coincides
here; neither value is a GPU timestamp. For RGBA it ends at CPU pixel availability,
with Vulkan upload counted in composition. Output delivery starts before source
decode and ends at ready encoder input, excluding that frame's encoder call.
Submit counters time host queue calls; wait counters measure observed completion.
Table values are medians of three per-trial statistics, not pooled percentiles.

The [complete CSV](VULKAN_DECODE_PIPELINE_WINDOWS_2026-10-17.csv) retains **all 18
attempts**, including three unavailable native 4K rows with reasons. Earlier
exploratory measurements with different FIFO widths are not used for conclusions.

## Final measurements

| Size | Input/output | Total ms | Frames/s | Input mean / p95 ms | Output delivery mean / p95 ms |
| --- | --- | ---: | ---: | ---: | ---: |
| 1920x1080 | RGBA/RGBA | 4049.06 | 44.46 | 6.172 / 7.168 | 13.295 / 14.589 |
| 1920x1080 | Native/native | 1626.91 | 110.64 | 1.964 / 2.794 | 6.642 / 7.813 |
| 2560x1440 | RGBA/RGBA | 7255.52 | 24.81 | 10.777 / 12.078 | 24.525 / 28.262 |
| 2560x1440 | Native/native | 2404.73 | 74.85 | 2.966 / 3.848 | 9.617 / 10.587 |
| 3840x2160 | RGBA/RGBA | 22338.50 | 8.06 | 29.256 / 34.513 | 79.298 / 96.065 |
| 3840x2160 | Native/native | Unavailable in all three attempts | — | — | — |

Median steady-state wall time falls by **59.82% at 1080p** and **66.86% at 1440p**
for this synthetic workload/device. No native 4K throughput or speedup is claimed.
These results do not establish user-project, cold-start or interactive performance.

Every successful native trial has 210 hardware-decoded frames, 210 input tickets
submitted/collected, zero decoded downloads, zero output readback and zero
explicit encoder upload. Its 1,935,360 uploaded bytes are the small CPU overlay,
not full decoded video. RGBA downloads 210 frames; its hardware-frame counter also
includes one initial admission probe (211 total), outside the timed interval.

| Size | RGBA decoded download bytes | RGBA video+overlay upload bytes | RGBA output readback bytes | Native NV12 GPU copy bytes | Native input blit bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1080p | 653184000 | 1743759360 | 1741824000 | 658022400 | 1741824000 |
| 1440p | 1161216000 | 3098511360 | 3096576000 | 1161216000 | 3096576000 |
| 4K | 2612736000 | 6969231360 | 6967296000 | Unavailable | Unavailable |

NV12 GPU copy bytes include decoder padding, explaining the 1080p difference
from visible downloaded NV12 bytes. Native output blit **and** D3D11 copy each
move 1741824000 bytes at 1080p or 3096576000 at 1440p. Shader conversion writes
and driver-private traffic are not bus measurements. RGBA's zero explicit encoder
upload counter excludes opaque transfers inside FFmpeg/NVENC and does not prove
absence of an encoder upload.

## Memory and the 4K refusal

The maximum known high-water value visible in completed rows is 246185504 bytes,
below 268435456 (256 MiB). This covers Vulkan, bridges, conversion storage and
the output-pool estimate. Shared BGRA backing is charged once. Peaks persist
across modes within a resolution and can include earlier native attempts; they
are not per-job live VRAM or measured driver residency.

The separate decoder reservation estimates are 87736320 / 154828800 / 348364800
bytes for 1080p/1440p/4K. The largest reported combined high-water estimate is
594550304 bytes. The chosen separate-decoder policy therefore does **not** bound
the entire process to 256 MiB. Driver-private, shader-object and opaque encoder
allocations remain unmeasured.

All three native 4K attempts reach the accounted Vulkan image-pool limit even
with one flight: `The bounded Vulkan image pool is occupied.` This is working-set
pressure with input, effects, composition and both bridges, not evidence that the
GPU cannot decode or encode 4K. The existing output-only 4K qualification still
passes. No limit is raised and no native attempt is silently replayed through
RGBA. Subsequent explicit RGBA trials in that session still succeed.

## Reproduction and artifacts

```powershell
cmake -S . -B build -DBUILD_VULKAN_NVENC_PROTOTYPE=ON
cmake --build build --config Release --target creative-suite-vulkan-nvenc-prototype creative-suite-main-editor --parallel 4
$probe='.\build\prototypes\vulkan-nvenc\Release\creative-suite-vulkan-nvenc-prototype.exe'
$env:CREATIVE_SUITE_VULKAN_VALIDATION='1'
$env:VK_LAYER_VALIDATE_SYNC='1'
& $probe --decode-self-test --require-native --output-dir=build/decoded-check
& $probe --self-test --require-native --output-dir=build/output-check
Remove-Item Env:CREATIVE_SUITE_VULKAN_VALIDATION
Remove-Item Env:VK_LAYER_VALIDATE_SYNC
& $probe --decode-benchmark --require-native --output-dir=build/decoded-benchmark
```

The benchmark reports unavailable rows and continues its matrix; a zero process
exit alone does not approve every scenario. A single required native source run
returns 1 when unavailable. Use `--decode-source=<file> --input=native --mode=native
--codec=h264_nvenc --require-native` for separate 12-frame pixel/output checks;
repeat with HEVC output. Native input admission never silently changes codec/path.

Ignored local evidence: `build/vulkan-decoded-required-*.log`,
`build/vulkan-decoded-ctest.log`, `build/vulkan-decoded-disabled-build.log`,
`build/vulkan-decoded-benchmark-final.log` and generated videos under
`build/vulkan-decoded-benchmark-final/`. After timing, independent ffprobe
N-125365-g9a01c1cb6a-20260630 verified all 15 completed files: 210 decoded frames,
210 packets, expected dimensions and ordered 30 fps PTS within container rounding.
Its JSON record is `build/vulkan-decoded-independent-verification.json`.
Pixel oracle downloads and these independent checks are outside measurements.

Prototype Release:
`C:\Users\Admin\Desktop\ProjetosCode\AdobeShoppee\build\prototypes\vulkan-nvenc\Release\creative-suite-vulkan-nvenc-prototype.exe`,
545280 bytes, modified **2026-10-17 11:00:50 -03:00**;
SHA-256 `E1B13503459DF6A350F867F35A79025F7C07EE22EC113773A3C513F5ED58483F`.
The canonical Video Editor was built directly at
`C:\Users\Admin\Desktop\ProjetosCode\AdobeShoppee\build\apps\video-editor\Release\creative-suite-video-editor.exe`,
3444736 bytes, modified **2026-10-17 10:58:21 -03:00**. No executable was copied
from an auxiliary tree; no version change, installer, commit or publication occurred.

## Recommendation

**Adjust intermediate resource pressure before integrating native input into the
editor.** The bridge is proven for the accepted 1080p/1440p reference cases with
correct pixels, valid NVENC files, safe reuse and zero full input/output CPU
downloads. The 4K working set is not accepted. Investigate eliminating redundant
normalization storage and scheduling/reusing intermediates with explicit barriers,
then repeat parity and the same cap/benchmark; do not broaden this delivery into
editor integration or increase the limit automatically.

Keep current editor RGBA input and qualified native output. Human editing,
actual device loss/watchdog expiry, other adapters/drivers and native Linux/macOS
interoperability remain pending. P010/HDR/orientation conversion and an application
audio/timeline engine are outside this prototype.
