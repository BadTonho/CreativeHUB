# Experimental visual GPU pipeline

Status: **implementation in progress; Windows/NVIDIA first; other native platforms pending**.
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

## Remaining integration and acceptance

Actual decode/composition/encoding paths, stage wall times, transfers, reserved
memory, and recovery reasons are present in application diagnostics.
- Validate integrated faults, cancellation, queue/file preservation, and affected
  shared consumers; measure sustained playback, export, and physical audio sync.
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
sustained playback and physical audio synchronization remain separate gates.
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

## Short pipeline profiling before the expanded codec build

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
