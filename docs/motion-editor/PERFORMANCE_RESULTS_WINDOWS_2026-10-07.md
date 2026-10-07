# Motion Studio Windows Performance Results — 2026-10-07

This report preserves the original Motion Studio measurements and records
subsequent CPU and GPU exports of a 3,405-frame composition after the GPU export
integration.

## Test and export run

The latest recorded CTest run on October 6 completed **63 of 63 tests with no
failures**, including the Motion Studio export, preview, UI, persistence, and
performance-metrics suites. These are correctness results; they do not measure
interactive throughput.

The 2026-10-07 Windows Release integration build passed all 11 Motion Studio
CTest targets, including export and GPU composition. These correctness results
do not measure full-project GPU export throughput.

The original Motion Studio log records a completed 1920 × 1080, 60 fps CPU
export:

| Metric | Result |
| --- | ---: |
| Frames rendered | 3,405 |
| Elapsed time | 100.76 s |
| Achieved throughput | 33.79 fps |
| Realtime factor | 0.563× |
| Average frame render | 22.20 ms |
| Maximum frame render | 101.21 ms |
| Average frame write | 6.95 ms |
| Maximum frame write | 32.84 ms |

At 60 fps, 3,405 frames represent 56.75 seconds of output, so this export took
about 1.78 seconds per second of finished video. Frame rendering accounts for
about 76% of the combined average render and write time; encoding/write time is
about 24%. The export record does not include its layer/effect configuration,
so its throughput cannot be attributed to the preview composition below.
This recorded export predates the opt-in GPU export integration and remains a
historical CPU baseline, not a paired GPU comparison.

## GPU export measurement

The updated Windows Release executable completed a GPU-requested export of
3,405 frames at 1920 × 1080 and 60 fps. The schema-2 summary confirms that the
GPU backend handled all frames, with no CPU fallback frames or composition
failures. A CPU export of the same frame count and output settings was also
recorded in the updated executable.

| Backend | Schema | Elapsed time | Throughput | Realtime factor | GPU frames | Fallbacks | Failures |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| CPU | 2 | 99.63 s | 34.18 fps | 0.570× | 0 | 0 | 0 |
| GPU | 2 | 54.96 s | 61.95 fps | 1.032× | 3,405 | 0 | 0 |

This paired run reduced elapsed time by 44.8% and delivered about 1.81× the CPU
throughput. The log also contains two earlier schema-1 CPU exports of the same
frame count, resolution, and frame rate (100.76 s and 100.07 s). Those records
show a consistent CPU baseline, but the diagnostic log contains only one
schema-2 GPU export, so GPU repeatability has not yet been established by the
available summaries.

The GPU run uploaded 35.43 GB and read back 28.24 GB across 3,405 compositions.
Average upload, draw-submission, and readback times were 0.95 ms, 0.18 ms, and
4.95 ms per composition, respectively. The effect counters also record 860
GPU Color Adjustment operations and 860 GPU Gaussian Blur operations. The
full-frame RGBA readback remains a substantial part of the GPU path before CPU
FFmpeg encoding.

## Interactive preview

The preview intervals report a 1920 × 1080, 60 fps composition with two layers,
two configured effects, and eight effect workers. The measurements below use
steady one-second intervals from the same process, separating intervals where
no effects were applied from intervals where Gaussian Blur ran.

| Preview state | Rendered frames | Frame render | Request to paint | Decode | Composition | Process CPU | Working set |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| No effects applied | 57–60 fps | 5.94 ms avg | 9.28 ms avg | 2.48 ms avg | 3.46 ms avg | 83–89% | 310–317 MiB |
| Gaussian Blur active | 10–12 fps | 84.89 ms avg | 96.69 ms avg | 12.19 ms avg | 4.66 ms avg | 438–518% | 303–327 MiB |

Across the seven steady blur-active intervals, the preview rendered 80 frames
from 403 requests and coalesced 323 requests (80%). Gaussian Blur averaged
54.7 ms per application and the full effects stage averaged 55.9 ms. Color
Adjustment measured roughly 0.0003 ms per application, which is negligible at
this scale. The composition stage stayed below 5 ms, so the compositor is not
the current preview bottleneck. Decode time also rose from about 2.48 ms with
no effects to 12.19 ms in blur-active intervals; CPU contention is a plausible
cause, although these logs alone do not prove it.

`process_cpu_percent` is calculated as process CPU time divided by wall time;
100% represents one fully occupied CPU core. The blur-active average was about
473%, or 4.7 core-equivalents. The host still reported about 16.8 GiB of
available system memory, so the measured slowdown does not point to system
memory pressure.

## Findings and limits

- Gaussian Blur is the dominant interactive cost in this workload. Applying
  it increases average frame-render time from about 5.9 ms to about 84.9 ms
  and reduces delivered preview rate from about 59 fps to about 11 fps.
- Color Adjustment and layer composition are too small in these samples to
  justify optimization ahead of Gaussian Blur.
- The original CPU export completed successfully at 0.563× realtime at 60 fps.
  Its render stage is the larger part of measured per-frame work; the export
  log does not reveal whether that work includes the same effects as preview.
- The updated GPU export completed at 1.032× realtime and about 1.81× the
  throughput of the schema-2 CPU run, with every frame composed on the GPU and
  no fallback or composition failures. This is one paired GPU measurement;
  repeatability should not be inferred from a single GPU run.
- The preview records use diagnostics schema 4. The current checkout emits
  schema 7, which includes GPU counters. This run therefore provides no GPU
  composition counts or timings and cannot establish whether the experimental
  GPU preview path was active or faster.
- Every Motion Studio executable found in the repository's Windows build
  outputs predates the current performance-metrics source file. The process
  that generated this log has exited, and the log does not record its
  executable path, so the exact launch route cannot be identified. The schema
  mismatch is consistent with the run using one of those stale builds.
- This is a real user run, but it is not the approved 1080p/30 fps,
  10-second, five-layer reference workload. The 60 fps, two-layer results are
  still useful for identifying the blur bottleneck; they should not be
  generalized to other composition sizes or hardware.

## GPU export follow-up

Motion Studio now uses the same experimental OpenGL compositor in offline
export when `CREATIVE_SUITE_MOTION_GPU_COMPOSITION=1`. CPU remains the default.
Each export summary uses schema 2 and records the requested and used backend,
GPU-composed and CPU-fallback frames, failures, uploaded/readback bytes, and
average upload, draw-submission, and readback times. The output still crosses
the existing RGBA readback boundary before CPU FFmpeg encoding.

Automated export coverage exercises no-surface CPU fallback, frame parity,
worker lifecycle, and GPU summary fields. The available runtime logs now
include a completed GPU export and its schema-2 CPU comparison. Two earlier
schema-1 CPU measurements provide additional historical context, but only one
GPU summary is available in the log; the measured improvement is promising but
not yet a repeated GPU benchmark result. The export summary does not identify
the layer/effect setup, so these throughput numbers should not be generalized
to other compositions or hardware.

For the paired manual measurement, use the same saved 3,405-frame composition,
output dimensions, frame rate, encoder, and quality for three CPU exports with
the environment variable unset and three GPU-requested exports with it set to
`1`. Compare median end-to-end elapsed time and achieved FPS, along with render
and write time, GPU/fallback frames, transfer bytes, and output parity. Record
each schema-2 summary. Claim a speedup only when the end-to-end improvement
repeats. Native macOS and Linux driver/performance checks remain pending.

## Recommended performance focus

Profile and improve Gaussian Blur first. Compare the existing worker settings
with the same composition, then compare CPU and opt-in GPU preview using the
current diagnostics schema. Measure GPU export separately from preview; it
still includes source upload and full-frame readback before encoding. Do not
prioritize Color Adjustment or composition changes based on these results.
