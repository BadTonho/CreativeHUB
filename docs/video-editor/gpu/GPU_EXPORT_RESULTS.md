# Experimental GPU Export: Delivery Evidence

Date: **2026-10-02**. Stage 3 is implemented, experimental and disabled by default.
Video Editor is the first export consumer of the shared adapter. The
[export contract](GPU_EXPORT.md) documents selection, resources, fallback and metrics.
Versions, formats, shortcuts and preview schemas 9/3 are unchanged.

The later 2026-10-10 Windows/NVIDIA decode/effects/Fusion/NVENC extension,
101-test regression gate, full-resolution export measurements, and prolonged
preview evidence are recorded in [GPU_PIPELINE.md](GPU_PIPELINE.md). The dated
measurements below retain their original driver and RGBA-input/readback scope.

## Platform and native evidence

Windows 11 Pro **10.0.26300**, NVIDIA GeForce GTX 1660 SUPER, NVIDIA driver
**616.92** (`32.0.16.1692`), Qt **6.7.2**, MSVC **2022**, x64. CPU/RAM reference:
Ryzen 5 3600, 32 GiB. The OS/GPU/driver were queried for this delivery. Native
tests use the Windows platform plugin and OpenGL 3.2 Core. This driver supports
the existing precise rotated path (`ARB_gpu_shader_fp64` and `ARB_gpu_shader5`).
Synthetic fixtures contain no private media. Exports use temporary directories
and preserve source files.

`creative-suite-composition-opengl` compares CPU, GPU RGBA and direct texture
readback at 1920×1080, 2560×1440, 3840×2160 and 2160×3840. Geometry and alpha must
match exactly; RGB rounding tolerance is two levels. Patterned transparency,
layer ordering, aspect fit, position/scale, padding, clipping and the independent
4096-axis limit are covered. Both lookup buffers contribute 32,768 known bytes.
Existing rotation, fences, leases, pool budget, retirement and cancellation
regressions remain active.

`creative-suite-main-editor-gpu-export` requires real GPU composition on its
available context; silently using CPU does not pass. It compares prepared frames
before encoding, covers output sizes through landscape/portrait 4K and runs text,
keyframe, transition, mixed-rate/trim, audio, gap and publication fixtures. A masked
PNG produced by Image Editor is resolved through the linked-variant reference and
is republished; the next export must use changed pixels. Queue checks include
CPU/GPU items, continuation after failure and active-worker shutdown. Unavailable
contexts may skip; a skip does not approve a driver. No native context skips
occurred in this delivery.

The decoded CPU/GPU output comparison uses **FFV1 in Matroska**, requiring the
same frame count, dimensions and exact alpha. The existing encoder selects YUV
formats and performs RGB/YUV conversion; decoded RGB tolerance is six levels in
this separate test. This is distinct from the stricter two-level pre-encoding
compositor comparison. Lossless coding does not make color conversion bit-exact.
Lossy encoders and packaged hardware encoder combinations require separate checks.

## Builds and automated gates

Full Debug and Release builds succeeded. Focused/shared checks passed in both
configurations; the complete Release suite also includes the Image Editor and
Motion Studio producer/consumer and existing application regressions.

| Gate | Result |
| --- | --- |
| Debug focused/shared rendering, playback, audio, settings, UI, export and queue | 25/25 passed |
| Release focused/shared equivalent set | 25/25 passed |
| Release complete suite | 67/67 passed |
| Native shared compositor, timeline composition, direct viewer and export | Passed in Debug and Release; no context skips |
| Export benchmark processes | Three sequential Release processes completed; all GPU benchmark frames used GPU |

The focused selection includes `animation`, `composition`, `preview`, `playback`,
`frame-step`, `settings`, `main-window`, `audio-mix`, `text-compositor`, `transform`,
`gpu-timeline`, `gpu-export`, `render-export` and `render-queue`. Existing CPU
export tests retain their offscreen target; the native alias adds `--native-gpu`.
Deterministic adapter fixtures exercise Unsupported, Failed, mid-item failure,
unexpected Busy, malformed output, missing surface, factory/callback exceptions,
fresh retry, cancellation and summaries for completed/failed/canceled attempts.
They validate control flow and do not constitute GPU driver evidence.

Local reproducibility logs are under the ignored `build/` directory:
`gpu-export-*-focused*.log`, `gpu-export-release-full*.log`,
`gpu-export-*-build.log` and `gpu-export-benchmark-{1,2,3}.log`. The checked-in
test source defines the fixtures; binaries/logs are not repository artifacts.

## Measurement method

Release `creative-suite-main-editor-render-export-tests --benchmark` runs three
resolutions, CPU then GPU, in each process. Three processes were run sequentially
after builds/tests had finished, with no concurrent benchmark process. Both paths
run on a worker and create a fresh per-item backend. GPU use is asserted.

Each item contains **six frames at 30 fps**, a 1920×1080 patterned opaque PNG and
two 1280×720 patterned transparent overlays on three tracks. Overlay scale is
0.72 and opacity 0.6; one rotates 23 degrees. Audio is disabled for these timings
and covered separately by regression. The encoder is FFV1/Matroska. This measures
composition plus readback, current encoder/color conversion, verification,
publication and backend teardown. Initial context setup is included. Fixture
creation, test parity comparisons and worker-thread launch are outside render
timings. Pre-encoding comparisons are disabled only for the measured items.

These are short synthetic cold-item measurements, not sustained throughput,
15-minute project performance or a universal gain. Encoding dominates part of
the totals, and CPU/GPU runs show variability. Timings are CPU wall/submission
measurements, not independent GPU execution queries.

### Total export wall time (ms, six frames)

| Output | Backend | Run 1 | Run 2 | Run 3 | Mean |
| --- | --- | ---: | ---: | ---: | ---: |
| 1920×1080 | CPU | 1351.685 | 1334.712 | 1275.262 | 1320.553 |
| 1920×1080 | GPU | 981.187 | 965.271 | 946.364 | 964.274 |
| 2560×1440 | CPU | 2226.927 | 1834.136 | 1832.693 | 1964.585 |
| 2560×1440 | GPU | 1538.313 | 1328.093 | 1326.424 | 1397.610 |
| 3840×2160 | CPU | 4184.679 | 4540.739 | 3974.119 | 4233.179 |
| 3840×2160 | GPU | 2443.599 | 2470.227 | 2444.870 | 2452.899 |

Mean GPU total time was approximately 27%, 29% and 42% lower respectively on
this fixture/driver. The setting remains experimental and default off.

### Mean component costs (ms, six frames)

| Output | Backend | Preparation | Composition | Encoding |
| --- | --- | ---: | ---: | ---: |
| 1920×1080 | CPU | 71.424 | 599.909 | 543.005 |
| 1920×1080 | GPU | 65.226 | 267.193 | 526.743 |
| 2560×1440 | CPU | 57.711 | 907.041 | 842.150 |
| 2560×1440 | GPU | 57.313 | 309.417 | 869.281 |
| 3840×2160 | CPU | 59.412 | 2004.234 | 1850.852 |
| 3840×2160 | GPU | 62.456 | 337.253 | 1735.979 |

| GPU output | Upload | Draw submission | Readback |
| --- | ---: | ---: | ---: |
| 1920×1080 | 13.155 | 2.375 | 146.179 |
| 2560×1440 | 13.320 | 3.629 | 188.380 |
| 3840×2160 | 13.978 | 2.623 | 231.608 |

GPU transfer/submission figures are included within composition. The remaining
total includes finalization, setup/teardown and other render work. Do not sum
nested categories as disjoint costs. CPU performs no GPU upload/readback.

### Transfers and known storage (bytes)

Values were identical across the three runs for each resolution. Transfers are
totals for six frames; peaks are storage gauges, not cumulative allocations.

| Output | GPU uploaded | GPU read back | Peak known GPU storage | Final CPU frame peak |
| --- | ---: | ---: | ---: | ---: |
| 1920×1080 | 94,147,200 | 49,766,400 | 16,621,568 | 8,294,400 |
| 2560×1440 | 94,195,200 | 88,473,600 | 23,072,768 | 14,745,600 |
| 3840×2160 | 94,291,200 | 199,065,600 | 41,504,768 | 33,177,600 |

Source uploads include the two axis lookup segments for unrotated layers. The
final RGBA read equals width × height × 4 × six. Both CPU and GPU retain known
source buffers peaking at **15,667,200 bytes**, and both allocate the final CPU
frame for the current encoder boundary. GPU peak includes the largest reusable
source texture, output texture and 32,768-byte lookup storage. It excludes driver
overhead, hidden decoder/encoder buffers, context memory and process totals.
Actual VRAM/RAM peaks and long-running project behavior remain unmeasured here.
Preview's three-target/64 MiB pool budget is unchanged and separate from export.

## Manual acceptance checklist

Record build, OS, GPU/driver, codecs, settings, actions, output metadata and outcome.
The following human/platform checks remain pending unless a dated result is added:

1. Launch the application: export checkbox is off. Enable it, prepare an item,
   disable it and prepare a CPU item. Check accessible description, tooltip,
   responsive panel layout and unchanged project/history/dirty state. Leave and
   return to Render: the panel choice persists; relaunch: it resets off.
2. Export CPU/GPU at 1080p, 1440p and UHD, with rotated/transparent media, text,
   both transitions, keyframes and black gaps. Check decoded dimensions, frame
   rate/count/duration and appearance. Evaluate lossy codec differences separately.
3. Change preview quality, grayscale, global preview GPU setting and presentation
   environment override. Export output and its captured GPU choice must remain
   independent. Preview/audio should remain usable during export.
4. Export mixed-rate trimmed sources and clips with audible gain/mute changes,
   silent gaps and transition boundaries. Check physical audio synchronization,
   final duration and selected codec/container behavior.
5. Publish/re-publish a masked imported image in Image Editor, refresh its linked
   PNG in Video Editor and export CPU/GPU. Confirm changed pixels, intact original
   media and no stale source reuse between export attempts.
6. Exercise unavailable context/precision/limits and technical failures where the
   platform permits. Verify same-frame CPU output, one brief warning, prior log
   cause/code/job/frame/source/output, captured preference and a fresh backend on
   retry. Inspect schema 1 summaries on completion, failure and cancellation.
7. Cancel and close during active export; retain a pre-existing destination,
   remove temporary output and release worker resources before the GUI surface.
   Retry failed/canceled items; completed items remain skipped and queue ordering
   survives CPU/GPU mixtures.
8. Repeat native builds, parity, queue/lifecycle and packaged codec/fallback checks
   on macOS/Linux and additional GPU vendors/drivers. Measure representative long
   projects, process memory and real driver resources before broader acceptance.

Windows automation is evidence for this tested driver. macOS/Linux, additional
drivers, human visual/audio/accessibility checks, packaged distributions and
representative long-project acceptance remain pending. Motion Studio and Image
Editor GPU adoption, effects, decode and encode are later work.
