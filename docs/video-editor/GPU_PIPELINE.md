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

- Aggregate actual stage backends, transfer counts/bytes, reserved memory, and
  recovery reasons in application diagnostics.
- Validate integrated faults, cancellation, queue/file preservation, and affected
  shared consumers; measure sustained playback, export, and physical audio sync.
- Add and natively validate AMF, QSV, VideoToolbox, and Linux VAAPI/NVENC separately.
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
These enable available hardware encoders without changing the default selection. Their package includes MIT notices. No CUDA toolkit
or proprietary NVIDIA driver/runtime is bundled. FFmpeg retains its dynamically
linked LGPL configuration without `--enable-gpl` or `--enable-nonfree`. A future
distribution must include matching source/build obligations and dependency notices.
This work creates no installer or release package.
