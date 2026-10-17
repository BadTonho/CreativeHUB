# Reduced-intermediate decoded Vulkan pipeline: Windows, 2026-10-17

Status: **standalone 4K native proof on the reference device**. Editor input
integration remains deferred. This follows the [original experiment](VULKAN_DECODE_PIPELINE_WINDOWS_2026-10-17.md),
whose CSV and capacity-refusal evidence remain unchanged. See the
[contract](../gpu/VULKAN_DECODE_PROTOTYPE.md) and
[CLI guide](../../../prototypes/vulkan-nvenc/README.md).

## Environment and implementation

Windows 11 Pro 10.0.26300, Ryzen 5 3600, GTX 1660 SUPER, driver 32.0.16.1742;
MSVC 14.44.35207, Qt 6.7.2, FFmpeg 7.1.2, Vulkan SDK 1.4.357.0, Release.
The adapter LUID bytes remain `f7b0000000000000` for decoder, Vulkan and encoder.
H.264/HEVC inputs are 8-bit identity-oriented SDR NV12. P010/HDR/orientation
conversion, audio and application timeline scheduling remain outside this probe.

The import kernel fetches shared BGRA texels directly into RGBA8_UINT with the
existing byte rounding. The previous normalized-image/blit path remains available
when sampled import is unsupported or `CREATIVE_SUITE_VULKAN_IMPORT_BLIT=1`.
An independent export kernel writes only RGBA8_UNORM, eliminating the discarded
integer scratch. Kernels compile to SPIR-V at build time; private two-binding
pipelines/views/sampler initialize lazily and retire before device destruction.
There is no new dependency, extension requirement or preview conversion change.

Ordered prototype effects now run before canvas composition, ending the original
image wrapper's lifetime after its last use. Each effect retains its original
byte quantization. The compositor defers canvas allocation until first-layer
effects finish and still clears an empty composition. Existing barriers and
recording-context allocation ownership authorize reuse; published leases or
other consumers prevent it. Shared textures, decoder frames, fences and keyed
mutexes retain their prior completion/retirement contracts. No heap aliasing or
global playback waits were introduced.

## Allocation baseline and result

The [stage CSV](VULKAN_DECODE_4K_MEMORY_WINDOWS_2026-10-17.csv) records first-frame
snapshots from the instrumented original implementation **before kernel changes**
and the optimized path. Both use the same 3840x2160 H.264 source, effects, native
output and one flight. Measurements came from `vulkan-4k-baseline-4k` and
`vulkan-4k-effects` logs under ignored `build/`. The latter pixel-checked run
completed with synchronization validation. Timing from these traced/oracle runs
is deliberately excluded from performance conclusions.

| Snapshot | Original known bytes | Optimized known bytes |
| --- | ---: | ---: |
| Submitted input | 179312672 | 145889312 |
| First color pass | 246159904 | 179313184 |
| Second color pass | 246159904 | 212736544 |
| Final layer composition | 246185504 | 212762144 |
| Output delivery submitted | Unavailable | 246185504 |

A full-size reference-device image allocation is 33423360 bytes (visible payload
33177600). Removing a conversion intermediate alone did not suffice: retaining
the original input through canvas composition still required another incompatible
format allocation at output. Ending its wrapper's use earlier makes the integer
working set reusable, leaving room for normalized output. The old refusal occurred
at its next allocation, before reaching a recorded delivery snapshot. Its partial
peak and the successful new peak can therefore be equal; this is improved usable
working set, not a claim that cache high-water must fall in every run.

For the four native 4K correctness cases, known peaks are **246185504 bytes** for
H.264 input and **246277664 bytes** for HEVC input, both below **268435456**.
The different decoder storage alignment accounts for the additional NV12 estimate.
Decoder-pool estimates remain separate: 348364800 bytes for the H.264 4K source and
350945280 for HEVC, giving a maximum combined high-water estimate of 597222944.
The 256 MiB cap is **not** a total-process/decoder VRAM bound. Driver-private,
shader/descriptor objects and opaque encoder allocations remain unmeasured.

`VulkanDevice::Diagnostics` separates active backing, backing retained by pending
contexts, idle cached images/transfers and external known reservations. Pending
classification takes priority over external CPU leases and is deduplicated across
contexts; the four classes sum exactly to known bytes. A completed but unreaped
submission can remain pending in a snapshot. Sampling does not wait or reap.
`CREATIVE_SUITE_VULKAN_MEMORY_TRACE=1` logs these stages; disable it for benchmarks.

## Correctness and regression execution

The required native 4K matrix passed H.264/HEVC D3D11VA input with H.264/HEVC NVENC
output: four 12-frame files, CPU/pre-encoder RGB tolerance 2 and exact alpha,
lossy output mean RGB tolerance 12, dimensions, count, order and 30 fps PTS.
Every case uses one flight and direct import, with zero decoded downloads,
zero output readback and zero explicit encoder upload. Ordered brightness +7,
saturation 80 and the rotated transparent overlay remain in the workload.
Both source codecs also pass cancellation and injected encoder-failure destination
preservation. These pixel-oracle downloads are verification-only, outside timing.

The small decoded self-test preserves all twelve input-codec/matrix/range cases,
padded storage, two tickets, FIFO/stale tickets, forced legacy import, retained
images across reset/slot reuse, saturation and one-flight reduction, all input
fault stages and safe retirement. The output self-test now explicitly checks
that conversion leaves the source image unchanged. Shared Vulkan tests cover
allocation-class reconciliation, empty output and first-layer effects.

The initial scoped run exposed an obsolete memory-pressure fixture: after scratch
removal, its 180 MiB reservation correctly admitted two flights. The fixture now
reserves 196 MiB and still requires eight native frames, exactly one pending flight
and no fallback; the separate 250 MiB case still requires nontechnical RGBA recovery.
A two-flight output test also explicitly retires unrelated fixture uploads before
asserting two available contexts, removing timing dependence from its setup.
Execution totals and final measured statistics follow below. Logs live under
`build/vulkan-4k-*`. Unsupported/skipped tests never
establish native acceptance. Actual driver loss/watchdog expiry, other adapters,
Linux/macOS native interoperability and paced human editing remain pending.

## Performance protocol

Repeat the original benchmark with 30 drained warm-up frames, 180 measured frames
and three interleaved RGBA/native trials at 1080p, 1440p and 4K. Both modes use one
application frame in flight and the same existing output-pool allowance (two at
1080p/1440p, one at 4K), hardware decoder, source, effects and NVENC settings.
Both receive the same early effect scheduling. H.264/NVENC source: 210 temporal
color-marker frames, 4 Mbps, BT.709 limited range, 30 fps. H.264/NVENC output:
20 Mbps, 30 fps, Matroska, matched delay=0/lookahead=0 diagnostic. Normal editor
RGBA encoder defaults are unchanged.

No validation, memory tracing, pixel oracles, tests or builds run during timing.
Wall time includes the measured window through encoder finalization and atomic
publication; setup and warm-up are excluded. Stage samples cover 180 frames and
counters all 210. Input preparation includes decode/import wait at one flight;
observed input completion is separately reported. Output delivery starts before
decode and ends at ready encoder input. Neither host submission nor these worker
observations is a GPU timestamp. Table entries are medians of per-trial statistics,
not pooled percentiles. Comparisons to the historical report are descriptive:
the new binary also adds allocation accounting; no native 4K baseline timing existed.

## Reproduction

```powershell
cmake -S . -B build -DBUILD_VULKAN_NVENC_PROTOTYPE=ON
cmake --build build --config Release --target creative-suite-vulkan-nvenc-prototype creative-suite-main-editor --parallel 4
$probe='.\build\prototypes\vulkan-nvenc\Release\creative-suite-vulkan-nvenc-prototype.exe'
$env:CREATIVE_SUITE_VULKAN_VALIDATION='1'
$env:VK_LAYER_VALIDATE_SYNC='1'
& $probe --decode-self-test --require-native --output-dir=build/decoded-check
& $probe --decode-4k-self-test --require-native --output-dir=build/decoded-4k-check
& $probe --self-test --require-native --output-dir=build/output-check
Remove-Item Env:CREATIVE_SUITE_VULKAN_VALIDATION
Remove-Item Env:VK_LAYER_VALIDATE_SYNC
& $probe --decode-benchmark --require-native --output-dir=build/vulkan-4k-benchmark
```

The ordinary benchmark still prints explicit unavailable rows rather than silently
recovering. The 4K test has 300 s in both CTest and its isolated child wrapper;
`creative-suite-isolated-timeout` validates default/custom forwarding and rejects
invalid timeout values. The shared library also requires the Vulkan-disabled
configuration check. No application input selection or preferences change.

## Artifacts and recommendation

The prototype and canonical editor are built directly in their configured Release
output locations. Final paths, timestamps and executable hash are recorded with
results below. No versions, project formats, installer, commit or publication.

The reduced native path establishes feasibility for this synthetic 4K workload on
the reference device. A future, separately scoped delivery can investigate editor
input integration with explicit RGBA recovery and session-budget contention.
Do not treat this prototype as acceptance of arbitrary 4K projects, more concurrent
frames, HDR/orientation support, other devices or platforms. Keep Automatic on
OpenGL and the current editor input path unchanged.

## Final execution and measured results

**33/33 scoped CTest entries passed without skips**, 283.27 s, with Vulkan
synchronization validation enabled. The dedicated 4K matrix took 127.95 s;
the 300 s inner/outer timeout is therefore material. Nine additional required
native executions passed: legacy output probe, OpenGL video conversion, shared
Vulkan and OpenGL, Vulkan timeline, two Viewers during native export, the real
editor's complete 60-tuple NVENC qualification matrix, Motion export and hardware
media. No VUID, SYNC-HAZARD or validation error was reported in their captured logs.
The Vulkan/media-disabled shared-library Release build passed; that is a Windows
compile check, not Linux/macOS native acceptance.

The [complete measured CSV](VULKAN_DECODE_4K_WINDOWS_2026-10-17.csv) retains **54/54
completed attempts**: the original 18-trial protocol, one full repeat and a final
18-trial run after the accounting-failure refinement. Each retains 30 warm-up,
180 measured frames and three repetitions; the CSV identifies its executable hash. There
are no unavailable 4K rows or discarded samples. The first 1080p RGBA/native pair
was unusually slow (36181.7 / 25115.0 ms), affecting input and delivery waits in
both paths. Subsequent pairs stabilized. An inspection found only this prototype
among the relevant test/build/editor processes, no validation overrides, and the
GPU at P0/1935 MHz/64 C at the observed point. This does not identify conditions
at the start of the outliers or establish their cause. The full repeat did not
reproduce that extreme slowdown. It is retained as an unexplained transient,
not silently removed or attributed to the implementation or an assumed driver cause.

| Size | Path | Initial median total ms / fps | Repeat median total ms / fps |
| --- | --- | ---: | ---: |
| 1920x1080 | rgba/rgba | 4538.81 / 39.66 | 4057.91 / 44.36 |
| 1920x1080 | native/native | 1644.94 / 109.43 | 1643.67 / 109.51 |
| 2560x1440 | rgba/rgba | 7356.31 / 24.47 | 7219.49 / 24.93 |
| 2560x1440 | native/native | 2403.43 / 74.89 | 2395.95 / 75.13 |
| 3840x2160 | rgba/rgba | 20070.30 / 8.97 | 22132.50 / 8.13 |
| 3840x2160 | native/native | 7027.12 / 25.61 | 7347.41 / 24.50 |

The repeated protocol's per-trial sample summaries are below; the CSV also retains
all initial-run means/p95s. These are medians of three statistics, not pooled p95.

| Size | Path | Input mean / p95 ms | Output delivery mean / p95 ms |
| --- | --- | ---: | ---: |
| 1920 | rgba | 6.159 / 6.878 | 13.359 / 14.574 |
| 1920 | native | 1.859 / 2.174 | 6.706 / 7.733 |
| 2560 | rgba | 10.628 / 12.069 | 24.169 / 27.790 |
| 2560 | native | 2.720 / 3.078 | 9.579 / 10.394 |
| 3840 | rgba | 28.553 / 33.499 | 78.771 / 95.477 |
| 3840 | native | 8.460 / 10.165 | 28.322 / 33.770 |

Compared descriptively with the historical medians, repeated native wall time is
1.03% higher at 1080p and 0.37% lower at 1440p. This does **not** establish a
latency improvement or a repeatable large regression at those sizes. The old 4K
native case was unavailable, so it has no speedup ratio against the old native
implementation. The successful native 4K medians are 25.62 / 24.50 fps across the
two protocols, versus 8.97 / 8.13 fps for RGBA. Native repeated 4K wall time is
66.80% lower than matched RGBA for this workload/device, but does not establish
real-time 4K/30 playback, arbitrary-project or OpenGL superiority.

Every native row has 210 hardware-decoded frames, 210 input submissions/collections,
210 direct conversions, zero decoded downloads, zero output readback and zero
explicit encoder upload. The 1935360 uploaded bytes are the 64x36 CPU overlay
across all 210 frames, not full decoded video. RGBA rows each download 210 frames;
their hardware counter also includes the admission probe (211). In all three protocols,
logical native input blit bytes fall to zero while GPU conversion writes remain.

| Size | Native input NV12 GPU copy bytes | Native output blit bytes | Native output D3D copy bytes | RGBA decoded download bytes | RGBA video+overlay upload bytes | RGBA output readback bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1920 | 658022400 | 1741824000 | 1741824000 | 653184000 | 1743759360 | 1741824000 |
| 2560 | 1161216000 | 3096576000 | 3096576000 | 1161216000 | 3098511360 | 3096576000 |
| 3840 | 2612736000 | 6967296000 | 6967296000 | 2612736000 | 6969231360 | 6967296000 |

Input copies include storage padding. GPU-copy counters are logical operation
payloads, not measured bus traffic. Source generation and pixel verification are
outside pipeline measurements. Zero explicit RGBA encoder upload does not count
opaque FFmpeg/NVENC internal transfers. Per-row host submission and wait counters
are retained in the CSV; they are not physical GPU execution timestamps.
The maximum known benchmark high-water value is 246185504 bytes; the H.264 decoder
estimate is separate and the largest combined estimate is 594550304 bytes. Cache
history persists across modes/trials; high-water is not current driver residency.

After timing, independent ffprobe decoding inspected all 54 benchmark files
(210 frames/packets each) and both retained sets of four 4K codec-matrix outputs
(12 each, including the delivered binary): **62 files**. Dimensions,
codec, 30 fps count and ordered packet PTS were verified within 0.0011 s container
rounding. Verification runs outside timing; its tool/version and per-file evidence
are in `build/vulkan-4k-independent-verification.json`.

Final logs: `build/vulkan-4k-ctest-final.log`, `vulkan-4k-required-*.log`,
`vulkan-4k-disabled-build.log`, both `vulkan-4k-benchmark*.log`, and the allocation
baseline/optimized logs identified above. The first scoped/test-setup failures
remain recorded separately; final reruns passed. Generated videos remain ignored
under `build/`; no media or build outputs are added to the repository.

Final prototype Release:
`C:\Users\Admin\Desktop\ProjetosCode\AdobeShoppee\build\prototypes\vulkan-nvenc\Release\creative-suite-vulkan-nvenc-prototype.exe`,
571904 bytes, modified **2026-10-17 11:59:40 -03:00**;
SHA-256 `75DA15A3FFC645A620F9A305F3059F74C1384BA15FA7C4A72E79D12106521E8A`.
Canonical Video Editor, built directly at
`C:\Users\Admin\Desktop\ProjetosCode\AdobeShoppee\build\apps\video-editor\Release\creative-suite-video-editor.exe`,
3456000 bytes, modified **2026-10-17 11:59:44 -03:00**;
SHA-256 `7FA101EFDB862E16CAA241885A4A2773384352CA94D274E2525E580D8F00EE14`.
No executable was copied from an auxiliary tree.

## Final accounting-failure refinement

Final review found that allocating a diagnostic map entry before charging its
backing could leak/underflow accounting if host allocation failed. Charging now
precedes registration within the existing cleanup guards, so all four resource
owners release exactly what was charged. Nonthrowing snapshots allocate no host
memory. The private metadata-failure regression covers images, transfer buffers,
input imports and output imports, asserting exact reservation restoration and a
still-valid device. Normal GPU operations, shaders and pixel tolerances are unchanged.

After this refinement, the delivered executable passed the required decoded
self-test with synchronization validation, then **3/3 focused CTest entries**
(shared Vulkan, integrated native export and the complete native 4K matrix),
118.12 s, no skips. The 4K entry took 107.61 s. The final Vulkan/media-disabled
library build passed too. This follow-up is in addition to the earlier 33-entry
clean scoped run and nine required native executions; it is not an additional
claim that all 33 entries were repeated after this error-path-only change.
Logs: `build/vulkan-4k-final-metadata.log`, `vulkan-4k-delivery-tests.log`,
`vulkan-4k-delivery-build.log`, `vulkan-4k-disabled-final-build.log` and
`vulkan-4k-benchmark-final.log`.

The final binary repeats the **entire** performance protocol after those tests,
with validation/tracing/fault injection disabled and no concurrent builds/tests.
All 18 scenarios complete, including all native 4K trials. Its results are below;
the previous 36 rows remain in the CSV, with their own binary hash. No rows are
replaced or excluded. The extreme initial 1080p pair does not recur.

| Size | Path | Final total ms | Frames/s | Input mean / p95 ms | Output delivery mean / p95 ms |
| --- | --- | ---: | ---: | ---: | ---: |
| 1920 | rgba | 3994.51 | 45.06 | 6.046 / 6.787 | 13.216 / 14.260 |
| 1920 | native | 1587.54 | 113.38 | 1.781 / 2.180 | 6.463 / 7.433 |
| 2560 | rgba | 7261.70 | 24.79 | 10.702 / 12.069 | 24.449 / 27.767 |
| 2560 | native | 2394.01 | 75.19 | 2.770 / 3.285 | 9.549 / 10.365 |
| 3840 | rgba | 22420.80 | 8.03 | 29.075 / 33.640 | 79.641 / 95.862 |
| 3840 | native | 7286.51 | 24.70 | 8.369 / 10.039 | 28.065 / 33.179 |

The final native 4K median is **24.70 fps**, versus **8.03 fps** for matched RGBA
(67.50% lower measured wall time). Its known peak remains 246185504 bytes, with
zero full input/output CPU downloads in all measured native rows. This establishes
bounded 4K prototype feasibility, not real-time playback or arbitrary-project
acceptance. Across all three protocols, small/medium timing varies; the initial
large slow pair is disclosed and remains unexplained. No OpenGL performance
comparison or universal Vulkan speed advantage is claimed.
