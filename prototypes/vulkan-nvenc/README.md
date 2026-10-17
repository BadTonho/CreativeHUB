# Experimental Vulkan / D3D11 / NVENC probe

Status: **provisional, Windows-only research executable**. The Video Editor now reuses this bridge for
[qualified Windows export jobs](../../docs/video-editor/gpu/VULKAN_NATIVE_EXPORT.md);
this standalone CLI remains independent of editor UI. RGBA remains the fallback;
Automatic, preferences, application versions and project formats are unchanged.
Linux/macOS interop requires separate investigation.

## Build and run

Use the existing Qt/FFmpeg/Vulkan development environment from
[the build guide](../../docs/video-editor/architecture/BUILD_AND_DEPENDENCIES.md).
No additional encoding SDK or package is added. Vulkan shaders are compiled during build.
`BUILD_VULKAN_NVENC_PROTOTYPE` defaults to **OFF**. The existing optional Vulkan
backend and native FFmpeg/D3D11 media integration must be available to run natively.

```powershell
cmake -S . -B build -DBUILD_VULKAN_NVENC_PROTOTYPE=ON
cmake --build build --config Release --target creative-suite-vulkan-nvenc-prototype --parallel 4
$probe = '.\build\prototypes\vulkan-nvenc\Release\creative-suite-vulkan-nvenc-prototype.exe'
& $probe --self-test --require-native
& $probe --mode=native --codec=h264_nvenc --require-native
& $probe --mode=native --codec=hevc_nvenc --require-native
& $probe --mode=rgba --codec=h264_nvenc --require-native
& $probe --mode=compare --benchmark --codec=h264_nvenc --require-native
```

`--output-dir=<directory>` selects the evidence destination (default
`build/vulkan-nvenc-evidence`). Normal runs verify 12 synthetic 320x180 frames.
`--benchmark` uses 1080p, 1440p and 4K, 30 warm-up plus 180 measured frames,
three repetitions per mode. `compare` interleaves RGBA then native in each trial.
Benchmark runs omit verification downloads/decoding from the measured path;
self-tests and normal runs perform explicit verification outside benchmarks.
`--cancel-after=2` cancels before collecting output frame 2 and publishes nothing.
An existing destination is preserved. Self-tests use an isolated temporary folder.
Initialization unavailability returns 77, or 1 with `--require-native`; subsequent
technical/encoder errors return 1. Native mode never changes to RGBA or another codec.

## Decoded input prototype

The new input experiment remains **outside the editor input flow**. The existing
output bridge is already integrated separately. See the
[input contract](../../docs/video-editor/gpu/VULKAN_DECODE_PROTOTYPE.md).

```powershell
& $probe --decode-self-test --require-native --output-dir=build/decoded-check
& $probe --decode-source=source.mkv --input=native --mode=native --codec=h264_nvenc --require-native
& $probe --decode-source=source.mkv --input=rgba --mode=native --codec=hevc_nvenc --require-native
& $probe --decode-source=source.mkv --input=native --mode=rgba --require-native
& $probe --decode-benchmark --require-native --output-dir=build/decoded-benchmark
```

Normal decoded-source runs process and verify the first 12 frames. Sources must
provide actual identity-oriented H.264/HEVC 8-bit NV12 hardware decoding; native
input cannot silently use the media library's software/orientation recovery.
P010 and HDR are excluded. Independent input/output selections isolate each
boundary. Existing commands without decode arguments retain their behavior.

`--decode-self-test` generates its fixtures, compares all 12 codec/matrix/range
combinations, exercises seek/device changes, array slices/aligned storage,
two tickets, pressure/one-slot reduction, cancellation and injected input faults.
It checks all four input/output combinations with both NVENC output codecs,
HEVC input end to end, output pixels, frame order/timestamps and destination
preservation. CTest registers `creative-suite-vulkan-decoded-pipeline`; an
unsupported-host skip is not native acceptance.

`--decode-benchmark` interleaves RGBA input/output and native input/output at
1080p/1440p/4K, three trials, 30 drained warm-up frames plus 180 measured frames.
Both use one application frame in flight and matched NVENC latency diagnostics.
It prints unavailable cases and reasons rather than falling back or exceeding
256 MiB. Transfer/stage counters include all 210 frames; wall time includes
encoder finalization and atomic publication. The decoder pool is separately
reported, including a combined high-water estimate; this is not total measured
VRAM. See [Windows results](../../docs/video-editor/performance/VULKAN_DECODE_PIPELINE_WINDOWS_2026-10-17.md).

The private NV12 converter reuses the existing D3D11 HLSL and Windows
`D3DCompile` dependency. Vulkan shaders remain compiled to SPIR-V during build;
no additional compiler package, codec SDK or runtime dependency is introduced.
CTest `creative-suite-vulkan-nvenc-prototype` runs `--self-test`, allows 77 on
unsupported hosts, and is not evidence of native acceptance when skipped.

For synchronization checks (not performance measurements):

```powershell
$env:CREATIVE_SUITE_VULKAN_VALIDATION = '1'
$env:VK_LAYER_VALIDATE_SYNC = '1'
& $probe --self-test --require-native
Remove-Item Env:CREATIVE_SUITE_VULKAN_VALIDATION
Remove-Item Env:VK_LAYER_VALIDATE_SYNC
```

## Provisional output interop contract

The encoder's **unchanged** two-frame BGRA native pool is created first. Its DXGI
adapter LUID selects the Vulkan device; the experimental creation overload enables
`VK_KHR_external_memory_win32` and `VK_KHR_win32_keyed_mutex` only on request.
Ordinary `VulkanDevice::create(cause)` keeps its existing policy. The bridge rejects
different adapters, invalid geometry, unsupported shared imports and missing
normalized-format blit features before sharing memory. The opt-in public header
contains opaque tickets and existing composition statuses, with Windows handles
confined to the private implementation. It is not a stable cross-platform API.

Each of at most two slots owns a D3D11 BGRA NT-shared/keyed-mutex intermediate,
a dedicated Vulkan import of the same allocation and a D3D11 event query.
The compositor's existing integer-to-normalized operation produces RGBA8 UNORM;
a nearest Vulkan blit converts channels into the imported BGRA image. The bridge
then makes a D3D11 GPU copy into the ordinary encoder pool surface. Thus this
eliminates full output readback/CPU encoder input, **not GPU copies**. Source
fixtures in the original synthetic modes still upload to the GPU; decoded modes
use the separate input bridge above. `--small-high-bitrate` selects
10 Mbps at 320x180 with the high-frequency fixture, without verification downloads,
for reproducing the earlier parameter-sensitive NVENC failure. It does not expand
the editor qualification policy.

One worker records composition/bridge commands into the existing single queue.
A submission acquires key 0, transfers EXTERNAL ownership to Vulkan, writes the
image, releases to EXTERNAL in GENERAL layout, then releases key 1. FIFO collection
waits for that submission, acquires key 1 from D3D11, issues CopyResource and waits
for its event before releasing key 0 and handing the destination to NVENC. A
submission receipt cannot be mistaken for completed GPU work. See the upstream
[keyed-mutex contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkWin32KeyedMutexAcquireReleaseInfoKHR.html)
and [image ownership contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageMemoryBarrier.html).

Tickets retain source, destination and completion state until collection/discard;
invalid/stale tickets are rejected and out-of-order collection is Busy. The bridge
slot is reusable after its D3D copy completes; the destination remains governed
by existing FFmpeg/pool references until encoder consumers release it. A cancelled
collection retains resources for safe retirement or a later collection. Discard
and destruction retire pending consumers. No context retains a device-owning
bridge reference: imported allocations retain only a private owner pointer, while
the bridge holds the device until its slots and worker have stopped.

Qt creates/destroys the device on the GUI thread. There is one joined worker for
composition, bridge collection and the existing encoder; no encoding thread or
second export engine is introduced. Worker waits check cancellation at 1 ms
intervals with a 10 s watchdog; shared device locks are released while waiting.
D3D context access uses the native frame device lock. Cancellation stops new
submissions/publication, then retires submitted resources; exceptional retirement
may finish despite cancellation. Failed native work terminates this executable.
The encoder writes a temporary file and publishes atomically only after finish.

The combined known reservation limit is 256 MiB. Vulkan's allocation budget is
reduced by the existing native pool estimate. Shared import backing is counted
once using Vulkan requirements, not again as a second D3D texture. A conservative
four-images-per-flight estimate reduces two slots to one before allocating; live
allocation checks and eviction of idle Vulkan pools still enforce the cap.
D3D output surfaces are estimates (width * height * 4 per surface); this is not
measured driver VRAM and excludes opaque driver/NVENC allocations.

## Evidence and next decision

[Windows results](../../docs/video-editor/performance/VULKAN_NVENC_PROTOTYPE_WINDOWS_2026-10-16.md)
record native correctness, limitations, regression commands and all benchmark
trials. Existing RGB tolerance 2 / exact alpha and lossy encoder mean RGB tolerance
12 are retained. Pixel verification includes distinct channels/transparency,
orientation, transforms, opacity and ordered color effects; decode/packet checks
cover frame count, frame markers, dimensions and 30 fps timestamps.

The executable reports wall time through encoder finalization, delivery mean/p95,
encoder call time, bridge waits/submission calls, flight occupancy, reservations,
source uploads, output readbacks and both logical GPU-copy byte counts. GPU-copy
bytes are payload sizes, not measured memory-bus traffic. Encoder-internal RGBA
uploads are opaque: the existing explicit-upload counter does not measure them.
The recommendation and acceptance boundary remain provisional and device-specific.

The shared bridge now exposes retirement confirmation and can retain an external
reservation lease. An unconfirmed GPU consumer causes conservative process-lifetime
retention rather than unsafe reuse/free; editor integration aborts that job and
logs the error. The [integrated acceptance record](../../docs/video-editor/performance/VULKAN_NATIVE_EXPORT_WINDOWS_2026-10-16.md)
separates qualified editor results from the historical prototype comparison.
