# Experimental visual GPU pipeline

Status: **Windows/NVIDIA implemented and native integration verified;
human acceptance and other native platforms pending**.
This extends the original composition, texture-delivery, and export experiments.
Implementation and hardware acceptance are separate gates. Experimental options
remain disabled by default; project formats, history, audio, and handoffs retain
their current contracts.

## Implemented boundaries

The shared media session adds `DecodeOptions`: software by default, opt-in
D3D11VA on Windows for H.264 baseline/main/high and HEVC main, 8-bit streams.
Unsupported profiles/codecs retain CPU decoding. Actual `AV_PIX_FMT_D3D11`
frames, software frames, downloads, and recovery are counted independently.
A technical failure logs its cause, reopens software decoding, and recovers the
same requested frame; that session stays in software on subsequent resets.

Existing RGBA APIs retain their contract. Hardware RGBA delivery downloads NV12
and losslessly repacks its chroma planes to the YUV420P reference before swscale.
The direct NV12 fast converter failed parity on the reference device. Native
APIs instead retain immutable FFmpeg/D3D11 surfaces with device ownership after
session reset/destruction. Explicit recovery downloads are counted. The cache
retains eight entries within its existing 64 MiB equivalent-frame limit (one
oversized entry may remain). Decoder surfaces also reserve space for these leases.

Settings > General adds **Use hardware video decoding for preview (Experimental)**,
globally stored as `performance/hardware_decoding_enabled`, default false.
Toggling recreates video resources, cancels stale preroll, invalidates composed
frames, and advances the delivery epoch. Render > Video has a separate
**Use hardware video decoding (Experimental)** checkbox, default false, captured
independently per queued job.

The existing GPU-composition option covers Grayscale, Brightness, Contrast, and
Saturation. Both preview and export submit an original immutable source through
`CompositionLayer::effect_stack`. GPU preparation preserves enabled-effect order,
byte rounding per pass, and alpha. CPU composition applies the original stack
exactly once, including after GPU recovery.
If final composition fails after Fusion or native decoding completed, preview
and export rebuild the requested frame from the original sessions and reevaluate
the complete graph on CPU. Recovery does not require a readable old GPU texture;
the RGBA session boundary can reopen software decoding if the decode device was
lost. A technical compositor failure remains latched for the current session/job.
Fusion CPU fallback uses a borrowed per-input recovery callback supplied by the
application. It fetches each original frame index through its session, so a lost
native transfer resource during graph evaluation also recovers software decoding.
Standalone graph consumers can retain explicit native-frame download behavior.

Fusion retains application-owned graph validation, source selection, scheduling,
and keyframe evaluation. Shared `processImage` operations execute Input, Transform,
Color, Effect, Merge, and Output with transparent RGBA8 intermediates. Merge uses
integer straight-alpha source-over arithmetic. Transform preserves dimensions
and alpha rounding. Leases synchronize consumers and release intermediates after
their last required edge. Incompatible, occupied, or failed operations recover
the complete CPU graph; cancellation publishes no partial result. `evaluateFrame`
can retain the completed graph as a lease into timeline composition. The existing
RGBA evaluation API remains available for CPU consumers and explicit recovery.
The graph pool allocates lazily within eight targets/256 MiB; preview delivery
retains its independent three-target/64 MiB budget.

The manifests request FFmpeg 7.1.2 `nvcodec`, `amf`, and `qsv` on Windows x64,
and `nvcodec`, `vaapi`, and `qsv` on Linux x86/x64. The baseline enables
VideoToolbox on macOS. Windows/NVIDIA runtime evidence is recorded separately. H.264/HEVC NVENC are labeled
**Hardware, Experimental** in output capability discovery and selection.
Registry presence is not runtime acceptance. Encoder selection is explicit per
job; failure fails that item and preserves the previous destination through
existing staging/atomic publication. Retry constructs new resources. RGBA input
remains available. The native encoder API accepts a matching BGRA D3D11 pool;
the Windows OpenGL adapter flips/copies the final texture on the GPU and NVENC
retains its FFmpeg frame. The output pool limits retention to three frames and
128 MiB. Native NVENC uses zero output delay and no lookahead to release input
resources predictably; selected encoder failures never change encoder silently.

The Windows bridge requires `WGL_NV_DX_interop2`. Decode NV12 is copied to a
shader-readable surface and converted to BGRA on D3D11, then synchronized for
OpenGL. Conversion follows the baseline swscale 8-bit signed fixed-point terms
and default SDR matrix/range behavior. A least-recently-used cache retains up to four device/dimension conversion bridges,
each bounded to 64 MiB (256 MiB in total). This avoids recreating shaders and
registrations when switching tracks. Both NV12 and BGRA storage are bounded;
an absent extension or unsupported request retains explicit RGBA recovery.

## Remaining acceptance

Actual decode/composition/encoding paths, stage wall times, transfers, reserved
memory, and recovery reasons are present in application diagnostics.
- Complete physical display/audio synchronization and the representative human
  editing/visual checks. Offscreen and encoded-stream checks do not establish
  device timing or packaged-installation acceptance.
- Investigate tail latency under controlled system load before accepting smooth
  30 fps presentation. The prolonged run preserved every GPU frame and its total
  schedule, but recorded individual stalls; its statistics are retained below.
- Natively validate AMF, QSV, VideoToolbox, and Linux VAAPI/NVENC separately.
  Windows evidence cannot qualify unavailable native environments.

## Evidence on 2026-10-10

Reference machine: GTX 1660 SUPER, Windows driver `32.0.16.1742` (617.42),
Qt 6.7.2, FFmpeg 7.1.2, MSVC Release. This is separate from older driver 616.92
measurements. The codec test with `--require-hardware` passed for H.264/HEVC
NVENC-generated 320x180, 30000/1001 media, requiring actual D3D11 decoding.
It covers forward/backward seeks, discarded intermediates, cache, cancellation,
reset, preference changes, and injected lost-resource recovery at the same index.
Geometry/alpha are exact and conversion RGB stays within two levels. Native
surface retention across cache hits, reset, and session destruction also passed.

The explicit native video-pipeline target passed D3D11 decode, GPU conversion,
OpenGL composition/import, GPU copy to BGRA D3D11, and NVENC D3D11 input. It
requires actual hardware and zero decoded-frame downloads. Explicit parity
readbacks verify geometry, orientation, alpha, and RGB within two levels before
encoding; output decoding verifies every submitted frame. The integrated Video Editor test also requires native preview with effects/Fusion
and three-track NVENC export with AAC, zero decode downloads and final readbacks,
cancellation and encoder failure preserving every byte of the previous destination,
and a successful retry with fresh resources. This is short integration evidence;
the prolonged offscreen result is recorded below. Physical audio synchronization
remains a separate gate.
The final H.264 and HEVC integration runs also passed live Full/Half/Quarter
preview parity, invalidation of a completed Fusion lease before CPU recovery,
whole-source/graph reevaluation after export composition failure, and an injected
lost native transfer that reopened the original input session in software at the
same index. Encoded recovery output was compared with all eight CPU reference
frames; technical composition failure was attempted once and remained latched.
Explicit RGBA encoder probes passed twelve ordered frames, 30 fps, duration,
and AAC at 48 kHz for both `h264_nvenc` and `hevc_nvenc`. Motion Studio's
`--require-gpu` consumer check reported one GPU frame and zero CPU recoveries.
AMF/QSV availability probes failed on this NVIDIA-only machine: the AMF runtime
DLL was absent and the MFX implementation was unsupported. These are unavailable
hardware results, not passing acceptance for AMD or Intel.

The final canonical Release build and all 101 registered CTest entries passed
on this machine (118.19 seconds). This includes shared media/effects/composition,
Video Editor application and export/queue checks, Image Editor producer and
consumer checks, Motion Studio, and Hub regressions. The known MainWindow
fixture failure was resolved without changing application animation behavior:
the older v8 layout is serialized from fresh named placeholder docks and loaded
by the real editor. H.264 and HEVC native pipeline checks passed again after
the final relink. These local results do not substitute for the CI platform matrix.

The shared effects, native OpenGL compositor, native Video Editor GPU timeline,
Settings, and CPU node-graph CTest entries passed (5/5). Native Fusion comparison
requires GPU execution and covers all current node operations, different source
dimensions, transparent/rotated intermediates, and cancellation. This is focused
evidence, not integrated pipeline or release acceptance.

The initial six-frame FFV1 baseline measured CPU/GPU export totals of
1648.595/835.865 ms at 1080p, 2134.649/1225.323 ms at 1440p, and
4299.729/2516.996 ms at 4K. GPU-run encoding consumed 505.735, 872.983, and
1893.864 ms; readback consumed 52.521, 41.590, and 125.719 ms respectively.
These synthetic samples guide transfer/encoder work, not sustained 30 fps claims.

Acceptance runs `creative-suite-hardware-video-pipeline-tests --require-hardware`
and native OpenGL/Video Editor GPU tests. Optional developer codec tests can skip
an absent NVIDIA runtime; a skip never qualifies hardware. Validate the 15-minute,
three-track, 30 fps project, Full/Half/Quarter quality, chained effects, branched
graphs/keyframes, mixed rates/transitions, rapid seeks, PNG refresh, live toggles,
audio sync, device/resource loss, encoder failure/retry, cancellation, and existing
destination preservation. Record stage times, CPU/RAM, reserved and measured VRAM.

## Dependencies

Both root/prototype manifests use the same pinned backend features. NVIDIA
`ffnvcodec` headers 12.2.72.0 use MIT; AMD AMF headers 1.4.36 use MIT; Intel
`mfx-dispatch` 1.35.1 uses BSD-3-Clause; Linux libva 2.20.0 uses MIT.
These enable available hardware encoders without changing the default selection. Matching package notices are retained. No CUDA toolkit
or proprietary NVIDIA driver/runtime is bundled. FFmpeg retains its dynamically
linked LGPL configuration without `--enable-gpl` or `--enable-nonfree`. A future
distribution must include matching source/build obligations and dependency notices.
This work creates no installer or release package.

## Additional encoding backends

AMF, QSV, VideoToolbox, and Linux NVENC use the existing RGBA encoder boundary
and their discovered software input formats. VAAPI creates an explicit FFmpeg
VAAPI device and eight-surface NV12 pool (128 MiB maximum), converts RGBA in
software, and uploads through `av_hwframe_transfer_data`. The optional shared
`hardware_device_name` selects a VAAPI render device; empty uses FFmpeg discovery.
These paths do not claim the Windows/NVIDIA transfer elimination. Device creation,
codec initialization, upload, or encoding failure terminates the selected item;
there is no implicit encoder replacement. Resources are recreated for retry.
VAAPI uploads are included in `encoding_uploaded_bytes`. Native decoding on
other operating systems remains outside the implemented D3D11 adapter.

The native encoder probe is an explicit acceptance command (missing hardware is
failure, never a pass or skip):

```powershell
build/libs/media/tests/Release/creative-suite-video-encoder-tests.exe --hardware-encoder h264_nvenc
build/libs/media/tests/Release/creative-suite-video-encoder-tests.exe --hardware-encoder hevc_nvenc
build/libs/media/tests/Release/creative-suite-video-encoder-tests.exe --hardware-encoder h264_amf
build/libs/media/tests/Release/creative-suite-video-encoder-tests.exe --hardware-encoder h264_qsv
```

Use `h264_videotoolbox` on macOS or `h264_vaapi /dev/dri/renderD128` on Linux,
with that platform's test executable path. It checks ordered encoded frames,
geometry, frame rate, duration, and AAC. Record native transfer/performance and
long-duration evidence separately before qualifying another combination.

The prolonged offscreen Windows test generates a 15-minute 1080p H.264 source,
then consumes all 27,000 timeline frames at a paced 30 fps through three video
tracks with effects/Fusion and a shared OpenGL consumer. It requires native GPU
leases, no fallback, and zero readback for every frame:

```powershell
build/apps/video-editor/tests/rendering/Release/creative-suite-main-editor-gpu-video-pipeline-tests.exe --stress 900
```

Shorter `--stress 30` runs verify the fixture; they do not qualify 15-minute
stability. Offscreen consumption does not establish physical display/audio sync.

## Prolonged native preview result

On 2026-10-10, `--stress 900` completed successfully with 27,000 frames in
900.003 seconds: 27,000 GPU compositions, 81,000 native video imports, zero
CPU fallback, zero final readback, and no playback errors or stale timeline
positions. The offscreen consumer acquired, drew, and released each shared
texture. Source generation preceded this interval and is excluded from it.

Frame work latency was 22.085 ms at p95 and 61.732 ms at p99, with a maximum
of 1862.350 ms. 719 frames (2.66%) exceeded the 33.333 ms frame budget.
The fixture renders every frame and catches up to its paced schedule; it does
not use the application's ordinary latest-frame skipping policy. Therefore
completion proves prolonged native execution and bounded scheduling over the
whole interval, not stall-free real-time presentation. The spikes require
controlled-load profiling and physical playback checks before that acceptance.
The preliminary 30-second run recorded p95/p99 11.715/15.553 ms, two late
frames, and a 190.229 ms maximum, illustrating why short results alone are
insufficient.

The resource sampler collected 36 samples at approximately 30-second intervals,
including fixture generation. The 29 samples identified as preview reported:

| Measurement | Observed preview range / result |
| --- | --- |
| Process CPU, normalized over 12 logical processors | 2.03–4.29%, mean 2.71% |
| Process working set | 20.36–248.88 MiB, peak 248.88 MiB |
| Process private committed memory | 452.29–476.79 MiB, mean 465.50 MiB |
| Whole-device used GPU memory | 2794–3500 MiB of 6144 MiB |
| Whole-device GPU / decode utilization | 11–41% / 17–52% |

GPU sampling includes the desktop and other processes; the pre-run device
baseline was 2594 MiB. These are not per-process GPU-memory or execution-time
measurements, and the working-set changes do not establish their cause.
Known per-stage reservations remain available through the application counters.
Private committed memory did not grow proportionally to timeline duration.

Local ignored evidence is retained in `build/gpu-stress-900.log`,
`build/gpu-stress-900-errors.log`, and `build/gpu-stress-900-resources.csv`;
the process exited with code 0 and the error stream was empty. Related final
logs are `build/gpu-final-accepted-ctest.log`, `build/gpu-final-benchmark.log`,
and `build/gpu-final-relinked-native-{h264,hevc}.log`. Fixtures are defined in
`apps/video-editor/tests/rendering/gpu_video_pipeline_test.cpp`, independent
of those local artifacts.

The directly runnable canonical output is
`build/apps/video-editor/Release/creative-suite-video-editor.exe`, modified
2026-10-10 10:05:49 (America/Sao_Paulo), 2,722,304 bytes. The deployed
`avcodec-61.dll` in Video Editor and Motion Studio matches the expanded installed
FFmpeg package byte-for-byte (SHA-256
`95D07C74E5128055736CE4953C40AC964751A1D1004459EB43F4E07A6E63F61F`).
No executable was copied from an auxiliary build tree.

## Final short pipeline profiling

The final expanded FFmpeg Release build ran the paired synthetic benchmark on
2026-10-10. Each export contains 30 full-resolution frames and three tracks,
Color Fusion plus Grayscale/Brightness, and H.264 NVENC at 20 Mbps on both paths.
The CPU row uses software decode/effects/composition and RGBA encoder input.
The native row uses D3D11 decode, shared GPU operations, and D3D11 encoder input.
Source generation and explicit full-resolution conversion parity checks are
outside the measured exports; parity passed at all three resolutions.

| Resolution | CPU total | Native total | Native source preparation | Native composition | Native encoding |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1920x1080 | 31604.5 ms | 1233.44 ms | 843.10 ms | 65.604 ms | 158.561 ms |
| 2560x1440 | 24437.5 ms | 1760.57 ms | 1145.12 ms | 147.639 ms | 228.267 ms |
| 3840x2160 | 57058.7 ms | 2447.15 ms | 1780.40 ms | 69.131 ms | 339.409 ms |

| Resolution | Native decode packet/send | Native decode receive | Native conversion | Native graph evaluation | Geometry uploads |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1920x1080 | 131.407 ms | 0.214 ms | 69.763 ms | 302.981 ms | 1082880 bytes |
| 2560x1440 | 146.918 ms | 0.479 ms | 81.736 ms | 517.017 ms | 1442880 bytes |
| 3840x2160 | 168.473 ms | 0.229 ms | 70.435 ms | 675.171 ms | 2162880 bytes |

Every native export required 90 hardware-decoded frames, zero software-decoded
frames, 30 native-encoded frames, zero decoded downloads, and zero final
readback. Decode conversion to CPU RGBA was zero. Packet/send timings include
decoder work; receive timings alone do not measure hardware decoding cost.
Graph/conversion timings nest inside preparation, so do not sum these columns
as disjoint stage costs. Initialization, synchronization, encoder flushing, and
publication are included in their enclosing totals. The small synthetic fixture,
variable CPU totals, and single reference device do not establish a general
speedup or real-time 4K playback guarantee.

## Earlier short profiling before the expanded codec build

One paired synthetic run used 30 full-resolution frames, three tracks, Color
Fusion plus Grayscale/Brightness, and the same H.264 NVENC output encoder at
20 Mbps for both paths. Source generation, explicit pixel parity checks, and
physical audio/display timing are outside these export times.

| Resolution | CPU decode/effects/composition + NVENC RGBA | Native D3D11/OpenGL/NVENC | Native conversion | Native encoding |
| --- | ---: | ---: | ---: | ---: |
| 1920x1080 | 15339.4 ms | 1571.71 ms | 95.965 ms | 162.425 ms |
| 2560x1440 | 23814.0 ms | 1670.34 ms | 76.391 ms | 230.559 ms |
| 3840x2160 | 56041.0 ms | 2804.95 ms | 86.312 ms | 358.496 ms |

Every native run counted 90 hardware-decoded and 30 native-encoded frames,
zero software-decoded frames, zero decoded downloads, and zero final readback.
Geometry uploads remain (1082880/1442880/2162880 bytes). This is not a general
30 fps guarantee or a quality comparison between different encoders. Before
reusing bridges across tracks, native conversion measured 809.131/802.525/
817.294 ms for the same workloads. CPU totals varied between runs; infer the
specific conversion improvement, not a stable whole-system speedup.
